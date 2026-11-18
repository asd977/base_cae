"""Prescribed translations, Poisson response, and compatible thermal-gradient fields."""
import argparse
import copy
import shutil
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path
from build_geotechnical_regression import read,write,clone,add_case

def uid():return '{'+str(uuid.uuid4())+'}'

def mesh(nx,ny,nz,quadratic=False):
    points=[];indices={};cells=[]
    def node(p):
        if p not in indices:indices[p]=len(points)+1;points.append(p)
        return indices[p]
    edge_slots=[(0,1),(1,2),(2,3),(3,0),(4,5),(5,6),(6,7),(7,4),(0,4),(1,5),(2,6),(3,7)]
    native=[0,8,1,9,2,10,3,11,16,17,18,19,4,12,5,13,6,14,7,15]
    faces=[]
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                x,X=2*i/nx,2*(i+1)/nx;y,Y=j/ny,(j+1)/ny;z,Z=k/nz,(k+1)/nz
                vertices=[(x,y,z),(X,y,z),(X,Y,z),(x,Y,z),(x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)]
                if quadratic:vertices += [tuple((vertices[a][axis]+vertices[b][axis])/2 for axis in range(3)) for a,b in edge_slots]
                ids=[node(p) for p in vertices];cells.append([ids[q] for q in native] if quadratic else ids)
                if i==nx-1:faces.append((len(cells)-1)*16+1)
    return points,cells,faces

def constraint(title,points,selection,flags,values=None):
    return {'uuid':uid(),'kind':'DisplacementConstraint','domain':'DisplacementConstraintWidget','title':title,
        'node_solid':[i for i,p in enumerate(points) if selection(p)],'page_data':dict(zip(['ux','uy','uz'],flags),**({'prescribed_values':values} if values else {}))}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    if Path('D:/IIIDIR.regression.lock').exists():raise RuntimeError('Do not change coverage while a runner owns its snapshot')
    coverage=read(a.suite/'coverage.json');seed=a.suite/'荷载对照_均匀面力'
    template=read(seed/'工程数据/solver.json')['items'][0]
    definitions=[('M06','正向指定位移',2,1,1,False,'prescribed',.0002),
        ('M07','负向指定位移',2,1,1,False,'prescribed',-.0002),
        ('M08','非零刚体平移',2,1,1,False,'translation',.0001),
        ('M09','泊松收缩粗网格',1,1,1,False,'poisson',.0002),
        ('M10','泊松收缩细网格',4,2,2,False,'poisson',.0002),
        ('M11','轴向温差梯度',2,2,2,True,'gradient_x',.0002),
        ('M12','横向温差热弯曲',2,2,2,True,'gradient_y',.0002)]
    for identifier,label,nx,ny,nz,quadratic,kind,value in definitions:
        directory='边界扩展_'+label;target=a.suite/directory
        if target.exists():raise RuntimeError('Preserve existing project '+str(target))
        data=target/'工程数据';inp=data/'计算输入';inp.mkdir(parents=True)
        xml=ET.parse(next(seed.glob('*.xml')));xml.getroot().find('Name').text=directory
        xml.write(target/(directory+'.xml'),encoding='utf-8',xml_declaration=True)
        points,cells,faces=mesh(nx,ny,nz,quadratic)
        text=f'{len(points)} 3 {len(cells)} {20 if quadratic else 8} 5 1 0 0 0\n7 3 0 2 0 1 0 1\n'
        text+=''.join(' '.join(map(str,p))+f' {i+1}\n' for i,p in enumerate(points))
        text+=''.join(f'{i+1} '+' '.join(map(str,ids))+f' {37 if quadratic else 6} 3 1 0 0\n' for i,ids in enumerate(cells))
        for path in [data/'mesh_snapshot.iiin.dat',inp/'IIIN.DAT']:path.write_text(text,encoding='ascii')
        mesh_state=read(seed/'工程数据/mesh.json');write(data/'mesh.json',mesh_state)
        tags=read(seed/'工程数据/mesh_map.json');tags['tag_sets'][0]['cell_ids']=[str(i) for i in range(len(cells))];write(data/'mesh_map.json',tags)
        material=read(seed/'工程数据/material.json')
        for model in material['models']:
            for entry in model['entries']:entry['properties']['泊松比μ']='0.25' if kind=='poisson' else '0'
        write(data/'material.json',material)
        for name in ['preprocessing.json','postprocessing.json']:shutil.copy2(seed/'工程数据'/name,data/name)
        expected={'x_end_m':value,'length_x_m':2,'thermal':False,'tolerance_m':2.e-7}
        items=[]
        if kind.startswith('gradient'):
            axis=0 if kind=='gradient_x' else 1
            first=(2.,0.,0.) if axis==0 else (0.,1.,0.)
            second=(0.,1.,0.) if axis==0 else (0.,0.,1.)
            flags=['0']*3;flags[axis]='1'
            items.append(constraint('原点三向约束',points,lambda p:p==(0,0,0),['0','0','0']))
            items.append(constraint('梯度轴去除刚体转动',points,lambda p:p==first,flags))
            flags=['1']*3;flags[2 if axis==0 else 0]='0'
            items.append(constraint('第三点去除刚体转动',points,lambda p:p==second,flags))
            for temperature in sorted({10*p[axis] for p in points}):
                items.append({'uuid':uid(),'kind':'TemperatureLoadPage','domain':'TemperatureLoadPage','title':f'节点温差 {temperature} K',
                    'node_solid':[i for i,p in enumerate(points) if 10*p[axis]==temperature],
                    'page_data':{'lineEditAlfa':'0.00001','lineEditDT':str(temperature)}})
            expected.update(field='thermal_gradient',gradient_axis=axis,strain_gradient_per_m=.0001)
        else:
            for axis,flags in [(0,['0','1','1']),(1,['1','0','1']),(2,['1','1','0'])]:
                values=None
                if axis==0 and kind=='translation':flags[0]='2';values=[value,0,0]
                items.append(constraint('坐标零面约束',points,lambda p,axis=axis:p[axis]==0,flags,values))
            if kind in ('prescribed','translation'):
                items.append(constraint('末端指定位移',points,lambda p:p[0]==2,['2','1','1'],[value,0,0]))
                if kind=='translation':expected.update(x_end_m=0,offset_x_m=value)
            else:
                items.append({'uuid':uid(),'kind':'FaceLoadPage','domain':'FaceLoadPage','title':'末端均匀面力',
                    'face_solid':faces,'page_data':{'lineEditX':'1000','lineEditY':'0','lineEditZ':'0'}})
                expected['poisson']=.25
        write(data/'physical_field.json',{'module':'PhysicalField','root_title':'约束与荷载','items':items})
        project={'module':'Solver','root_title':'计算分析','state':{'compute_device':'gpu','gpu_device_mode':2},'items':[]};cases=[]
        node=clone(template,identifier+'－'+label);add_case(project,node,[label,'独立解析位移场'],cases)
        cases[0]['analytic_displacement']=expected
        write(data/'solver.json',project)
        record={'directory':directory,'seed':'structured independent compatible-displacement patch model','cases':cases}
        write(target/'case_coverage.json',record);coverage['projects'].append(record)
        write(target/'model_definition.json',{'nodes':len(points),'elements':len(cells),'ketype':37 if quadratic else 6,'analytic':expected})
    write(a.suite/'coverage.json',coverage)
    write(a.suite/'coverage_extension_20261029.json',{'status':'in_progress','new_cases':[d[0] for d in definitions],
        'scope':['非零指定位移','泊松收缩及网格细化','非均匀温差与自由热弯曲'],'solver_source_modified':False})

if __name__=='__main__':main()
