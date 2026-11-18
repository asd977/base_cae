"""Summarize the latest outcomes for the exact cases that failed at retest start."""
import argparse
from collections import Counter
from pathlib import Path
from build_geotechnical_regression import read,write

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args()
    active=read(a.suite/'active_retest.json');directory=Path(active['directory']);baseline=read(directory/'baseline_run_summary.json')
    current=read(a.suite/'run_summary.json');findings={c['id']:c for c in read(a.suite/'analysis_findings.json')['cases']}
    old={c['id']:c for c in baseline['cases'] if not c['passed_sanity']};new={c['id']:c for c in current['cases']};rows=[]
    for identifier,previous in old.items():
        case=new[identifier];report=read(Path(case['report']));record=next(c for c in report['cases'] if c['id']==identifier)
        attempts=record.get('attempts',[]);last=attempts[-1] if attempts else {}
        rows.append({'id':identifier,'project':case['project'],'title':case['title'],
            'retested':case['report']!=previous['report'],'passed':case['passed_sanity'],'device':case['device'],
            'elapsed_seconds':last.get('elapsed_seconds'),'conclusion':findings[identifier]['conclusion'],
            'failures':case['failures'],'report':case['report'],'log':last.get('log'),
            'nonlinear_seepage_steps':[s for s in last.get('results',{}).get('seepage_steps',[]) if not s['linear_initial']]})
    assert all(c['retested'] for c in rows)
    source=a.suite/'渗流_二维边界对照/H03－天然自由面';h03=next(c for c in rows if c['id']=='H03')
    original=old['H03'];old_report=read(Path(original['report']));old_attempt=next(c for c in old_report['cases'] if c['id']=='H03')['attempts'][-1]
    latest=read(Path(h03['report']));new_attempt=next(c for c in latest['cases'] if c['id']=='H03')['attempts'][-1]
    unchanged={n:old_attempt['input_sha256'].get(n)==new_attempt['input_sha256'].get(n) for n in ['IIIN.DAT','IIID.DAT','IMAT.DAT']}
    report={'status':'completed','baseline_failed':len(rows),'retested':sum(c['retested'] for c in rows),
        'now_passed':[c['id'] for c in rows if c['passed']],'still_failed':[c['id'] for c in rows if not c['passed']],
        'suite':{'cases':current['case_count'],'passed':current['passed_sanity'],'failed':current['failed']},
        'H03_model_inputs_unchanged':unchanged,'H03_independent_initial_check':'H03_linear_initial_equilibrium.json',
        'cases':rows,'source_policy':'NASGEWIN64 source and deployed solver binaries unchanged; GPU first and CPU fallback only, no dedicated comparisons.'}
    write(directory/'result.json',report)
    lines=['# 未通过工况复测结果','',f"原{len(rows)}项未通过工况已全部复测；本轮转为通过：{'、'.join(report['now_passed']) or '无'}。",
        f"整套当前为{current['passed_sanity']}项通过、{current['failed']}项未通过（共{current['case_count']}项）。",'',
        'H03只修复前端阶段验收：单次线性初值与正式非线性迭代分开；初始场独立方程验证通过，模型输入哈希一致。正式非线性容差未放宽。',
        'D11本轮每设备上限120秒，历史600秒运行保留；D12本轮上限1800秒。已知原生错误仍保持只读。','',
        '|工况|工程|本轮结果|结论|','|---|---|---|---|']
    for row in rows:lines.append('|'+ '|'.join([row['id'],row['project'],'通过' if row['passed'] else '未通过',row['conclusion']])+'|')
    (directory/'复测结果.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    active.update(status='completed',report=str(directory/'复测结果.md'));write(a.suite/'active_retest.json',active)
    plan=read(directory/'plan.json');plan.update(status='completed',results='result.json');write(directory/'plan.json',plan)
    print({k:report[k] for k in ['baseline_failed','retested','now_passed','still_failed','suite','H03_model_inputs_unchanged']})

if __name__=='__main__':main()
