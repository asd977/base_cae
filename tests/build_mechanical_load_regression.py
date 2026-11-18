"""Independent load patch tests using the real frontend and native SLOP IIWL route."""
import argparse
import copy
import shutil
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path
from build_geotechnical_regression import read,write,clone,add_case

def uid():return '{'+str(uuid.uuid4())+'}'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    coverage=read(a.suite/'coverage.json')
    seed=a.suite/'洞室_边界专项回归'
    template=read(a.suite/'边坡_多工况回归/工程数据/solver.json')['items'][0]
    points=[(0,0,0),(2,0,0),(2,1,0),(0,1,0),(0,0,1),(2,0,1),(2,1,1),(0,1,1)]
    definitions=[('M01','均匀面力','FaceLoadPage',{'lineEditX':'1000','lineEditY':'0','lineEditZ':'0'},{'face_solid':[1]},.0002),
        ('M02','等效节点点力','PointForcePage',{'spinFx':250,'spinFy':0,'spinFz':0,'spinMx':0,'spinMy':0,'spinMz':0},{'node_solid':[2,3,6,7]},.0002),
        ('M03','四边线荷载','LinearForcePage',{'lineEditX':'250','lineEditY':'0','lineEditZ':'0'},{'edge_solid':[1,5,9,11]},.0002),
        ('M04','任意方向体力','BodyForcePage',{'spinFbx':1,'spinFby':0,'spinFbz':0},{},.0002),
        ('M05','均匀变温','TemperatureLoadPage',{'lineEditAlfa':'0.00001','lineEditDT':'20'},{'node_solid':list(range(1,9))},.0004)]
    for identifier,label,kind,params,selection,end in definitions:
        directory='荷载对照_'+label;target=a.suite/directory
        if target.exists():raise RuntimeError('Preserve existing project: '+str(target))
        data=target/'工程数据';inp=data/'计算输入';inp.mkdir(parents=True)
        xml=ET.parse(next(seed.glob('*.xml')));xml.getroot().find('Name').text=directory
        xml.write(target/(directory+'.xml'),encoding='utf-8',xml_declaration=True)
        text='8 3 1 8 5 1 0 4 2\n7 3 0 2 0 1 0 1\n'
        text+=''.join(f'{x} {y} {z} {i+1}\n' for i,(x,y,z) in enumerate(points))
        text+='1 1 2 3 4 5 6 7 8 6 3 1 0 0\n'
        for p in [data/'mesh_snapshot.iiin.dat',inp/'IIIN.DAT']:p.write_text(text,encoding='ascii')
        write(data/'mesh.json',{'module':'Mesh','root_title':'网格','has_grid':True,'snapshot_file':'工程数据/mesh_snapshot.iiin.dat','mesh_restore_mode':'dat','vertical_axis':3,'nodes':[]})
        write(data/'mesh_map.json',{'mesh_dim':3,'material_group_category':'MATYPE','excavation_group_category':'LAYER',
            'tag_sets':[{'key':{'category':'MATYPE','dim':3,'id':3},'name':'弹性小应变材料','cell_ids':['0'],'point_ids':[],'face_keys':[],'edge_keys':[],'source':'patch test'}],
            'phys_id_to_dim':{},'phys_id_to_name':{}})
        material=read(seed/'工程数据/material.json');entry=copy.deepcopy(next(m for m in material['models'] if m['key']=='huangjinping_underground_plant')['entries'][0])
        entry.update(id=uid(),name='低荷载弹性对照',applied_matypes=[3]);entry['properties'].update({'变形模量E（GPa）':'0.01','泊松比μ':'0','容重t/m3':'1'})
        for m in material['models']:m['entries']=[entry] if m['key']=='huangjinping_underground_plant' else []
        material['added_entry_ids']=[entry['id']];write(data/'material.json',material)
        items=[]
        for axis,mask in [(0,('0','1','1')),(1,('1','0','1')),(2,('1','1','0'))]:
            items.append({'uuid':uid(),'kind':'DisplacementConstraint','domain':'DisplacementConstraintWidget','title':'坐标零面法向约束',
                'node_solid':[i for i,p in enumerate(points) if p[axis]==0],'page_data':dict(zip(['ux','uy','uz'],mask))})
        persisted_selection={key:([i-1 for i in ids] if key=='node_solid' else ids) for key,ids in selection.items()}
        items.append({'uuid':uid(),'kind':kind,'domain':kind,'title':label,'page_data':params,**persisted_selection})
        write(data/'physical_field.json',{'module':'PhysicalField','root_title':'约束与荷载','items':items})
        for name in ['preprocessing.json','postprocessing.json']:shutil.copy2(seed/'工程数据'/name,data/name)
        project={'module':'Solver','root_title':'计算分析','state':{'compute_device':'gpu','gpu_device_mode':2},'items':[]};cases=[]
        node=clone(template,identifier+'－'+label);values=node['state']['base_values'];values[3]='0 0 0';values[7]='2';values[5]='20'
        node['state']['strength_reserve']=[{'factor':'1.0','ifpr':'1'}]
        add_case(project,node,[label,'IIWL自定义荷载入口','单元补丁对照'],cases)
        cases[0]['analytic_displacement']={'x_end_m':end,'length_x_m':2.,'tolerance_m':2.e-7,'thermal':identifier=='M05'}
        write(data/'solver.json',project)
        record={'directory':directory,'seed':'independent unit cube patch test','cases':cases}
        write(target/'case_coverage.json',record);coverage['projects'].append(record)
        write(target/'model_definition.json',{'nodes':8,'hex8_elements':1,'E_Pa':1.e7,'poisson':0,'density_kg_m3':1000,
            'size_m':[2,1,1],'load_page':kind,'parameters':params,'selection':selection,'analytic':cases[0]['analytic_displacement']})
    write(a.suite/'coverage.json',coverage)

if __name__=='__main__':main()
