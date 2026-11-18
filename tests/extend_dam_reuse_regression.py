"""Add an explicit dynamic-only reuse case; keep provenance of real static results."""
import argparse
import hashlib
import shutil
from pathlib import Path
from build_geotechnical_regression import read,write,clone,child,add_case

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    project=a.suite/'坝_多工况回归';path=project/'工程数据/solver.json';solver=read(path)
    source=next(n for n in solver['items'] if n['title'].startswith('D12－'))
    baseline=next(c for p in read(a.suite/'coverage.json')['projects'] for c in p['cases'] if c['id']=='D12')
    if baseline.get('result_status')!='passed_sanity':raise RuntimeError('D12 must pass before reusing its state')
    if any(n['title'].startswith('D17－') for n in solver['items']):raise RuntimeError('Preserve existing D17')
    eigen=project/source['title']/'CASE01MODE/IEIGEN.TXT'
    lines=eigen.read_text('gbk').splitlines();omega=float(lines[3].split()[2])
    if int(lines[0].split()[0])<1 or omega<=0:raise RuntimeError('A verified positive modal frequency is required')
    node=clone(source,'D17－已有静力状态续算短时程');node['state']['base_values'][18]='2'
    node['state']['rock_values']=['0','0']
    dynamic=child(node,'DamDynamicAnalysis')['state']
    dynamic['values'].update(IFMODE='0',IFWAVE='1',DT='.02',TSTA='0',TEND='.4',WOMI_1=str(omega))
    workspace=project/node['title'];workspace.mkdir()
    evidence=[]
    for folder in ['DISK00','CASE01WETT']:
        destination=workspace/folder;destination.mkdir()
        for name in ['IIIN.DAT','IIIN.TXT','IIID.DAT','IMAT.DAT','ISGMAR.TXT','IIICMM.TXT','IIDISP.TXT','MODSTR.TXT','STATIC_STATE.json']:
            p=project/source['title']/folder/name
            if not p.exists():raise RuntimeError('Missing native static source '+str(p))
            q=destination/name;shutil.copy2(p,q)
            evidence.append({'source':str(p),'copy':str(q),'sha256':hashlib.sha256(q.read_bytes()).hexdigest()})
    record=next(p for p in read(a.suite/'coverage.json')['projects'] if p['directory']==project.name)
    add_case(solver,node,['仅动力续算','真实静力状态复用','固定边界短脉冲','不重复求模态'],record['cases'],dependencies=['D12'])
    record['cases'][-1]['timeout_seconds']=600
    record['cases'][-1]['static_reuse_provenance']='工程数据/D17_static_reuse_provenance.json'
    write(project/'工程数据/D17_static_reuse_provenance.json',{'source_case':'D12','first_circular_frequency_rad_s':omega,'files':evidence,
        'qualification':'Copied computed static states; the dynamic response must be freshly computed and is not copied.'})
    write(path,solver);coverage=read(a.suite/'coverage.json')
    next(p for p in coverage['projects'] if p['directory']==project.name)['cases']=record['cases']
    write(a.suite/'coverage.json',coverage);write(project/'case_coverage.json',record)

if __name__=='__main__':main()
