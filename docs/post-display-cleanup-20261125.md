# 后处理显示冗余清理（2026-11-25）

范围为前两轮云图后台准备、缓存及交互简化移除涉及的代码，没有对整个工程进行无差别删除。

本轮删除：

- 没有实现或引用的 `selectGridNode` 声明；没有调用的范围设置/解析函数。
- 只转发调用的读取、LUT、色标样式包装函数；合并重复的显示刷新路径。
- 七个无效或无需跨调用保留的成员：旧范围更新标志、等值线关联标志、旧轮廓输入状态、临时色标位置状态。保留原工程色标位置恢复逻辑。
- 十三个无用包含项、一次多余的表面浅拷贝，以及重复更新范围编辑器的代码。
- 五处手动连接的下拉框信号。生成的 `ui_NephogramSettingWidget.h` 已调用 `QMetaObject::connectSlotsByName`，对应 `on_<对象名>_<信号>` 槽保留自动连接，避免一次选择重复执行。

另外，关闭法向计算时，分裂开关和折角参数不再导致相同完整表面的缓存失效；后台任务完成后释放已结束请求持有的网格引用。

完整显示表面和未按法向分裂的等值线表面仍有不同用途，保留必要缓存及过期结果取消逻辑。交互 LOD 的成员、生成、缓存和切换代码均未恢复，拖动始终使用完整模型。

编译成功，PostResultFunChecks、PostSurfacePipelineChecks、UiGeometryPostChecks、CloudLightingChecks、VectorDisplayChecks 五项通过。新增检查关闭法向时复用表面、下拉框自动更新文件和字段。只读加载 1117 位移结果，连续 20 帧交互保持同一个完整 Mapper 和 200456 个显示面片，原始结果文件未修改。

日志：`tmp/post_cleanup_build_final_20261125.log`、`tmp/post_cleanup_tests_final_20261125.log`、`tmp/post_cleanup_real_final_20261125.log`。测试产物：`tmp/post-cleanup-audit-20261125/`。运行模块更新为 `build/release/plugins/Base_PostProcessing.dll`，修改前源码和模块保存在 `tmp/before_post_cleanup_20261125/`。
