"""Independent homogeneous Q4 Laplace assembly for the H03 initial field."""
import argparse
from pathlib import Path
import numpy as np
from build_geotechnical_regression import read,write

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    project=a.suite/'渗流_二维边界对照';w=project/'H03－天然自由面/SEEPFREE'
    entries=[e for m in read(project/'工程数据/material.json')['models'] for e in m['entries']]
    assert len(entries)==1 and entries[0]['seepage_model_key']=='jk_corewall_steady_seepage'
    assert entries[0]['applied_matypes']==[3] and float(entries[0]['seepage_properties']['渗透系数K(Lu)'])>0
    lines=(w/'IIIN.TXT').read_text('gbk').splitlines();n,dim,ne,width=map(int,lines[0].split()[:4]);assert dim==2 and width==4
    xy={int(row.split()[-1]):np.array(list(map(float,row.split()[:2]))) for row in lines[2:n+2]}
    ids=sorted(xy);index={node:i for i,node in enumerate(ids)};K=np.zeros((n,n))
    for row in lines[2+n:2+n+ne]:
        t=list(map(int,row.split()));assert t[5]==3 and t[6]==3
        nodes=t[1:5];X=np.array([xy[node] for node in nodes]);ke=np.zeros((4,4))
        for x in [-1/np.sqrt(3),1/np.sqrt(3)]:
            for y in [-1/np.sqrt(3),1/np.sqrt(3)]:
                d=np.array([[-(1-y),1-y,1+y,-(1+y)],[-(1-x),-(1+x),1+x,1-x]])/4
                J=d@X;assert np.linalg.det(J)>0;B=np.linalg.solve(J,d);ke+=B.T@B*np.linalg.det(J)
        local=[index[node] for node in nodes];K[np.ix_(local,local)]+=ke
    rows=(w/'IIWL.DAT').read_text('gbk').splitlines();start=next(i for i,line in enumerate(rows) if 'NUM OF CONSTRAINTS' in line)
    count=int(rows[start].split()[0]);cona={int(row.split()[0]):float(row.split()[1]) for row in rows[start+2:start+2+count]}
    flags={int(row.split()[-1]):int(row.split()[0]) for row in (w/'IIID.DAT').read_text('gbk').splitlines()[2:]}
    assert all(flags[node]!=0 for node in ids)
    fixed=[index[node] for node in ids if flags[node]<0];free=[index[node] for node in ids if flags[node]>0]
    reference=np.array([cona[-flags[node]] if flags[node]<0 else 0 for node in ids]);reference[free]=np.linalg.solve(K[np.ix_(free,free)],-K[np.ix_(free,fixed)]@reference[fixed])
    result={int(row.split()[0]):float(row.split()[1]) for row in (w/'IIDISP.TXT').read_text('gbk').splitlines()[3:]}
    actual=np.array([result[node] for node in ids]);residual=K@actual
    absolute=float(np.max(np.abs(actual-reference)))
    scaled_error=absolute/(1+float(np.max(np.abs(reference))))
    backward=float(np.max(np.abs(residual[free])))/(float(np.linalg.norm(K,np.inf))*float(np.max(np.abs(actual))))
    boundary=float(np.max(np.abs(actual[fixed]-reference[fixed])))
    passed=all(np.isfinite(actual)) and scaled_error<=1.e-6 and backward<=1.e-6 and boundary<=1.e-6
    report={'nodes':n,'elements':ne,'max_head_difference_m':absolute,'scaled_head_error':scaled_error,
        'free_node_normalized_backward_error':backward,'fixed_head_error_m':boundary,
        'relative_tolerance':1.e-6,'passed':bool(passed),
        'scope':'Independent unit-conductivity homogeneous Q4 initial Laplace solve, zero source/flux and the six prescribed native heads. Constant positive isotropic conductivity cancels; not final nonlinear free-surface accuracy validation.'}
    write(a.output,report);print(report)
    if not passed:raise SystemExit(1)

if __name__=='__main__':main()
