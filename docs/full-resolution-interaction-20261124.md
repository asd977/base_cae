# 拖动始终使用完整模型（2026-11-24）

用户反馈交互 LOD 导致拖动时模型外观明显改变。本轮移除云图的简化面生成、简化缓存、交互事件监听及 Mapper 切换，不保留自动降精度开关。静止、拖动和松开鼠标均使用同一个完整显示面及 Mapper。

保留后台读取、完整外表面与节点法向准备、缓存复用、缓存内存限制、过期任务取消和阈值切分复用。原始工程和计算结果未修改。

验证：五项回归测试 PostResultFunChecks、PostSurfacePipelineChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks 全部通过。新增/调整检查完整面片不减少、每次交互事件前后 Mapper 和输入数据指针相同、字段与关联不变、交互结束后几何修改时间不变。

后台只读加载 1117 工程位移结果，连续旋转 20 帧，完整显示面始终保持 200456 个面片，`interactionUsesFullGeometry=true`。读取准备期间界面心跳继续响应，最大间隔 7 ms。本机测试窗口中完整模型交互平均每帧约 1.53 ms；这是本次模型和测试窗口测量，不是所有工程的速度保证。上一轮基于简化面的约 38% 提升不再适用于当前版本。

证据：`tmp/remove_interactive_lod_tests.log`、`tmp/remove_interactive_lod_real.log`、`tmp/full-resolution-interaction-audit/performance.json`。

运行模块更新为 `build/release/plugins/Base_PostProcessing.dll`，修改前源码与运行模块备份在 `tmp/before_remove_interactive_lod_20261124/`。
