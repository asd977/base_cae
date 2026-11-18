"""Run real frontend exports and deployed solvers serially, GPU before CPU.

No solver sources are edited. Each attempt retains a log and input hashes.
The global legacy IIIDIR.DAT is restored even if an attempt fails.
"""
import argparse
import ctypes
import datetime
import hashlib
import json
import math
import os
import re
from pathlib import Path
import subprocess
import time

from build_geotechnical_regression import read, write, walk
from static_state_fingerprint import model_hashes
from seepage_convergence import inspect_seepage_convergence


def inspect_analytic_displacement(workspace, started, definition):
    errors=[];metrics={}
    path=workspace/'CASE01/IIDISP.TXT';mesh=path.parent/'IIIN.TXT'
    try:
        if path.stat().st_mtime<started-2:raise ValueError('stale displacement output')
        lines=mesh.read_text('gbk').splitlines();count=int(lines[0].split()[0])
        coordinates={int(r.split()[-1]):[float(v) for v in r.split()[:3]] for r in lines[2:count+2]}
        rows=path.read_text('gbk').splitlines()[3:]
        if len(rows)!=count:raise ValueError('incomplete displacement output')
        coefficient=definition.get('x_end_m',0)/definition['length_x_m'];differences=[];rotation_errors=[]
        for row in rows:
            fields=row.split();xyz=coordinates[int(fields[0])]
            if definition.get('field')=='planar_beam':
                x=xyz[0];L=definition['length_x_m'];EI=definition['flexural_rigidity_Nm2'];P=definition['load']
                loading=definition['loading']
                if loading=='force':v=P*x*x*(3*L-x)/(6*EI);theta=P*x*(2*L-x)/(2*EI)
                elif loading=='moment':v=P*x*x/(2*EI);theta=P*x/EI
                elif loading=='uniform':v=P*x*x*(6*L*L-4*L*x+x*x)/(24*EI);theta=P*x*(3*L*L-3*L*x+x*x)/(6*EI)
                elif loading=='rotation':v=P*x*x/(2*L);theta=P*x/L
                else:raise ValueError('unknown beam loading')
                expected=[0,v,theta]
            elif definition.get('field')=='thermal_gradient':
                axis=definition['gradient_axis'];beta=definition['strain_gradient_per_m']
                expected=[beta*xyz[axis]*xyz[i] for i in range(3)]
                expected[axis]=.5*beta*(xyz[axis]**2-sum(xyz[i]**2 for i in range(3) if i!=axis))
            else:
                transverse=coefficient if definition['thermal'] else -definition.get('poisson',0)*coefficient
                expected=[definition.get('offset_x_m',0)+coefficient*xyz[0],transverse*xyz[1],transverse*xyz[2]]
            actual=[float(v.replace('D','E')) for v in fields[1:4]]
            if len(actual)!=3 or not all(math.isfinite(v) for v in actual):raise ValueError('invalid displacement')
            differences.extend(abs(v-e) for v,e in zip(actual[:2] if definition.get('field')=='planar_beam' else actual,expected))
            if definition.get('field')=='planar_beam':rotation_errors.append(abs(actual[2]-expected[2]))
        maximum=max(differences)
        metrics={'node_count':count,'max_error_m':maximum,'tolerance_m':definition['tolerance_m']}
        if maximum>definition['tolerance_m']:errors.append('analytic displacement error exceeds tolerance')
        if rotation_errors:
            metrics.update(max_rotation_error_rad=max(rotation_errors),tolerance_rad=definition['tolerance_rad'])
            if max(rotation_errors)>definition['tolerance_rad']:errors.append('analytic rotation error exceeds tolerance')
    except (OSError,ValueError,KeyError,IndexError) as e:errors.append('analytic displacement check failed: '+str(e))
    return {'metrics':metrics,'errors':errors}


def inspect_static_convergence(text,node):
    if node.get('type')!='SlopeStabilityAnalysis':return {'steps':[],'errors':[],'warnings':[]}
    tolerance=float(node['state']['base_values'][6]);steps=[];errors=[];warnings=[]
    for step,iteration,strength,error in re.findall(r'ISTEP=\s*(\d+)\s+IT=\s*(\d+)\s+FS=\s*([\d.Ee+-]+).*?ER=\s*([^\s]+)\s+HG=\s*1\b',text):
        try:value=float(error.replace('D','E'))
        except ValueError:value=float('nan')
        converged=math.isfinite(value) and 0<=value<=tolerance
        steps.append({'step':int(step),'iterations':int(iteration),'strength_factor':float(strength),
            'relative_error':value if math.isfinite(value) else None,'tolerance':tolerance,'converged':converged})
    if any(s['relative_error'] is None or s['relative_error']<0 for s in steps):errors.append('invalid static convergence residual')
    if any(s['relative_error'] is not None and s['relative_error']>tolerance for s in steps):warnings.append('a completed static equilibrium step exceeds requested tolerance')
    return {'steps':steps,'errors':errors,'warnings':warnings}


def inspect_analytic_head(workspace,started,definition):
    path=workspace/'SEEPCASE01/IIDISP.TXT';mesh=path.parent/'IIIN.TXT'
    errors=[];metrics={}
    try:
        if path.stat().st_mtime<started-2:raise ValueError('stale head output')
        lines=mesh.read_text('gbk',errors='replace').splitlines();count=int(lines[0].split()[0])
        coordinates={int(row.split()[-1]):float(row.split()[0]) for row in lines[2:count+2]}
        rows=path.read_text('gbk',errors='replace').splitlines()[3:]
        if len(rows)!=count:raise ValueError('incomplete head output')
        differences=[abs(float(row.split()[1])-(definition['intercept_m']+
            definition['gradient_x']*coordinates[int(row.split()[0])])) for row in rows]
        maximum=max(differences)
        if not all(math.isfinite(v) for v in differences) or maximum>definition['tolerance_m']:
            errors.append('analytic Darcy head error exceeds tolerance')
        metrics={'node_count':count,'max_error_m':maximum,'tolerance_m':definition['tolerance_m']}
    except (OSError,ValueError,KeyError,IndexError) as e:errors.append('analytic Darcy head check failed: '+str(e))
    return {'metrics':metrics,'errors':errors}


def inspect_feature_results(workspace, started, node):
    """Require fresh native mode/wave outputs, not just preceding static results."""
    settings=[p.get('state',{}) for p in walk(node)]
    def enabled(key):
        return any(str(s.get(key.lower(),s.get('values',{}).get(key,'0'))) not in ('0','0.0','False','') for s in settings)
    errors=[];artifacts=[]
    def fresh(name):
        return [p for p in workspace.rglob(name) if '计算输入' not in p.parts
                and p.stat().st_mtime>=started-2 and p.stat().st_size>0]
    if enabled('IFMODE'):
        valid=False
        for path in fresh('IEIGEN.TXT'):
            lines=path.read_text('gbk',errors='replace').splitlines()
            try:
                count=int(lines[0].split()[0]);rows=[line.split() for line in lines[3:3+count]]
                eigen=[[float(v.replace('D','E')) for v in row[1:8]] for row in rows]
                ok=count>0 and len(rows)==count and all(len(row)==8 and int(row[0])==i+1 for i,row in enumerate(rows))
                ok=ok and all(all(math.isfinite(v) for v in row) and all(v>0 for v in row[:4]) for row in eigen)
                if ok:artifacts.append({'file':str(path),'mode_count':count,'frequency_hz':[v[2] for v in eigen]});valid=True
            except (ValueError,IndexError):pass
        if not valid:errors.append('missing fresh valid modal eigenvalues/mode output')
    if enabled('IFWAVE'):
        for name in ['IIACCE.TXT','IIUMAX.TXT']:
            valid=False
            for path in fresh(name):
                try:
                    lines=path.read_text('gbk',errors='replace').splitlines();count=int(lines[0].split()[1])
                    rows=[line.split() for line in lines[3:]]
                    values=[float(v.replace('D','E')) for row in rows for v in row[1:]]
                    ok=len(rows)==count and all(len(row)>=3 and int(row[0])==i+1 for i,row in enumerate(rows))
                    ok=ok and bool(values) and all(math.isfinite(v) for v in values) and max(abs(v) for v in values)>0
                    if ok:artifacts.append({'file':str(path),'node_count':count,'max_abs':max(abs(v) for v in values)});valid=True
                except (ValueError,IndexError):pass
            if not valid:errors.append('missing fresh finite nonzero wave response '+name)
    return {'artifacts':artifacts,'errors':errors}


def inspect_results(workspace, started, seepage=False):
    files = [p for p in workspace.rglob('*') if p.is_file()
             and p.stat().st_mtime >= started - 2 and '计算输入' not in p.parts]
    displacements = []
    errors = []
    for path in files:
        if path.name.upper() != 'IIDISP.TXT':
            continue
        active_nodes = None
        mesh_path = path.parent/'IIIN.TXT'
        if mesh_path.exists():
            mesh_lines = mesh_path.read_text('gbk',errors='replace').splitlines()
            header = mesh_lines[0].split()
            node_count,element_count,width = int(header[0]),int(header[2]),int(header[3])
            active_nodes = set()
            for row in mesh_lines[node_count+2:node_count+2+element_count]:
                fields = row.split()
                if int(fields[width+2]) != 1:  # native material 1 is void
                    active_nodes.update(int(v) for v in fields[1:width+1] if int(v)>0)
        rows = []
        inactive = []
        for line in path.read_text('gbk', errors='replace').splitlines()[3:]:
            fields = line.split()
            if len(fields) < 2 or not fields[0].isdigit():
                continue
            try:
                # Seepage WRITEU emits HEAD(m), PRESSURE(Pa) in IIDISP.
                # Pressure is not a second displacement component.
                physical_values = [float(v.replace('D', 'E')) for v in fields[1:4]]
                planar_beam=not seepage and mesh_path.exists() and int(header[1])==2 and width==2 and any(int(r.split()[width+1])==21 for r in mesh_lines[node_count+2:node_count+2+element_count])
                values = physical_values[:1] if seepage else physical_values[:2] if planar_beam else physical_values
            except ValueError:
                errors.append(str(path) + ': invalid displacement')
                continue
            if not all(math.isfinite(v) for v in physical_values):
                errors.append(str(path) + ': nonfinite displacement')
            destination = rows if active_nodes is None or int(fields[0]) in active_nodes else inactive
            destination.extend(abs(v) for v in values)
        if rows:
            maximum = max(rows)
            displacements.append({'file': str(path), 'max_abs_m': maximum,
                'active_nodes':len(active_nodes) if active_nodes is not None else None,
                'max_abs_void_only_node_m':max(inactive) if inactive else None})
            if maximum > 10 and not seepage:
                errors.append(str(path) + ': displacement exceeds 10 m regression sanity limit')
    return {'fresh_file_count': len(files), 'displacements': displacements, 'errors': errors}


def attempt(driver, project, case, phase, device, logs, timeout, node, debugger=None,postprocess=False):
    # Geostress prepares the initial state and does not publish final results.
    postprocess = postprocess and phase != 'geostress'
    name = case['id'] + '_' + phase + '_' + device
    log = logs/(name + '.log')
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='windows', OMP_NUM_THREADS='4', MKL_NUM_THREADS='4')
    env['PATH'] = 'C:/Qt/6.7.3/msvc2022_64/bin;' + env['PATH']
    started = time.time()
    command=[str(driver),str(project),case['analysis_uuid'],device,phase+'-with-post' if postprocess else phase,str(timeout)]
    if debugger:
        script=logs/(name+'.cdb.txt')
        # Address comes from a unique byte match against the original COFF
        # objects. Refuse a stale map rather than break at an invented address.
        mapping=Path(__file__).resolve().parents[1]/f'build/native_diagnostics/{device}_symbol_map.json'
        break_error=''
        if mapping.exists():
            symbols=read(mapping)
            module='PCCAE_'+device.upper()
            native=driver.parent/f'solver/{module}/{module}.exe'
            error_show=[f for f in symbols['functions'] if f['name']=='ERRORSHOW']
            if hashlib.sha256(native.read_bytes()).hexdigest()==symbols['image_sha256'] and len(error_show)==1:
                break_error=f'sxe -c "bp /1 {module}+0x{error_show[0]["rva"]:x};g" ld:{module}\n'
        script.write_text('sxi ibp\n'+break_error+'sxe -c ".echo NATIVE_EXCEPTION;.exr -1;r;kv;lm m PCCAE*;q" av\n'
            'sxe -c ".echo NATIVE_HEAP_EXCEPTION;.exr -1;r;kv;lm m PCCAE*;q" c0000374\n'
            'sxe -c ".echo NATIVE_BREAK;.exr -1;r;~* kv;lm m PCCAE*;q" bpe\nsx\ng\n',encoding='ascii')
        command=[str(debugger),'-o','-hd','-G','-y',str(driver.parent),'-cf',str(script)]+command
    with log.open('wb') as stream:
        process = subprocess.Popen(command, cwd=driver.parent,
                                   env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            code = process.wait(timeout=timeout+30)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           stdout=stream, stderr=stream, creationflags=subprocess.CREATE_NO_WINDOW)
            code = process.wait()
    text = log.read_text('utf-8', errors='replace')
    manifest_path = project/case['title']/'分析信息.json'
    manifest = read(manifest_path) if manifest_path.exists() else {}
    results = inspect_results(manifest_path.parent, started,node['type']=='SeepageAnalysis')
    results['warnings']=list(manifest.get('convergenceWarnings',[]))
    results['postprocessing_generated']='post_prepared=1' in text
    if postprocess and not results['postprocessing_generated']:results['errors'].append('postprocessing results were not generated')
    if phase=='analysis':
        equilibrium=inspect_static_convergence(text,node)
        results['static_equilibrium_steps']=equilibrium['steps'];results['errors'].extend(equilibrium['errors'])
        results['warnings'].extend(equilibrium['warnings'])
        feature=inspect_feature_results(manifest_path.parent,started,node)
        results['feature_outputs']=feature['artifacts'];results['errors'].extend(feature['errors'])
        if case.get('analytic_head'):
            comparison=inspect_analytic_head(manifest_path.parent,started,case['analytic_head'])
            results['analytic_head']=comparison['metrics'];results['errors'].extend(comparison['errors'])
        if case.get('analytic_displacement'):
            comparison=inspect_analytic_displacement(manifest_path.parent,started,case['analytic_displacement'])
            results['analytic_displacement']=comparison['metrics'];results['errors'].extend(comparison['errors'])
    native_log = manifest_path.parent/'III110.DAT'
    if native_log.exists() and native_log.stat().st_mtime >= started-2:
        native_bytes = native_log.read_bytes()
        (logs/(name+'.III110.DAT')).write_bytes(native_bytes)
        native_text = native_bytes.decode('gbk', errors='replace')
        native_errors = re.findall(r'^\s*(ERROR[^\r\n]*)',native_text,re.MULTILINE|re.IGNORECASE)
        results['errors'].extend(native_errors)
    clean_fast_completion = code == 8 and 'DRIVER_FINISH started=0 dialog_failure=0' in text
    success = ((code == 0 or clean_fast_completion) and manifest.get('state') == 'completed'
               and '成功运行程序' in text and not results['errors'])
    if phase != 'geostress':
        success = success and bool(results['displacements'])
    for token in ['forrtl: severe', 'ERROR ', 'NaN', 'Infinity']:
        if token in text:
            success = False
    if node['type']=='SeepageAnalysis':
        convergence=inspect_seepage_convergence(text,manifest_path.parent,started,phase=='steady' or node['state'].get('compute_type')!=2)
        results['seepage_steps']=convergence['steps'];results['errors'].extend(convergence['errors'])
        results['warnings'].extend(convergence['warnings'])
        if convergence['errors']:success=False
    results['warnings']=list(dict.fromkeys(results['warnings']))
    inputs = manifest_path.parent/'计算输入'
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
              for p in inputs.iterdir() if p.is_file() and p.suffix.upper() in ('.DAT','.TXT')}
    return {'device': device, 'phase': phase, 'returncode': code, 'started_at_epoch':started,
            'elapsed_seconds': round(time.time()-started, 2), 'log': str(log),
            'completed':bool(success),'passed':bool(success and not results['warnings']), 'manifest': manifest, 'results': results,
            'input_sha256': hashes,
            'directory_files_match': Path('C:/IIIDIR.DAT').exists()
                and Path('C:/IIIDIR.DAT').read_bytes()==Path('D:/IIIDIR.DAT').read_bytes(),
            'native_error_bridge_active': 'solver\\Runtime\\' in Path('D:/IIIDIR.DAT').read_text('gbk',errors='replace')}


def accepted_static_dependency(root,project,case,dependency):
    """A failed rerun does not invalidate separately preserved, proven old state."""
    reference=case.get('accepted_static_dependency',{})
    if reference.get('id')!=dependency:return False
    try:
        report=read(Path(reference['report']));source=next(c for c in report['cases'] if c['id']==dependency)
        if not source.get('passed') or report.get('diagnostic_only'):return False
        accepted=next(a for a in reversed(source['attempts']) if a['phase']=='analysis' and a.get('passed'))
        provenance=read(Path(reference['provenance']))
        if provenance.get('accepted_source_report')!=reference['report'] or provenance.get('source_case')!=dependency:return False
        if not provenance['files']:return False
        for record in provenance['files']:
            path=Path(record['copy'])
            if hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:return False
        workspace=root/project['directory']/case['title']
        source_definition=next(c for c in project['cases'] if c['id']==dependency)
        source_input=root/project['directory']/source_definition['title']/'计算输入'
        for name in ['IIIN.DAT','IIID.DAT','IMAT.DAT']:
            if hashlib.sha256((source_input/name).read_bytes()).hexdigest()!=accepted['input_sha256'].get(name):return False
        expected_model=model_hashes(source_input)
        for folder in ['DISK00','CASE01WETT']:
            certificate=read(workspace/folder/'STATIC_STATE.json')
            if certificate.get('version')!=2:return False
            for name,digest in certificate['modelInputs'].items():
                if expected_model.get(name)!=digest:return False
            if set(certificate['modelInputs'])!={'IIIN.DAT','IIID.DAT','IMAT.DAT'}:return False
            for name,digest in certificate['stateFiles'].items():
                if hashlib.sha256((workspace/folder/name).read_bytes()).hexdigest()!=digest:return False
        return True
    except (OSError,ValueError,KeyError,StopIteration):return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', type=Path)
    parser.add_argument('--driver', type=Path, default=Path(__file__).resolve().parents[1]/'build/smesh_release/SolverRegressionDriver.exe')
    parser.add_argument('--cases', nargs='*')
    parser.add_argument('--timeout', type=int, help='Override per-case timeout_seconds (otherwise default 180 s)')
    parser.add_argument('--devices',nargs='+',choices=['gpu','cpu'],default=['gpu','cpu'])
    parser.add_argument('--debugger',type=Path,help='Diagnostic run under CDB; not a numerical acceptance run')
    parser.add_argument('--comparison-only',action='store_true',help='Retain device comparison results without replacing primary case status')
    parser.add_argument('--postprocess',action='store_true',help='Load real postprocessing plugin and wait for generated result cache')
    args = parser.parse_args()
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    coverage = read(args.suite/'coverage.json')
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    logs = args.suite/'runs'/stamp
    logs.mkdir(parents=True)
    lock = Path('D:/IIIDIR.regression.lock')
    process_list = subprocess.check_output(['tasklist', '/FO', 'CSV', '/NH'],
        creationflags=subprocess.CREATE_NO_WINDOW).decode('gbk', errors='replace').lower()
    if 'pccae_gpu.exe' in process_list or 'pccae_cpu.exe' in process_list:
        raise RuntimeError('A solver is already running; leave its global IIIDIR and process untouched')
    with lock.open('x') as stream:
        stream.write(str(os.getpid()))
    global_input = Path('D:/IIIDIR.DAT')
    original = global_input.read_bytes() if global_input.exists() else None
    legacy_input = Path('C:/IIIDIR.DAT')
    legacy_original = legacy_input.read_bytes() if legacy_input.exists() else None
    if original is not None:
        (logs/'IIIDIR.before.dat').write_bytes(original)
    if legacy_original is not None:
        (logs/'C-IIIDIR.before.dat').write_bytes(legacy_original)
    report = {'started_at': stamp, 'cases': [], 'solver_source_modified': False,'diagnostic_only':bool(args.debugger),'comparison_only':args.comparison_only}
    try:
        for project in coverage['projects']:
            solver_nodes = {n['uuid']:n for n in read(args.suite/project['directory']/'工程数据/solver.json')['items']}
            for case in project['cases']:
                if args.cases and case['id'] not in args.cases:
                    continue
                record = {'project': project['directory'], 'id': case['id'], 'attempts': []}
                report['cases'].append(record)
                missing=[dependency for dependency in case.get('depends_on',[])
                    if not any(c['id']==dependency and c.get('result_status') in ('passed_sanity','completed_with_warnings') for c in project['cases'])
                    and not accepted_static_dependency(args.suite,project,case,dependency)]
                if missing:
                    record.update(passed=False,blocked_by=missing)
                    case['result_status']='blocked_dependency'
                    write(logs/'results.json',report)
                    if not(args.debugger or args.comparison_only):write(args.suite/'coverage.json',coverage)
                    print(case['id'],'blocked_dependency',missing,flush=True)
                    continue
                node = solver_nodes[case['analysis_uuid']]
                timeout=args.timeout if args.timeout is not None else case.get('timeout_seconds',180)
                phases = (['geostress','analysis'] if node['type']=='CaveExcavationAnalysis'
                    else ['steady','analysis'] if node['type']=='SeepageAnalysis' and node['state'].get('compute_type')==2
                    else ['analysis'])
                passed = True
                for phase in phases:
                    phase_passed = False
                    for device in args.devices:
                        print(case['id'], phase, device, flush=True)
                        result = attempt(args.driver, args.suite/project['directory'], case,
                                         phase, device, logs, timeout,node,args.debugger,args.postprocess)
                        record['attempts'].append(result)
                        write(logs/'results.json', report)
                        if result.get('completed',result['passed']):
                            phase_passed = True
                            break
                    if not phase_passed:
                        passed = False
                        break
                warnings=list(dict.fromkeys(w for a in record['attempts'] if a.get('completed',a['passed']) for w in a.get('results',{}).get('warnings',[])))
                record.update(completed=passed,passed=passed and not warnings,warnings=warnings,
                    result_status='completed_with_warnings' if passed and warnings else 'passed_sanity' if passed else 'failed')
                if not(args.debugger or args.comparison_only):
                    case['result_status'] = record['result_status']
                    case['last_run_report'] = str(logs/'results.json')
                    write(args.suite/'coverage.json', coverage)
                write(logs/'results.json', report)
                print(case['id'], case['result_status'], flush=True)
    finally:
        if original is not None:
            global_input.write_bytes(original)
        elif global_input.exists():
            global_input.unlink()
        if legacy_original is not None:
            legacy_input.write_bytes(legacy_original)
        elif legacy_input.exists():
            legacy_input.unlink()
        report['global_input_restored'] = not global_input.exists() if original is None else global_input.read_bytes() == original
        report['legacy_input_restored'] = not legacy_input.exists() if legacy_original is None else legacy_input.read_bytes()==legacy_original
        write(logs/'results.json', report)
        lock.unlink()
    print(str(logs/'results.json'), flush=True)


if __name__ == '__main__':
    main()
