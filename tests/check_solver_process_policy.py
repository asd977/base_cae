"""Exercise the real frontend with a controlled child process in an isolated bundle.

This verifies process handling, not numerical correctness. No deployed native
solver executable or original test project is changed.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def read(path):
    return json.loads(path.read_text('utf-8'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--cave-project', type=Path, required=True)
    ap.add_argument('--seepage-project', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    a.build = a.build.resolve()
    a.output = a.output.resolve()
    # Refuse to merge fixture executables into any existing directory.
    a.output.mkdir(parents=True, exist_ok=False)
    bundle = a.output / 'fixture_bundle'
    bundle.mkdir()
    for source in a.build.glob('*.dll'):
        os.link(source, bundle / source.name)
    shutil.copy2(a.build / 'SolverRegressionDriver.exe', bundle)
    for name in ['platforms', 'styles', 'imageformats']:
        if (a.build / name).is_dir():
            shutil.copytree(a.build / name, bundle / name)
    if (a.build / 'qt.conf').is_file():
        shutil.copy2(a.build / 'qt.conf', bundle)
    (bundle / 'plugins').mkdir()
    for source in (a.build / 'plugins').glob('*.dll'):
        shutil.copy2(source, bundle / 'plugins' / source.name)
    shutil.copytree(a.build / 'solver/Runtime', bundle / 'solver/Runtime')
    for mode in ['CPU', 'GPU']:
        directory = bundle / 'solver' / ('PCCAE_' + mode)
        directory.mkdir()
        shutil.copy2(a.build / 'SolverProcessFixture.exe', directory / ('PCCAE_' + mode + '.exe'))
    env = os.environ.copy()
    env['PATH'] = 'C:/Qt/6.7.3/msvc2022_64/bin;' + env['PATH']
    env['CAE_REGRESSION_REAL_OUTPUT'] = '1'

    processes = subprocess.check_output(['tasklist', '/FO', 'CSV', '/NH'],
                                       creationflags=subprocess.CREATE_NO_WINDOW).decode('gbk', errors='replace').lower()
    if 'pccae_gpu.exe' in processes or 'pccae_cpu.exe' in processes:
        raise RuntimeError('A solver is running; leave its directory protocol untouched')
    lock = Path('D:/IIIDIR.regression.lock')
    with lock.open('x') as stream:
        stream.write(str(os.getpid()))
    globals_ = {p: p.read_bytes() if p.exists() else None
                for p in [Path('D:/IIIDIR.DAT'), Path('C:/IIIDIR.DAT')]}
    cases = []
    try:
        for name, source, identifier, code, stop in [
            ('cave_controls_and_error_log', a.cave_project, 'C06', 0, False),
            ('seepage_invalid_residual_and_no_head', a.seepage_project, 'D01', 0, False),
            ('actual_nonzero_exit', a.cave_project, 'C06', 17, False),
            ('user_stop', a.cave_project, 'C06', 0, True),
        ]:
            project = a.output / name
            shutil.copytree(source / '工程数据', project / '工程数据')
            data_path = project / '工程数据/solver.json'
            data = read(data_path)
            node = next(n for n in data['items'] if n['title'].startswith(identifier))
            data['items'] = [node]
            if identifier == 'C06':
                def walk(n):
                    yield n
                    for child in n.get('children', []):
                        yield from walk(child)
                support = next(n for n in walk(node) if n['type'] == 'CaveSupportComputation')
                support['state'].update(ifmode='3', ifwave='1', dt=.02, tsta=0., tend=.4)
                shutil.copytree(source / node['title'] / 'GEOSTR', project / node['title'] / 'GEOSTR')
            data_path.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
            env['CAE_FIXTURE_EXIT_CODE'] = str(code)
            phase = 'analysis-manual-stop-check' if stop else 'analysis'
            try:
                result = subprocess.run([str(bundle / 'SolverRegressionDriver.exe'), str(project), node['uuid'],
                                         'gpu', phase, '20'], cwd=bundle, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=35,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
            except subprocess.TimeoutExpired as error:
                (project / 'fixture-driver.log').write_bytes(error.stdout or b'')
                raise
            (project / 'fixture-driver.log').write_bytes(result.stdout)
            workspace = project / node['title']
            manifest = read(workspace / '分析信息.json')
            expected = 'stopped' if stop else 'failed' if code else 'completed'
            assert manifest['state'] == expected, (name, manifest, result.stdout[-2000:])
            assert result.returncode == (8 if code else 0), (name, result.returncode)
            marker = workspace / 'fixture_natural_exit.json'
            if stop:
                assert not marker.exists(), name
            else:
                exit_record = read(marker)
                assert exit_record['elapsed_ms'] >= 1900 and exit_record['exit_code'] == code, name
                assert b'FIXTURE_NATURAL_EXIT=' in result.stdout, name
            raw = (workspace / '求解日志.txt').read_bytes()
            assert b'FIXTURE_STDERR' in raw and b'ERR=NaN' in raw and b'ERROR fixture stdout' in raw, name
            assert 'resultQuality' not in manifest and 'convergenceWarnings' not in manifest, name
            cases.append({'name': name, 'state': manifest['state'], 'passed': True,
                          'fixture_only': True, 'raw_stdout_stderr_preserved': True})
            print(name, expected, 'PASS', flush=True)
    finally:
        for path, original in globals_.items():
            if original is None:
                if path.exists():
                    path.unlink()
            else:
                path.write_bytes(original)
        lock.unlink()
    (a.output / 'process_policy_checks.json').write_text(
        json.dumps({'passed': True, 'fixture_only': True, 'cases': cases}, ensure_ascii=False, indent=2),
        encoding='utf-8')


if __name__ == '__main__':
    main()
