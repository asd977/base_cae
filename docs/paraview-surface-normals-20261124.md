# ParaView 表面法向流程对齐（2026-11-24）

最终已增加与本机 ParaView 原生过滤器的实际数值对照，详见 [最终实机对照记录](paraview-final-native-audit-20261124.md)。

## 核对结果

ParaView 6.0 并不默认为缺少法向的表面生成节点法向。`ComputePointNormals` 默认 0，`Splitting` 默认 1，`FeatureAngle` 默认 30°。没有节点法向时，Gouraud 仍可能显示各三角面的明暗差；因此不能把“有分面感”直接判定为未正确对齐 ParaView。

官方依据：

- [Surface Normals 控件与默认值](https://github.com/Kitware/ParaView/blob/v6.0.0/Remoting/Views/Resources/representationsinternal_remotingviews.xml)
- [vtkPVGeometryFilter 的法向管线](https://github.com/Kitware/ParaView/blob/v6.0.0/VTKExtensions/FiltersRendering/vtkPVGeometryFilter.cxx)

## 实现

“结果显示”补齐“计算表面节点法向”“保留明显折角”和“折角角度”控件，默认值分别为关闭、开启、30°，设置随工程保存恢复。

开启计算且输入缺少节点法向时，使用 VTK 原生 `vtkGeometryFilter → vtkPolyDataNormals`。计算节点法向、不额外计算单元法向；折角分裂和角度由控件设置。与 ParaView 对应实现一致，AutoOrientNormals、Consistency、NonManifoldTraversal、FlipNormals 均关闭，坐标输出保持原精度。已有的节点法向直接保留。

主云图和阈值以下透明区域都使用这条可选显示管线。只对显示外表面计算，不改变原始结果网格、材料/分层数据或标量值。保留原始点、单元 ID 数组。特征线继续使用未分裂的当前可见体网格，避免计算法向后失去轮廓功能。

管线保留在页面内，VTK 根据输入与参数的修改状态更新；切颜色或仅切光照插值时不重新计算未变化的法向。换结果文件时释放旧管线。

## 验证

Release 构建完成，PostResultFunChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks 共 4 项通过。

实际插件测试覆盖默认值、设置保存恢复、90° 折角保留、关闭折角分裂、曲面明暗平滑、原始点/单元 ID 保留、标量值不变、颜色/光照更改复用法向、透明区域及关闭计算后恢复原网格。

以只读方式加载 `C:/test/rock/1117/边坡分析/计算结果/CASE01/displacement.vtm` 验证：858020 个体单元，开启后的显示外表面为 200456 个单元；源网格修改时间、结果文件大小与修改时间均保持不变。开启更新耗时是本机一次观测值，非通用性能承诺。

日志：`tmp/paraview_normals_final_tests.log`、`tmp/paraview_normals_actual_final.log`。

实际工程对比：`tmp/paraview-normals-1117/actual-result-faceted.png`、`tmp/paraview-normals-1117/actual-result-point-normals.png`。合成曲面对比在构建目录 `test-evidence/native-cloud/cloud-curved-faceted.png`、`cloud-curved-point-normals.png`。

已更新 `build/release/plugins/Base_PostProcessing.dll`，前一版备份在 `tmp/before_paraview_normals_20261124/`。用户工程及结果文件未修改。
