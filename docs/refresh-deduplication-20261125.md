# 其他页面重复计算与刷新清理（2026-11-25）

本轮扩大检查至其他后处理页面及代理模型界面，清理已确认的重复入口和无效重算，保留完整模型交互显示。

## 重复信号连接

逐项对照 `.ui` 对象名、信号名、槽名及生成代码的 `connectSlotsByName`，删除已有自动连接的 41 处手动连接：

| 页面/组件 | 删除数量 |
| --- | ---: |
| 切块 | 9 |
| 切面 | 9 |
| 等值面 | 7 |
| 等值分割 | 10 |
| 取值 | 2 |
| 代理模型 DNN 编辑 | 3 |
| 拉丁超立方采样按钮 | 1 |

对象名与槽名不对应的显式映射继续保留。等值分割的两种互斥模式另有一次重复通知：仅保留百分比模式按钮的 toggled，覆盖开启和关闭两种转换，避免同一次切换先后处理两个互斥按钮。

## 计算及刷新路径

- 载入结果、选择默认字段/切面方向、恢复公共显示选项时抑制中间控件信号，完成设置后统一更新；避免先计算临时字段再计算最终字段。
- 切块和分割首次进入页面不再重复执行已由文件选择槽完成的计算；节点载入和状态恢复中的绘制请求合并到末尾一次执行。
- 删除 12 处挂载/移除 Actor 时立即绘制的默认调用，交由已有操作末尾的渲染处理，减少不完整中间画面和重复绘制。切面单模式切换也不再在内部绘制后再次绘制。
- 切块、切面、分割和等值面的只读路径复用已有结果网格/筛选输出。之前默认浅拷贝每次创建新输入对象，即使数据相同，VTK 仍会重新执行下游滤镜。
- 切块先配置新输入和目标平面再执行裁剪，不再先计算旧平面。字段着色使用 Mapper 显式数组选择，不再改动输入的活动标量、触发无关裁剪。
- 等值面使用已指定的输入数组，删除额外的输入活动标量设置和无条件 `Modified()`；阈值未变时由 VTK 自身复用输出，参数改变时仍正常计算。
- 矢量页将色标、范围和显示外观更新与采样/箭头生成分开，复用现有 Actor、Glyph 和采样数据。筛选、字段、密度等几何相关变化仍执行必要重建，未知刷新原因也保留重建路径。

矢量生成仍使用独立包装写入 Vector/VectorMag；单元标量转节点时仍保留必要的独立数据准备，防止污染原始结果。

## 验证

编译通过 Base_PostProcessing、Base_SurrogateModel 和相关测试目标。六项回归测试通过：PostRefreshChecks、VectorDisplayChecks、PostResultFunChecks、PostSurfacePipelineChecks、UiGeometryPostChecks、CloudLightingChecks。

新增的 PostRefreshChecks 用实际页面和 VTK 渲染窗口统计服务绘制请求及原生窗口绘制事件。测试临时工程中的文件切换、字段、位置、阈值、模式、节点载入等共 21 类操作，每次均只请求一次绘制；并监听原生滤镜执行事件，确认：

- 切块换文件执行一次裁剪，切换着色字段不执行裁剪。
- 等值面阈值变化执行一次 Contour，阈值未变的刷新不重新执行 Contour。
- 矢量范围/外观变化保留相同 Glyph、Actor、采样数据及修改时间，范围更新正确；非激活页面修改外观后重入也复用箭头。

只读复核 1117 位移结果，完整显示面为 200456 面片，连续交互保持同一个完整 Mapper/显示面，`interactionUsesFullGeometry=true`。原工程和结果数据未修改。

这些是本次覆盖路径的检查结果，不代表所有界面、所有事件组合都已不存在重复工作。

## 记录及部署

- 构建：`tmp/refresh_dedup_build_verified_20261125.log`、`tmp/refresh_dedup_build_cleanup_20261125.log`。
- 回归测试：`tmp/refresh_dedup_tests_final_20261125.log`。
- 调用计数：`tmp/refresh_counts_final_20261125.log`。
- 静态连接复查：`tmp/refresh_connection_audit_final_20261125.log`，已无上述可自动连接的重复项。
- 实际结果：`tmp/refresh_dedup_real_20261125.log`、`tmp/refresh-dedup-audit-20261125/`。
- 修改前源码、连接报告及原运行模块：`tmp/before_refresh_dedup_20261125/`。
- 更新运行模块：`build/release/plugins/Base_PostProcessing.dll`、`build/release/plugins/Base_SurrogateModel.dll`。
