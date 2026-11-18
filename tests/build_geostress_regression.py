"""Prepare gravity and non-gravity geostress cases from a copied small project.

Measurements are explicitly synthetic, sampled from an existing self-weight
reference result. This prepares test inputs, not engineering calibration data.
"""
import argparse
import json
from pathlib import Path

from build_geotechnical_regression import prepare, write, clone, child


def synthetic_measurements(points, cells, stress):
    # Cell centres lie inside the Gauss-point cloud. Boundary-node samples can
    # leave the legacy 3-D interpolation stencil coplanar and singular.
    candidates = []
    for cell in cells:
        centre = [sum(points[j][axis] for j in cell)/len(cell) for axis in range(3)]
        if abs(centre[0]) < 7 or abs(centre[2]) > 9:
            continue
        # IGEOS takes MPa with compression positive; the reference output is Pa
        # in the elastic (tension-positive) sign convention.
        values = [-sum(float(stress[j+1][axis]) for j in cell)/len(cell)/1.e6 for axis in range(6)]
        candidates.append((centre, values))
    chosen = [candidates[i*(len(candidates)-1)//23] for i in range(24)]
    rows = []
    for centre, values in chosen:
        row = {'node_id':'', 'pid':'-1', **dict(zip(['x','y','z'],map(str,centre)))}
        row.update(dict(zip(['sgx','sgy','sgz','txy','tyz','tzx'],map(str,values))))
        rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    name = '地应力_自重与合成测点回归'
    solver = prepare(a.source, a.output, name)
    template = next(n for n in solver['items'] if n['title'].startswith('B01'))
    with (a.source / '工程数据/mesh_snapshot.iiin.dat').open('rb') as stream:
        n, dim, m, width = map(int, stream.readline().split()[:4])
        stream.readline()
        points = [list(map(float, stream.readline().split()[:dim])) for _ in range(n)]
        cells = [[int(x)-1 for x in stream.readline().split()[1:width+1]] for _ in range(m)]
    faces = [[0,4,7,3],[1,2,6,5],[0,1,5,4],[3,7,6,2],[0,3,2,1],[4,5,6,7]]
    bounds = [(min(p[i] for p in points), max(p[i] for p in points)) for i in range(3)]
    def boundary(axis, level):
        return [(i << 4) | j for i,c in enumerate(cells) for j,face in enumerate(faces)
                if all(abs(points[c[k]][axis]-level)<1.e-9 for k in face)]

    reference = a.source / template['title'] / 'GEOSTR/MODSTR.TXT'
    lines = reference.read_text('gbk', errors='replace').splitlines()
    stress = {int(row[0]): row[1:7] for line in lines[3:] if len(row := line.split()) >= 7}
    measurements = synthetic_measurements(points, cells, stress)

    gravity = clone(template, 'G01－仅自重（无需侧立面或测点）')
    geo = child(gravity, 'GeoStressRegressionAnalysis')
    geo['state'].update(calc_mode=0, factor='0.95', **{'lambda':'0'})
    for item in geo['children']:
        if item['type']=='GeoStressRegressionSideFace':item['state']['picked_faces']=[]
        elif item['type']=='GeoStressRegressionOuterForce':item['state']['picked_faces']=[]

    regression = clone(template, 'G02－四侧面合成测点回归')
    geo = child(regression, 'GeoStressRegressionAnalysis')
    geo['state'].update(calc_mode=1, factor='1', **{'lambda':'0'})
    prototype = child(geo, 'GeoStressRegressionSideFace')
    sides = []
    for i,(label,axis,level) in enumerate([('左侧立面',0,bounds[0][0]),('右侧立面',0,bounds[0][1]),
                                         ('前侧立面',1,bounds[1][0]),('后侧立面',1,bounds[1][1])],1):
        side = clone(prototype, label)
        side['state'].update(face_index=i, idir=str(axis+1), height=str(bounds[2][1]), gama='2500',
                             pgrout='0', picked_faces=boundary(axis,level))
        sides.append(side)
    geo['children'] = sides + [c for c in geo['children'] if c['type']!='GeoStressRegressionSideFace']
    child(geo,'GeoStressRegressionMeasurePoint')['state']={'measure_points':measurements}
    outer=child(geo,'GeoStressRegressionOuterForce')
    outer['state'].update(picked_faces=boundary(2,bounds[2][1]),fx='0',fy='0',fz='0')
    solver['items']=[gravity,regression]
    write(a.output/'工程数据/solver.json',solver)
    report={'source_project':str(a.source),'synthetic_measurements':True,'reference':str(reference),
            'measurement_count':len(measurements),'cases':[{'id':'G01','title':gravity['title'],'uuid':gravity['uuid'],'calc_mode':0},
            {'id':'G02','title':regression['title'],'uuid':regression['uuid'],'calc_mode':1,'side_face_counts':[len(s['state']['picked_faces']) for s in sides]}],
            'status':'prepared, not yet calculated','solver_modified':False}
    write(a.output/'测试定义.json',report)
    (a.output/'说明.md').write_text('本工程验证地应力计算模式。\n\n'
        'G01：仅自重，侧立面和外面力不选区，验证不再要求配置未生效的输入。\n'
        'G02：非自重模式，使用四个侧立面、顶面和24个合成测点，验证实测回归路径。\n\n'
        '测点取自既有B01自重参考结果，属于合成测试数据，不代表现场实测。计算完成不等于工程校准通过。\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':
    main()
