# 后处理性能对标与实测（2026-11-24）

> 后续修正：用户反馈拖动时简化模型的外观变化不可接受，已彻底移除交互 LOD 的生成、缓存和 Mapper 切换。当前拖动始终使用完整显示面，后台准备与缓存优化保留。以下含 LOD 的内容和速度对比是上一轮历史记录，不代表当前版本；参见 [完整模型交互修正记录](full-resolution-interaction-20261124.md)。

本轮将适合当前单机 Qt/VTK 程序的结果准备、缓存和交互 LOD 接入云图页面。几何处理采用 VTK 原生接口；后台任务调度和 Qt 页面生命周期按本程序结构实现，没有新增几何指纹或自研几何简化算法。

## 已实现

- VTU/VTM/Tecplot 读取、外表面提取、可选节点法向和 LOD 准备在后台线程完成；GPU 渲染仍在界面线程。页面显示“正在准备结果显示…”，完成后一起提交几何、字段与色标。
- 每页只运行一个准备任务，同一事件循环内合并重复请求，队列最多保留两个显示请求；切换文件、改变法向选项或离开页面时取消旧任务，过期结果不会提交。销毁页面时等待原生任务退出，避免访问已销毁的页面。
- 缓存完整外表面、法向和 LOD。切换色标、光照等显示属性复用已有几何；阈值切分按源网格修改时间、字段、关联和阈值复用，改变透明度不再重复切分。
- 大表面（至少 5 MiB、超过 50000 个多边形）使用 `vtkQuadricClustering`。参数对照 ParaView 非 VTK-m 分支：LOD factor=0.5，对应 85×85×85 divisions，UseInputPoints=1、CopyCellData=1、UseInternalTriangles=0。只有输出确实更小时启用。
- 相机交互时切换 LOD，结束交互时恢复原来的完整 Mapper 和完整显示面。鼠标按下不触发 LOD，避免影响点击取值。明确查看网格、轮廓线、等值线或阈值显示时使用完整模型。
- 显示准备缓存最多四项、软预算 256 MiB，保留阈值显示所需的两个表面作为最小工作集；共享结果缓存最多八项、软预算 512 MiB，磁盘表面缓存最多八项、软预算 128 MiB。当前单帧超过预算时保留该帧，不反复逐出当前工作集。这些是缓存持有量限制，不是进程总内存限制。
- 未激活的云图页面不因恢复工程设置而读取整个结果网格，实际进入页面时再准备；待恢复字段在异步完成前保留。文件大小或修改时间改变时重新读取。

参考：[ParaView vtkGeometryRepresentationInternal.h](https://github.com/Kitware/ParaView/blob/v6.0.0/Remoting/Views/vtkGeometryRepresentationInternal.h)、[vtkGeometryRepresentation.cxx](https://github.com/Kitware/ParaView/blob/v6.0.0/Remoting/Views/vtkGeometryRepresentation.cxx)。本工程没有启用该源码中的 VTK-m/TBB 加速分支，采用其 VTK 原生回退分支。

## 1117 工程实测

只读加载 `C:/test/rock/1117/边坡分析/计算结果/CASE01/displacement.vtm`，输入 858020 个体单元。实际 Base_PostProcessing/Base_Visualization 插件由后台测试驱动加载，未控制用户电脑操作。GPU 为 RTX 4060 Laptop，渲染窗口 892×219，两个模式各旋转 20 帧并等待 GPU 完成。

| 项目 | 本次结果 |
| --- | --- |
| 完整显示面 | 200456 面片 |
| 交互 LOD | 68646 面片，减少约 65.75% |
| 完整面平均帧耗时 | 1.41970 ms |
| LOD 平均帧耗时 | 0.87861 ms，降低约 38.11% |
| 读取与显示准备就绪 | 380 ms |
| 界面线程提交读取请求 | 小于计时器的 1 ms 分辨率 |
| 读取准备期间 5 ms 界面心跳 | 69 次，最大间隔 8 ms |
| 首次生成节点法向并显示就绪 | 305 ms；请求提交小于 1 ms |

这是本次机器、窗口和模型的测量，完整面与 LOD 的帧耗时比较来自同一新版本；不代表旧版总耗时对比，也不代表与 ParaView 整体速度相同。读取测试为本进程首次读取，未清空操作系统文件缓存。异步处理改善界面响应，不意味着磁盘读取或 VTK 算法自身变成零耗时。

## 正确性与显示复核

- PostResultFunChecks、PostSurfacePipelineChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks 五项通过。
- 新增检查重复任务合并、缓存命中与失效、取消后的结果丢弃、页面销毁、读取错误、LOD 保留节点与单元数组、原始编号、源网格未改变，以及交互结束后恢复同一个完整 Mapper。
- 实际工程中 LOD 保留 UH(cm) 字段及字段关联；明确查看网格时不切换 LOD。
- 本机 ParaView 5.13.1 的 vtkPVGeometryFilter 对照：外表面点数/单元数/连接关系/原始节点编号一致，坐标误差和结果标量误差为零，节点法向最大向量差约 2.15e-7。
- 相同相机与灰色材质下再对照实际 RenderView：分面、平滑、旋转后平滑、透明表面轮廓重合率均为 100%；旋转后平滑模型区域像素一致。其他三种模型区域 RGB 平均绝对差分别约 0.000075、0.000047、0.001341（0～255）。网格线仍有小量像素差异，轮廓重合率约 99.799%，不宣称所有模式逐像素相同。

原始工程及结果文件未修改。静止显示使用完整外表面，LOD 只用于交互显示。

## 范围

本轮针对云图读取与表面显示路径；共享读缓存的内存限制也作用于其他结果页面。没有将 ParaView 的 MPI、远程渲染、IceT、VTK-m 后端或整个 ServerManager 架构迁入本工程；矢量、切面等其他页面的所有计算也没有统一改为异步。筛选、首次阈值切分和等值线生成仍使用已有原生 VTK 路径，部分首次计算仍在界面线程，因此不能称所有操作都已与 ParaView 同速。

## 证据与部署

- 数据、图像和计时：`tmp/paraview-performance-audit/`，其中 `performance.json`、`native-normal-comparison.json`、`appearance-comparison.json` 为机器可读记录。
- 构建与测试日志：`tmp/paraview_performance_build_final.log`、`tmp/paraview_performance_final_tests.log`、`tmp/paraview_performance_real2.log`。
- 参考对照日志：`tmp/paraview_performance_native_reference.log`、`tmp/paraview_performance_appearance_reference.log`。
- 运行模块：`build/release/plugins/Base_PostProcessing.dll`；原运行模块和本轮修改前文件保留于 `tmp/before_paraview_performance_20261124/`。
