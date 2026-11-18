"""Account for every case and every known boundary gap, separating evidence levels."""
import argparse
from collections import Counter
import hashlib
import re
from pathlib import Path
from build_geotechnical_regression import read,write


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args();root=a.suite
    summary=read(root/'run_summary.json');matrix=read(root/'boundary_feature_matrix.json')
    classification={
        'D01':('非线性未收敛','初始稳态误差超过容差，原生以误差停滞作为完成条件；有限超限不阻断输出，仍标记未收敛。'),
        'D03':('前置初始场未收敛','初始稳态残差超限允许继续快速降水瞬态；是否实际完成以最新运行记录为准，不把初始残差归因于降水曲线。'),
        'D10':('非线性未收敛','正式自由面稳态迭代未达到容差；单次线性初始场另行验收，不能将其初值变化与正式非线性残差混同。'),
        'H03':('渗流阶段验收待核对','单次线性SEEPFREE初始场不能用相对零初值变化判断非线性收敛；必须核对IKEY、完整有限初始水头和正式稳态迭代。'),
        'D11':('非线性振荡/计算超时','600秒GPU和CPU均未收敛，残差约0.0285/0.0287交替；11个排水节点是y=108.5米水平线，H04小模型排水通过。合法输入与数值不收敛并存，不能仅凭超时断定源码错误。'),
        'D12':('本次模态重算超时，保留历史通过','历史两次CPU运行得到5阶有效频率，原始报告保留；本次420秒止于填筑，900秒完成静力后止于模态BANRED约2380/5668。最新完整复算不计通过，超时不能唯一归为设置或源码错误。GPU仍有EOF/访问异常历史。'),
        'D15':('性能/原生执行问题','旧目录300/600秒超时；已归档旧结果并在全新目录重算。若仍未通过，保留最后阶段与日志，不将超时当作代码缺陷的充分证据。'),
        'D16':('三维粘弹性法向缺陷','有效面集已补齐，原生GEDYNA_NORM仍使用未初始化KETYPE/NDIM。'),
        'S05':('三维粘弹性法向缺陷','193个八节点边界面已实际导出，原生法向转换分支失败。'),
        'C08':('三维粘弹性法向缺陷','原生GEDYNA_NORM；还需修复洞室动力控制参数覆盖，不能以更换卸载法宣称该边界通过。'),
        'B09':('三维粘弹性法向缺陷','72个实际边界面几何检查有效；失败仍在原生三维法向。'),
        'B02':('三维衬砌法向缺陷','FREEFTVECT读入4个节点但没有填充JMC，再调用INDEP/TRAN3D。'),
        'B05':('三维衬砌法向缺陷','联合衬砌和吊车梁在衬砌分支先失败，不能据此判定梁设置错误或联合功能通过。'),
        'B03':('首级安装阶段命名缺陷','合法第1级安装被原生转换为IEXCA_BCDCL=0，EXNAME2拒绝；不能改成第2级冒充验证首级。'),
        'P03':('二维应力分量不匹配','前序NCOM=4，而DCLIKEY重置LXM=2导致分配MACOM=3；READRR按四分量写入三分量数组。CDB链确认READRR→BACKRR→IINPUT后堆损坏。'),
        'C06':('洞室模态参数被覆盖','CAVEIKEY将NMODES置0；大模型此前在零阶请求下仍耗时进行带宽约化。前端现检查生成控制。'),
        'B07':('洞室模态参数被覆盖','小模型证明正常结束仍可能IEIGEN阶数0；前端现拒绝。通用入口试验改为1阶后又在GEBD_STATLOAD访问异常，未部署未验证的替代流程。'),
        'P06':('洞室模态参数被覆盖','面力卸载避免未分配JMFS后，仍因原生NMODES=0失败。'),
        'C07':('洞室时程参数被覆盖','原生将DT=.0002、TT=100覆盖短时程；前端现识别并停止。'),
        'B08':('洞室时程参数被覆盖','通用入口诊断能在.02秒步长/.4秒时段算完，但正式洞室入口仍覆盖参数，未计为通过。'),
        'P04':('洞室时程参数被覆盖','面力卸载后仍被改为50万步；早期超时不能解释为二维模型太大。'),
        'P05':('洞室时程参数被覆盖','二维粘弹性几何与三维法向缺陷不同，但正式入口先受到时间参数覆盖阻挡，不能宣称二维吸收边界已验收。'),
        'P07':('单元卸载后的未分配数组','保留原始单元卸载分支；DYNAIIWL保留非零NFS但未分配JMFS/FSLOAD，WRIT_IIWL空指针读取。'),
        'P08':('单元卸载后的未分配数组','保留原始单元卸载衬砌分支；CHENIIWL→WRIT_IIWL空指针读取。面力卸载P02通过不能替代此项。'),
    }
    special_pass={
        'H03':'复测定位并修复前端误判：SEEPFREE为ITER=0的单次线性初始求解，独立Q4方程组归一化残差约2.9e-8；原设置下正式SEEPCASE误差1.4576e-4小于1e-3，GPU通过。初始水头仍检查完整、有限和时效，未放宽正式迭代容差。',
        'P02':'二维衬砌内/外边界在面力卸载后通过，原单元法由P08保留。',
        'S02':'设置迭代上限由20改100，五个强度系数各自达到1e-4；实际需要16/25/37/50/77次。原失败含设置不足及前端假完成判定。',
        'S06':'迭代上限20不足；提高后初始静力平衡达到1e-4，再计算动力对照。',
        'D12':classification['D12'][1],
        'D17':'复用此前D12已验收并保存在D17的原始静力状态；16个文件匹配当时SHA256来源记录，输入亦匹配历史通过报告，再建立续算校验记录。新时程CPU通过，GPU异常保留；未使用本次D12未完成输出。',
        'D15':'在全新目录完成静动力联合计算；旧超时及GPU失败记录保留，不能删去历史后宣称原先从未失败。',
    }
    rows=[]
    for case in summary['cases']:
        identifier=case['id'];passed=case['passed_sanity']
        if passed:category='已通过基本/对应解析检查';conclusion=special_pass.get(identifier,'真实前端导出与原生计算通过当前验收项；不扩大到尚未测试的材料、精度或边界。')
        else:
            if identifier not in classification:raise RuntimeError('Unclassified failed case: '+identifier)
            category,conclusion=classification[identifier]
        if case.get('completed_with_warnings'):
            category='结果已生成（残差超限）'
            conclusion='按用户要求，有限残差超限仅提示，已继续计算并生成结果；保留未收敛标记，不计为数值收敛通过。'+ '；'.join(case.get('warnings',[]))
        if identifier=='D12' and case.get('report'):
            run=read(Path(case['report']));record=next(c for c in run['cases'] if c['id']=='D12')
            attempts=[v for v in record.get('attempts',[]) if v['phase']=='analysis']
            if attempts:
                last=attempts[-1]
                if passed:
                    eigen=next((a for a in case['verified_feature_outputs'] if a.get('mode_count')), {})
                    conclusion=f"最新完整复测以{last['device']}在{last['elapsed_seconds']}秒完成，生成{eigen.get('mode_count','有效')}阶新模态。此前GPU早期异常、CPU 900秒超时和历史CPU通过记录均保留；本次通过不等于已修复原生随机访问异常。"
                else:
                    category='模态复算未完成'
                    text=Path(last['log']).read_text('utf8',errors='replace');progress=re.findall(r'BANRED K=\s*(\d+)\s+N2_TOT=\s*(\d+)',text)
                    where=f"最后带宽约化进度{progress[-1][0]}/{progress[-1][1]}；" if progress else ''
                    conclusion=f"最新{last['device']}尝试耗时{last['elapsed_seconds']}秒，未生成符合验收的新模态；{where}历史通过模态及先前超时均保留，不能用旧结果充当本次完成。"
        rows.append({'id':identifier,'project':case['project'],'title':case['title'],'passed':passed,
            'generated':case.get('generated',passed),'completed_with_warnings':case.get('completed_with_warnings',False),
            'category':category,'conclusion':conclusion,'device':case['device'],'failure_messages':case['failures'],'run_report':case['report']})
    gaps=[r for r in matrix if r['status']=='未覆盖/未支持']
    evidence=read(root/'solver_readonly_findings.json')
    payload={'analysis_complete':True,'case_count':len(rows),'cases':rows,'boundary_gaps':gaps,
        'evidence':evidence,'scope':'Every current case and listed boundary category is accounted for; analysis completion does not mean all solver defects are fixed or every boundary has passed.',
        'unresolved_root_causes':['非零流量自由面及原大模型排水的非线性不收敛不能唯一归为设置或源码错误',
            'GPU EOF/访问异常已复现，相关未初始化变量已查到，但每种随机异常的精确内存原因未全部定位'],
        'source_policy':'NASGEWIN64 remained read-only; no native source or native binary patch was made.'}
    write(root/'analysis_findings.json',payload)
    counts=Counter(c['category'] for c in rows if not c['passed'])
    doc=['# 全工况分析结论','',
        f"当前 {len(rows)} 个工况：{summary['passed_sanity']} 项通过数值验收，{summary.get('completed_with_warnings',0)} 项结果已生成但残差超限，{summary['failed']} 项运行失败。",'',
        '## 设置问题、前端问题和求解器问题','',
        '三类都存在，不能把所有失败归给模型设置，也不能把所有超时归给求解器代码。',
        '- 设置不足已修复：结冰与浸润冲突；渗流耦合初始场和水位不匹配；S03所需前序文件；S02/S06的20次静力迭代不足。',
        '- 前端问题已修复：洞室状态目录、网格头维数、坝材料集清理顺序、组合渗流续算与验收、通用IIWL段落、8种页面持久化、温差/膨胀系数、面边合并、三维线力等效积分和未收敛状态误报完成。',
        '- 原生实现缺陷有源码/断点证据：洞室模态与时程参数写死、单元卸载后未分配数组、三维衬砌/粘弹性法向变量未初始化、首级吊车梁阶段0、二维梁应力分量越界、MAKEFL错传NFT。',
        '- 数值不收敛单列：非零流量自由面及原大模型排水，不能通过提高容差、删除失败步或只看最后成功字样认定通过。',
        '- H03的天然线性初始场只求解一次，相对初值变化不是非线性收敛残差；独立方程验证后修复阶段识别，H03原参数已通过。按最新要求，D01/D03/D10/D11有限非线性残差超限不阻断，实际生成情况见逐项记录，未收敛不计为数值通过。',
        '- GPU执行异常单列：CPU回退得到有效结果能验证该工况，但不能证明GPU分支已修复。','',
        '## 逐项结论','', '|编号|当前结果|分类|结论|','|---|---|---|---|']
    for r in rows:doc.append('|'+ '|'.join([r['id'],'通过（'+str(r['device'])+'）' if r['passed'] else '已生成（残差超限）' if r['completed_with_warnings'] else '未通过',r['category'],r['conclusion']])+'|')
    doc+=['','## 边界是否全部考虑','',
        '全部已列入分析清单，但没有全部验证通过。详见功能与边界覆盖矩阵.md。通过项、已配置失败项和缺少完整验证模型的项目分开统计。',
        '新增已验证：正/负非零指定位移、刚体平移、泊松收缩及粗细网格、轴向/横向兼容温度梯度、真正平面梁端力/力矩/均布荷载/指定转角。非零约束仅限自定义荷载静力入口；梁仅限二维线弹性。',
        '尚缺：三维梁六自由度、非线性塑性铰；一般多材料热失配；接触/节理开闭滑移；显式锚杆锚索及预应力安装；完整地下水—衬砌内外水压耦合；所有材料本构、任意断点重启、施工历史语义兼容和一般网格收敛/动力精度。',
        '具体限制：EXCAIIWL在CAE07:6572–6573以NPAIR=0/GOTO555跳过显式预应力杆荷载；地下水入口默认洞内无水，不能代替洞内外多水位组合。仅动力续算核对IIIN/IIID/IMAT和状态文件哈希，尚不替代施工阶段与荷载历史兼容性审查。',
        '接触/节理的KETYPE7/8/9可见于原生表和导入路径，但当前材料/几何创建及开闭、滑移对照没有完成专用验证，不能仅凭可导入单元认定支持完整接触分析。','',
        'S02只验证1.0–1.4五档稳定计算，未夹逼失稳临界点，末档1.4不是极限安全系数。独立极限平衡/滑面搜索、洞室块体稳定与规范校核入口尚缺专项模型，不能用实体FEM工况替代。','',
        '## 验证证据与限制','',
        'M01–M05：独立面力、点力、线力、任意方向体力、均匀变温的逐节点解析对照通过；5项CPU/GPU输入SHA256相同，位移差值0。见device_comparison/comparison.json。8种实际页面保存/恢复通过，见validation/load_page_roundtrip/result.json。',
        'M06–M12：给定位移、刚体平移、泊松收缩和二阶hex20温差梯度解析对照；M13–M16：2米KETYPE21悬臂梁，EI=2e7 N·m²，挠度和Rz转角分别验收。新增设备对照见device_comparison_20261029；9类页面在实体/平面梁两种项目加载保存通过，见validation/*page_roundtrip_20261029/result.json。',
        '梁对照同时修复了前端编号21被改为1、梁被计入INOUSE杆连接表、纯线网格空间维度被设成1，以及力矩被控件截为99.99的问题。线弹性METHOD_ITER=0现可选择；不折减时仍输出一个单位荷载步，避免原NSTEP=0。旧非线性和错误参数配置的失败日志保留。',
        'H01/H05仅为简单Darcy/静水解析对照。C05等效加固不等于显式锚杆，B04/B06实体吊车梁不等于梁单元。',
        'D17使用此前验收且16个原始文件哈希不变的静力状态，重新计算地震响应；此前D12的900秒超时单独保留，最新D12复测结论见上表，不使用未完成输出冒充状态。模型/文件失配与缺证书拒绝见validation/boundary_rejections_20261029。此验证不代表任意施工历史或中断点都可安全重启。',
        'CDB诊断不计通过率；匹配原始COFF对象机器码只用于确认函数，未虚构PDB行号。符号证据和断点链见native_symbols与native_debugger_findings.json。',
        '仅在复制的输入中试验通用入口：短时程可按请求时间算完；模态修改阶数后仍在GEBD_STATLOAD异常。因此没有将该试验流程直接部署为洞室动力替代方案。',
        'NASGEWIN64源码始终只读。已部署exe沿用用户提供Release二进制；前端错误接收桥不修改求解器本体。所有正常回归按GPU优先、失败后CPU，诊断和设备对照分别标记。',
        '', '## 仍需原生修复与后续验收','',
        '优先修复：CAVEIKEY参数透传；DYNAIIWL/CHENIIWL的NFS/数组生命周期；三维两个法向函数；首级吊车梁阶段0和二维DCLIKEY应力维数；IINPUT零初应力变量；MAKEFL计数。源码证据逐项见设置修复与代码定位.md。',
        '修复后复跑原失败工况，保留面力与单元卸载、二维与三维、固定与粘弹性边界的成对对照。数值不收敛类需要单独检查自由面/干湿转换、阻尼/松弛及离散精度；目前没有证据支持把所有情况简单归为用户设置错误。']
    (root/'全工况分析结论.md').write_text('\n'.join(doc)+'\n',encoding='utf8')
    progress=read(root/'analysis_progress.json');progress.update(status='analysis_complete',pending=[],
        final={'projects':len(read(root/'coverage.json')['projects']),'cases':len(rows),'passed':summary['passed_sanity'],'completed_with_warnings':summary.get('completed_with_warnings',0),'failed':summary['failed']},
        classification_counts=dict(counts),remaining_solver_or_numerical_failures=[r['id'] for r in rows if not r['passed'] and not r['completed_with_warnings']],
        qualification=payload['scope'])
    write(root/'analysis_progress.json',progress)
    print({'accounted_for':len(rows),'failed_categories':dict(counts)})

if __name__=='__main__':main()
