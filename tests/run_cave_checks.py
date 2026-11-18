"""Compile cave protocol checks; optionally replay an existing project in isolated runs.

python tests/run_cave_checks.py --build-dir build/Desktop_Qt_6_7_3_MSVC2022_64bit_Release
Add --project C:/test/rock/<project> --output C:/test/rock/<new-test-directory>
for the legacy solver integration regression. Originals and D:/IIIDIR.DAT are preserved.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--project', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    stage = build.parent / 'cave_checks'
    stage.mkdir(exist_ok=True)
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    compiler = re.search(r'^CMAKE_CXX_COMPILER:\w+=(.*)$', cache, re.M)[1]
    vcvars = Path(compiler.split('/VC/Tools/')[0]) / 'VC/Auxiliary/Build/vcvars64.bat'
    qt = Path(re.search(r'^Qt6_DIR:\w+=(.*)$', cache, re.M)[1]).parents[2]
    exe = stage / 'CaveExcavationProtocolChecks.exe'
    script = stage / 'build.cmd'
    script.write_text(
        f'@echo off\ncall "{vcvars}"\n'
        f'cl /nologo /EHsc /std:c++17 /Zc:__cplusplus /permissive- /MD /utf-8 /I"{repo}" '
        f'/I"{qt}/include" /I"{qt}/include/QtCore" '
        f'/Fo"{stage}/checks.obj" /Fe"{exe}" '
        f'"{repo}/tests/CaveExcavationProtocolChecks.cpp" "{qt}/lib/Qt6Core.lib"\n',
        encoding='utf-8')
    with (stage / 'build.log').open('wb') as log:
        result = subprocess.run(['cmd', '/c', str(script)], cwd=stage, stdout=log,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError((stage / 'build.log').read_text(errors='replace'))
    env = os.environ.copy()
    env['PATH'] = str(qt / 'bin') + os.pathsep + env.get('PATH', '')
    subprocess.run([str(exe)], env=env, check=True)
    if not args.project:
        return
    if not args.output or args.output.exists():
        raise ValueError('Integration requires a new --output directory')
    source = args.project.resolve()
    work = next(p.parent for p in source.rglob('ICAVE.DAT') if (p.parent / 'GEOSTR').is_dir())
    original_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in source.rglob('*') if p.is_file()}
    args.output.mkdir(parents=True)
    solver = build / 'solver/PCCAE_CPU/PCCAE_CPU.exe'
    control = Path('D:/IIIDIR.DAT')
    original_control = control.read_bytes()
    summaries = []
    try:
        for label, method in [('baseline', 1), ('face_fixed', 1), ('element_fixed', 2)]:
            run = args.output.resolve() / label
            run.mkdir()
            shutil.copytree(work / '计算输入', run / 'input')
            shutil.copytree(work / 'GEOSTR', run / 'GEOSTR')
            for name in ['ICAVE.DAT', 'IGEOS.DAT', 'IMID_GEOS.DAT']:
                shutil.copy2(work / name, run / name)
            old_input = (work / 'ICAVE.DAT').read_text(encoding='gbk').splitlines()[0].split("'")[1]
            old_root = old_input.rstrip('\\').rsplit('\\', 1)[0] + '\\'
            for p in run.rglob('*'):
                if p.is_file() and p.suffix.upper() in ['.DAT', '.TXT']:
                    text = p.read_text(encoding='gbk', errors='replace')
                    text = text.replace(old_input, str(run / 'input') + '\\')
                    text = text.replace(old_root, str(run) + '\\')
                    p.write_text(text, encoding='gbk', errors='replace')
            p = run / 'ICAVE.DAT'
            lines = p.read_text(encoding='gbk').splitlines()
            if 'IEXCA_MAX' in lines[25]:
                lines[25:25] = ["'0' 26", "'FA_DYNA.DAT' 27", "'---' 28", "'---' 29", "'---' 30"]
            levels = int(lines[30].split()[0])
            lines[22] = f"'{method}' 23"
            p.write_text('\n'.join(lines) + '\n', encoding='gbk')
            (run / 'MESG_IAUTO.DAT').write_text('5\n')
            if label == 'face_fixed':
                p = run / 'input/IMAT.DAT'
                rows = p.read_text(encoding='gbk').splitlines()
                # This customer fixture uses material 4 for the excavated rock.
                for i, row in enumerate(rows):
                    fields = row.split()
                    if len(fields) == 11 and fields[-1] == '58':
                        fields[3] = '1.0000E+00'
                        rows[i] = ' '.join(fields)
                        break
                p.write_text('\n'.join(rows) + '\n', encoding='gbk')
                subprocess.run([str(exe), '--rewrite-faces', str(run / 'input')], env=env, check=True)
            elif label == 'element_fixed':
                subprocess.run([str(exe), '--prepare-elements', str(run / 'input'), str(run),
                                str(levels)], env=env, check=True)
            lines = original_control.decode('gbk').splitlines()
            lines[0] = "'" + str(run) + "\\'"
            lines[4:7] = ['0 IDEVICE', '0 ILOG_GZKZR', '0 ILOG_SOLVE']
            lines[7] = "'" + str(run / 'input') + "\\'"
            lines[8] = '6'
            control.write_text('\n'.join(lines) + '\n', encoding='gbk')
            run_env = env.copy()
            run_env['PATH'] = str(solver.parent) + os.pathsep + run_env['PATH']
            run_env['OMP_NUM_THREADS'] = '4'
            started = time.time()
            try:
                with (run / 'console.log').open('wb') as log:
                    result = subprocess.run([str(solver)], cwd=run, env=run_env, stdout=log,
                                            stderr=subprocess.STDOUT, timeout=90,
                                            creationflags=subprocess.CREATE_NO_WINDOW)
            finally:
                control.write_bytes(original_control)
            log = (run / 'console.log').read_text(encoding='gbk', errors='replace')
            # Legacy MAIN waits for a key after completing. Headless execution
            # may return 24 there; require success marker and fresh complete results.
            assert 'Z05_AUTOCAVE:*****' in log and '成功运行程序' in log, log[-2500:]
            assert result.returncode in [0, 24], result.returncode
            if result.returncode == 24:
                assert 'MAIN__' in log and 'CONIN$' in log
            values = {}
            for p in sorted(run.rglob('IIDISP.TXT')):
                if p.parent.name == 'GEOSTR':
                    continue
                assert p.stat().st_mtime >= started, 'stale result'
                rows = [list(map(float, l.split()[1:]))
                        for l in p.read_text(encoding='gbk', errors='replace').splitlines()[3:]
                        if l.split()]
                values[str(p.parent.relative_to(run))] = max(math.hypot(*row) for row in rows) * 1000
            assert len(values) == 8, values
            assert all(v == 0 for v in values.values()) if label == 'baseline' else all(v > 0 for v in values.values())
            summary = {'test': label, 'exit_code': result.returncode, 'max_displacement_mm': values}
            summaries.append(summary)
            print(json.dumps(summary), flush=True)
    finally:
        control.write_bytes(original_control)
        assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in original_hashes.items())
    (args.output / 'summary.json').write_text(json.dumps(summaries, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
