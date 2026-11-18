"""Small homogeneous 2D Darcy projects for independent boundary diagnostics."""
import argparse
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path
from build_geotechnical_regression import read,write,clone,child,install_pulse,add_case


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    target=a.suite/'渗流_二维边界对照';source=a.suite/'坝_多工况回归';base=a.suite/'洞室_二维边界对照'
    if target.exists():raise RuntimeError('Preserve existing seepage comparison')
    data=target/'工程数据';inp=data/'计算输入';inp.mkdir(parents=True)
    xml=ET.parse(next(base.glob('*.xml')));xml.getroot().find('Name').text=target.name
    xml.write(target/(target.name+'.xml'),encoding='utf-8',xml_declaration=True)
    points=[(5*x,y) for y in range(11) for x in range(21)]
    cells=[]
    for y in range(10):
        for x in range(20):
            p=y*21+x+1;cells.append([p,p+1,p+22,p+21])
    text=f'{len(points)} 2 {len(cells)} 4 5 1 4 4 2\n8 2 0 100 0 10 0 0\n'
    text+=''.join(f'{x} {y} {i+1}\n' for i,(x,y) in enumerate(points))
    text+=''.join(f'{i+1} '+ ' '.join(map(str,ns))+' 3 3 1 0 0\n' for i,ns in enumerate(cells))
    for p in [data/'mesh_snapshot.iiin.dat',inp/'IIIN.DAT']:p.write_text(text,encoding='ascii')
    mesh=read(base/'工程数据/mesh.json');mesh.update(vertical_axis=2,nodes=[]);write(data/'mesh.json',mesh)
    tags=[{'key':{'category':category,'dim':2,'id':value},'name':title,
        'cell_ids':[str(i) for i in range(len(cells))],'point_ids':[],'face_keys':[],'edge_keys':[],'source':'homogeneous Darcy rectangle'}
        for category,value,title in [('MATYPE',3,'均质渗流介质'),('LAYER',1,'整体区域')]]
    write(data/'mesh_map.json',{'mesh_dim':2,'material_group_category':'MATYPE','excavation_group_category':'LAYER',
        'tag_sets':tags,'phys_id_to_dim':{},'phys_id_to_name':{}})
    material=read(source/'工程数据/material.json');entries=[]
    for model in material['models']:
        model['entries']=[e for e in model['entries'] if 3 in e['applied_matypes']]
        for e in model['entries']:
            e.update(name='均质饱和渗流介质',seepage_model_key='jk_corewall_steady_seepage',
                seepage_properties={'渗透系数K(Lu)':'400','比储量S':'0.001','允许比降':'0.38'})
            entries.append(e['id'])
    material['added_entry_ids']=entries;write(data/'material.json',material)
    write(data/'physical_field.json',{'module':'PhysicalField','root_title':'约束与荷载','items':[]})
    for name in ['preprocessing.json','postprocessing.json']:shutil.copy2(base/'工程数据'/name,data/name)
    install_pulse(target)
    templates=read(source/'工程数据/solver.json')['items'];template=next(n for n in templates if n['title'].startswith('D04'))
    project={'module':'Solver','root_title':'计算分析','state':{'compute_device':'gpu','gpu_device_mode':2},'items':[]};cases=[]
    for id,title,free,drain,combined in [('H01','饱和定水头解析解',False,False,False),
        ('H02','定水头稳态转瞬态',False,False,True),('H03','天然自由面',True,False,False),
        ('H04','底部排水井',False,True,False),('H05','天然自由面静水对照',True,False,False)]:
        n=clone(template,id+'－'+title);up,down=(5.,5.) if id=='H05' else (10.,5.) if free else (20.,15.)
        n['state'].update(compute_type=3 if combined else 1,upstream_water_level=up,downstream_water_level=down,
            upstream_flux=0.,downstream_flux=0.,natural_free_surface=free,drain_well=drain,itag_free=int(free),
            tsta=0.,tend=600.,dt=60.,iter=300)
        for kind,x,head in [('SeepageBoundaryUpstreamWater',0,up),('SeepageBoundaryDownstreamWater',100,down)]:
            state=child(n,kind)['state'];state.update(picked_line=[],picked_solid=[i for i,p in enumerate(points) if p[0]==x and (not free or p[1]<=head)],
                boundary_condition_type='head',water_level=head,flux=0.,water_level_curve=[{'time':0.,'head':head},
                {'time':600.,'head':head+2 if combined and x==0 else head}])
        child(n,'SeepageBoundaryNaturalSeepage')['state'].update(picked_line=[],
            picked_solid=[i for i,(x,y) in enumerate(points) if x in ((0,20,40,60,80,100) if id=='H03' else (20,40,60,80))
                and y==up-(up-down)*x/100] if free else [],
            natural_head_mode='elevation',natural_correction=0)
        child(n,'SeepageBoundaryDrainWell')['state'].update(picked_line=[],
            picked_solid=[i for i,p in enumerate(points) if p==(50,0)] if drain else [])
        add_case(project,n,[title,'均质二维矩形','独立边界对照'],cases)
        if id in ('H01','H05'):cases[-1]['analytic_head']={'intercept_m':up,'gradient_x':(down-up)/100,'tolerance_m':.001}
    write(data/'solver.json',project)
    write(target/'model_definition.json',{'points':len(points),'cells':len(cells),'size_m':[100,10],
        'K_Lu':400,'specific_storage':.001,'analytic_saturated_head':'h(x)=20-0.05x; zero flux on top/bottom',
        'drain':'one open bottom outlet at (50,0), pressure head zero',
        'free_surface_initial_nodes':[(x,10-x/20) for x in (0,20,40,60,80,100)],
        'hydrostatic_free_surface':'H05: h=5m, initial interface y=5m; zero flow'})
    coverage=read(a.suite/'coverage.json');record={'directory':target.name,'seed':'homogeneous 2D boundary diagnostics','cases':cases}
    coverage['projects'].append(record);write(a.suite/'coverage.json',coverage);write(target/'case_coverage.json',record)
    print(target)


if __name__=='__main__':main()
