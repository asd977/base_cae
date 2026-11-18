"""Reseed D17 only from a freshly accepted D12 run, with input/state certificates."""
import argparse
import hashlib
import shutil
from pathlib import Path
from build_geotechnical_regression import read,write
from static_state_fingerprint import model_hashes

MODEL=['IIIN.DAT','IIID.DAT','IMAT.DAT']
STATE=MODEL+['ISGMAR.TXT','IIICMM.TXT','IIDISP.TXT']
def hashes(folder,names):
    return {n:hashlib.sha256((folder/n).read_bytes()).hexdigest() for n in names}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path)
    ap.add_argument('--from-preserved-provenance',type=Path,help='Explicitly migrate preserved D17 files using this previously accepted D12 report; never use the current incomplete rerun')
    a=ap.parse_args()
    if Path('D:/IIIDIR.regression.lock').exists():raise RuntimeError('Runner active')
    coverage=read(a.suite/'coverage.json');p=next(p for p in coverage['projects'] if p['directory']=='坝_多工况回归')
    source=next(c for c in p['cases'] if c['id']=='D12');target=next(c for c in p['cases'] if c['id']=='D17')
    if not a.from_preserved_provenance and source.get('result_status')!='passed_sanity':raise RuntimeError('D12 not accepted')
    report_path=a.from_preserved_provenance or Path(source['last_run_report'])
    report=read(report_path);case=next(c for c in report['cases'] if c['id']=='D12')
    if not case.get('passed'):raise RuntimeError('Source report does not accept D12')
    accepted=next(at for at in reversed(case['attempts']) if at['phase']=='analysis' and at['passed'])
    root=a.suite/p['directory'];old=root/source['title'];new=root/target['title']
    if a.from_preserved_provenance:
        path=root/'工程数据/D17_static_reuse_provenance.json';evidence=read(path)
        for record in evidence['files']:
            copied=Path(record['copy'])
            if hashes(copied.parent,[copied.name])[copied.name]!=record['sha256']:
                raise RuntimeError('Preserved state no longer matches its original provenance: '+str(copied))
        model=hashes(old/'计算输入',MODEL)
        if any(accepted['input_sha256'].get(n)!=h for n,h in model.items()):raise RuntimeError('Historical source inputs do not match accepted D12 report')
        if model_hashes(old/'计算输入')!=model_hashes(new/'计算输入'):raise RuntimeError('Preserved state model differs from accepted historical model')
        for folder in ['DISK00','CASE01WETT']:
            state=new/folder;write(state/'STATIC_STATE.json',{'version':2,'modelInputs':model_hashes(new/'计算输入'),'stateFiles':hashes(state,STATE)})
        evidence.update(accepted_source_report=str(report_path),
            qualification='Original D17 static copies exactly match the SHA256 provenance recorded when D12 passed. Migrated from that historical accepted run; the latest incomplete D12 rerun is not used.')
        write(path,evidence)
        target['accepted_static_dependency']={'id':'D12','report':str(report_path),'provenance':str(path)}
        write(a.suite/'coverage.json',coverage)
        print({'source':'historically accepted D12','state':'preserved original D17 copies','files_verified':len(evidence['files'])});return
    model=hashes(old/'计算输入',MODEL)
    if any(accepted['input_sha256'].get(n)!=h for n,h in model.items()):raise RuntimeError('D12 inputs changed since accepted run')
    provenance=[]
    for folder in ['DISK00','CASE01WETT']:
        src=old/folder;dst=new/folder;dst.mkdir(parents=True,exist_ok=True)
        for name in STATE:
            if (src/name).stat().st_mtime<accepted['started_at_epoch']-2:
                raise RuntimeError('D12 state predates accepted run: '+str(src/name))
        # This is an explicit migration of a proven accepted run, not inference
        # from the mere presence of legacy result files.
        certificate={'version':2,'modelInputs':model_hashes(old/'计算输入'),'stateFiles':hashes(src,STATE)}
        existing=src/'STATIC_STATE.json'
        if existing.exists() and read(existing)!=certificate:raise RuntimeError('D12 certificate mismatch')
        if not existing.exists():write(existing,certificate)
        for name in STATE+['IIIN.TXT','MODSTR.TXT','STATIC_STATE.json']:
            shutil.copy2(src/name,dst/name)
            provenance.append({'source':str(src/name),'copy':str(dst/name),'sha256':hashes(dst,[name])[name]})
    path=root/'工程数据/D17_static_reuse_provenance.json';evidence=read(path)
    evidence.update(files=provenance,accepted_source_report=source['last_run_report'],
        qualification='Refreshed exclusively from a fresh accepted D12 state; model and state SHA256 certificates checked before reuse.')
    write(path,evidence);print({'source':'D12','destination':'D17','files':len(provenance)})

if __name__=='__main__':main()
