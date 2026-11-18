"""Independent cantilever checks through the imported native planar beam path."""
import argparse
import shutil
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path
from build_geotechnical_regression import read,write,clone,add_case

def uid():return '{'+str(uuid.uuid4())+'}'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    if Path('D:/IIIDIR.regression.lock').exists():raise RuntimeError('Runner is active')
    coverage=read(a.suite/'coverage.json');seed=a.suite/'荷载对照_均匀面力'
    template=read(seed/'工程数据/solver.json')['items'][0]
    for identifier,label,loading in [('M13','梁端集中力','force'),('M14','梁端力矩','moment'),
            ('M15','梁均布荷载','uniform'),('M16','梁端指定转角','rotation')]:
        directory='梁边界对照_'+label;target=a.suite/directory
        if target.exists():raise RuntimeError('Preserve existing project '+str(target))
        data=target/'工程数据';inp=data/'计算输入';inp.mkdir(parents=True)
        xml=ET.parse(next(seed.glob('*.xml')));xml.getroot().find('Name').text=directory
        xml.getroot().find('./GlobalValues/Value[@key="mesh_dimension"]').text='2'
        xml.write(target/(directory+'.xml'),encoding='utf-8',xml_declaration=True)
        text='2 2 1 2 5 1 0 0 0\n7 3 0 2 0 1 0 1\n0 0 1\n2 0 2\n1 1 2 21 3 1 0 0\n'
        for path in [data/'mesh_snapshot.iiin.dat',inp/'IIIN.DAT']:path.write_text(text,encoding='ascii')
        state=read(seed/'工程数据/mesh.json');state['vertical_axis']=2;write(data/'mesh.json',state)
        tags=read(seed/'工程数据/mesh_map.json');tags['mesh_dim']=2;tags['tag_sets'][0]['key']['dim']=1
        tags['tag_sets'][0]['name']='弹性梁';write(data/'mesh_map.json',tags)
        entry_id=uid();entry={'id':entry_id,'name':'平面弹性梁','applied_matypes':[3],
            'properties':{'序号':'3','弹性模量E（GPa）':'200','截面积A（m2）':'0.01',
                '截面惯性矩Iz（m4）':'0.0001','容重t/m3':'7.85'},'seepage_properties':{},
            'transition_matype':-1,'transition_only':False,'creep_model_key':'','seepage_model_key':''}
        write(data/'material.json',{'module':'Material','added_entry_ids':[entry_id],
            'material_transitions':[],'show_transition_node':False,'models':[
                {'key':'planar_elastic_beam','display_name':'平面弹性梁','entries':[entry]}]})
        for name in ['preprocessing.json','postprocessing.json']:shutil.copy2(seed/'工程数据'/name,data/name)
        items=[{'uuid':uid(),'kind':'DisplacementConstraint','domain':'DisplacementConstraintWidget',
            'title':'固定端约束','node_line':[0],'page_data':{'ux':'0','uy':'0','uz':'1','rz':'0'}}]
        if loading=='rotation':
            items.append({'uuid':uid(),'kind':'DisplacementConstraint','domain':'DisplacementConstraintWidget',
                'title':'自由端指定转角','node_line':[1],'page_data':{'ux':'1','uy':'1','uz':'1','rz':'2','prescribed_rz':.0001}})
        elif loading=='uniform':
            items.append({'uuid':uid(),'kind':'BeamLoadPage','domain':'BeamLoadPage','title':'全梁均布荷载',
                'node_line':[0],'page_data':{'spinBoxLDTYPE':6,'doubleSpinBoxA':0,'doubleSpinBoxP':1000,'spinBoxIDIR':1}})
        else:
            items.append({'uuid':uid(),'kind':'PointForcePage','domain':'PointForcePage','title':label,
                'node_line':[1],'page_data':{'spinFx':0,'spinFy':1000 if loading=='force' else 0,
                    'spinFz':0,'spinMx':0,'spinMy':0,'spinMz':1000 if loading=='moment' else 0}})
        write(data/'physical_field.json',{'module':'PhysicalField','root_title':'约束与荷载','items':items})
        project={'module':'Solver','root_title':'计算分析','state':{'compute_device':'gpu','gpu_device_mode':2},'items':[]};cases=[]
        node=clone(template,identifier+'－'+label)
        node['state']['base_values'][2]='0'  # no strength reduction
        node['state']['base_values'][4]='0'  # native METHOD_ITER=0, elastic beam
        add_case(project,node,['平面梁','线弹性分析','独立挠度及转角解析解',label],cases)
        cases[0]['analytic_displacement']={'field':'planar_beam','loading':loading,'length_x_m':2,
            'flexural_rigidity_Nm2':2.e7,'load':.0001 if loading=='rotation' else -1000 if loading=='uniform' else 1000,
            'load_convention':'NFG positive P acts in local negative transverse direction (native FLTYPE); point Fy and Mz use global signs',
            'tolerance_m':2.e-8,'tolerance_rad':2.e-8}
        write(data/'solver.json',project)
        record={'directory':directory,'seed':'independent 2D Euler cantilever, native KETYPE=21','cases':cases}
        write(target/'case_coverage.json',record);coverage['projects'].append(record)
    write(a.suite/'coverage.json',coverage)

if __name__=='__main__':main()
