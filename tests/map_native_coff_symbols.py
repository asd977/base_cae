"""Map unmodified Release COFF routine bytes to the deployed PE image.

No source, executable or object files are changed. Relocation bytes are masked;
only unique full-prefix matches become evidence, without claiming source lines.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def u32(data,offset):return struct.unpack_from('<I',data,offset)[0]


def sections(data,start,count):
    result=[]
    for i in range(count):
        p=start+i*40
        name=data[p:p+8].rstrip(b'\0').decode('ascii',errors='replace')
        result.append({'name':name,'virtual':u32(data,p+12),'size':u32(data,p+16),'raw':u32(data,p+20),
            'relocations':u32(data,p+24),'relocation_count':struct.unpack_from('<H',data,p+32)[0]})
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('objects',type=Path);ap.add_argument('image',type=Path);ap.add_argument('output',type=Path);args=ap.parse_args()
    pe=args.image.read_bytes();p=u32(pe,0x3c);nsec=struct.unpack_from('<H',pe,p+6)[0];optional=struct.unpack_from('<H',pe,p+20)[0]
    image_sections=sections(pe,p+24+optional,nsec)
    code=next(s for s in image_sections if s['name']=='.text');image=pe[code['raw']:code['raw']+code['size']]
    matches=[];unmatched=[];inputs={}
    for path in args.objects.glob('*.obj'):
        obj=path.read_bytes();inputs[path.name]=hashlib.sha256(obj).hexdigest()
        nsec=struct.unpack_from('<H',obj,2)[0];symptr=u32(obj,8);nsym=u32(obj,12)
        strings=obj[symptr+nsym*18:];secs=sections(obj,20,nsec);symbols=[];i=0
        while i<nsym:
            s=symptr+i*18;name=obj[s:s+8]
            if name[:4]==b'\0'*4:
                off=u32(name,4);name=strings[off:].split(b'\0',1)[0]
            name=name.rstrip(b'\0').decode('ascii',errors='replace');value=u32(obj,s+8)
            section,typ,storage,aux=struct.unpack_from('<hHBB',obj,s+12)
            if typ==0x20 and storage in (2,3) and section>0:
                symbols.append((section,value,name))
            i+=aux+1
        symbols.sort()
        for index,(section,value,name) in enumerate(symbols):
            sec=secs[section-1]
            if not sec['name'].startswith('.text'):continue
            end=symbols[index+1][1] if index+1<len(symbols) and symbols[index+1][0]==section else sec['size']
            size=end-value;length=min(size,192)
            if length<12:unmatched.append(name);continue
            pattern=obj[sec['raw']+value:sec['raw']+value+length];mask=bytearray(b'\1'*length)
            for j in range(sec['relocation_count']):
                r=sec['relocations']+j*10;offset=u32(obj,r)-value;kind=struct.unpack_from('<H',obj,r+8)[0]
                width=8 if kind==1 else 2 if kind==10 else 4
                for k in range(max(0,offset),min(length,offset+width)):mask[k]=0
            runs=[];begin=None
            for j,v in enumerate(mask+b'\0'):
                if v and begin is None:begin=j
                if not v and begin is not None:runs.append((begin,j));begin=None
            anchor_start,anchor_end=max(runs,key=lambda r:r[1]-r[0]);anchor=pattern[anchor_start:anchor_end]
            if len(anchor)<8:unmatched.append(name);continue
            hits=[];pos=0
            while True:
                hit=image.find(anchor,pos)
                if hit<0:break
                candidate=hit-anchor_start;pos=hit+1
                if candidate>=0 and candidate+length<=len(image) and all(not mask[j] or image[candidate+j]==pattern[j] for j in range(length)):
                    hits.append(candidate)
            if len(hits)==1:matches.append({'name':name,'rva':code['virtual']+hits[0],'size':size,'object':path.name,'matched_prefix_bytes':length})
            else:unmatched.append(name)
    result={'image':str(args.image),'image_sha256':hashlib.sha256(pe).hexdigest(),'objects_sha256':inputs,
        'method':'unique byte prefix matches, masking COFF relocations; no PDB/source line claims',
        'functions':sorted(matches,key=lambda x:x['rva']),'unmatched':unmatched}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print({'matched':len(matches),'unmatched':len(unmatched),'output':str(args.output)})


if __name__=='__main__':main()
