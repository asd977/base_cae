"""Stage-aware acceptance: linear initialization vs nonlinear steady iteration."""
import math
import re
from pathlib import Path

def inspect_seepage_convergence(text,workspace,started=None,required=False):
    steps=[];errors=[];warnings=[];directory=None;waiting=False
    def linear_initial(path):
        try:
            if path.resolve()!=(workspace/'SEEPFREE').resolve():return False
            lines=(path/'IKEY.DAT').read_text('gbk').splitlines();p=lines[2].split();s=lines[11].split();kind=int(lines[23].split()[0])
            return int(p[0])==21 and int(s[3])==0 and int(s[4])==0 and 0<=kind<2
        except (OSError,ValueError,IndexError):return False
    for line in text.replace('__RAW__','').splitlines():
        if 'WORKING DIR=' in line:
            path=line.split('WORKING DIR=',1)[1].strip();directory=Path(path) if path else None;waiting=not path
            continue
        if waiting and line.strip():directory=Path(line.strip());waiting=False
        for match in re.finditer(r'tol=\s*(\S+)\s+ERR=\s*(\S+)(?:\s+IFHG=\s*(\d+))?',line):
            try:tol=float(match[1].replace('D','E'));err=float(match[2].replace('D','E'))
            except ValueError:tol=err=float('nan')
            valid=math.isfinite(tol) and tol>0 and math.isfinite(err) and err>=0
            initial=directory is not None and linear_initial(directory)
            row={'directory':str(directory) if directory else None,'linear_initial':initial,
                'tolerance':tol if math.isfinite(tol) else None,'error':err if math.isfinite(err) else None,'completed':match[3]=='1'}
            steps.append(row)
            if not valid:errors.append('invalid seepage convergence record')
            elif not initial and row['completed'] and err>tol:warnings.append('a completed nonlinear seepage step exceeds requested tolerance')
    nonlinear=[s for s in steps if not s['linear_initial']]
    if nonlinear and (nonlinear[-1]['error'] is None or nonlinear[-1]['tolerance'] is None or nonlinear[-1]['error']>nonlinear[-1]['tolerance']):
        if nonlinear[-1]['error'] is not None and nonlinear[-1]['tolerance'] is not None:
            warnings.append('final nonlinear seepage iteration exceeds requested tolerance')
    if required and not nonlinear:errors.append('missing final nonlinear steady seepage convergence record')
    if any(s['linear_initial'] for s in steps) and started is not None:
        try:
            path=workspace/'SEEPFREE/IIDISP.TXT'
            if path.stat().st_mtime<started-2:raise ValueError('stale linear initial head')
            lines=path.read_text('gbk').splitlines();count=int(lines[0].split()[1]);rows=lines[3:]
            if count<=0 or len(rows)!=count:raise ValueError('incomplete linear initial head')
            for i,row in enumerate(rows,1):
                f=row.split()
                if len(f)<3 or int(f[0])!=i or not all(math.isfinite(float(v.replace('D','E'))) for v in f[1:3]):raise ValueError('invalid linear initial head/pressure')
        except (OSError,ValueError,IndexError) as exc:errors.append('linear initial field validation failed: '+str(exc))
    return {'steps':steps,'errors':list(dict.fromkeys(errors)),'warnings':list(dict.fromkeys(warnings))}
