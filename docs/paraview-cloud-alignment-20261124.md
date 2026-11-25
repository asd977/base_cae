# ParaView 表面显示参数对齐（2026-11-24）

后续已补齐可选的 Surface Normals 控制和原生计算管线，见 [表面法向对齐记录](paraview-surface-normals-20261124.md)。

本轮替换此前的柔和云图配置，以已核对的 ParaView 6.0.0 原生参数作为基准；保留岩土工程的结果选择、材料/分层筛选和阈值透明功能。

参考官方源码：

- [表面显示默认参数](https://github.com/Kitware/ParaView/blob/v6.0.0/Remoting/Views/Resources/representationsinternal_remotingviews.xml)
- [RenderView 默认灯光参数](https://github.com/Kitware/ParaView/blob/v6.0.0/Remoting/Views/Resources/views_remotingviews.xml)
- [共面深度偏移默认参数](https://github.com/Kitware/ParaView/blob/v6.0.0/Remoting/Views/Resources/utilities_remotingviews.xml)

## 当前设置

| 项目 | 设置 |
| --- | --- |
| 环境光/漫反射/高光 | 0 / 1 / 0 |
| 高光指数 | 100（高光为 0 时不产生高光） |
| 云图光照插值 | Gouraud 默认，可切换 Flat |
| 节点标量先插值后映射颜色 | 默认开启，独立于光照插值 |
| 单元标量 | 保留分片常值，不转换为节点结果 |
| 灯光 | 原生 vtkLightKit，按 ParaView 主光/补光/背光/头灯参数配置 |
| 默认表面 | 不带网格边线，几何特征线默认关闭 |
| 原生边线与特征线宽 | 1.0 |
| 深度偏移模式 | PolygonOffset，Faces=1 |
| 全局面/线/点偏移 | (0,0) / (0,-4) / -8 |
| 特征线 mapper 额外偏移 | (0,0)，使用全局原生策略 |

灯光仅在后处理生效。进入时保存原灯光集合与自动灯光开关，挂载原生 LightKit；离开时移除 LightKit 并恢复原灯光。全局面、线、点深度偏移也保存恢复，避免影响几何和网格工作台。

## 三项交互行为

1. “结果显示”新增“云图光照插值”和“节点云图颜色插值”。两项分别保存、恢复，切换光照插值只更新属性，不重建云图拓扑。Gouraud 不额外生成节点法向。
2. 表面、表面带单元边线、线框继续通过 VTK 原生 Representation/EdgeVisibility 控制；几何轮廓/折角线单独勾选才执行原生 FeatureEdges 管线，不将其用于常规网格边线显示。
3. 主云图和阈值以下透明区域都保持 Specular=0，避免高光干扰数值颜色。灯光产生的明暗调制与标量数值/色标映射相互独立。

本次对齐以上表面参数、灯光和深度偏移；没有将整个应用替换成 ParaView，也没有改变原始计算结果、主题配色或用户工程数据。

## 验证与部署

Release 构建通过。PostResultFunChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks 共 4 项测试通过。实际插件集成检查覆盖两个插值控件独立生效、保存恢复、原生三种显示模式、按需特征线、大坐标旋转、无高光、透明区域及退出后恢复灯光/偏移。渲染图人工检查已完成。

日志：`tmp/paraview_cloud_final_tests.log`。预览图：`build/Desktop_Qt_6_7_3_MSVC2022_64bit_Release/test-evidence/native-cloud/cloud-paraview.png`。当前运行模块：`build/release/plugins/Base_PostProcessing.dll`；前一版备份在 `tmp/before_paraview_cloud_alignment_20261124/`。
