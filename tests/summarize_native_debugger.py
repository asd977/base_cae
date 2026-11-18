"""Resolve observed CDB PE offsets using verified COFF byte matches."""
import argparse
import json
from pathlib import Path
import re
import shutil


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);ap.add_argument('--symbols',type=Path,default=Path('build/native_diagnostics/cpu_symbol_map.json'));a=ap.parse_args()
    mappings={'CPU':json.loads(a.symbols.read_text('utf8'))};records=[]
    gpu=a.symbols.with_name('gpu_symbol_map.json')
    if gpu.exists():mappings['GPU']=json.loads(gpu.read_text('utf8'))
    evidence=a.suite/'native_symbols';evidence.mkdir(exist_ok=True)
    shutil.copy2(a.symbols,evidence/a.symbols.name)
    if gpu.exists():shutil.copy2(gpu,evidence/gpu.name)
    paths=list((a.suite/'runs').glob('*/*analysis_cpu.log'))+list((a.suite/'runs').glob('*/*analysis_gpu.log'))+list((a.suite/'probes').glob('*/native.log'))
    for path in sorted(paths):
        text=path.read_text('utf8',errors='replace')
        marker=re.search(r'^NATIVE_(?:EXCEPTION|HEAP_EXCEPTION|BREAK|ERRORSHOW)\s*$',text,re.MULTILINE)
        if not marker:continue
        excerpt=text[marker.start():];frames=[];seen=set()
        for device,value in re.findall(r'PCCAE_(CPU|GPU)\+0x([\da-fA-F]+)',excerpt):
            mapping=mappings[device]
            rva=int(value,16)
            if rva in seen:continue
            seen.add(rva);candidates=[f for f in mapping['functions'] if f['rva']<=rva<f['rva']+f['size']]
            frames.append({'rva':hex(rva),'verified_matches':[{'routine':f['name'],'offset':hex(rva-f['rva']),
                'object':f['object']} for f in candidates]})
        records.append({'case':path.name.split('_')[0] if path.name!='native.log' else path.parent.name,'log':str(path),'frames':frames,'exception_excerpt':excerpt[:18000]})
    output=a.suite/'native_debugger_findings.json';output.write_text(json.dumps({'image_sha256':{d:m['image_sha256'] for d,m in mappings.items()},
        'method':mappings['CPU']['method'],'records':records},ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    for record in records:print(record['case'],[[m['routine'] for m in f['verified_matches']] for f in record['frames']])


if __name__=='__main__':main()
