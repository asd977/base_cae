"""Attach setting, export and read-only native evidence to the latest retest."""
import argparse
import hashlib
import math
import re
from pathlib import Path
from build_geotechnical_regression import read, write, walk


GROUPS = [
    (['D16', 'C08', 'S05', 'B09'], 'SUBROUTINE GEDYNA_NORM',
     '求解器：三维粘弹性法向', '有效边界面进入原生法向转换后失败；局部KETYPE/NDIM未赋值。'),
    (['B02', 'B05'], 'SUBROUTINE FREEFTVECT',
     '求解器：三维衬砌法向', '原生读出面节点后没有填入JMC，就调用坐标转换。'),
    (['B03'], 'SUBROUTINE CPU_AUTOCAVE',
     '求解器：首级吊车梁阶段', '第1级安装导出为-1；原生再加1得到0，EXNAME2拒绝阶段0。'),
    (['P03'], 'SUBROUTINE DCLIKEY',
     '求解器：二维吊车梁应力维数', '前序四分量应力被读入原生重置后的三分量缓冲区；既有CDB链已定位READRR。'),
    (['C06', 'B07', 'P06'], 'SUBROUTINE CAVEIKEY',
     '求解器：模态参数被覆盖', '前端导出IFMODE=1；原生生成的IKEY模态阶数为0，前端按真实错误停止。'),
    (['C07', 'B08', 'P04', 'P05'], 'SUBROUTINE CAVEIKEY',
     '求解器：时程参数被覆盖', '工程时间设置已写入ICAVE；原生IKEY改成步长0.0002秒、终点100秒。'),
    (['P07'], 'SUBROUTINE DYNAIIWL',
     '求解器：单元卸载后动力数组', '单元卸载留下非零NFS，动力写入时JMFS/FSLOAD未分配；既有CDB空指针证据。'),
    (['P08'], 'SUBROUTINE CHENIIWL',
     '求解器：单元卸载后衬砌数组', '单元卸载留下非零NFS，衬砌写入时JMFS/FSLOAD未分配；既有CDB空指针证据。'),
]


def audit_export(workspace, node, started):
    support = next((n['state'] for n in walk(node) if n['type'] == 'CaveSupportComputation'), None)
    result = {'input_files': {}, 'errors': []}
    inputs = workspace / '计算输入'
    for name in ['IIIN.DAT', 'IIID.DAT', 'IMAT.DAT', 'IIWL.DAT',
                 'FA_DYNA.DAT', 'IIJX_CHEN.DAT', 'IIJX_WAI.DAT', 'MASET_DCL.DAT']:
        path = inputs / name
        if path.exists():
            result['input_files'][name] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            if name in ['FA_DYNA.DAT', 'IIJX_CHEN.DAT', 'IIJX_WAI.DAT', 'MASET_DCL.DAT']:
                result['input_files'][name]['declared_count'] = int(path.read_bytes().split()[0])
    if support is None:
        return result
    path = workspace / 'ICAVE.DAT'
    lines = path.read_text('gbk', errors='replace').splitlines()
    result['icave'] = {'path': str(path), 'fresh': path.stat().st_mtime >= started-2}
    if support.get('crane_beam_enabled'):
        exported = int(lines[9].split()[0])
        expected = -int(support['crane_beam_install_stage'])
        result['icave']['INSTALL_DCL'] = {'requested': expected, 'exported': exported, 'matches': exported == expected}
        if exported != expected:
            result['errors'].append('吊车梁安装阶段与工程不符')
    for label, keys in [('IFMODE   IFWAVE', ['ifmode', 'ifwave']), ('DT TSTA TEND', ['dt', 'tsta', 'tend'])]:
        line = next((s for s in lines if label in s), '')
        actual = [float(v) for v in line.split()[:len(keys)]]
        expected = [float(support[k]) for k in keys]
        matches = len(actual) == len(expected) and all(math.isclose(a, b, abs_tol=1e-6) for a, b in zip(actual, expected))
        result['icave'][label] = {'requested': expected, 'exported': actual, 'matches': matches}
        if not matches:
            result['errors'].append('ICAVE设置与工程不符: ' + label)
    for phase in ['CASE01MODE', 'CASE01WAVE']:
        path = workspace / phase / 'IKEY.DAT'
        if path.exists() and path.stat().st_mtime >= started-2:
            key = path.read_text('gbk', errors='replace').splitlines()
            result[phase] = {'path': str(path), 'time_control': key[14].strip(), 'mode_control': key[20].strip()}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('suite', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args(); root = args.suite
    args.output.mkdir(parents=True, exist_ok=True)
    summary = read(root / 'run_summary.json')
    coverage = read(root / 'coverage.json')
    findings = read(root / 'solver_readonly_findings.json')
    by_section = {f['section']: f for f in findings}
    origins = {id: (section, category, explanation) for ids, section, category, explanation in GROUPS for id in ids}
    definitions = {c['id']: (p, c) for p in coverage['projects'] for c in p['cases']}
    rows = []
    for case in summary['cases']:
        identifier = case['id']
        if identifier not in origins and identifier not in ['D01', 'D03', 'D10', 'D11']:
            continue
        project, definition = definitions[identifier]
        node = next(n for n in read(root / project['directory'] / '工程数据/solver.json')['items'] if n['uuid'] == definition['analysis_uuid'])
        record = next(c for c in read(Path(case['report']))['cases'] if c['id'] == identifier)
        attempts = [a for a in record['attempts'] if a['phase'] == 'analysis'] or record['attempts']
        last = attempts[-1]
        audit = audit_export(root / project['directory'] / case['title'], node, last['started_at_epoch'])
        text = Path(last['log']).read_text('utf8', errors='replace')
        excerpts = re.findall(r'^.*(?:forrtl: severe|ERROR IN|覆盖了时程设置|模态阶数为0|failed:|后处理结果已生成).*$', text, re.M)
        row = {'id': identifier, 'project': case['project'], 'title': case['title'],
               'workspace': str(root / case['project'] / case['title']),
               'generated': case['generated'], 'converged': case['passed_sanity'],
               'completed_with_warnings': case['completed_with_warnings'],
               'run_report': case['report'], 'log': last['log'], 'log_evidence': excerpts[-6:],
               'export_audit': audit, 'postprocessing_generated': last['results'].get('postprocessing_generated', False)}
        if identifier in origins:
            section, category, explanation = origins[identifier]
            row.update(category=category, explanation=explanation, source_evidence=by_section[section],
                       historical_debugger_evidence=str(root / 'native_debugger_findings.json'))
            if case['generated']:
                row['explanation'] += ' 本轮已输出，历史缺陷仍需与本轮日志分别评估。'
        else:
            row.update(category='数值未收敛：尚不能唯一归为设置或求解器',
                       explanation='输入被接受并实际生成水头、渗压、速度和后处理结果；有限残差超限仅警告。误差停滞/振荡不能单凭日志确定全部根因，需进一步研究自由面/干湿转换、排水离散和迭代策略。',
                       warnings=case['warnings'], source_evidence=by_section['SUBROUTINE PROB21'])
        if audit['errors']:
            row['category'] = '前端导出待修复'
        rows.append(row)
    unchanged = all(hashlib.sha256(Path(f['file']).read_bytes()).hexdigest() == f['sha256'] for f in findings)
    payload = {'cases': rows, 'solver_sources_unchanged': unchanged,
               'counts': {k: summary[k] for k in ['case_count', 'passed_sanity', 'completed_with_warnings', 'failed']},
               'policy': 'GPU first, CPU fallback on actual failure; no dedicated comparisons. Finite residual excess is nonblocking.'}
    write(args.output / '问题归属.json', payload)
    doc = ['# 未通过工况问题归属复测', '',
           f"共{summary['case_count']}项：{summary['passed_sanity']}项数值检查通过，{summary['completed_with_warnings']}项结果已生成但残差超限，{summary['failed']}项运行失败。", '',
           '设置、导出与求解器问题分别核对。下表原生问题结合本轮复现日志和只读源码/既有断点证据；不根据报错文字单独定责。',
           '四项残差超限不再阻断，未收敛标记保留；D03已完成快速降水瞬态。求解器源码未修改。', '',
           '|编号|工程|本轮结果|归属|依据|', '|---|---|---|---|---|']
    for r in rows:
        status = '数值检查通过' if r['converged'] else '结果已生成，残差超限' if r['completed_with_warnings'] else '运行失败'
        doc.append('|' + '|'.join([r['id'], r['project'], status, r['category'], r['explanation']]) + '|')
    doc += ['', '详细设置/ICAVE一致性、原生IKEY、输入SHA256、当前日志路径和源码位置见同目录“问题归属.json”。',
            '历史已修复设置包括迭代上限不足、冲突开关、前序状态/水位不匹配；历史已修复导出包括目录、网格头、材料集及荷载段落。它们与本轮仍失败的原生路径分开记录。',
            '目前没有证据将四项渗流未收敛全部归为设置错误，也不能把生成结果当作数值收敛。']
    (args.output / '问题归属.md').write_text('\n'.join(doc) + '\n', encoding='utf8')
    print(payload['counts'], 'source_hashes_unchanged=', unchanged)


if __name__ == '__main__':
    main()
