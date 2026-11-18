"""Verify real static/seepage outputs and warning metadata from focused runs."""
import argparse
import hashlib
from pathlib import Path
import xml.etree.ElementTree as ET
from build_geotechnical_regression import read, write


def check(report_path, suite, identifier):
    report = read(report_path)
    record = next(c for c in report['cases'] if c['id'] == identifier)
    assert record['completed'] and not record['passed']
    assert record['result_status'] == 'completed_with_warnings'
    assert all(a['device'] == 'gpu' for a in record['attempts']), 'unexpected fallback on residual warning'
    outputs = []
    for attempt in record['attempts']:
        assert attempt['completed'] and attempt['results']['postprocessing_generated']
        assert attempt['manifest']['state'] == 'completed'
        assert attempt['manifest']['resultQuality'] == 'residual_exceeded'
        assert attempt['manifest']['convergenceWarnings']
        assert not attempt['results']['errors']
        outputs.append({'phase': attempt['phase'], 'seconds': attempt['elapsed_seconds'], 'log': attempt['log'],
                        'warnings': attempt['manifest']['convergenceWarnings']})
    coverage = read(suite / 'coverage.json')
    project, case = next((p, c) for p in coverage['projects'] for c in p['cases'] if c['id'] == identifier)
    workspace = suite / project['directory'] / case['title']
    manifest = read(workspace / '分析信息.json')
    assert manifest['state'] == 'completed' and manifest['resultQuality'] == 'residual_exceeded'
    last = record['attempts'][-1]
    files = []
    for vtm in (workspace / '计算结果').rglob('*.vtm'):
        assert vtm.stat().st_mtime >= last['started_at_epoch'] - 2
        references = [node.attrib['file'] for node in ET.parse(vtm).iter() if 'file' in node.attrib]
        assert references
        for name in references:
            path = vtm.parent / name
            assert path.is_file() and path.stat().st_size > 0
            assert path.stat().st_mtime >= last['started_at_epoch'] - 2
        files.append({'path': str(vtm), 'sha256': hashlib.sha256(vtm.read_bytes()).hexdigest(),
                      'nonempty_fresh_vtu_blocks': len(references)})
    assert files, 'no actual VTM result cache'
    if identifier == 'D03':
        key = (workspace / 'SEEPDYNA01/IKEY.DAT').read_text('gbk', errors='replace').splitlines()[14].split()
        assert float(key[4]) == 1200
        assert (workspace / 'SEEPDYNA01/STEP10_IIDISP.TXT').stat().st_mtime >= last['started_at_epoch'] - 2
    return {'id': identifier, 'workspace': str(workspace), 'phases': outputs, 'result_files': files,
            'static_steps': last['results'].get('static_equilibrium_steps', []),
            'passed_nonblocking_output_check': True}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('suite', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args(); root = args.suite
    summary = read(root / 'run_summary.json')
    by_id = {c['id']: c for c in summary['cases']}
    checks = [check(Path(by_id[id]['report']), root, id) for id in ['D01', 'D03', 'D10', 'D11']]
    static_suite = root / 'validation/nonblocking_static_20261029'
    static_report = sorted((static_suite / 'runs').glob('*/results.json'))[-1]
    checks.append(check(static_report, static_suite, 'S02'))
    write(args.output, {'checks': checks, 'passed': True,
                        'qualification': 'Output generation and warning propagation verified; residuals remain unconverged. S02 is an isolated copy with 20 iterations; original S02 settings unchanged.'})
    print('PASS: four seepage cases, transient warning inheritance, isolated static residual, fresh VTM/VTU output')


if __name__ == '__main__':
    main()
