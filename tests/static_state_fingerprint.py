"""Same model mesh fingerprint as StaticStateProtocol; ignores runtime controls."""
import hashlib

def mesh_digest(path):
    lines=path.read_text('ascii').splitlines();h=lines[0].split();c=lines[1].split()
    count,dim,elements,width=map(int,h[:4])
    canonical=' '.join(str(int(v)) for v in h[:5])+'\naxis '+str(int(c[1]))+'\n'
    for line in lines[2:2+count]:
        row=line.split();coords=[float(v) for v in row[:dim]]
        canonical+=' '.join(format(v if v else 0,'.17g') for v in coords)+' '+str(int(row[dim]))+'\n'
    for line in lines[2+count:2+count+elements]:canonical+=' '.join(str(int(v)) for v in line.split()[:width+6])+'\n'
    return hashlib.sha256(canonical.encode('ascii')).hexdigest()

def model_hashes(folder):
    return {name:mesh_digest(folder/name) if name=='IIIN.DAT' else hashlib.sha256((folder/name).read_bytes()).hexdigest()
        for name in ['IIIN.DAT','IIID.DAT','IMAT.DAT']}
