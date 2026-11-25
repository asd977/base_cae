# 矢量显示对齐 TMSR（2026-11-24）

参考 `C:/project/CAE/code/TMSR/plugins/Base_PostProcessing/VectorSettingWidget.*`，迁移到当前岩土后处理页面：

- 自动采用空间体素均匀抽样，每个体素保留靠近中心的有效点，过滤零矢量；手动模式仍按有效点间隔采样。
- 默认箭头长度为模型对角线的 2.4%，头部长度 0.32、头部半径 0.095、杆半径 0.028，圆周分辨率为 12；长度和粗细倍率实时更新。
- 增加完整矢量场选择、按幅值缩放选项；完整三分量字段隐藏冗余分量控件。缩放选项随工程保存、恢复。
- 颜色按采样后的模值分布显示；与 TMSR 一致，当极端峰值超过 P95 的 2.5 倍时，自动颜色上限采用 P95，仍可通过公共显示范围手动设置。
- 保留岩土位移、速度分量优先识别和结果材料/分层筛选；不将无关标量默认拼装成矢量。单元字段可插值到节点，已有节点字段保持优先。
- 切页复用已生成箭头，显示设置或筛选变化后重新生成；返回页面恢复本页独立色标及显示控制上下文。
- 采样点使用双精度，保留工程测量坐标精度。

Release 构建通过。VectorDisplayChecks、PostResultFunChecks、UiGeometryPostChecks 共 3 项测试通过。新测试覆盖加密网格抽样、零矢量、箭头形状、实时缩放、设置保存恢复、切页复用和失效、独立色标恢复、节点/单元字段兼容及纯标量结果。

合成位移场渲染图：`tmp/tmsr-vector/vectors.png`；验证日志：`tmp/tmsr_vector_regression_final.log`。运行目录 `build/release/plugins/Base_PostProcessing.dll` 已更新，旧模块备份在 `tmp/before_tmsr_vector_20261124/`。用户工程和 TMSR 参考目录未修改。
