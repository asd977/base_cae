"""Create independent multi-case CAE projects without changing seeds or solver.

python tests/build_geotechnical_regression.py --output C:/test/rock/回归测试_20261028
python tests/build_geotechnical_regression.py --verify C:/test/rock/回归测试_20261028

This prepares UI projects, not validated numerical reference solutions. It never
launches a solver, changes IIIDIR.DAT, or installs an executable.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import uuid
import xml.etree.ElementTree as ET


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def walk(node):
    yield node
    for child in node.get('children', []):
        yield from walk(child)


def child(node, kind):
    return next(n for n in walk(node) if n.get('type') == kind)


def clone(node, title):
    result = copy.deepcopy(node)
    for n in walk(result):
        n['uuid'] = '{'+str(uuid.uuid4())+'}'
        n.get('state', {}).pop('last_job', None)
    result.update(title=title, custom_title=title)
    return result


def source_evidence(root):
    path = root/'PCCAE_CPU/PCCAE_CPU.F90'
    text = path.read_text(encoding='gbk', errors='replace').splitlines()
    functions = ['CPU_AUTOPACK', 'CPU_AUTOSEEP', 'CPU_AUTOCAVE', 'CPU_AUTOGEOS', 'CPU_AUTOSLOP']
    binaries=[]
    for candidate in [root/'PCCAE_CPU/x64/Release/PCCAE_CPU.exe',
                      Path(__file__).resolve().parents[1]/'build/smesh_release/solver/PCCAE_CPU/PCCAE_CPU.exe']:
        if candidate.is_file():binaries.append({'path':str(candidate),'sha256':digest(candidate),
            'size':candidate.stat().st_size,'mtime':candidate.stat().st_mtime,
            'source_newer_than_binary':path.stat().st_mtime>candidate.stat().st_mtime})
    return {'root': str(root), 'file': str(path), 'sha256': digest(path),'binaries':binaries,
            'entry_points': {f: [i+1 for i, line in enumerate(text)
                                  if ('SUBROUTINE '+f+'(') in line.upper()] for f in functions},
            'exe_policy': 'No executable copied or executed. Source presence does not certify any binary.'}


def prepare(seed, target, name):
    target.mkdir()
    shutil.copytree(seed/'工程数据', target/'工程数据')
    data = target/'工程数据'
    # Result displays can retain paths to old jobs. Keep the new project clean.
    if (data/'postprocessing.json').exists():
        post = read(data/'postprocessing.json')
        if 'items' in post: post['items'] = []
        if 'nodes' in post: post['nodes'] = []
        write(data/'postprocessing.json', post)
    manifest = ET.parse(next(seed.glob('*.xml')))
    manifest.getroot().find('Name').text = name
    manifest.write(target/(name+'.xml'), encoding='UTF-8', xml_declaration=True)
    inputs = data/'计算输入'
    inputs.mkdir(exist_ok=True)
    if not (inputs/'IIIN.DAT').exists():
        source = next(p.parent for p in seed.rglob('IIIN.DAT') if p.parent.name == '计算输入')
        for p in source.iterdir():
            if p.is_file(): shutil.copy2(p, inputs/p.name)
    # Only portable shared inputs are copied; CASE*/GEOSTR old outputs are not.
    solver = read(data/'solver.json')
    solver['state'] = {'compute_device': 'gpu', 'gpu_device_mode': 2}
    return solver


def install_pulse(project):
    rows = []
    for i in range(101):
        t = i*.02
        envelope = math.sin(math.pi*t/2)**2
        values = [t, .05*envelope*math.sin(2*math.pi*t),
                  .03*envelope*math.sin(3*math.pi*t), .02*envelope*math.sin(4*math.pi*t)]
        rows.append({f'c{k}': f'{v:.8f}' for k, v in enumerate(values)})
    path = project/'工程数据/physical_field.json'
    physical = read(path)
    physical['items'] = [n for n in physical['items'] if n.get('kind') != 'SeismicAndAddedMassPage']
    physical['items'].append({'uuid': '{'+str(uuid.uuid4())+'}', 'title': '回归测试－合成短脉冲',
                             'kind': 'SeismicAndAddedMassPage', 'domain': 'SeismicAndAddedMassPage',
                             'page_data': {'amp_x': 1., 'amp_y': 1., 'amp_z': 1., 'rows': rows}})
    write(path, physical)
    # Match PhysicalField::generateWaveFiles, so the shared input directory also
    # contains usable samples before the UI regenerates it.
    for axis,index in [('AX',1),('AY',2),('AZ',3)]:
        text=f'{len(rows):6d}      1.0000 NUM  AM\n   T(S)           {axis}(m/S^2)       II\n'
        text+=''.join(f"{row['c0']}       {row[f'c{index}']}       {i+1}\n" for i,row in enumerate(rows))
        (project/'工程数据/计算输入'/f'WAVE-{axis}.DAT').write_text(text,encoding='ascii')
    trace = project/'工程数据/计算输入/TDTRACE.DAT'
    if not trace.exists():trace.write_text('0\n',encoding='ascii')
    quiet = project/'工程数据/计算输入/FA_DYNA.DAT'
    snapshot = project/'工程数据/mesh_snapshot.iiin.dat'
    if not quiet.exists() and snapshot.exists():
        lines=snapshot.read_text('ascii').splitlines();header=lines[0].split()
        if int(header[1])==3 and int(header[3])==8:
            n=int(header[0]);bounds=list(map(float,lines[1].split()[2:8]))
            points=[list(map(float,line.split()[:3])) for line in lines[2:n+2]]
            faces=[];seen=set()
            slots=[[0,4,7,3],[1,2,6,5],[0,1,5,4],[3,7,6,2],[0,3,2,1],[4,5,6,7]]
            for row in lines[n+2:]:
                values=list(map(int,row.split()))
                for face in slots:
                    nodes=[values[i+1] for i in face];key=tuple(sorted(nodes))
                    if len(set(nodes))<3 or key in seen:continue
                    coords=[points[i-1] for i in nodes]
                    if any(all(abs(p[a]-v)<1.e-8 for p in coords) for a,v in
                           [(0,bounds[0]),(0,bounds[1]),(1,bounds[2]),(1,bounds[3]),(2,bounds[4])]):
                        seen.add(key);faces.append((values[0],nodes))
            if faces:
                quiet.write_text(f'{len(faces)} 6 NJX MAKOD\nKJX KE1 KE2 JD1 JD2 JD3 JD4 JAT\n'+
                    ''.join(f'{i+1} {ke} 0 '+' '.join(map(str,nodes))+' 0\n' for i,(ke,nodes) in enumerate(faces)),encoding='ascii')


def add_case(project, node, features, cases, dependencies=None, blockers=None):
    identifier = node['title'].split('－')[0]
    cases.append({'id': identifier, 'title': node['title'], 'analysis_uuid': node['uuid'],
                  'features': features, 'depends_on': dependencies or [],
                  'status': 'blocked_inputs' if blockers else 'prepared_not_run',
                  'blockers': blockers or [], 'result_status': 'not_run',
                  'acceptance': ['由当前前端重新生成本工况输入，输出目录独立',
                                 '求解日志无输入/装配/收敛错误，正常完成标记与新生成结果同时存在',
                                 '结果数值有限；检查反力/位移/水头等对应物理量，首次人工确认后建立数值基准']})
    project['items'].append(node)


def dam(seed, target):
    project = prepare(seed, target, target.name)
    original = project['items']; project['items'] = []; cases = []
    for i, template in enumerate(original, 1):
        n = clone(template, f'D{i:02d}－'+template['title'])
        blockers = []
        for kind,label in [('SeepageBoundaryDrainWell','排水井'),('SeepageBoundaryNaturalSeepage','天然自由面')]:
            if label not in n['title']:continue
            boundary = child(n,kind)['state']
            if not boundary.get('picked_solid') and not boundary.get('picked_line'):
                blockers.append(label+'选择为空，需补选物理边界')
        if '结冰' in n['title']: blockers.append('需核对结冰区域及冰材料参数，不能仅凭功能开关认定有效')
        if n['type'] == 'SeepageAnalysis' and '快速降水' in n['title']:
            n['state']['compute_type'] = 2
            up = child(n, 'SeepageBoundaryUpstreamWater')['state']
            up['water_level_curve'] = [{'time': 0., 'head': 176.5}, {'time': 1200., 'head': 125.5}]
            n['state'].update(upstream_water_level=176.5, tsta=0., tend=1200., dt=120.)
        if n['type']=='SeepageAnalysis' and '分级蓄水' in n['title']:
            previous = float(n['state']['tend'])
            n['state'].update(compute_type=2,tend=600.)
            for boundary in n['children']:
                for row in boundary.get('state',{}).get('water_level_curve',[]):
                    row['time'] = float(row['time'])*600./previous
        add_case(project, n, [template['title']], cases, blockers=blockers)
        if cases[-1]['id'] in ['D11','D12']:cases[-1]['timeout_seconds']=600
    steady = next(n for n in project['items'] if n['title'].startswith('D02'))
    wet = next(n for n in project['items'] if '单阶段填筑蓄水' in n['title'])
    coupled = clone(wet, 'D14－稳态渗流结果耦合蓄水')
    f = child(coupled, 'FillStorageCycle')['state']
    f.update(seepage_source_uuid=steady['uuid'], seepage_source_name=steady['title'], seepage_result_type='steady')
    f['circle_table'][-1][4]=str(steady['state']['upstream_water_level'])
    # Homogeneous dam export fixes IFIIWL_SEEP=0. The actual imported seepage
    # body-load switch is IFIIWL_BODY=2 (1 is an obsolete solver branch).
    f['values'].update(IFIIWL_SEEP='0',IFIIWL_BODY='2')
    add_case(project, coupled, ['渗流结果引用', '孔压耦合'], cases, ['D02'])
    cases[-1]['timeout_seconds']=600
    modal = next(n for n in project['items'] if '模态测试' in n['title'])
    for num, quiet in [(15, False), (16, True)]:
        n = clone(modal, f'D{num}－短脉冲时程'+('与静力边界转换' if quiet else ''))
        n['state']['quiet_boundary_enabled'] = quiet
        child(n, 'DamDynamicAnalysis')['state']['values'].update(IFMODE='0', IFWAVE='1', DT='.02', TSTA='0', TEND='2')
        add_case(project, n, ['静动力联合', '非零地震时程', '静力边界转换' if quiet else '固定边界'], cases,
                 blockers=['需核对动力材料映射；非零地震输入已提供'])
        if num==15:cases[-1]['timeout_seconds']=900
    return project, cases


def cave(seed, target):
    project = prepare(seed, target, target.name)
    # The seed assigns the same ordinary-rock entry to both the cavity and
    # surrounding rock. Native LXCONMAT needs an actual excavation role.
    path = target/'工程数据/material.json'
    materials = read(path)
    model = next(m for m in materials['models'] if m['key']=='huangjinping_underground_plant')
    base = next(e for e in model['entries'] if 3 in e['applied_matypes'])
    rock = copy.deepcopy(base)
    rock.update(name='未开挖围岩', applied_matypes=[4])
    rock['properties'].update({'编号':'4','母岩材料编号':'4','岩类分区':'0'})
    excavation = copy.deepcopy(base)
    excavation.update(id='{'+str(uuid.uuid4())+'}', name='开挖围岩', applied_matypes=[3])
    excavation['properties'].update({'编号':'3','母岩材料编号':'4','岩类分区':'-1'})
    model['entries'] = [rock, excavation]
    materials['added_entry_ids'] = [e['id'] for e in model['entries']]
    write(path, materials)
    templates = project['items']; project['items'] = []; cases = []
    names = ['整体开挖', '分步开挖', '整体加固', '单元法分步开挖', '单元法分步加固']
    for i, template in enumerate(templates):
        n = clone(template, f'C{i+1:02d}－'+names[i])
        blockers = []
        if i in (2, 4):
            field = child(n, 'CaveSupportComputation')['state'].setdefault('reinforcement_field', {})
            field.update(mode=2,bar_length=9.,bar_delta_c_mpa=.3,cable_length=30.,
                         cable_delta_c_mpa=.5,concept_stiffness=1.05)
        add_case(project, n, [names[i], '地应力回归', '单元卸载法' if i >= 3 else '节点面力卸载法'], cases, blockers=blockers)
    for num, modal, wave, quiet in [(6, True, False, False), (7, False, True, False), (8, False, True, True)]:
        n = clone(templates[1], f'C{num:02d}－'+('开挖后模态' if modal else '短脉冲时程')+('与静力边界转换' if quiet else ''))
        s = child(n, 'CaveSupportComputation')['state']
        s.update(ifmode=str(int(modal)), ifwave=str(int(wave)), quiet_boundary_enabled=quiet,
                 dt=.02, tsta=0., tend=2., line_22='3')
        add_case(project, n, ['分步开挖', '模态' if modal else '动力时程', '静力边界转换' if quiet else '固定边界'], cases,
                 blockers=['需核对当前网格约束和动力材料适用性'])
    n = clone(templates[3], 'C09－三步开挖工序')
    support = child(n, 'CaveSupportComputation')['state']
    # Use only the two existing selected excavation domains, do not invent a third.
    n['title'] = n['custom_title'] = 'C09－分步开挖施工间隔对照'
    support['excavation_days'] = ['1', '3']
    add_case(project, n, ['相同开挖域不同施工间隔', '时间参数输入'], cases)
    return project, cases


def slope(seed, target):
    project = prepare(seed, target, target.name)
    template = project['items'][0]; project['items'] = []; cases = []
    definitions = [('S01－静力基准', '1', '0', '0', False, [1.]),
                   ('S02－强度折减扫描', '1', '0', '0', False, [1., 1.1, 1.2, 1.3, 1.4]),
                   ('S03－静力后模态', '3', '1', '0', False, [1.]),
                   ('S04－静力后短脉冲时程', '3', '0', '1', False, [1.]),
                   ('S05－静力边界转换时程', '3', '0', '1', True, [1.]),
                   ('S06－折减系数1.2的动力对照', '3', '0', '1', False, [1.2])]
    for title, mode, modal, wave, quiet, factors in definitions:
        n = clone(template, title);s = n['state'];s['base_values'][18] = mode
        s.update(ifmode=modal, ifwave=wave, dt='.02', tsta='0', tend='2', quiet_boundary_enabled=quiet,
                 strength_reserve=[{'factor': str(v), 'ifpr': '1'} for v in factors])
        add_case(project, n, [title.split('－')[1]], cases,
                 blockers=['需核对动力材料参数和约束适用性'] if mode == '3' else [])
    return project, cases


def normalize_paths(value, seed, target):
    if isinstance(value, dict): return {k: normalize_paths(v, seed, target) for k,v in value.items()}
    if isinstance(value, list): return [normalize_paths(v, seed, target) for v in value]
    if isinstance(value, str):
        return value.replace(str(seed).replace('/', '\\'), str(target).replace('/', '\\')).replace(str(seed),str(target))
    return value


def verify(root):
    manifest = read(root/'coverage.json');errors = []
    for record in manifest['projects']:
        project = root/record['directory'];data = project/'工程数据';solver = read(data/'solver.json')
        nodes = {n['uuid']: n for n in solver['items']}
        all_ids=[page['uuid'] for n in solver['items'] for page in walk(n)]
        if len(all_ids)!=len(set(all_ids)):errors.append('分析节点 UUID 重复')
        if len(nodes) != len(record['cases']):errors.append(record['directory']+': 工况数不一致')
        for case in record['cases']:
            if case['analysis_uuid'] not in nodes:errors.append(case['id']+': 工况丢失')
        xml = ET.parse(project/(project.name+'.xml'))
        for tag in xml.getroot().find('ProjectFiles'):
            if tag.tag != 'Surrogate' and not (project/tag.text).exists():errors.append(str(tag.text)+': 工程文件缺失')
        for filename in ['IIIN.DAT','IMAT.DAT','IIID.DAT']:
            if not (data/'计算输入'/filename).is_file() and not list(project.glob('*/计算输入/'+filename)):
                errors.append(record['directory']+': '+filename+' 缺失')
        physical = read(data/'physical_field.json')
        waves = [n for n in physical['items'] if n.get('kind')=='SeismicAndAddedMassPage']
        requires_wave=any(str(p.get('state',{}).get('ifwave',p.get('state',{}).get('values',{}).get('IFWAVE','0')))!='0'
            for n in solver['items'] for p in walk(n))
        if (requires_wave or waves) and (len(waves)!=1 or len(waves[0]['page_data']['rows'])!=101):errors.append('共享地震加速度表无效')
        for axis in (['AX','AY','AZ'] if requires_wave or waves else []):
            lines=(data/'计算输入'/f'WAVE-{axis}.DAT').read_text('ascii').splitlines()
            if int(lines[0].split()[0])!=101 or len(lines)!=103:errors.append('地震波采样数不一致')
            if not any(abs(float(line.split()[1]))>1.e-8 for line in lines[2:]):errors.append('地震波为零')
        for node in solver['items']:
            if node['title'].startswith('D14'):
                fill=child(node,'FillStorageCycle')['state']
                if fill['values'].get('IFIIWL_BODY')!='2' or fill['seepage_source_uuid'] not in nodes:
                    errors.append('渗流耦合未实际启用或关联丢失')
                else:
                    source=nodes[fill['seepage_source_uuid']]
                    boundary=child(source,'SeepageBoundaryUpstreamWater')['state']
                    if boundary.get('boundary_condition_type')!='head' or abs(float(fill['circle_table'][-1][4])-float(boundary['water_level']))>1.e-6:
                        errors.append('D14定水头耦合对照的施工水位与关联初始水头不一致')
            if node['type']=='SeepageAnalysis' and '快速降水' in node['title']:
                if node['state']['compute_type'] not in (2,3):errors.append('快速降水仍为稳态')
        for case in record['cases']:
            if any(dep not in {c['id'] for c in record['cases']} for dep in case['depends_on']):errors.append('工况依赖丢失')
    evidence=manifest['solver_source']
    if digest(Path(evidence['file']))!=evidence['sha256']:errors.append('求解器源码在记录后发生变化')
    result={'project_count':len(manifest['projects']), 'case_count':sum(len(p['cases']) for p in manifest['projects']),
            'prepared_not_run':sum(c['status']=='prepared_not_run' for p in manifest['projects'] for c in p['cases']),
            'blocked_inputs':sum(c['status']=='blocked_inputs' for p in manifest['projects'] for c in p['cases']),
            'checks':'project structure, input presence, UUID references, wave samples, source hash',
            'solver_executed':any(c.get('result_status','not_run')!='not_run' for p in manifest['projects'] for c in p['cases']),
            'errors':errors}
    write(root/'preflight.json',result)
    if errors:raise ValueError(errors)
    print(json.dumps(result,ensure_ascii=False))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed-root',type=Path,default=Path('C:/test/rock'))
    parser.add_argument('--solver-source',type=Path,default=Path('C:/D_Drive/NASGEWIN64'))
    parser.add_argument('--output',type=Path)
    parser.add_argument('--verify',type=Path)
    args=parser.parse_args()
    if args.verify:return verify(args.verify)
    if not args.output:parser.error('--output is required')
    if args.output.exists():parser.error('Output already exists; choose a new directory to preserve local changes')
    evidence=source_evidence(args.solver_source)
    args.output.mkdir(parents=True)
    manifest={'version':1,'solver_source':evidence,'projects':[],
              'scope':'3D geotechnical regression; no certified numerical baseline yet',
              'not_covered':['二维单元分支','GPU/CPU一致性','材料本构逐模型验证','接触与节理专门模型',
                             '衬砌与吊车梁完整材料/区域定义','渗流排水井区域','冰冻融化完整输入',
                             '断点续算与仅动力复用','网格收敛和解析解精度基准']}
    for seed_name,name,builder in [('Z03_JUNZ3D_13_多工况','坝_多工况回归',dam),
                                  ('7209_洞室多工况','洞室_多工况回归',cave),
                                  ('Z08_SLOP3D','边坡_多工况回归',slope)]:
        seed=args.seed_root/seed_name;target=args.output/name
        # Seed fingerprints prove preparation did not mutate existing examples.
        seed_hashes={str(p.relative_to(seed)):digest(p) for p in (seed/'工程数据').rglob('*') if p.is_file()}
        project,cases=builder(seed,target)
        project=normalize_paths(project,seed,target)
        for n in project['items']:
            for page in walk(n):
                values=page.get('state',{}).get('base_values')
                if values:values[0]=str(target/'工程数据/计算输入')+'/'
        write(target/'工程数据/solver.json',project)
        install_pulse(target)
        changed=[name for name,old in seed_hashes.items() if digest(seed/name)!=old]
        if changed:raise RuntimeError('Seed modified: '+str(changed))
        manifest['projects'].append({'directory':name,'seed':str(seed),'seed_files_unchanged':True,'cases':cases})
    write(args.output/'coverage.json',manifest)
    lines=['# 岩土多工况回归工程','',
           '分别打开各子目录中的同名 XML。每个分析节点是独立工况，共用本工程网格、材料和约束。',
           '全部工况尚未运行。prepared_not_run 只表示配置已建立，不代表数值正确；blocked_inputs 表示还有明确的输入待核对。',
           '求解器源码保持只读，没有复制、部署或运行构建目录中的旧 exe，也没有改写全局 IIIDIR.DAT。',
           '运行前请先部署与当前源码匹配的求解器，再用当前前端逐工况生成控制文件并计算，勿直接回放种子中的旧 CASE 输出。',
           '地震输入是 0–2 秒、步长 0.02 秒的低幅合成脉冲，仅用于功能回归，不代表设计地震。',
           '结果验收：先确认生成文件和功能开关，再检查正常结束与新结果，最后人工确认物理量并冻结基准值。',
           '','|工程|工况|覆盖|准备状态|待补输入|','|---|---|---|---|---|']
    for record in manifest['projects']:
        for case in record['cases']:
            lines.append('|'+ '|'.join([record['directory'],case['title'],'、'.join(case['features']),case['status'],'；'.join(case['blockers'])])+'|')
    lines+=['','## 仍未覆盖','']+['- '+s for s in manifest['not_covered']]
    (args.output/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    verify(args.output)


if __name__=='__main__':main()
