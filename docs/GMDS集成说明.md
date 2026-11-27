# GMDS 半自动分块

网格模块的“扫掠分块 → GMDS 半自动分块”使用 CEA GMDS Blocking 生成候选切片。用户预览、采纳后，切面保存为现有网格方案的分块规则；最终仍由 OpenCASCADE 切割、SMESH 划分。原始 CAD 不被替换。

原自写 `suggestSemiAutoPartitionPlanes` 候选搜索及其入口已删除；手动坐标切割、点面切、点边切、圆柱面切割数据兼容和扫掠源/目标面分析保留。

## 固定依赖与构建

- GMDS：`6c8090df3b3dab9c087053661d83b12831d766b3`，https://github.com/LIHPC-Computational-Geometry/gmds
- Eigen：3.4.0，官方归档 SHA256 `eba3f3d414d2f8cba2919c78ec6daab08fc71ba2ba4ae502b7e5d4d99fc02cda`
- CGAL：5.6.2，官方发布归档 SHA256 `ec522d4e55dba7e2e117e028239d392667338e32d123b659e4e135f20374fec4`
- 复用项目已有 Boost、Qt 6、OCC 7.9.3；不引入另一套 OCC。

在项目目录运行 `./scripts/SetupGMDS.ps1`，然后按现有 MSVC/Qt Kit 配置并编译。脚本安装到被 Git 忽略的 `libs/`，不会重置已有第三方源码。不通过宿主工程链接 GTS、Python、GMDS 研究性网格模块。

CMake 可指定 `CAE_GMDS_SOURCE_DIR`、`CAE_GMDS_EIGEN_ROOT`、`CAE_GMDS_CGAL_ROOT`。`cmake/GMDSBackend.cmake` 按上游核心模块源码清单构建 GMDSUtils、GMDSMath、GMDSIg、GMDSIo，以及所需的 GMDSCad、GMDSBlocking 源文件。上游源码未修改；独立进程 `CAE_GMDSWorker.exe` 必须与主程序一起部署。

## 实际处理流程

1. 几何模块按全局实体编号导出不含显示三角片的临时 BREP。序列化前跳过曲面和拓扑过多的实体。
2. 工作进程建立精确 OCC `GeomManager`，提供点、曲线、曲面、实体及相邻关系、投影服务。坐标归一化后交给 GMDS `CurvedBlocking`、`CurvedBlockingClassifier::list_Possible_Cuts`、`cut_sheet`。
3. 从 GMDS 切片结果提取平面。OCC 对候选实际试切，检查合法性、体积守恒、最小子块体积，以及六面块拓扑的改善。这里的六面块预检不承诺实际网格必定成功。
4. 界面显示候选，由用户采纳。切割和网格仍使用既有拓扑映射、材料归属与共享节点流程；SMESH 的实际计算结果是最终判断依据。

## 范围与预算

首版仅分析由平面和直线边组成的有效多面体。曲面、曲线边、无效实体、拓扑过多的实体会跳过，不替换成近似几何。

- 每次最多 16 个实体；更多实体需要先指定目标。
- 每个实体最多 48 面、144 边、96 点。
- 每个残余块最多取 12 个 GMDS 候选；全局最多 24 次 OCC 试切。
- 最多 32 个子块；子块体积不低于原实体的 0.5%。
- 工作进程约 3.5 秒停止新增试切；界面在 6 秒终止尚未退出的进程。
- 界面一次最多建议 4 个切面。几何和规则相同的成功结果有单项缓存。

允许用户取消。离开页面或切换方案时取消分析；返回结果前重新检查几何、全局实体编号和原分块规则，变化后的旧结果不允许采纳。当前没有实现任意复杂曲面、完整自动全六面体分块或 GMDS 替代 SMESH。

## 验证

`ctest --test-dir <构建目录> --output-on-failure -R GmdsPartitionChecks`

测试使用真实 GMDS 工作进程，验证 L 形几何的候选生成、OCC 分割和原生 SMESH 六面体划分、界面共节点、原材料编号保留；同时覆盖无需切割的方体、切面数量限制、非连续全局编号、已有切面、缩放平移、曲面与复杂几何跳过、缓存、取消、超时恢复、取消后的回调隔离和坏 BREP。

## 第三方源码与许可

GMDS 仓库使用 LGPL-2.1；CGAL 各头文件使用其各自声明的许可证，本集成的 Generalized_map 头文件为 LGPL-3.0-or-later 或商业许可；Eigen 核心为 MPL-2.0，Boost 为 BSL-1.0。GMDS 内置 iniparser 使用 MIT，Predicates_psm 使用其文件内 BSD 风格声明。

构建目录的 `gmds-licenses/` 保留上述许可原文及本说明。更新打包脚本同时包含工作进程及这些文件。上游固定源码、下载校验值、本项目适配代码、构建清单均可用于重新构建和重新链接工作进程；分发静态链接程序时仍需随交付方式满足相应许可证的源码和重新链接要求。
