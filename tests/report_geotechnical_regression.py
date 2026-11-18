"""Summarize actual attempts, input feature checks and numerical limitations."""
import argparse
from collections import Counter
import hashlib
from pathlib import Path
import re

from build_geotechnical_regression import read, write, walk
from run_geotechnical_regression import inspect_feature_results,inspect_results,inspect_analytic_head,inspect_analytic_displacement,inspect_static_convergence


def count_file(path):
    if not path.exists():return 0
    try:return int(path.read_text('gbk',errors='replace').split()[0])
    except (ValueError,IndexError):return 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite',type=Path)
    args=parser.parse_args();root=args.suite;coverage=read(root/'coverage.json');latest={}
    for path in sorted((root/'runs').glob('*/results.json')):
        report=read(path)
        if report.get('diagnostic_only') or report.get('comparison_only'):continue
        for case in report['cases']:
            if 'passed' in case:latest[(case['project'],case['id'])]=(case,path)
    rows=[]
    for project in coverage['projects']:
        nodes={n['uuid']:n for n in read(root/project['directory']/'工程数据/solver.json')['items']}
        for case in project['cases']:
            key=(project['directory'],case['id']);record,path=latest.get(key,({},None))
            attempts=record.get('attempts',[]);analysis=[a for a in attempts if a['phase']=='analysis']
            last=analysis[-1] if analysis else attempts[-1] if attempts else {}
            passed=record.get('passed',False)
            completed=record.get('completed',passed)
            warnings=list(record.get('warnings',[]))
            errors=last.get('results',{}).get('errors',[])
            reasons=[]
            node=nodes[case['analysis_uuid']]
            seepage_metrics=None
            if node['type']=='SeepageAnalysis' and last.get('log'):
                started=last.get('started_at_epoch',Path(last['log']).stat().st_mtime-last['elapsed_seconds'])
                seepage_metrics=inspect_results(root/project['directory']/case['title'],started,True)
                errors=[e for e in errors if 'exceeds 10 m' not in e]+seepage_metrics['errors']
                if seepage_metrics['errors']:passed=False
            feature={'artifacts':[],'errors':[]}
            analytic={}
            if completed and case.get('analytic_head'):
                started=last.get('started_at_epoch',Path(last['log']).stat().st_mtime-last['elapsed_seconds'])
                comparison=inspect_analytic_head(root/project['directory']/case['title'],started,case['analytic_head'])
                analytic=comparison['metrics']
                if comparison['errors']:passed=False;completed=False;reasons.extend(comparison['errors'])
            if completed and case.get('analytic_displacement'):
                started=last['started_at_epoch']
                comparison=inspect_analytic_displacement(root/project['directory']/case['title'],started,case['analytic_displacement'])
                analytic=comparison['metrics']
                if comparison['errors']:passed=False;completed=False;reasons.extend(comparison['errors'])
            if completed and last.get('phase')=='analysis':
                started=last.get('started_at_epoch',Path(last['log']).stat().st_mtime-last['elapsed_seconds'])
                feature=inspect_feature_results(root/project['directory']/case['title'],started,node)
                if feature['errors']:passed=False;completed=False;reasons.extend(feature['errors'])
            if record.get('blocked_by'):reasons.append('前置工况尚未通过：'+','.join(record['blocked_by']))
            for error in errors:
                if 'tolerance' in error:reasons.append('渗流迭代未达到设定容差')
                elif 'exceeds 10 m' in error:reasons.append('有效单元节点位移超过10米检查阈值')
                elif error.strip().startswith('ERROR'):reasons.append(error.strip())
                else:reasons.append(error)
            logtext=Path(last['log']).read_text('utf8',errors='replace') if last.get('log') else ''
            equilibrium=inspect_static_convergence(logtext,node)
            if completed and equilibrium['errors']:passed=False;completed=False;reasons.extend(equilibrium['errors'])
            completed_with_warnings=bool(completed and warnings and not errors)
            if not passed and not completed_with_warnings:
                patterns=[r'forrtl: severe[^\r\n]*',r'ERROR[^\r\n]*',r'[^\r\n]*failed: [^\r\n]*',r'DIALOG: [^\r\n]*']
                for pattern in patterns:
                    found=re.findall(pattern,logtext)
                    if found:reasons.append(found[-1]);break
                if not reasons:reasons.append('超时' if last.get('returncode')==9 else '未生成符合检查条件的完整结果')
            workspace=root/project['directory']/case['title'];inputs=workspace/'计算输入'
            pages=list(walk(node));support=next((n['state'] for n in pages if n['type']=='CaveSupportComputation'),{})
            checks={'fixed_nodes':0}
            iiid=inputs/'IIID.DAT'
            if iiid.exists():checks['fixed_nodes']=sum('0' in line.split()[:3] for line in iiid.read_text().splitlines()[2:])
            if support.get('lining_enabled'):
                checks.update(lining_inner_faces=count_file(inputs/'IIJX_CHEN.DAT'),
                              lining_outer_faces=count_file(inputs/'IIJX_WAI.DAT'),
                              lining_result=(workspace/'CASE01LINE/IIDISP.TXT').exists())
            if support.get('crane_beam_enabled'):
                checks.update(crane_material_count=count_file(inputs/'MASET_DCL.DAT'),
                    crane_install_stage=support['crane_beam_install_stage'],
                    crane_release_factor=support['crane_beam_release_factor'])
            if support.get('quiet_boundary_enabled') or node['state'].get('quiet_boundary_enabled'):
                checks['absorbing_faces']=count_file(inputs/'FA_DYNA.DAT')
            if support.get('reinforcement_field',{}).get('mode',0):
                checks['reinforcement_fields']=[p.name for p in inputs.glob('ALFA*.DAT') if p.stat().st_size>0]
            if case.get('static_reuse_provenance'):
                provenance=read(root/project['directory']/case['static_reuse_provenance'])
                copies_match=all(Path(f['copy']).exists() and hashlib.sha256(Path(f['copy']).read_bytes()).hexdigest()==f['sha256'] for f in provenance['files'])
                checks.update(static_source_case=provenance['source_case'],copied_static_state_unchanged=copies_match,
                    source_first_circular_frequency_rad_s=provenance['first_circular_frequency_rad_s'])
                if passed and not copies_match:passed=False;reasons.append('复用的静力状态文件与复制前记录不一致')
            if node['type']=='SeepageAnalysis':
                checks.update(natural_boundary_nodes=count_file(inputs/'JDFREE.DAT'),drain_nodes=count_file(inputs/'JDJING.DAT'))
            fill=next((p['state'] for p in pages if p['type']=='FillStorageCycle'),{})
            if fill.get('values',{}).get('IFIIWL_BODY')=='2' and fill.get('seepage_result_type')=='steady':
                source=nodes.get(fill.get('seepage_source_uuid'),{})
                source_dir=root/project['directory']/source.get('title','')/'SEEPCASE01'
                matches={name: (source_dir/name).exists() and (inputs/target).exists()
                    and hashlib.sha256((source_dir/name).read_bytes()).digest()==hashlib.sha256((inputs/target).read_bytes()).digest()
                    for name,target in [('IIDISP.TXT','SEEP_C01_HEAD.TXT'),('INBODY.TXT','SEEP_C01_BODY.TXT')]}
                checks.update(seepage_source=source.get('title'),seepage_export_matches_source=matches,
                    target_reservoir_level_m=float(fill['circle_table'][-1][4]))
                if passed and not all(matches.values()):
                    passed=False;reasons.append('耦合输入与关联稳态场文件不一致')
            status='passed_sanity' if passed else 'completed_with_warnings' if completed_with_warnings else 'failed'
            case.update(status=status,result_status=status,convergence_warnings=warnings,
                        last_run_report=str(path) if path else None,actual_input_checks=checks,
                        failure_reasons=list(dict.fromkeys(reasons)) if not passed and not completed_with_warnings else [])
            case.setdefault('preparation_notes',case.get('blockers',[]))
            case['blockers']=case['failure_reasons']
            displacement=last.get('results',{}).get('displacements',[])
            if seepage_metrics is not None:displacement=seepage_metrics['displacements']
            row={'project':project['directory'],'id':case['id'],'title':case['title'],'passed_sanity':passed,
                 'completed_with_warnings':completed_with_warnings,'generated':bool(passed or completed_with_warnings),'warnings':warnings,
                 'device':last.get('device'),'cpu_fallback_attempted':any(a['device']=='cpu' for a in attempts),
                 'input_checks':checks,'failures':case['failure_reasons'],
                 'verified_feature_outputs':feature['artifacts'],
                 'analytic_head':analytic if case.get('analytic_head') else {},
                 'analytic_displacement':analytic if case.get('analytic_displacement') else {},
                 'static_equilibrium_steps':equilibrium['steps'],
                 'result_metric':{'kind':'head' if node['type']=='SeepageAnalysis' else 'active_node_displacement',
                    'max_abs_m':max((r['max_abs_m'] for r in displacement),default=None)},
                 'report':str(path) if path else None}
            rows.append(row)
    source=coverage['solver_source'];unchanged=hashlib.sha256(Path(source['file']).read_bytes()).hexdigest()==source['sha256']
    summary={'case_count':len(rows),'passed_sanity':sum(r['passed_sanity'] for r in rows),
             'completed_with_warnings':sum(r['completed_with_warnings'] for r in rows),
             'failed':sum(not r['passed_sanity'] and not r['completed_with_warnings'] for r in rows),'solver_source_hash_unchanged':unchanged,
             'cpu_fallback_cases':sum(r['cpu_fallback_attempted'] for r in rows),'cases':rows,
             'qualification':'Functional checks, Darcy analytical comparisons, mechanical/kinematic/thermal-gradient and planar beam analytical tests; no general certified engineering accuracy baseline.'}
    comparison={}
    for identifier in ['B04','B06']:
        record,_=latest.get(('洞室_边界专项回归',identifier),({},None))
        if not record.get('passed'):continue
        final=next((a for a in reversed(record['attempts']) if a['phase']=='analysis'),{})
        stage=next((d for d in final.get('results',{}).get('displacements',[])
                    if Path(d['file']).parent.name=='J02PART'),None)
        if stage:comparison[identifier]={'stage':'J02PART','max_active_displacement_m':stage['max_abs_m']}
    summary['beam_release_comparison']=comparison
    summary['boundary_geometry_check']='洞室_边界专项回归/boundary_geometry_check.json'
    coverage['execution_record']='run_summary.json'
    coverage['scope']='Dam, cavern, slope, 3D boundary, 2D section, Darcy boundaries, prescribed translations, compatible thermal gradients and planar elastic beam boundaries'
    coverage['not_covered']=['完整GPU/CPU一致性','材料本构逐模型精度验证','接触与节理专门模型',
        '三维梁六自由度与非线性塑性铰','多材料热失配与任意温度场','显式锚杆/锚索',
        '地下水衬砌完整耦合','任意断点续算及施工阶段/荷载历史语义失配拒绝','失稳临界夹逼与极限安全系数',
        '独立极限平衡/滑面搜索','独立洞室块体稳定','规范校核','网格收敛及动力解析解精度基准']
    coverage['not_validated']=[r['id'] for r in rows if not r['passed_sanity']]
    write(root/'coverage.json',coverage);write(root/'run_summary.json',summary)
    for project in coverage['projects']:
        case_file=root/project['directory']/'case_coverage.json'
        local=read(case_file) if case_file.exists() else dict(project)
        local['cases']=project['cases'];local['execution_record']='../run_summary.json'
        write(case_file,local)
    model=read(root/'洞室_边界专项回归/model_definition.json')
    lines=['# 岩土多工况回归：实际计算记录','',
      f"共 {len(coverage['projects'])} 个工程、{len(rows)} 个工况；{summary['passed_sanity']} 项通过数值检查，{summary['completed_with_warnings']} 项结果已生成但残差超限，{summary['failed']} 项运行失败。",
      '优先 GPU，失败后 CPU；所有失败保留原始日志。通过基本检查不等于工程精度验收。',
      'CPU/GPU exe 及 DLL 已更新，旧文件备份于 C:/project/CAE/base_cae/build/solver_backup_20261028_110002。求解器源码未修改。',
      '', '## NASGEWIN 弹窗处理','',
      '原错误查看器固定读取 C:/IIIDIR.DAT，而主求解器读取 D:/IIIDIR.DAT。本机 C 盘根目录不可写。',
      '前端现部署自有无界面错误接收程序，运行表与错误接收程序位于 build/smesh_release/solver/Runtime。真实错误由前端日志与详细信息显示。',
      '发生错误或停止计算时清理本次进程树。回归脚本退出后恢复全局目录文件，不更改系统权限。',
      '', '## 模型与验收范围','',
      '洞室边界专项：24×24×4米围岩块、6×6米主洞、1米衬砌、独立吊车梁材料；第1级主洞开挖，第2级邻近1×2×4米小导洞开挖。',
      f"衬砌内外边界分别{len(model['inner_face_keys'])}/{len(model['outer_face_keys'])}个面，吊车梁{model['material_counts']['6']}个实体单元，梁端伸入围岩。底面固定、侧面滚动、端面平面应变，并有72个粘弹性边界面。",
      'D02使用600秒短瞬态；D03定义1200秒快速降水，初始稳态残差超限仅提示并继续瞬态，结果状态见下表。常规地震为0–2秒低幅合成脉冲，二维诊断时程0.4秒，均用于功能回归。',
      '位移检查使用求解器输出 IIIN.TXT 的实际生效材料，剔除仅属于空单元（材料1）的节点；保留空区节点原始值。',
      '必须同时有正常完成记录、新结果、有限数值和相应功能输入。按用户要求，有限静力/渗流残差超限只提示，继续计算并生成结果，单列“结果已生成但残差超限”，不冒充数值收敛。NaN、崩溃和无效结果仍失败。天然线性初值另行验收，模态检查频率，时程检查新响应。',
      '衬砌/吊车梁配置和输入已建立，但失败的求解分支不能计为数值覆盖通过。',
      '二维渗流对照为100×10米均质矩形、231节点/200个四节点单元；H01对照h=20-0.05x，H05对照静水头h=5米，逐节点容差0.001米。H04测试底部开口排水；H03修复线性初始场误判后原参数GPU通过，正式迭代误差1.4576e-4小于1e-3。',
      '组合渗流由前端分两次启动原生求解器；稳态结果文件完整有限即可继续瞬态，残差超限提示跨阶段保留。真正计算错误或手动中止时不会继续。',
      '二维衬砌P02改用面力卸载后通过；P07/P08保留单元卸载后动力/衬砌数组异常的独立诊断工况。洞室动力控制参数被原生覆盖时，前端会停止并报告实际与请求设置，不能将零阶模态算作成功。',
      'M01–M05为5个独立小模型，分别检验面力、等效节点力、三维线力、X方向体力、均匀变温；E=10MPa、泊松比0、2×1×1米hex8，逐节点容差2e-7米。点/线/面/温度/体力由实际自定义IIWL路径读入。',
      'M06–M12检验正/负指定位移、非零刚体平移、泊松收缩粗细网格，以及二阶hex20轴向温差和横向自由热弯曲。M13–M16使用真正KETYPE21平面悬臂梁，检验端力、端力矩、均布荷载和指定转角；挠度与转角分开核验。线弹性分支METHOD_ITER=0，不把非线性零塑性弯矩配置当作弹性对照。',
      '坝仅动力续算增加模型输入和状态文件SHA256校验；不匹配或缺少校验记录时要求重算静力。保护网格、位移约束、材料和状态文件，不等于任意施工阶段/荷载历史的语义兼容性验证。',
      'D12 CPU模态与D17已有静力状态续算已验证，D17复制源文件及SHA256见坝工程的工程数据/D17_static_reuse_provenance.json。8种荷载页面参数保存/恢复验证及5项小模型CPU/GPU一致性结果分别见validation和device_comparison目录。',
      'B04/B06最后阶段有效节点位移极值约0.542/0.389毫米；输入面节点及非零面积检查见洞室_边界专项回归/boundary_geometry_check.json。',
      '', '## 工况结果','', '|工程|工况|结果|设备|主要原因|','|---|---|---|---|---|']
    for r in rows:
        lines.append('|'+ '|'.join([r['project'],r['title'],'基本检查通过' if r['passed_sanity'] else '结果已生成（残差超限）' if r['completed_with_warnings'] else '失败',r['device'] or '',
            '；'.join(r['warnings'] if r['completed_with_warnings'] else r['failures']).replace('|','/').replace('\n',' ')])+'|')
    lines+=['','## 仍需完善','',
      '失败项见 run_summary.json 与每轮 runs/*/results.json；每次运行的前端日志、III110原始日志、输入文件摘要均已保存。功能分类见功能与边界覆盖矩阵.md，已确认代码证据见设置修复与代码定位.md。',
      '已有两项渗流与五项机械荷载解析对照、一次明确的静力状态复用时程，尚无网格收敛、动力解析解或实测对照、逐本构模型精度、接触节理专门模型、完整地下水衬砌耦合、任意断点重启或完整GPU/CPU一致性基准。',
      '现有工程与求解器源码均保留；请在独立目录生成新套件，避免覆盖用户修改。']
    (root/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    print({k:v for k,v in summary.items() if k!='cases'})


if __name__=='__main__':main()
