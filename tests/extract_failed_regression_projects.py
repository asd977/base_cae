"""Extract compatible failed cases into standalone, independently editable projects."""
import argparse
import copy
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import uuid
import xml.etree.ElementTree as ET

from build_geotechnical_regression import read, write, walk, verify


GROUPS = [
    ('01_坝_粘弹性边界', '坝_多工况回归', ['D16']),
    ('02_边坡_粘弹性边界', '边坡_多工况回归', ['S05']),
    ('03_洞室原模型_动力边界', '洞室_多工况回归', ['C06', 'C07', 'C08']),
    ('04_洞室三维_衬砌吊车梁动力', '洞室_边界专项回归', ['B02', 'B03', 'B05', 'B07', 'B08', 'B09']),
    ('05_洞室二维_吊车梁卸载动力', '洞室_二维边界对照', ['P03', 'P04', 'P05', 'P06', 'P07', 'P08']),
    ('06_坝_渗流残差超限', '坝_多工况回归', ['D01', 'D03', 'D10', 'D11']),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rebase(value, source, target, ids):
    if isinstance(value, dict):
        return {k: rebase(v, source, target, ids) for k, v in value.items()}
    if isinstance(value, list):
        return [rebase(v, source, target, ids) for v in value]
    if isinstance(value, str):
        if value in ids:
            return ids[value]
        for old, new in [(str(source), str(target)), (source.as_posix(), target.as_posix())]:
            value = value.replace(old, new)
        return value
    return value


def rebase_files(folder, source, target, ids):
    for path in folder.rglob('*'):
        if not path.is_file():
            continue
        if path.suffix.lower() == '.json':
            write(path, rebase(read(path), source, target, ids))
        elif path.suffix.lower() in ['.dat', '.txt', '.vtm', '.xml']:
            data = original = path.read_bytes()
            for encoding in ['utf8', 'gbk']:
                for old, new in [(str(source), str(target)), (source.as_posix(), target.as_posix())]:
                    data = data.replace(old.encode(encoding), new.encode(encoding))
            if data != original:
                path.write_bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('suite', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args(); root = args.suite.resolve(); output = args.output.resolve()
    if output.exists():
        raise RuntimeError('Output already exists; preserve existing projects: ' + str(output))
    coverage = read(root / 'coverage.json')
    definitions = {p['directory']: p for p in coverage['projects']}
    summary = {c['id']: c for c in read(root / 'run_summary.json')['cases']}
    findings = {c['id']: c for c in read(root / 'analysis_findings.json')['cases']}
    source_hashes = {str(p): sha(p) for name in {g[1] for g in GROUPS}
                     for p in (root / name / '工程数据').rglob('*') if p.is_file()}
    output.mkdir(parents=True)
    extracted = {'version': 1, 'source_suite': str(root), 'solver_source': coverage['solver_source'],
                 'scope': '17 runtime failures plus 4 separately classified residual warnings; original models and settings retained.',
                 'projects': []}
    index = []
    for name, source_name, identifiers in GROUPS:
        source = root / source_name; target = output / name
        data = target / '工程数据'; data.mkdir(parents=True)
        original = read(source / '工程数据/solver.json')
        selected = [n for n in original['items'] if n['title'].split('－')[0] in identifiers]
        assert len(selected) == len(identifiers)
        ids = {n['uuid']: '{' + str(uuid.uuid4()) + '}' for node in selected for n in walk(node)}
        solver = copy.deepcopy(original); solver['items'] = copy.deepcopy(selected)
        for node in solver['items']:
            for n in walk(node):
                n.get('state', {}).pop('last_job', None)
        solver = rebase(solver, source, target, ids)
        for path in (source / '工程数据').iterdir():
            if path.name == 'D17_static_reuse_provenance.json':
                continue
            if path.is_dir():
                if path.name == '计算输入':
                    shutil.copytree(path, data / path.name)
            elif path.name != 'solver.json':
                shutil.copy2(path, data / path.name)
        rebase_files(data, source, target, ids)
        write(data / 'solver.json', solver)
        write(data / 'postprocessing.json', {'items': []})
        write(data / 'surrogate_model.json', {'module': 'SurrogateModel', 'items': []})
        manifest = ET.parse(next(source.glob('*.xml')))
        manifest.getroot().find('Name').text = name
        manifest.write(target / (name + '.xml'), encoding='UTF-8', xml_declaration=True)
        project = {'directory': name, 'seed': str(source), 'cases': []}
        notes = ['# ' + name, '', '打开同目录的同名 XML；每个计算分析节点是一个工况。',
                 '模型、材料、约束、选择区域和计算参数沿用原工程，分析节点采用新的 UUID，输入路径已迁入本工程。',
                 '洞室保留各工况已有地应力初始场；需要重算时，先运行该工况下的地应力回归，再运行开挖与加固。',
                 '渗流工程保留原先已输出但残差超限的结果，包括D03稳态初始场和瞬态；它们是历史结果副本，未宣称在新目录重新求解。',
                 '原始失败/警告日志、问题归属及输入来源见“问题记录”。', '',
                 '|工况|原结果|问题归属|', '|---|---|---|']
        for identifier in identifiers:
            definition = copy.deepcopy(next(c for c in definitions[source_name]['cases'] if c['id'] == identifier))
            previous = summary[identifier]; node = next(n for n in solver['items'] if n['title'] == definition['title'])
            assert not definition.get('depends_on'), 'Explicit dependency needs extraction too'
            old_workspace = source / definition['title']; workspace = target / definition['title']
            warning = previous.get('completed_with_warnings', False)
            if warning:
                shutil.copytree(old_workspace, workspace)
                rebase_files(workspace, source, target, ids)
                state = read(workspace / '分析信息.json')
                state['extractedFrom'] = str(old_workspace)
                state['historicalResultCopy'] = True
                write(workspace / '分析信息.json', state)
            else:
                workspace.mkdir()
                for directory in ['计算输入', 'GEOSTR']:
                    if (old_workspace / directory).exists():
                        shutil.copytree(old_workspace / directory, workspace / directory)
                rebase_files(workspace, source, target, ids)
            record_dir = target / '问题记录' / identifier; record_dir.mkdir(parents=True)
            report = read(Path(previous['report']))
            record = next(c for c in report['cases'] if c['id'] == identifier)
            for attempt in record.get('attempts', []):
                log = Path(attempt['log'])
                if log.exists():
                    shutil.copy2(log, record_dir / log.name)
                native = log.with_suffix('.III110.DAT')
                if native.exists():
                    shutil.copy2(native, record_dir / native.name)
            write(record_dir / '原运行记录.json', record)
            write(record_dir / '问题定位.json', findings[identifier])
            provenance = {'id': identifier, 'source_project': str(source), 'source_workspace': str(old_workspace),
                          'source_analysis_uuid': definition['analysis_uuid'], 'new_analysis_uuid': node['uuid'],
                          'source_result_status': 'completed_with_warnings' if warning else 'failed',
                          'copied_historical_results': warning, 'new_directory_solver_executed': False,
                          'mesh_snapshot_sha256': sha(data / 'mesh_snapshot.iiin.dat'),
                          'source_mesh_snapshot_sha256': sha(source / '工程数据/mesh_snapshot.iiin.dat')}
            assert provenance['mesh_snapshot_sha256'] == provenance['source_mesh_snapshot_sha256']
            write(record_dir / '提取来源.json', provenance)
            definition.update(analysis_uuid=node['uuid'], status='prepared_not_run', result_status='not_run',
                              source_result_status=provenance['source_result_status'], source_report=previous['report'],
                              last_run_report=None, blockers=[], failure_reasons=[], extraction_provenance=str(record_dir.relative_to(target) / '提取来源.json'))
            project['cases'].append(definition)
            status = '已生成，残差超限' if warning else '运行失败'
            notes.append('|' + '|'.join([definition['title'], status, findings[identifier]['category']]) + '|')
            index.append({'project': name, 'id': identifier, 'title': definition['title'], 'status': status,
                          'category': findings[identifier]['category'], 'xml': str(target / (name + '.xml'))})
        write(target / 'case_coverage.json', project)
        (target / '说明.md').write_text('\n'.join(notes) + '\n', encoding='utf8')
        extracted['projects'].append(project)
    unchanged = all(sha(Path(path)) == value for path, value in source_hashes.items())
    assert unchanged, 'Source project was changed'
    extracted['source_projects_unchanged'] = unchanged
    write(output / 'coverage.json', extracted)
    write(output / '提取清单.json', {'projects': 6, 'runtime_failures': 17, 'residual_warnings': 4, 'cases': index,
                                    'source_projects_unchanged': True, 'new_solver_runs': 0})
    doc = ['# 未通过工况专项工程', '',
           '共6个独立工程：17项原运行失败工况，另列4项已生成结果但残差超限的渗流工况。',
           '按模型及维度分组：一个工程共用一套网格，同类工况放在同一个工程；不同网格未强行合并。',
           '分别打开各目录下的同名XML。原工程未修改；新分析UUID和运行路径均独立。',
           '“问题记录”中的日志为提取前的真实记录；本次提取没有重新计算。洞室地应力初始场及渗流已有输出已保留。', '',
           '|工程|工况|', '|---|---|']
    for p in extracted['projects']:
        doc.append('|' + p['directory'] + '|' + '、'.join(c['id'] for c in p['cases']) + '|')
    doc += ['', '各工况问题归属、原运行结果、日志和提取来源见各工程“说明.md”及“问题记录”。',
            '已有渗流结果保留未收敛标记；生成结果不代表达到数值容差。',
            '复算建议使用 C:/project/CAE/base_cae/build/release/POWERCHINA-GEOCAE.exe。']
    (output / 'README.md').write_text('\n'.join(doc) + '\n', encoding='utf8')
    verify(output)
    print(json.dumps({'output': str(output), 'projects': len(extracted['projects']), 'cases': len(index), 'sources_unchanged': unchanged}, ensure_ascii=False))


if __name__ == '__main__':
    main()
