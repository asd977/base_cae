"""Compare real CPU/GPU native results with matching frontend input hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from build_geotechnical_regression import read,write

def nodal(path):
    return {int(row.split()[0]):[float(v) for v in row.split()[1:4]] for row in path.read_text('gbk').splitlines()[3:]}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path)
    ap.add_argument('--cases',nargs='+');ap.add_argument('--output-name',default='device_comparison');a=ap.parse_args()
    output=a.suite/a.output_name;output.mkdir(exist_ok=True)
    definitions=[(project,case) for project in read(a.suite/'coverage.json')['projects']
        for case in project['cases'] if case['id'].startswith('M') and (not a.cases or case['id'] in a.cases)]
    def run_device(device):
        result=subprocess.run([sys.executable,'-X','utf8',str(Path(__file__).with_name('run_geotechnical_regression.py')),str(a.suite),
            '--cases']+[case['id'] for _,case in definitions]+['--devices',device,'--comparison-only','--timeout','90'],
            check=True,capture_output=True,text=True,encoding='utf8')
        print(result.stdout,end='',flush=True)
        path=Path(result.stdout.strip().splitlines()[-1]);report=read(path)
        if len(report['cases'])!=len(definitions) or not all(case.get('passed') for case in report['cases']):
            raise RuntimeError(device+' comparison run did not pass all analytical checks')
        return str(path)
    gpu_report=run_device('gpu')
    cases=[]
    for project in read(a.suite/'coverage.json')['projects']:
        for case in project['cases']:
            if not case['id'].startswith('M') or (a.cases and case['id'] not in a.cases):continue
            workspace=a.suite/project['directory']/case['title']
            files=['IIIN.DAT','IIID.DAT','IIWL.DAT','IMAT.DAT']
            cases.append({'id':case['id'],'workspace':str(workspace),'planar_beam':case.get('analytic_displacement',{}).get('field')=='planar_beam','gpu':nodal(workspace/'CASE01/IIDISP.TXT'),
                'gpu_input_sha256':{name:hashlib.sha256((workspace/'计算输入'/name).read_bytes()).hexdigest() for name in files}})
    write(output/'gpu_before_cpu.json',cases)
    cpu_report=run_device('cpu')
    for case in cases:
        workspace=Path(case['workspace']);cpu=nodal(workspace/'CASE01/IIDISP.TXT');gpu=case['gpu']
        case['cpu']=cpu
        case['same_nodes']=set(cpu)==set(gpu)
        case['max_difference_m']=max(abs(v-cpu[i][j]) for i,row in gpu.items() for j,v in enumerate(row[:2] if case['planar_beam'] else row)) if case['same_nodes'] else None
        case['max_rotation_difference_rad']=max(abs(row[2]-cpu[i][2]) for i,row in gpu.items()) if case['same_nodes'] and case['planar_beam'] else 0
        case['cpu_input_sha256']={name:hashlib.sha256((workspace/'计算输入'/name).read_bytes()).hexdigest() for name in case['gpu_input_sha256']}
        case['input_match']=case['cpu_input_sha256']==case['gpu_input_sha256']
        case['passed']=case['input_match'] and case['same_nodes'] and case['max_difference_m']<=1.e-9 and case['max_rotation_difference_rad']<=1.e-9
    write(output/'comparison.json',{'cases':cases,'passed':all(c['passed'] for c in cases),
        'gpu_run_report':gpu_report,'cpu_run_report':cpu_report,
        'scope':'Selected small mechanical models only; displacement(m) and beam rotation(rad) checked separately; not general GPU/CPU equivalence.'})
    print([(c['id'],c['passed'],c['max_difference_m']) for c in cases])

if __name__=='__main__':main()
