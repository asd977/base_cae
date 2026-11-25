# 材料透明参照及边界网格显示回归修复（2026-11-25）

问题已在原生可视化注册表、真实工程树切换流程及 PickManager 中复现：从几何视图进入后处理，再进入材料域选择或边界点选择时，选中域仍显示，背景完整网格却保持隐藏。

## 根因及修复

模块可见性遮罩与对象自身可见性是两个状态。几何视图本来就会用模块遮罩隐藏网格；后处理隔离逻辑记录了网格“当前不可见”，随后调用 `setVisible(false)` 修改对象选择，退出后处理时又将该状态写回对象选择。因此切回材料/边界模块、解除模块遮罩后，网格仍不可见。

可视化服务新增临时场景可见性入口 `setTemporaryVisible`，原生视图只更新实际 Prop，不修改对象可见性选择，并同时尊重对象选择和模块遮罩。`setVisible` 继续用于明确改变对象选择。后处理隔离及恢复改走临时入口，不再将模块遮罩造成的不可见固化为对象选择。

没有取消模块遮罩或强行显示所有网格。用户主动隐藏的对象保持隐藏。材料域的独立前景高亮、其余网格的半透明样式和离开域选择后的样式恢复仍由原有 PickManager 负责。

## 验证

新增 SceneVisibilityChecks，使用真实 Base_UiFrame/Base_Visualization/Base_ProjectManager/Base_PostProcessing 插件及原生 PickManager，内存网格提供两个材料域；测试页面分别激活与材料、边界设置相同的 Domain/Node 选择模式。通过实际工程树连续切换两轮“几何 → 后处理 → 材料域选择 → 边界点选择”。

| 检查 | 修复前，两轮均 | 修复后，两轮均 |
| --- | --- | --- |
| 材料：完整背景网格可见、半透明，选中域前景可见 | 失败 | 通过 |
| 边界：完整网格可见，恢复原不透明度 | 失败 | 通过 |
| 主动隐藏对象保持隐藏 | 通过 | 通过 |

已检查原始截图：修复前只有选中域；修复后其他域作为半透明背景出现，边界模式显示完整两域。

七项测试通过：SceneVisibilityChecks、PostRefreshChecks、CloudLightingChecks、UiGeometryPostChecks、PostSurfacePipelineChecks、PostResultFunChecks、VectorDisplayChecks。测试不修改用户工程。上一轮单次刷新优化和完整模型交互保持有效。

## 记录与运行更新

- 构建：`tmp/scene_visibility_fix_build_20261125.log`。
- 复现：`tmp/scene_visibility_before_20261125.log`、`tmp/scene-visibility-before-20261125/`。
- 修复验证：`tmp/scene_visibility_after_20261125.log`、`tmp/scene-visibility-after-20261125/`。
- 回归：`tmp/scene_visibility_regression_20261125.log`。
- 修改前源码及运行模块：`tmp/before_scene_visibility_fix_20261125/`。
- 运行目录使用与测试一致的 core.dll 及相关原生显示、工程树、界面、物理场、后处理和输出模块；部署逐文件校验，原模块保留备份。
