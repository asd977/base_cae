"""Run copied native inputs with explicit controls, preserving global directory files.

Diagnostic only: this never marks a regression case passed or edits NASGEWIN64.
"""
import argparse
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--device',choices=['gpu','cpu'],default='gpu')
    ap.add_argument('--modes',type=int);ap.add_argument('--dt',type=float)
    ap.add_argument('--end',type=float);ap.add_argument('--timeout',type=int,default=90)
    ap.add_argument('--debugger',type=Path)
    ap.add_argument('--auto',type=int,default=0,help='Native automatic entry, using copied IFILL and input files')
    a=ap.parse_args();ctypes.windll.kernel32.SetErrorMode(0x8003)
    processes=subprocess.check_output(['tasklist','/FO','CSV','/NH'],creationflags=subprocess.CREATE_NO_WINDOW).decode('gbk',errors='replace').lower()
    if 'pccae_gpu.exe' in processes or 'pccae_cpu.exe' in processes:
        raise RuntimeError('Native solver is running; leave its directory protocol untouched')
    lock=Path('D:/IIIDIR.regression.lock')
    with lock.open('x') as f:f.write(str(os.getpid()))
    globals={p:p.read_bytes() if p.exists() else None for p in [Path('D:/IIIDIR.DAT'),Path('C:/IIIDIR.DAT')]}
    try:
        a.output.mkdir(parents=True,exist_ok=False)
        for p in a.source.glob('*.DAT'):
            if p.name not in ['III110.DAT','MESG_IAUTO.DAT']:
                shutil.copy2(p,a.output/p.name)
        if a.auto:
            shutil.copytree(a.source/'计算输入',a.output/'input')
            controls=a.output/'IFILL.DAT';control_lines=controls.read_text('gbk').splitlines()
            control_lines[0]="'"+str((a.output/'input').resolve())+"\\' 1"
            controls.write_text('\n'.join(control_lines)+'\n',encoding='gbk')
        key=a.output/'IKEY.DAT';lines=key.read_text('gbk').splitlines() if key.exists() else []
        if a.modes is not None:
            row=lines[20].split();row[3]=str(a.modes);lines[20]=' '.join(row)
        if a.dt is not None:
            row=lines[14].split();row[3]='0';row[4]=str(a.end);row[5]=str(a.dt);lines[14]=' '.join(row)
        if lines:key.write_text('\n'.join(lines)+'\n',encoding='gbk')
        (a.output/'MESG_IAUTO.DAT').write_text(str(a.auto)+'\n',encoding='ascii')
        template=globals[Path('D:/IIIDIR.DAT')].decode('gbk').splitlines()
        native=str(a.output.resolve())+'\\'
        template[0]="'"+native+"'";template[7]="'"+native+"'"
        runtime=Path(__file__).resolve().parents[1]/'build/smesh_release/solver/Runtime'
        template[3]="'"+str(runtime)+'\\' + "'"
        template[4]=('2' if a.device=='gpu' else '0')+' IDEVICE'
        Path('D:/IIIDIR.DAT').write_text('\n'.join(template)+'\n',encoding='gbk')
        exe=Path(__file__).resolve().parents[1]/('build/smesh_release/solver/PCCAE_'+a.device.upper()+'/PCCAE_'+a.device.upper()+'.exe')
        inputs={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in a.output.glob('*.DAT')}
        started=time.time();env=os.environ.copy();env.update(OMP_NUM_THREADS='4',MKL_NUM_THREADS='4')
        command=[str(exe)]
        if a.debugger:
            script=a.output/'debug.cdb.txt'
            mapping=Path(__file__).resolve().parents[1]/f'build/native_diagnostics/{a.device}_symbol_map.json'
            symbols=json.loads(mapping.read_text('utf8'));module='PCCAE_'+a.device.upper()
            errors=[f for f in symbols['functions'] if f['name']=='ERRORSHOW']
            breakpoint=''
            if len(errors)==1 and symbols['image_sha256']==hashlib.sha256(exe.read_bytes()).hexdigest():
                breakpoint=f'bp {module}+0x{errors[0]["rva"]:x} ".echo NATIVE_ERRORSHOW;r;kv;lm m PCCAE*;q"\n'
            script.write_text('sxi ibp\n'+breakpoint+'sxe -c ".echo NATIVE_EXCEPTION;.exr -1;r;kv;lm m PCCAE*;q" av\ng\n',encoding='ascii')
            command=[str(a.debugger),'-hd','-G','-cf',str(script)]+command
        with (a.output/'native.log').open('wb') as log:
            process=subprocess.Popen(command,cwd=a.output,env=env,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
            timed_out=False
            try:process.communicate(b'\n',timeout=a.timeout)
            except subprocess.TimeoutExpired:
                timed_out=True
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
                process.wait()
        result={'diagnostic_only':True,'source':str(a.source),'device':a.device,'modes':a.modes,'dt':a.dt,'end':a.end,
            'elapsed_seconds':round(time.time()-started,2),'returncode':process.returncode,'timed_out':timed_out,'input_sha256':inputs}
        (a.output/'probe.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        print(json.dumps({k:v for k,v in result.items() if k!='input_sha256'},ensure_ascii=False))
    finally:
        for p,data in globals.items():
            if data is not None:p.write_bytes(data)
            elif p.exists():p.unlink()
        lock.unlink()


if __name__=='__main__':main()
