# ParaView 实际显示效果对照（2026-11-24）

本轮直接比较实际渲染图，排除原程序与 ParaView 色表差异。参考为本机 ParaView 5.13.1 的真实 RenderView，当前程序使用真实 Base_Visualization/Base_PostProcessing 插件，通过后台测试驱动加载 1117 位移结果。

## 对照条件

- 同一份 858020 单元网格；相机位置、焦点、向上方向、投影、视角、画幅相同。
- 统一灰色材质、纯灰背景，不按结果标量着色。
- 对照分面表面、生成节点法向后的平滑表面、旋转 35° 后表面、表面带网格、50% 透明表面五种状态。
- 使用原生 VTK/ParaView 渲染。截图为 2676×657；图像比较排除左右两侧 UI 标记区域，只比较模型与模型轮廓，不修改原始截图。

## 本轮发现和修正

此前只确认了 FXAA 开启，遗漏了原程序仍保留 8 倍 MSAA，以及 VTK 自带 FXAA 阈值与 ParaView 设置不完全相同的问题。

后处理视图现采用 MSAA=0、FXAA 开启，完整 FXAA 参数为：RelativeContrastThreshold=0.125、HardContrastThreshold=0.045、SubpixelBlendLimit=0.75、SubpixelContrastThreshold=0.25、UseHighQualityEndpoints=true、EndpointSearchIterations=12。

透明显示采用 ParaView 默认的原生 depth peeling，最大 4 层、OcclusionRatio=0。进入后处理时保存原 FXAA 开关和配置，离开恢复；其他工作台保留原有 MSAA、透明显示参数。相关操作均为 VTK 原生属性和接口。

参考官方参数：[ParaView RenderViewSettings](https://github.com/Kitware/ParaView/blob/v5.13.1/Remoting/Views/Resources/utilities_remotingviews.xml)。实际也通过本机 pvpython 读取属性核对。

## 修正后的图像结果

| 模式 | 模型轮廓重合率 | 模型区域 RGB 平均绝对差（0～255） |
| --- | --- | --- |
| 分面表面 | 100% | 约 0.000019 |
| 平滑表面 | 100% | 0 |
| 平滑表面旋转后 | 100% | 0 |
| 表面带网格 | 约 99.826% | 约 0.760 |
| 50% 透明表面 | 100% | 约 0.000099 |

平滑表面在本次相机和画幅下，模型区域像素一致；分面和透明状态仅有极少量像素的最低色阶差异。表面带网格仍有少量边线像素差异，不能宣称所有模式逐像素完全相同。结果说明本次测试的表面平滑度、明暗和立体轮廓显示已实际对齐，不能推广为所有工程、所有硬件的像素保证。两端透明状态均确认实际使用了 depth peeling。

原先 8 倍 MSAA 的报告和截图保留为 `appearance-comparison-before.json`、`appearance-current-*-before.png`。

## 验证与部署

4 项回归测试通过：PostResultFunChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks；新增检查后处理 MSAA/FXAA 参数、透明绘制参数和退出后原配置恢复。原始用户工程和结果文件没有修改。

原始截图与报告：`tmp/paraview-appearance-audit/`，其中 `appearance-current-*` 为当前程序，`appearance-paraview-*` 为 ParaView；`appearance-comparison.json` 为图像比较结果。日志：`tmp/paraview_appearance_final_tests.log`、`tmp/paraview_appearance_reference_final.log`。

已更新 `build/release/plugins/Base_Visualization.dll`，旧模块备份于 `tmp/before_paraview_appearance_20261124/`。
