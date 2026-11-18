"""Build a small 3D cavern with explicit liner and crane-beam domains.

Only the generated regression suite is changed; original examples are read-only.
"""
import argparse
import copy
from collections import Counter
from pathlib import Path
import uuid
import xml.etree.ElementTree as ET

from build_geotechnical_regression import read, write, clone, child, add_case, install_pulse


def uid():
    return '{'+str(uuid.uuid4())+'}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', type=Path)
    args = parser.parse_args()
    root = args.suite
    name = '洞室_边界专项回归'
    target = root/name
    if target.exists():
        raise RuntimeError('Target already exists; preserve existing project')
    data = target/'工程数据'
    inputs = data/'计算输入'
    inputs.mkdir(parents=True)
    seed = root/'洞室_多工况回归'
    manifest = ET.parse(next(seed.glob('*.xml')))
    manifest.getroot().find('Name').text = name
    manifest.write(target/(name+'.xml'), encoding='utf-8', xml_declaration=True)
    # 24 m rock block, 6 x 6 m square opening, 1 m liner, 4 m length.
    axis = [-12,-8,-5,-4,-3,-2,0,2,3,4,5,8,12]
    ys = [0,2,4]
    points = [(x,y,z) for z in axis for y in ys for x in axis]
    ids = {p:i+1 for i,p in enumerate(points)}
    cells = []
    inner = []
    outer = []
    sides = [[],[]]
    roof = []
    for k,z in enumerate(axis[:-1]):
        for j,y in enumerate(ys[:-1]):
            for i,x in enumerate(axis[:-1]):
                xx, yy, zz = axis[i+1], ys[j+1], axis[k+1]
                cx,cz = (x+xx)/2,(z+zz)/2
                radius = max(abs(cx),abs(cz))
                beam = 2 < abs(cx) < 5 and -2 < cz < 0
                gallery = 4 < cx < 5 and 0 < cz < 2
                material = 6 if beam else 4 if radius < 3 else 5 if radius < 4 else 7 if gallery else 3
                layer = -1 if material==4 else -2 if material in (5,7) else -1 if material==6 else 1
                vertices = [(x,y,z),(xx,y,z),(xx,yy,z),(x,yy,z),
                            (x,y,zz),(xx,y,zz),(xx,yy,zz),(x,yy,zz)]
                cid = len(cells)
                cells.append(([ids[p] for p in vertices],material,layer))
                if x == -12: sides[0].append(cid*16)
                if xx == 12: sides[1].append(cid*16+1)
                if zz == 12: roof.append(cid*16+5)
                if material == 5:
                    for face,coordinate,lo,hi,other in [(0,x,-3,-4,cz),(1,xx,3,4,cz),
                                                       (4,z,-3,-4,cx),(5,zz,3,4,cx)]:
                        # Opposing faces of left/right/bottom/top liner strips.
                        if abs(coordinate) == 3 and abs(other) < 3:
                            inner.append(cid*16+face)
                        if abs(coordinate) == 4 and abs(other) < 4:
                            outer.append(cid*16+face)
    text = f'{len(points)} 3 {len(cells)} 8 5 1 0 0 0\n6 3 -12 12 0 4 -12 12\n'
    text += ''.join(f'{x} {y} {z} {i+1}\n' for i,(x,y,z) in enumerate(points))
    text += ''.join(f'{i+1} '+ ' '.join(map(str,v))+f' 6 {m} {l} 0 0\n' for i,(v,m,l) in enumerate(cells))
    (data/'mesh_snapshot.iiin.dat').write_text(text,encoding='ascii')
    (inputs/'IIIN.DAT').write_text(text,encoding='ascii')
    # Absorbing faces: both lateral rock walls and bottom, excluding free roof
    # and plane-strain end planes. Four corners in outward order, native MAKOD=6.
    face_slots = [[0,4,7,3],[1,2,6,5],[0,1,5,4],[3,7,6,2],[0,3,2,1],[4,5,6,7]]
    quiet_rows = []
    for ci,(vertices,_,_) in enumerate(cells):
        for face in [0,1,4]:
            node_ids = [vertices[i] for i in face_slots[face]]
            coords = [points[n-1] for n in node_ids]
            axis_index,value = (0,-12) if face==0 else (0,12) if face==1 else (2,-12)
            if all(p[axis_index]==value for p in coords):
                quiet_rows.append((ci+1,node_ids))
    (inputs/'FA_DYNA.DAT').write_text(f'{len(quiet_rows)} 6 NJX MAKOD\nKJX KE1 KE2 JD1 JD2 JD3 JD4 JAT\n'+
        ''.join(f'{i+1} {element} 0 '+ ' '.join(map(str,nodes))+' 0\n' for i,(element,nodes) in enumerate(quiet_rows)),encoding='ascii')
    (inputs/'TDTRACE.DAT').write_text('0\n',encoding='ascii')
    write(data/'mesh.json',{'module':'Mesh','root_title':'网格','has_grid':True,
        'snapshot_file':'工程数据/mesh_snapshot.iiin.dat','mesh_restore_mode':'dat',
        'vertical_axis':3,'nodes':[],'edge_rules':[],'face_rules':[],'volume_rules':[]})
    tag_sets = []
    for category,values in [('MATYPE',[3,4,5,6,7]),('LAYER',[-1,-2,1])]:
        for value in values:
            tag_sets.append({'key':{'category':category,'dim':3,'id':value},
              'name':{3:'围岩',4:'主洞开挖区',5:'混凝土衬砌',6:'吊车梁',7:'第二级邻近小导洞'}.get(value,f'施工层 {value}')
                     if category=='MATYPE' else f'施工层 {value}',
              'cell_ids':[str(i) for i,(_,m,l) in enumerate(cells) if (m if category=='MATYPE' else l)==value],
              'face_keys':[],'edge_keys':[],'point_ids':[],'source':'structured regression'})
    for index,(label,faces) in enumerate([('衬砌内边界',inner),('衬砌外边界',outer),
                                         ('自由地表',roof),('左侧地应力面',sides[0]),('右侧地应力面',sides[1])],101):
        tag_sets.append({'key':{'category':'RegressionBoundary','dim':2,'id':index},
                         'name':label,'face_keys':[str(f) for f in faces],
                         'cell_ids':[],'edge_keys':[],'point_ids':[],'source':'structured regression'})
    write(data/'mesh_map.json',{'mesh_dim':3,'material_group_category':'MATYPE',
          'excavation_group_category':'LAYER','phys_id_to_dim':{},'phys_id_to_name':{},'tag_sets':tag_sets})
    materials = read(seed/'工程数据/material.json')
    template = next(m['entries'][1] for m in materials['models'] if m['entries'])
    entries = []
    for number,title,role,modulus,stage in [(3,'围岩',0,7,0),(4,'待开挖围岩',-1,7,0),
                                          (5,'再生混凝土衬砌',1,25,-2),(6,'独立吊车梁',1,30,-1),
                                          (7,'第二级待开挖围岩',-1,7,0)]:
        e = copy.deepcopy(template)
        e.update(id=uid(),name=title,applied_matypes=[number])
        e['properties'].update({'编号':str(number),'母岩材料编号':'3','岩类分区':str(role),
            '再生时对应开挖级':str(stage),'变形模量E（GPa）':str(modulus),
            '容重t/m3':'2.5','凝聚力C‘（MPa）':'5','抗剪断强度内摩擦系数':'1'})
        entries.append(e)
    for m in materials['models']:
        m['entries'] = entries if m['key']=='huangjinping_underground_plant' else []
    materials['added_entry_ids'] = [e['id'] for e in entries]
    write(data/'material.json',materials)
    physical = {'module':'PhysicalField','root_title':'约束与荷载','items':[]}
    for title,test,flags in [('底面固定',lambda x,y,z:z==-12,('0','0','0')),
                             ('左右侧向滚动',lambda x,y,z:abs(x)==12 and z!=-12,('0','1','1')),
                             ('端面平面应变',lambda x,y,z:y in (0,4) and z!=-12 and abs(x)!=12,('1','0','1'))]:
        physical['items'].append({'uuid':uid(),'title':title,'kind':'DisplacementConstraint',
          'domain':'DisplacementConstraintWidget','node_solid':[i for i,p in enumerate(points) if test(*p)],
          'page_data':dict(zip(['ux','uy','uz'],flags))})
    write(data/'physical_field.json',physical)
    for filename,content in [('preprocessing.json',{'module':'Preprocessing','items':[]}),
                             ('postprocessing.json',{'items':[]})]:write(data/filename,content)
    source = read(seed/'工程数据/solver.json')['items'][0]
    project = {'module':'Solver','root_title':'计算分析','state':{'compute_device':'gpu','gpu_device_mode':2},'items':[]}
    cases = []
    for num,title,lining,beam,stage,release in [
        (1,'开挖及边界基准',False,False,1,1.),
        (2,'衬砌内外界面',True,False,1,1.),
        (3,'吊车梁第一级安装',False,True,1,1.),
        (4,'吊车梁第二级安装',False,True,2,1.),
        (5,'衬砌与吊车梁联合',True,True,2,1.),
        (6,'吊车梁半荷载释放',False,True,2,.5)]:
        n = clone(source,f'B{num:02d}－'+title)
        geo = child(n,'GeoStressRegressionAnalysis')
        geo['state'].update(calc_mode=0,factor='0.95',**{'lambda':'0','reserved':'0','roof':'FAROOF.DAT'})
        geo['children'] = [{'uuid':uid(),'type':'GeoStressRegressionSideFace',
            'title':f'左右地应力面{i+1}','state':{'face_index':i+1,'gama':'2500',
            'height':'40','idir':'3','pgrout':'0','pick_mode':'face',
            'picked_faces':faces,'picked_edges':[]}} for i,faces in enumerate(sides)]
        geo['children'].append({'uuid':uid(),'type':'GeoStressRegressionOuterForce',
            'title':'自由地表面','state':{'pick_mode':'face','fx':'0','fy':'0','fz':'0',
                                         'picked_faces':roof,'picked_edges':[]}})
        support = child(n,'CaveSupportComputation')
        support['children'] = []
        for layer in [-1,-2]:
            support['children'].append({'uuid':uid(),'type':'CaveExcavationBody','title':f'第{-layer}级开挖',
                'state':{'excavation_layer':layer,'selected_group_category':'LAYER','selected_group_dim':3,
                         'selected_groups':[layer],'selected_domains':[{'category':'LAYER','dim':3,'id':layer}]}})
        for kind,faces,label in [('CaveLiningInnerBoundary',inner,'衬砌内边界'),
                                 ('CaveLiningOuterBoundary',outer,'衬砌外边界')]:
            support['children'].append({'uuid':uid(),'title':label,'type':kind,
                                       'state':{'pick_mode':'face','face_solid':faces}})
        support['children'].append({'uuid':uid(),'title':'吊车梁区域','type':'CaveCraneBeamArea',
                    'state':{'selected_domains':[{'category':'MATYPE','dim':3,'id':6}]}})
        s = support['state']
        s.update(lining_enabled=lining,crane_beam_enabled=beam,crane_beam_install_stage=stage,
                 crane_beam_release_factor=release,ifmode='0',ifwave='0',line_22='1',line_23='2',
                 excavation_days=['0','1'],iexca_max=2,reinforcement_field={'mode':0})
        s['cases'] = [{'icase':1,'iexca_sta':'-1','iexca_end':'-2','if_eall':'1','if_jall':'0',
                       'if_e1toa':'1','if_j1toa':'1' if lining or beam else '0',
                       'ja_method':'2' if lining else '1','if_chen':'1' if lining else '0','iday_case':'0'}]
        add_case(project,n,['底部固定','侧面滚动','端面平面应变']+
                 (['衬砌内界面','衬砌外界面','再生衬砌'] if lining else [])+
                 (['独立材料吊车梁','安装阶段及释放系数'] if beam else []),cases)
    for number,title,modal,quiet in [(7,'固定边界模态',True,False),
                                    (8,'固定边界短脉冲',False,False),
                                    (9,'粘弹性边界短脉冲',False,True)]:
        n = clone(project['items'][0],f'B{number:02d}－'+title)
        child(n,'CaveSupportComputation')['state'].update(
            line_22='3',line_23='2' if quiet else '1',ifmode='1' if modal else '0',
            ifwave='0' if modal else '1',dt=.02,tsta=0.,tend=2.,quiet_boundary_enabled=quiet)
        add_case(project,n,[title,'非零合成输入' if not modal else '固有振动'],cases)
    write(data/'solver.json',project)
    install_pulse(target)
    definition = {'geometry':'24x24x4 m block; 6x6 m opening; 1 m liner; two beam ledges; 1x2x4 m adjacent gallery',
                  'excavation_sequence':{'1':'主洞全断面开挖','2':'右侧邻近小导洞开挖'},
                  'points':len(points),'cells':len(cells),'material_counts':dict(Counter(m for _,m,_ in cells)),
                  'inner_face_keys':inner,'outer_face_keys':outer,
                  'note':'Synthetic functional regression, not an engineering design or certified benchmark.'}
    write(target/'model_definition.json',definition)
    coverage = read(root/'coverage.json')
    coverage['projects'].append({'directory':name,'seed':'structured synthetic model','cases':cases})
    write(target/'case_coverage.json',coverage['projects'][-1])
    write(root/'coverage.json',coverage)
    print(definition)


if __name__=='__main__':main()
