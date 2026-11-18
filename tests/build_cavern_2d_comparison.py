"""Same-section 2D diagnostic comparison; never edits the 3D model or solver."""
import argparse
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import Counter
import uuid
from build_geotechnical_regression import read,write,clone,child,install_pulse,add_case

def uid():return '{'+str(uuid.uuid4())+'}'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    source=a.suite/'洞室_边界专项回归';target=a.suite/'洞室_二维边界对照';data=target/'工程数据';inp=data/'计算输入'
    if target.exists():raise RuntimeError('Preserve existing comparison project')
    inp.mkdir(parents=True)
    xml=ET.parse(next(source.glob('*.xml')));xml.getroot().find('Name').text=target.name
    for item in xml.getroot().find('GlobalValues'):
        if item.attrib.get('key')=='mesh_dimension':item.text='2'
    xml.write(target/(target.name+'.xml'),encoding='utf-8',xml_declaration=True)
    lines=(source/'工程数据/mesh_snapshot.iiin.dat').read_text().splitlines();np=int(lines[0].split()[0])
    oldpoints=[list(map(float,r.split()[:3])) for r in lines[2:np+2]]
    pointmap={};points=[]
    for i,(x,y,z) in enumerate(oldpoints):
        if y==0:pointmap[i+1]=len(points)+1;points.append((x,z))
    cells=[];inner=[];outer=[];sides=[[],[]];roof=[];quiet=[]
    for row in lines[np+2:]:
        r=list(map(int,row.split()));v=r[1:9]
        if any(i not in pointmap for i in [v[0],v[1],v[5],v[4]]):continue
        ns=[pointmap[i] for i in [v[0],v[1],v[5],v[4]]];material,layer=r[10:12];ci=len(cells);cells.append((ns,material,layer))
        for edge,(aa,bb) in enumerate([(0,1),(1,2),(2,3),(3,0)]):
            p,q=points[ns[aa]-1],points[ns[bb]-1];key=ci*16+edge
            if p[0]==q[0]==-12:sides[0].append(key)
            if p[0]==q[0]==12:sides[1].append(key)
            if p[1]==q[1]==12:roof.append(key)
            if p[0]==q[0] and abs(p[0])==12 or p[1]==q[1]==-12:quiet.append((ci+1,ns[aa],ns[bb]))
            if material==5:
                for distance,out in [(3,inner),(4,outer)]:
                    if any(abs(p[axis])==abs(q[axis])==distance and p[axis]==q[axis]
                           and abs(p[1-axis])<=distance and abs(q[1-axis])<=distance for axis in [0,1]):out.append(key)
    text=f'{len(points)} 2 {len(cells)} 4 5 1 0 4 2\n6 2 -12 12 -12 12 0 0\n'
    text+=''.join(f'{x} {y} {i+1}\n' for i,(x,y) in enumerate(points))
    text+=''.join(f'{i+1} '+ ' '.join(map(str,ns))+f' 3 {m} {l} 0 0\n' for i,(ns,m,l) in enumerate(cells))
    for p in [data/'mesh_snapshot.iiin.dat',inp/'IIIN.DAT']:p.write_text(text,encoding='ascii')
    mesh=read(source/'工程数据/mesh.json')
    for k in ['binary_cache_file','surface_cache_file','line_cache_file']:mesh.pop(k,None)
    mesh.update(vertical_axis=2,nodes=[]);write(data/'mesh.json',mesh)
    tags=[]
    for category,index,values in [('MATYPE',1,[3,4,5,6,7]),('LAYER',2,[-1,-2,1])]:
        for value in values:tags.append({'key':{'category':category,'dim':2,'id':value},'name':f'{category} {value}',
            'cell_ids':[str(i) for i,c in enumerate(cells) if c[index]==value],'point_ids':[],'face_keys':[],'edge_keys':[],'source':'2D comparison'})
    write(data/'mesh_map.json',{'mesh_dim':2,'material_group_category':'MATYPE','excavation_group_category':'LAYER','tag_sets':tags,'phys_id_to_dim':{},'phys_id_to_name':{}})
    shutil.copy2(source/'工程数据/material.json',data/'material.json')
    phy={'module':'PhysicalField','root_title':'约束与荷载','items':[]}
    for title,mask,select in [('底部固定',('0','0','1'),lambda x,y:y==-12),
                             ('两侧法向约束',('0','1','1'),lambda x,y:abs(x)==12 and y!=-12)]:
        phy['items'].append({'uuid':uid(),'kind':'DisplacementConstraint','domain':'DisplacementConstraintWidget',
            'title':title,'node_solid':[i for i,p in enumerate(points) if select(*p)],'page_data':dict(zip(['ux','uy','uz'],mask))})
    write(data/'physical_field.json',phy)
    for f in ['preprocessing.json','postprocessing.json']:shutil.copy2(source/'工程数据'/f,data/f)
    inp.joinpath('FA_DYNA.DAT').write_text(f'{len(quiet)} 4 NJX MAKOD\nKJX KE1 KE2 JD1 JD2 JAT\n'+''.join(f'{i+1} {e} 0 {na} {nb} 0\n' for i,(e,na,nb) in enumerate(quiet)),encoding='ascii')
    install_pulse(target)
    original=read(source/'工程数据/solver.json')['items'];project={'module':'Solver','root_title':'计算分析','state':{'compute_device':'gpu','gpu_device_mode':2},'items':[]};cases=[]
    for number,title,template,mode,quiet_flag in [(1,'二维开挖基准',0,None,False),(2,'二维衬砌内外界面',1,None,False),
        (3,'二维吊车梁第二级安装',3,None,False),(4,'二维固定边界时程',0,'wave',False),
        (5,'二维粘弹性边界时程',0,'wave',True),(6,'二维模态',0,'modal',False),
        (7,'单元卸载后模态诊断',0,'modal',False),(8,'单元卸载后衬砌诊断',1,None,False)]:
        n=clone(original[template],f'P{number:02d}－'+title);geo=child(n,'GeoStressRegressionAnalysis')
        geo['children']=[{'uuid':uid(),'title':f'侧立面{i+1}','type':'GeoStressRegressionSideFace','state':{'face_index':i+1,
            'idir':'2','height':'40','gama':'2500','pgrout':'0','pick_mode':'edge','picked_edges':values,'picked_faces':[]}}
            for i,values in enumerate(sides)]
        geo['children'].append({'uuid':uid(),'title':'自由地表','type':'GeoStressRegressionOuterForce',
            'state':{'fx':'0','fy':'0','fz':'0','pick_mode':'edge','picked_edges':roof,'picked_faces':[]}})
        support=child(n,'CaveSupportComputation')
        if number in (2,4,5,6):support['state']['line_23']='1'
        if number in (7,8):support['state']['line_23']='2'
        for c in support['children']:
            state=c['state']
            for k in ['selected_group_dim']: 
                if k in state:state[k]=2
            for dom in state.get('selected_domains',[]):dom['dim']=2
            if c['type'] in ['CaveLiningInnerBoundary','CaveLiningOuterBoundary']:
                state.update(pick_mode='edge',face_solid=[],edge_solid=inner if c['type']=='CaveLiningInnerBoundary' else outer)
        if mode:support['state'].update(line_22='3',ifmode='1' if mode=='modal' else '0',ifwave='1' if mode=='wave' else '0',
             tsta=0.,tend=.4,dt=.02,quiet_boundary_enabled=quiet_flag)
        add_case(project,n,[title,'二维同截面对照'],cases)
    write(data/'solver.json',project)
    record={'directory':target.name,'seed':'same section as 3D boundary project','cases':cases};write(target/'case_coverage.json',record)
    cov=read(a.suite/'coverage.json');cov['projects'].append(record);write(a.suite/'coverage.json',cov)
    write(target/'model_definition.json',{'points':len(points),'cells':len(cells),'material_counts':dict(Counter(m for _,m,_ in cells)),
        'inner_edges':len(inner),'outer_edges':len(outer),'quiet_edges':len(quiet),'dimension':2})
    print(target)

if __name__=='__main__':main()
