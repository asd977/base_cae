# 云图柔和光照与可选轮廓（方案一）

后续已按用户要求改为 ParaView 参数，当前行为见 [ParaView 对齐记录](paraview-cloud-alignment-20261124.md)。下文保留上一阶段的实现记录。

2026-11-24：按用户选定的方案一，仅调整 VTK 原生显示属性和接口；没有新增几何、法向、轮廓提取或着色算法。

- 云图主表面及阈值以下透明区域开启光照：Ambient=0.80、Diffuse=0.22、Specular=0，保留原生平面插值，以柔和明暗区分转折面。
- 新工程及没有保存该设置的旧工程，几何轮廓/折角线默认关闭。已经明确保存的开关值继续恢复。
- 手动勾选时继续使用原有 VTK 清理、表面提取和 FeatureEdges 管线；线宽改为 1.6。
- 后处理启用原生 PolygonOffset 模式，PolygonOffsetFaces=0，面偏移为 (0,0)，轮廓 mapper 的相对线偏移为 (-1,-4)。离开后处理恢复进入前的全局偏移设置。VTK 9.2 OpenGL2 按图元分别读取面、线偏移，因此并未只依赖 Faces 开关。
- 轮廓默认关闭时，在提面和特征线提取之前返回，不执行该管线。

Release 构建及以下四项测试通过：PostResultFunChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks。

CloudLightingChecks 通过真实后处理 DLL 和实际显示控件载入合成六面体台阶模型（X≈510000、Y≈3440000），检查默认轮廓关闭、原生光照属性、手动轮廓、三个旋转视角、结果网格不变及透明阈值区域光照。未对百万级工程作耗时承诺；光照会对显示颜色产生轻微明暗调制，LUT 和原始数值不变。

渲染对比在 `build/Desktop_Qt_6_7_3_MSVC2022_64bit_Release/test-evidence/native-cloud/`：`cloud-before-unlit.png`、`cloud-soft-light.png`、`cloud-outline.png` 及三个旋转视角图。测试日志：`tmp/native_cloud_final_tests.log`。

运行目录 `build/release/plugins/Base_PostProcessing.dll` 已更新；旧模块备份于 `tmp/before_native_cloud_lighting_20261124/`。未改动用户工程数据。
