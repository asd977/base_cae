#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <core_global.h> // 假设这是你的项目全局头文件

// -------------------------------------------------------------
// VTK 类的前置声明：仅用于指针或引用
// -------------------------------------------------------------
class vtkUnsignedCharArray;
class vtkUnstructuredGrid;
class vtkDataSetMapper;
class vtkActor;
class vtkIdTypeArray;
class vtkCallbackCommand;
class vtkObject;
class vtkRenderWindowInteractor;
class vtkRenderer;
typedef long long vtkIdType;                // 简化处理：与 64bit VTK 相同
class vtkIntArray;
class vtkStringArray;
template <typename T> class vtkSmartPointer; // 智能指针模板前置声明
// -------------------------------------------------------------


// 哪个 solver：一次/二次电流分布之类的
enum class SolverType
{
    PrimaryCurrent,   // 一次电流分布
    SecondaryCurrent, // 二次电流分布
    // 将来继续加 TertiaryCurrent, ...
};

// 每个 solver 在某个物理组上的参数：通用 key-value
struct BCSolverParam
{
    SolverType solver;                                 // 哪个 solver
    std::string bcId;
    std::map<std::string, double> scalars;            // 标量参数，比如 "phi" / "sigma" / "i0" ...
};

// Bulk 提交用的记录（一条记录 = 一个物理组在一个 solver 下的一组参数）
struct BCSolverRecord
{
    int dim = -1;                                     // 0/1/2/3
    int physId = -1;                                  // 物理组 ID
    SolverType solver = SolverType::PrimaryCurrent;   // solver 类型
    std::string bcId;
    std::map<std::string, double> scalars;            // 参数
};

// 导出给 solver 的最终条目
struct BCSolverEntry
{
    int dim = -1;
    int physId = -1;
    std::string name;                        // 物理组名
    std::vector<vtkIdType> cellIds;         // 单元 id 列表
    SolverType solver;
    std::string bcId;
    std::map<std::string, double> scalars;  // 参数
};

// 你自己的：从 .msh 里读取 physical names 等
struct GmshPhysicalInfo
{
    // key: (dim, physId) -> name  比如 (2, 35) -> "face_3"
    std::map<std::pair<int,int>, std::string> physicalNameMap;
};

// 选择模式（按 gmsh_dim）
enum class BCSelectMode
{
    Volume = 3,
    Face   = 2,
    Edge   = 1,
    Point  = 0
};

// 暴露给上层，用于下拉框等
struct PhysGroupInfo
{
    int dim    = -1;             // 0/1/2/3
    int physId = -1;             // Gmsh Physical ID
    std::string name;            // "face_1" / "volume_1" ...
};

// 选中结果
struct BCSelectedGroup
{
    int dim = -1;                // 0/1/2/3
    int physId = -1;             // Gmsh Physical ID
    std::string name;            // 物理组名
    std::vector<vtkIdType> cellIds;      // 属于该物理组的所有 cellId
};

// 私有前置声明，用于 Pimpl Idiom
class BCPrivate;

class CORE_EXPORT BoundaryConditions
{
public:
    // ******** 单例访问 ********
    static BoundaryConditions& instance();

    // -------- 网格载入 & Actor 访问 --------

    // 读取 Gmsh 2.2 网格文件；fileName 由上层传入
    bool loadMesh(const std::string& fileName);

    // 拿网格和 actor 给外部用（外部负责 addActor）
    vtkSmartPointer<vtkUnstructuredGrid> grid() const;
    vtkSmartPointer<vtkActor> actor() const;

    // -------- 点选（鼠标拾取）控制 --------

    // 设置 interactor；上层从自己的 QVTKWidget 里拿
    void setInteractor(vtkRenderWindowInteractor* iren);

    // 启用 / 禁用拾取
    void enablePicking(bool on);

    // 设置拾取模式（点击时只对某个维度生效）
    void setPickMode(BCSelectMode mode);

    // -------- 通用：预选 / 已选 / 预删 （点/线/面/体通用） --------
    // 三种状态对应三套接口：

    // 1) 预选：比如 “添加” 下拉框 hover
    //    physId <= 0 表示取消当前预选
    void previewSelection(BCSelectMode mode, int physId);

    // 2) 预删：比如 “已选列表” hover，要展示即将删除的那组
    //    physId <= 0 表示取消当前预删
    void previewRemoveSelection(BCSelectMode mode, int physId);

    // 3) 确认添加：点击“添加”按钮
    void addSelection(BCSelectMode mode, int physId);

    // 4) 确认删除：点击“删除”按钮
    void removeSelection(BCSelectMode mode, int physId);

    // 5) 获取当前所有“已选”集合（不清空）
    std::vector<BCSelectedGroup> getAllSelections() const;

    // 6) 获取并清空所有“已选”集合（同时清空预选/预删，恢复默认颜色）
    std::vector<BCSelectedGroup> takeSelections();

    // -------- （兼容用）一次性高亮 + 取结果：主要给拾取回调用 --------

    // 传入模式 + 单个 physId，高亮并记录一次性结果
    void applySelection(BCSelectMode mode, int physId);

    // 传入模式 + 多个 physId，一次性高亮
    void applySelection(BCSelectMode mode, const std::vector<int>& physIds);

    // 取出“最近一次 applySelection 的结果”，并清空
    std::vector<BCSelectedGroup> takeSelectionOnce();

    void clearHighlight();
    // -------- 父子关系 & 物理组列表 --------

    // 按维度获取所有物理组（用于下拉框）
    // dim = 0/1/2/3
    std::vector<PhysGroupInfo> getGroupsForDim(int dim) const;

    // 体 -> 面：给定 volume_physId，返回属于该体的 face_physId 列表
    std::vector<int> getFacesOfVolume(int volumePhysId) const;

    // 面 -> 线：给定 face_physId，返回属于该面的 edge_physId 列表
    std::vector<int> getEdgesOfFace(int facePhysId) const;

    // 物理组 -> cell 列表：给定 (dim, physId)，返回所有 cellId
    std::vector<vtkIdType> getCellsOfGroup(int dim, int physId) const;

    std::vector<int> getPhysIdsForBc(const std::string &bcId, SolverType solver, int dim) const;

    // ========== 边界条件参数: 设置 & 查询 & 导出 ==========

    // 批量提交本页面的配置：
    //   - 典型用法：在 beforeUnload() 里，把当前页面所有 dim/physId 的配置打包成 records，
    //              调一次 setSolverParamsBulk。
    //   - merge = true 表示合并：同 (dim, physId, solver) 会覆盖旧值，其他保持不变。
    void setSolverParamsBulk(const std::vector<BCSolverRecord>& records,
                             bool merge = true);

    // 单条设置（有时候页面只改一个也可以用）
    void setSolverParam(int dim, int physId,
                        SolverType solver,
                        const std::string& bcId,
                        const std::map<std::string, double>& scalars);

    // 判断某个 (dim, physId, solver) 是否已经有设置 —— UI 可据此弹 “是否覆盖？”
    bool hasSolverParam(int dim, int physId, SolverType solver) const;

    // 获取某个 (dim, physId, solver) 的参数 —— 用于 beforeLoad 回填界面
    bool getSolverParam(int dim, int physId,
                        SolverType solver,
                        BCSolverParam& outParam) const;

    // 导出给某个 solver 的所有边界条件条目：
    //   - solver: 哪个 solver
    //   - dimFilter: 若 >=0 则只导出指定维度 (0/1/2/3)，否则导出所有
    std::vector<BCSolverEntry> exportForSolver(SolverType solver,
                                               int dimFilter = -1) const;

    void highlightForPage(int dim, const std::vector<int>& physIds);

    void clearSolverParamsForPage(const std::string &bcId, SolverType solver, int dim);
private:
    // 禁止外部构造 / 拷贝 / 移动
    BoundaryConditions();
    ~BoundaryConditions();

    BoundaryConditions(const BoundaryConditions&)             = delete;
    BoundaryConditions& operator=(const BoundaryConditions&) = delete;
    BoundaryConditions(BoundaryConditions&&)                 = delete;
    BoundaryConditions& operator=(BoundaryConditions&&)      = delete;

    // 构建 dim+physId -> cellIds 映射 & dim+physId -> nodeSet 映射
    void buildDimPhysIndex();

    // 构建 体->面、面->线 父子关系
    void buildHierarchy();

    // 清空颜色（全部默认灰色）
    void resetCellColors();

    // 高亮某个物理组（只改颜色，不记录 selection）
    void highlightDimPhys(int dim, int physId,
                          const unsigned char color[3]);

    // 根据 “已选 + 预选 + 预删” 三种状态统一刷新颜色
    void updateSelectionColors();

    // 拾取回调（内部）
    void handlePickEvent(vtkObject* caller,
                         unsigned long eventId,
                         void* callData);

    // static 回调
    static void OnLeftButtonPress(vtkObject* caller,
                                  unsigned long eventId,
                                  void* clientData,
                                  void* callData);



private:
    // -------------------------------------------------------------
    // Pimpl Idiom: 所有 VTK 成员都移到了私有实现类中
    // -------------------------------------------------------------
    BCPrivate* m_private;

    // Physical 信息 (非 VTK 依赖，保留)
    GmshPhysicalInfo m_gmshInfo;

    // 体 -> 面（volume_physId -> [face_physId...]) (非 VTK 依赖，保留)
    std::map<int, std::vector<int>> m_volumeToFaces;

    // 面 -> 线（face_physId -> [edge_physId...]) (非 VTK 依赖，保留)
    std::map<int, std::vector<int>> m_faceToEdges;

    // ====== 通用 “已选 / 预选 / 预删” 状态 ======

    // 已选物理组：key = (dim, physId)
    std::map<std::pair<int,int>, bool> m_selectedGroups;

    // 当前预选物理组：dim < 0 表示无
    std::pair<int,int> m_previewGroup;

    // 当前预删物理组：dim < 0 表示无
    std::pair<int,int> m_previewRemoveGroup;

    // ====== 兼容旧逻辑：一次性 applySelection 的结果 ======

    // 当前本轮“一次性选中”的结果（applySelection / picking 更新）
    std::vector<BCSelectedGroup> m_currentSelection;

    // 拾取相关 (非 VTK 依赖，保留)
    BCSelectMode m_pickMode = BCSelectMode::Face;

    std::vector<BCSolverRecord> m_solverRecords;

    std::map<std::tuple<int,int,SolverType>, BCSolverParam> m_solverParams;
};
