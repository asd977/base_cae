"""Exercise actual plugin page load/save on an isolated copy, without solving."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import uuid
from build_geotechnical_regression import read,write

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path)
    ap.add_argument('--output-name',default='load_page_roundtrip');ap.add_argument('--planar-beam',action='store_true');a=ap.parse_args()
    seed=a.suite/('梁边界对照_梁端指定转角' if a.planar_beam else '荷载对照_均匀变温');target=a.suite/'validation'/a.output_name
    target.mkdir(parents=True,exist_ok=False);shutil.copytree(seed/'工程数据',target/'工程数据')
    definitions={
        'DisplacementConstraint':dict(ux='2',uy='1',uz='1',prescribed_values=[.0002,0.,0.],rz='2' if a.planar_beam else '1',prescribed_rz=-.0001 if a.planar_beam else 0.),
        'PointForcePage':dict(spinFx=250,spinFy=2,spinFz=3,spinMx=4000,spinMy=-5000,spinMz=1000),
        'LinearForcePage':dict(lineEditX='10',lineEditY='20',lineEditZ='30'),
        'FaceLoadPage':dict(lineEditX='40',lineEditY='50',lineEditZ='60'),
        'BodyForcePage':dict(spinFbx=1,spinFby=2,spinFbz=3),
        'TemperatureLoadPage':dict(lineEditAlfa='0.00001',lineEditDT='20'),
        'BeamLoadPage':dict(spinBoxLDTYPE=6,doubleSpinBoxA=.25,doubleSpinBoxP=-123456.5,spinBoxIDIR=1),
        'GeoStressPointPage':dict(spinSx=1,spinSy=2,spinSz=3,spinSxy=4,spinSxz=5,spinSyz=6),
        'GeoStressFitFacePage':dict(editFname='FA_TEST.DAT',spinII=1,spinRou=2.5,spinHeight=40,spinPgrout=10,comboIdir=1),
    }
    path=target/'工程数据/physical_field.json'
    write(path,{'module':'PhysicalField','root_title':'约束与荷载','items':[
        {'uuid':'{'+str(uuid.uuid4())+'}','kind':kind,'domain':'DisplacementConstraintWidget' if kind=='DisplacementConstraint' else kind,'title':kind,'page_data':values} for kind,values in definitions.items()]})
    driver=Path(__file__).resolve().parents[1]/'build/smesh_release/SolverRegressionDriver.exe'
    node=read(target/'工程数据/solver.json')['items'][0]
    env=os.environ.copy();env['PATH']='C:/Qt/6.7.3/msvc2022_64/bin;'+env['PATH']
    with (target/'driver.log').open('wb') as log:
        result=subprocess.run([str(driver),str(target),node['uuid'],'gpu','roundtrip-loads'],cwd=driver.parent,env=env,
            stdout=log,stderr=subprocess.STDOUT,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
    failures=[]
    for item in read(path)['items']:
        for key,expected in definitions[item['kind']].items():
            actual=item['page_data'].get(key)
            if actual!=expected:failures.append({'page':item['kind'],'field':key,'expected':expected,'actual':actual})
    report={'returncode':result.returncode,'pages':len(definitions),'failures':failures,'passed':result.returncode==0 and not failures}
    write(target/'result.json',report);print(report)
    if not report['passed']:raise SystemExit(1)

if __name__=='__main__':main()
