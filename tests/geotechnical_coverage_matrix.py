"""Generate an explicit boundary/feature matrix from actual inputs and runs."""
import argparse
import hashlib
from pathlib import Path
from collections import Counter
from build_geotechnical_regression import read,write

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('suite',type=Path);a=ap.parse_args();root=a.suite
    summary=read(root/'run_summary.json');cases={c['id']:c for c in summary['cases']}
    definitions=[
      ('零位移固定/法向滚动约束',['D06','C04','S01','B01','P01'],'基本位移及约束方向；不同模型分别核验'),
      ('端面约束',['S01','B01'],'边坡前端面固定/后端面法向约束；专项工程端面平面应变约束'),
      ('自重体力',['D06','C04','S01','P01'],'重力方向对应各模型竖轴，来自原生求解入口'),
      ('上/下游水头',['D01','D02','D03','H01','H02'],'含非稳态水位变化；有限残差超限保留未收敛标记并继续，初始水头必须完整、有限、新生成'),
      ('定通量边界',['D05'],'非零通量，不能只看复选框'),
      ('天然渗流初始面',['D10','H03','H05'],'H03/H05通过；H03曾误将单次线性初始解的初值变化当成未收敛，现按IKEY区分阶段并验证初始水头完整有限。D10正式非线性残差仍超限'),
      ('排水井区域',['D11','H04'],'H04底部开口排水节点通过；D11的11节点位于y=108.5米的水平排水线，残差交替振荡；最新完成100次迭代并生成结果，仍标记未收敛，历史超时保留'),
      ('稳态自动转瞬态',['D04','H02','D03'],'前端检查稳态结果完整性，以独立原生进程计算瞬态；残差超限警告跨阶段保留'),
      ('库水面力/填筑蓄水',['D07','D08'],'检查实际 FA_DAMUP、分层与水位过程'),
      ('骤降边界过程',['D09','D03'],'结构骤降与瞬态渗流分别测试'),
      ('结冰区域',['D13'],'已排除浸润/骤降/蓄水期流变冲突'),
      ('渗流压力到坝体耦合',['D14'],'关联D02已收敛的定水头稳态初始场，施工目标水位125.5米与其实际上游水头一致'),
      ('衬砌内界面',['B02','B05','P02','P08'],'三维28面；二维14边；P02面力卸载通过，P08保留单元卸载后未分配数组异常'),
      ('衬砌外界面',['B02','B05','P02','P08'],'三维44面；二维22边；三维法向及单元卸载路径仍有独立缺陷'),
      ('吊车梁第1级安装',['B03'],'单独保留原生阶段0错误，禁止伪造安装成功'),
      ('吊车梁第2级安装',['B04','P03'],'独立材料；梁端伸入围岩'),
      ('吊车梁荷载释放',['B04','B06'],'1.0/0.5对照，检查最终阶段实际结果变化'),
      ('围岩等效加固',['C05'],'非零ALFAIE/ALFAJD，概念加固；不是显式锚杆单元验证'),
      ('模态计算及初始状态',['C06','S03','B07','P06','P07','D12'],'D12本轮GPU完整5阶通过，频率与每阶24891节点振型均完整、新生成且有限；此前CPU900秒超时与早期GPU异常保留。洞室入口写死NMODES=0，前端识别并停止'),
      ('固定边界地震时程',['S04','S06','C07','B08','P04','D15','D17'],'非零合成脉冲；洞室入口覆盖DT/TT会被前端拒绝；D17真实静力状态复用后CPU通过'),
      ('静力结果复用/仅动力续算',['D17'],'静力及频率来自已通过D12；输入IIIN/IIID/IMAT及状态文件SHA256证书；模型/状态失配与缺证书拒绝另见validation/boundary_rejections_20261029；不覆盖任意施工历史语义兼容'),
      ('强度折减静力收敛',['S02','S06'],'原20次迭代不足；改100次，S02的1.0–1.4五个系数实际迭代16/25/37/50/77次达到1e-4；S06亦要求初始静力平衡'),
      ('极限安全系数/失稳临界夹逼',[],'S02只验证1.0–1.4扫描各档收敛，没有稳定/失稳两侧夹逼，不把末档1.4当作极限安全系数'),
      ('边坡极限平衡/滑面搜索',[],'这是独立分析入口；尚缺明确滑面、分条参数、搜索域及与参考解对照的专项工程，不包含在FEM强度扫描中'),
      ('洞室块体稳定/结构面组合',[],'这是独立分析入口；尚缺结构面几何、贯通组合与已知安全系数的专项模型；实体开挖通过不代表块体分析通过'),
      ('规范校核',[],'规范校核属于独立流程，尚无规范条文、控制组合及对照计算书的验收模型'),
      ('粘弹性边界',['S05','C08','B09','P05','D16'],'须有效几何面集；三维法向程序另有未赋值变量'),
      ('非零指定位移',['M06','M07','M08'],'正/负轴向给定位移、非零刚体平移，独立逐节点解析解；同自由度冲突及不支持的自动流程拒绝；当前只开放自定义荷载静力入口'),
      ('节点力矩/转角约束',['M14','M16'],'二维KETYPE21，3自由度含Rz；端力矩和指定转角分别解析核验；实体节点力矩/二维实体转角拒绝，三维梁六自由度尚未支持'),
      ('独立点力/线力/面压',['M01','M02','M03'],'真实自定义IIWL入口，常量面力、节点力及三维线力各自独立对照；线力积分为等效节点荷载避开原生MAKEFL错传NFT'),
      ('温度荷载',['M05'],'温差写入IIWL、膨胀系数写入当前分析IMAT第57项；同材料冲突系数拒绝；均匀变温解析位移对照通过'),
      ('非均匀温差/热弯曲',['M11','M12'],'81节点8个hex20，线性温度梯度与兼容二次位移场；轴向梯度及横向自由热弯曲；多材料热失配和一般温度场仍未验证'),
      ('梁分布荷载',['M15'],'二维KETYPE21线弹性悬臂梁；原生均布P正值作用于局部负横向；E/A/Iz、端部固定转角，挠度/转角独立解析解。其余分布类型及非线性梁未验证'),
      ('梁端集中力',['M13'],'2米悬臂梁，EI=2e7 N·m²，端力1000N；端挠度0.133333mm，转角0.1mrad；保留梁编号并排除杆连接表'),
      ('泊松收缩与网格细化',['M09','M10'],'ν=0.25粗/细网格均逐节点匹配恒应变场；只验证线性补片，不代替一般网格收敛研究'),
      ('任意体力',['M04'],'X方向单位加速度的体力独立对照，区别于自动自重'),
      ('接触/节理边界',[],'尚缺专门几何、材料和参数对照'),
      ('地下水—衬砌耦合',[],'尚缺匹配地下水场与内外水头的完整验收模型'),
      ('显式锚杆/锚索',[],'尚缺含杆单元及安装级次的独立验证'),
      ('荷载页面保存/恢复',[],'9种页面在三维实体与二维梁分别保存恢复通过，含非零位移、指定转角、1000N·m力矩及负大数值梁荷载；见validation/*page_roundtrip_20261029/result.json；与数值验证分别统计'),
      ('小模型GPU/CPU一致性',[f'M{i:02d}' for i in range(1,17)],'原5项见device_comparison；新增11项见device_comparison_20261029。核对前端输入SHA256、逐节点位移及梁转角，单位分别为米/弧度；不代表完整GPU/CPU一致性'),
      ('均质饱和渗流解析解',['H01'],'100×10米矩形，231节点；h=20-0.05x，逐节点容差0.001米'),
      ('天然自由面静水解析解',['H05'],'静水头h=5米，初始自由面y=5米，逐节点容差0.001米；不能代替非零流量自由面验证'),
      ('机械荷载解析解',['M01','M02','M03','M04','M05'],'2×1×1米单个hex8，E=10MPa、ν=0、密度1000kg/m³；逐节点绝对容差2e-7米；不是网格收敛研究'),
      ('网格收敛/动力解析解对照',[],'尚无网格收敛、动力解析解或实测基准，不能据此宣称工程精度已全面验收')]
    rows=[]
    for feature,ids,note in definitions:
        good=[id for id in ids if cases.get(id,{}).get('passed_sanity')]
        warned=[id for id in ids if cases.get(id,{}).get('completed_with_warnings')]
        failed=[id for id in ids if id in cases and not cases[id]['passed_sanity'] and id not in warned]
        state='未覆盖/未支持' if not ids else '部分通过' if good and (failed or warned) else '基本检查通过' if good else '已生成，残差超限' if warned and not failed else '已配置，尚未通过'
        if feature=='荷载页面保存/恢复':state='9种页面持久化通过'
        rows.append({'feature':feature,'cases':ids,'passed':good,'completed_with_warnings':warned,'not_passed':failed,'status':state,'limits':note})
    write(root/'boundary_feature_matrix.json',rows)
    lines=['# 功能与边界覆盖矩阵','',
        '本表区分已配置、已导出、基本检查通过、尚未覆盖。所有工程共用约束不代表所有边界类型已覆盖；未覆盖项不计入通过率。',
        '', '|功能/边界|通过工况|已生成但残差超限|运行失败工况|状态及限制|','|---|---|---|---|---|']
    for r in rows:lines.append('|'+ '|'.join([r['feature'],'、'.join(r['passed']) or '—','、'.join(r['completed_with_warnings']) or '—','、'.join(r['not_passed']) or '—',r['status']+'；'+r['limits']])+'|')
    (root/'功能与边界覆盖矩阵.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    records=[]
    native=Path('C:/D_Drive/NASGEWIN64')
    for file,section,description in [
        ('B_VICSUB/CAE09.f90','SUBROUTINE GEDYNA_NORM','函数内使用KETYPE及NDIM，但该函数没有给这两个局部变量赋值；三维分支依赖它们。建议从面所属单元取得KETYPE，并按MADIM遍历坐标。'),
        ('PCCAE_CPU/PCCAE_CPU.F90','SUBROUTINE CPU_AUTOPACK','释放XY/JM/MAT之后，在材料集缺失分支将这些数组传给GEMA_ROCK/GEMA_DAMUP。前端现在预先输出真实材料集，求解器仍需修复该后备分支。'),
        ('B_VICSUB/CAE09.f90','SUBROUTINE IINPUT()','IFIN_S=0零初应力分支没有给局部NELEMT1赋值，却在4061行传给BACKRR并用作4065行循环上限。GPU间歇出现DISK00/IIINIT.DAT读到文件尾；这是相关的未初始化变量证据，需在零初应力分支明确设置单元数并校验文件完整性。'),
        ('B_VICSUB/CAE08.f90','SUBROUTINE FREEFTVECT','三维分支2712–2721行读出NNA/NNB/NNC/NND后，没有写入JMC，就在2724行调用INDEP并求法向。P02面力卸载后的二维衬砌已通过；P08保留不同的单元卸载数组异常。'),
        ('B_VICSUB/CAE08.f90','SUBROUTINE CAVEIKEY','2244–2247行将DT=.0002、TSTA=0、TT=100、NMODES=0写死，覆盖用户洞室动力设置。前端检测生成控制与设置不符后停止，避免零阶模态假完成和50万步误算。'),
        ('B_VICSUB/CAE07.f90','SUBROUTINE DYNAIIWL','未将前次单元卸载留下的NFS归零，也未分配JMFS/FSLOAD，就调用WRIT_IIWL；CDB捕获WRIT_IIWL空指针读取，P07等原始分支保留。'),
        ('B_VICSUB/CAE08.f90','SUBROUTINE CHENIIWL','单元卸载后衬砌同样留下非零NFS而未分配内应力荷载数组；P08捕获CHENIIWL→WRIT_IIWL异常。面力卸载NFS=0的P02已通过，不能据此宣称单元法已修复。'),
        ('PCCAE_CPU/PCCAE_CPU.F90','SUBROUTINE CPU_AUTOCAVE','第一级吊车梁安装编码-1，5413行IEXCA_BCDCL=INSTALL_DCL+1成为0，5694行仍调用EXNAME2(0)，CAE05:6684拒绝非负阶段。'),
        ('B_VICSUB/CAE08.f90','SUBROUTINE DCLIKEY','二维梁安装IKEY保留LXM=2/MACOM=3，前一开挖应力文件为LXM=4/NCOM=4；READRR按文件四分量写入三分量RR。CDB链为READRR→BACKRR→IINPUT，随后堆损坏。'),
        ('B_VICSUB/CAE09.f90','SUBROUTINE MAKEFL','2037/2044行将没有初始化的局部NFT传给FL3D8N，实参应来自NFL。独立线荷载测试原先结果全零；前端积分为等效节点力后通过解析解及CPU/GPU对照。'),
        ('B_VICSUB/CAE13.f90','SUBROUTINE PROB21','6380–6381行按相邻误差差值<1e-5设置IFHG=1，不要求误差已达到tol；D01/D03/D10正式非线性迭代仍不通过。H03的单次线性初始场不应用这一非线性判据，其正式迭代已达到容差。该终止条件不等于已经确定了大模型不收敛的全部原因。')]:
        path=native/file;raw=path.read_bytes();text=raw.decode('gbk',errors='replace').splitlines()
        location=next((i+1 for i,line in enumerate(text) if section.upper() in line.upper()),None)
        records.append({'file':str(path),'sha256':hashlib.sha256(raw).hexdigest(),'section':section,'line':location,'finding':description,'modified':False})
    write(root/'solver_readonly_findings.json',records)
    doc=['# 设置修复与只读代码定位','',
      '前端已修复：洞室DIRDYNA固定CASE01、网格头CDN/CDE为0、均质坝材料集清理/导出冲突。组合渗流独立进程完成稳态及非稳态，检查新水头完整有限后继续；有限残差超限仅警告，结果生成后标记未收敛。',
      '工况已修复：结冰与浸润冲突；时程工况先求模态；边坡193个八节点粘弹性面已生成；坝3800个四节点面已生成。',
      'S03计算强度系数1.0/1.05两个真实状态；原生仅NSTEP>1输出带系数文件，模态读取其中1.0的CZ100CB100结果。',
      '新增修复：通用IIWL补齐约束/内应力/面力控制/动力尾段，8种荷载与地应力页面持久化，温差及材料膨胀系数匹配；三维线力等效节点积分；同单元多个面/边分别输出。小模型验证M01–M05全部GPU通过，CPU对照结果相同。',
      'S02/S06原20次迭代不足，改100次后均达到真实静力平衡容差。按用户最新要求，有限静力残差超限也生成结果，但单列未收敛，不能计为数值收敛通过。',
      '新增渗流验收修复：仅当SEEPFREE的IKEY确认IPROB=21、ITER=0、METHOD_ITER=0且为Darcy问题时，按线性初始解处理；仍要求初始水头完整、有限、新生成，正式稳态迭代保持原容差。H03原设置已GPU通过；独立Q4初始方程组验证的归一化残差约2.9e-8。',
      'D12本轮GPU完整复测通过，5阶频率及每阶24891节点振型均已核对；此前CPU900秒超时和GPU早期异常保留。D17使用原先保留且16文件SHA256不变的历史已验收状态，续算校验后CPU新算时程通过，不自动改换续算来源。',
      '原求解器源码保持只读。以下区分源码证据和仍待调试的现象：','']
    for r in records:doc+=['- '+r['finding']+' 源码：'+r['file']+':'+str(r['line'])]
    doc+=['','详细工况状态见run_summary.json，边界清单见功能与边界覆盖矩阵.md。']
    (root/'设置修复与代码定位.md').write_text('\n'.join(doc)+'\n',encoding='utf8')
    print(Counter(r['status'] for r in rows))

if __name__=='__main__':main()
