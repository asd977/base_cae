"""Invalid boundary/restart settings must be rejected before native launch."""
import argparse
import copy
import ctypes
import os
import shutil
import subprocess
import uuid
from pathlib import Path
from build_geotechnical_regression import read,write

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    if Path('D:/IIIDIR.regression.lock').exists():raise RuntimeError('Do not run alongside native regression')
    processes=subprocess.check_output(['tasklist','/FO','CSV','/NH'],creationflags=subprocess.CREATE_NO_WINDOW).decode('gbk',errors='replace').lower()
    if 'pccae_cpu.exe' in processes or 'pccae_gpu.exe' in processes:raise RuntimeError('A native solver is already running')
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    output=a.suite/'validation/boundary_rejections_20261029';output.mkdir(parents=True,exist_ok=False)
    driver=Path(__file__).resolve().parents[1]/'build/smesh_release/SolverRegressionDriver.exe'
    env=os.environ.copy();env['PATH']='C:/Qt/6.7.3/msvc2022_64/bin;'+env['PATH']
    checks=[]
    def run(name,seed,expected,change,static=False):
        src=a.suite/seed;dst=output/name;dst.mkdir();shutil.copytree(src/'工程数据',dst/'工程数据')
        settings=dst/'工程数据/solver.json';solver=read(settings)
        node=next(n for n in solver['items'] if n['title'].startswith('D17－')) if static else solver['items'][0]
        solver['items']=[node];write(settings,solver)
        if static:
            for folder in ['DISK00','CASE01WETT']:shutil.copytree(src/node['title']/folder,dst/node['title']/folder)
        change(dst,node)
        with (dst/'driver.log').open('wb') as log:
            result=subprocess.run([str(driver),str(dst),node['uuid'],'gpu','analysis','30'],cwd=driver.parent,env=env,
                stdout=log,stderr=subprocess.STDOUT,timeout=50,creationflags=subprocess.CREATE_NO_WINDOW)
        text=(dst/'driver.log').read_text('utf8',errors='replace')
        ok=result.returncode==8 and expected in text and 'IDEVICE=' not in text and 'started=0' in text
        checks.append({'name':name,'passed':ok,'returncode':result.returncode,'expected_message':expected,
            'native_launched':'IDEVICE=' in text,'log':str(dst/'driver.log')});print(name,ok,flush=True)
    def conflict(dst,node):
        path=dst/'工程数据/physical_field.json';p=read(path);extra=copy.deepcopy(p['items'][-1]);extra['uuid']='{'+str(uuid.uuid4())+'}'
        extra['title']='冲突的固定约束';extra['page_data']['ux']='0';p['items'].append(extra);write(path,p)
    def wrong_mode(dst,node):
        path=dst/'工程数据/solver.json';p=read(path);p['items'][0]['state']['base_values'][7]='1';write(path,p)
    def moment(dst,node):
        path=dst/'工程数据/physical_field.json';p=read(path)
        next(n for n in p['items'] if n['kind']=='PointForcePage')['page_data']['spinMz']=1000;write(path,p)
    def solid_rotation(dst,node):
        mesh='4 2 1 4 5 1 0 0 0\n7 2 0 2 0 1 0 0\n0 0 1\n2 0 2\n2 1 3\n0 1 4\n1 1 2 3 4 3 3 1 0 0\n'
        for path in [dst/'工程数据/mesh_snapshot.iiin.dat',dst/'工程数据/计算输入/IIIN.DAT']:path.write_text(mesh,encoding='ascii')
        path=dst/'工程数据/physical_field.json';p=read(path);p['items']=p['items'][:1];n=p['items'][0]
        n.pop('node_line',None);n['node_solid']=[0];n['page_data']['rz']='0';write(path,p)
    def changed_boundary(dst,node):
        path=dst/'工程数据/physical_field.json';p=read(path)
        n=next(n for n in p['items'] if n['kind']=='DisplacementConstraint');v=n['page_data'];v['ux']='1' if v['ux']=='0' else '0';write(path,p)
    def changed_state(dst,node):
        path=dst/node['title']/'CASE01WETT/ISGMAR.TXT';path.write_bytes(path.read_bytes()+b'\n')
    def missing_certificate(dst,node):(dst/node['title']/'CASE01WETT/STATIC_STATE.json').unlink()
    lock=Path('D:/IIIDIR.regression.lock')
    originals={p:p.read_bytes() if p.exists() else None for p in [Path('D:/IIIDIR.DAT'),Path('C:/IIIDIR.DAT')]}
    with lock.open('x') as stream:stream.write(str(os.getpid()))
    try:
        run('conflicting_prescribed_displacement','边界扩展_正向指定位移','位移约束冲突',conflict)
        run('prescribed_displacement_in_auto_load','边界扩展_正向指定位移','请检查指定位移',wrong_mode)
        run('moment_on_solid','荷载对照_等效节点点力','节点力矩不适用',moment)
        run('rotation_on_solid','梁边界对照_梁端指定转角','转角约束不适用',solid_rotation)
        run('restart_changed_boundary','坝_多工况回归','静力续算校验失败',changed_boundary,True)
        run('restart_changed_state','坝_多工况回归','静力续算校验失败',changed_state,True)
        run('restart_missing_certificate','坝_多工况回归','静力续算校验失败',missing_certificate,True)
    finally:
        for path,content in originals.items():
            current=path.read_bytes() if path.exists() else None
            if current==content:continue
            if content is None:path.unlink()
            else:path.write_bytes(content)
        lock.unlink()
    restored=all((p.read_bytes() if p.exists() else None)==b for p,b in originals.items())
    report={'checks':checks,'passed':len(checks)==7 and all(c['passed'] for c in checks) and restored,
        'global_directory_files_restored':restored,'scope':'Expected frontend rejection; excluded from numerical solver case pass counts.'}
    write(output/'result.json',report)
    if not report['passed']:raise SystemExit(1)

if __name__=='__main__':main()
