#include "BoundaryConditions.h"

// -------------------------------------------------------------
// 在 .cpp 文件中包含所有 VTK 头文件，以隐藏对外部的依赖
// -------------------------------------------------------------
#include "mesh/GmshV2Reader.h"
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>
#include <vtkCellData.h>
#include <vtkIntArray.h>
#include <vtkStringArray.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkIdTypeArray.h>
#include <vtkIdList.h>
#include <vtkCallbackCommand.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderWindow.h>
#include <vtkRendererCollection.h>
#include <vtkRenderer.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>

#include <algorithm>
#include <iostream>

// -------------------------------------------------------------
// Pimpl Idiom: 私有实现类定义
// -------------------------------------------------------------
class BCPrivate
{
public:
    // 网格 & 显示
    vtkSmartPointer<vtkUnstructuredGrid>   m_fullGrid;
    vtkSmartPointer<vtkUnsignedCharArray>  m_cellColors;
    vtkSmartPointer<vtkDataSetMapper>      m_meshMapper;
    vtkSmartPointer<vtkActor>              m_meshActor;

    // dim+physId -> cellIds
    std::unordered_map<std::uint64_t, vtkSmartPointer<vtkIdTypeArray>> m_dimPhysToCellIds;

    // dim+physId -> 节点集合（用于父子关系）
    std::unordered_map<std::uint64_t, std::set<vtkIdType>> m_dimPhysToNodeSet;

    // 拾取相关
    vtkRenderWindowInteractor* m_interactor   = nullptr;
    vtkSmartPointer<vtkCallbackCommand> m_pickCallback;
    bool m_pickingEnabled = false;

    BCPrivate() = default;
};

// ========== 单例接口 ==========

BoundaryConditions& BoundaryConditions::instance()
{
    static BoundaryConditions inst;    // Meyers singleton
    return inst;
}

// ========== 构造 / 析构（私有） ==========

BoundaryConditions::BoundaryConditions()
    : m_private(new BCPrivate)
{
    m_private->m_pickingEnabled = false;
    m_previewGroup        = std::make_pair(-1, -1);
    m_previewRemoveGroup  = std::make_pair(-1, -1);
}

BoundaryConditions::~BoundaryConditions()
{
    delete m_private;
}

// ========== 代理方法：返回 VTK 对象 ==========

vtkSmartPointer<vtkUnstructuredGrid> BoundaryConditions::grid() const
{
    return m_private->m_fullGrid;
}

vtkSmartPointer<vtkActor> BoundaryConditions::actor() const
{
    return m_private->m_meshActor;
}

// ========== 载入网格 ==========

bool BoundaryConditions::loadMesh(const std::string& fileName)
{
    GmshPhysicalInfo info;
    auto grid = ReadGmshV22ToVtk(fileName, info);
    if (!grid)
    {
        std::cerr << "[BoundaryConditions] ReadGmshV22ToVtk failed: "
                  << fileName << std::endl;
        return false;
    }

    m_gmshInfo = info;
    m_private->m_fullGrid = grid;

    // ---- 1. 创建 / 重建 cell 颜色数组 ----
    vtkIdType nCells = m_private->m_fullGrid->GetNumberOfCells();

    m_private->m_cellColors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    m_private->m_cellColors->SetName("cell_colors");
    m_private->m_cellColors->SetNumberOfComponents(3);  // RGB
    m_private->m_cellColors->SetNumberOfTuples(nCells);

    unsigned char baseColor[3] = { 200, 200, 200 };  // 默认灰色
    for (vtkIdType i = 0; i < nCells; ++i)
        m_private->m_cellColors->SetTypedTuple(i, baseColor);

    m_private->m_fullGrid->GetCellData()->SetScalars(m_private->m_cellColors);

    // ---- 2. mapper + actor（只用一个 actor） ----
    if (!m_private->m_meshMapper)
        m_private->m_meshMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    m_private->m_meshMapper->SetInputData(m_private->m_fullGrid);
    m_private->m_meshMapper->SetScalarModeToUseCellData();
    m_private->m_meshMapper->SetColorModeToDirectScalars();
    m_private->m_meshMapper->ScalarVisibilityOn();

    if (!m_private->m_meshActor)
    {
        m_private->m_meshActor = vtkSmartPointer<vtkActor>::New();
        m_private->m_meshActor->SetMapper(m_private->m_meshMapper);
        m_private->m_meshActor->GetProperty()->SetEdgeVisibility(1);
        m_private->m_meshActor->GetProperty()->SetOpacity(0.9);
        // 注意：addActor 由上层负责
    }

    // ---- 3. 构建索引 & 父子关系 ----
    buildDimPhysIndex();
    buildHierarchy();

    // 清空所有状态
    m_selectedGroups.clear();
    m_previewGroup       = std::make_pair(-1, -1);
    m_previewRemoveGroup = std::make_pair(-1, -1);
    m_currentSelection.clear();

    // 同步颜色
    updateSelectionColors();

    return true;
}

// ========== 设置 interactor & 拾取开关 ==========

void BoundaryConditions::setInteractor(vtkRenderWindowInteractor* iren)
{
    m_private->m_interactor = iren;
    if (!m_private->m_interactor)
        return;

    if (!m_private->m_pickCallback)
    {
        m_private->m_pickCallback = vtkSmartPointer<vtkCallbackCommand>::New();
        m_private->m_pickCallback->SetCallback(&BoundaryConditions::OnLeftButtonPress);
        m_private->m_pickCallback->SetClientData(this);
    }

    m_private->m_interactor->AddObserver(
        vtkCommand::LeftButtonPressEvent, m_private->m_pickCallback);
}

void BoundaryConditions::enablePicking(bool on)
{
    m_private->m_pickingEnabled = on;
}

void BoundaryConditions::setPickMode(BCSelectMode mode)
{
    m_pickMode = mode;
}

// ========== 构建 dim+physId -> cellIds & nodeSet ==========

void BoundaryConditions::buildDimPhysIndex()
{
    m_private->m_dimPhysToCellIds.clear();
    m_private->m_dimPhysToNodeSet.clear();

    if (!m_private->m_fullGrid)
        return;

    auto dimArray = vtkIntArray::SafeDownCast(
        m_private->m_fullGrid->GetCellData()->GetArray("gmsh_dim"));
    auto physIdArray = vtkIntArray::SafeDownCast(
        m_private->m_fullGrid->GetCellData()->GetArray("gmsh_physical_id"));

    if (!dimArray || !physIdArray)
        return;

    vtkIdType nCells = m_private->m_fullGrid->GetNumberOfCells();
    vtkSmartPointer<vtkIdList> pts = vtkSmartPointer<vtkIdList>::New();

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        int dim  = dimArray->GetValue(cid);
        int pid  = physIdArray->GetValue(cid);

        std::uint64_t key =
            (static_cast<std::uint64_t>(dim) << 32) |
            static_cast<std::uint32_t>(pid);

        // cell ids
        auto& arr = m_private->m_dimPhysToCellIds[key];
        if (!arr)
        {
            arr = vtkSmartPointer<vtkIdTypeArray>::New();
            arr->SetNumberOfComponents(1);
        }
        arr->InsertNextValue(cid);

        // node set
        auto& nodeSet = m_private->m_dimPhysToNodeSet[key];
        m_private->m_fullGrid->GetCellPoints(cid, pts);
        for (vtkIdType i = 0; i < pts->GetNumberOfIds(); ++i)
        {
            vtkIdType nid = pts->GetId(i);
            nodeSet.insert(nid);
        }
    }
}

// ========== 构建父子关系：体->面，面->线 ==========

void BoundaryConditions::buildHierarchy()
{
    m_volumeToFaces.clear();
    m_faceToEdges.clear();

    // 分类 key
    std::vector<std::uint64_t> volumeKeys;
    std::vector<std::uint64_t> faceKeys;
    std::vector<std::uint64_t> edgeKeys;

    for (const auto& kv : m_private->m_dimPhysToNodeSet)
    {
        std::uint64_t key = kv.first;
        int dim = static_cast<int>(key >> 32);
        if (dim == 3)      volumeKeys.push_back(key);
        else if (dim == 2) faceKeys.push_back(key);
        else if (dim == 1) edgeKeys.push_back(key);
    }

    // volume -> faces
    for (auto vKey : volumeKeys)
    {
        int vPid = static_cast<int>(vKey & 0xffffffffu);
        const auto& vNodes = m_private->m_dimPhysToNodeSet[vKey];

        std::vector<int>& faces = m_volumeToFaces[vPid];

        for (auto fKey : faceKeys)
        {
            int fPid = static_cast<int>(fKey & 0xffffffffu);
            const auto& fNodes = m_private->m_dimPhysToNodeSet[fKey];

            bool subset = true;
            for (auto nid : fNodes)
            {
                if (vNodes.find(nid) == vNodes.end())
                {
                    subset = false;
                    break;
                }
            }
            if (subset)
                faces.push_back(fPid);
        }
    }

    // face -> edges
    for (auto fKey : faceKeys)
    {
        int fPid = static_cast<int>(fKey & 0xffffffffu);
        const auto& fNodes = m_private->m_dimPhysToNodeSet[fKey];

        std::vector<int>& edges = m_faceToEdges[fPid];

        for (auto eKey : edgeKeys)
        {
            int ePid = static_cast<int>(eKey & 0xffffffffu);
            const auto& eNodes = m_private->m_dimPhysToNodeSet[eKey];

            bool subset = true;
            for (auto nid : eNodes)
            {
                if (fNodes.find(nid) == fNodes.end())
                {
                    subset = false;
                    break;
                }
            }
            if (subset)
                edges.push_back(ePid);
        }
    }
}

// ========== 重置颜色 ==========

void BoundaryConditions::resetCellColors()
{
    if (!m_private->m_fullGrid || !m_private->m_cellColors)
        return;

    vtkIdType nCells = m_private->m_fullGrid->GetNumberOfCells();
    unsigned char baseColor[3] = { 200, 200, 200 };

    for (vtkIdType i = 0; i < nCells; ++i)
        m_private->m_cellColors->SetTypedTuple(i, baseColor);

    m_private->m_cellColors->Modified();
}

// ========== 高亮某个物理组（只改颜色） ==========

void BoundaryConditions::highlightDimPhys(int dim, int physId,
                                          const unsigned char color[3])
{
    if (!m_private->m_fullGrid || !m_private->m_cellColors)
        return;

    std::uint64_t key =
        (static_cast<std::uint64_t>(dim) << 32) |
        static_cast<std::uint32_t>(physId);

    auto it = m_private->m_dimPhysToCellIds.find(key);
    if (it == m_private->m_dimPhysToCellIds.end() || !it->second)
        return;

    auto ids = it->second;
    vtkIdType n = ids->GetNumberOfTuples();
    for (vtkIdType i = 0; i < n; ++i)
    {
        vtkIdType cid = ids->GetValue(i);
        if (cid >= 0 && cid < m_private->m_cellColors->GetNumberOfTuples())
            m_private->m_cellColors->SetTypedTuple(cid, color);
    }

    m_private->m_cellColors->Modified();
}

// ========== 统一刷新：已选 + 预选 + 预删 ==========

void BoundaryConditions::updateSelectionColors()
{
    if (!m_private->m_fullGrid || !m_private->m_cellColors)
        return;

    vtkIdType nCells = m_private->m_fullGrid->GetNumberOfCells();
    if (nCells <= 0)
        return;

    const unsigned char baseColor[3]      = { 200, 200, 200 }; // 默认灰
    const unsigned char selectedColor[3]  = {   0, 128, 255 }; // 已选：蓝
    const unsigned char previewColor[3]   = { 255, 255,   0 }; // 预选：黄
    const unsigned char previewDelColor[3]= { 255,   0,   0 }; // 预删：红

    // 1) 全部灰
    for (vtkIdType cid = 0; cid < nCells; ++cid)
        m_private->m_cellColors->SetTypedTuple(cid, baseColor);

    // 2) 已选：蓝
    for (const auto &kv : m_selectedGroups)
    {
        if (!kv.second)
            continue;
        int dim    = kv.first.first;
        int physId = kv.first.second;

        auto cells = getCellsOfGroup(dim, physId);
        for (vtkIdType cid : cells)
        {
            if (cid >= 0 && cid < nCells)
                m_private->m_cellColors->SetTypedTuple(cid, selectedColor);
        }
    }

    // 3) 预选：黄（覆盖已选）
    int pDim    = m_previewGroup.first;
    int pPhysId = m_previewGroup.second;
    if (pDim >= 0 && pPhysId > 0)
    {
        auto cells = getCellsOfGroup(pDim, pPhysId);
        for (vtkIdType cid : cells)
        {
            if (cid >= 0 && cid < nCells)
                m_private->m_cellColors->SetTypedTuple(cid, previewColor);
        }
    }

    // 4) 预删：红（最高优先级）
    int pdDim    = m_previewRemoveGroup.first;
    int pdPhysId = m_previewRemoveGroup.second;
    if (pdDim >= 0 && pdPhysId > 0)
    {
        auto cells = getCellsOfGroup(pdDim, pdPhysId);
        for (vtkIdType cid : cells)
        {
            if (cid >= 0 && cid < nCells)
                m_private->m_cellColors->SetTypedTuple(cid, previewDelColor);
        }
    }

    m_private->m_cellColors->Modified();
}

// ========== 通用：预选 / 预删 / 添加 / 删除 ==========

void BoundaryConditions::previewSelection(BCSelectMode mode, int physId)
{
    int dim = static_cast<int>(mode);
    if (physId <= 0)
        m_previewGroup = std::make_pair(-1, -1);
    else
        m_previewGroup = std::make_pair(dim, physId);

    updateSelectionColors();
}

void BoundaryConditions::previewRemoveSelection(BCSelectMode mode, int physId)
{
    int dim = static_cast<int>(mode);
    if (physId <= 0)
        m_previewRemoveGroup = std::make_pair(-1, -1);
    else
        m_previewRemoveGroup = std::make_pair(dim, physId);

    updateSelectionColors();
}

void BoundaryConditions::addSelection(BCSelectMode mode, int physId)
{
    if (physId <= 0)
        return;

    int dim = static_cast<int>(mode);
    std::pair<int,int> key(dim, physId);

    m_selectedGroups[key] = true;

    if (m_previewGroup == key)
        m_previewGroup = std::make_pair(-1, -1);
    if (m_previewRemoveGroup == key)
        m_previewRemoveGroup = std::make_pair(-1, -1);

    updateSelectionColors();
}

void BoundaryConditions::removeSelection(BCSelectMode mode, int physId)
{
    if (physId <= 0)
        return;

    int dim = static_cast<int>(mode);
    std::pair<int,int> key(dim, physId);

    auto it = m_selectedGroups.find(key);
    if (it != m_selectedGroups.end())
        m_selectedGroups.erase(it);

    if (m_previewGroup == key)
        m_previewGroup = std::make_pair(-1, -1);
    if (m_previewRemoveGroup == key)
        m_previewRemoveGroup = std::make_pair(-1, -1);

    updateSelectionColors();
}

// ========== 获取 / 清空 已选 集合 ==========

std::vector<BCSelectedGroup> BoundaryConditions::getAllSelections() const
{
    std::vector<BCSelectedGroup> result;

    for (const auto &kv : m_selectedGroups)
    {
        if (!kv.second)
            continue;

        int dim    = kv.first.first;
        int physId = kv.first.second;

        BCSelectedGroup g;
        g.dim    = dim;
        g.physId = physId;

        auto itName = m_gmshInfo.physicalNameMap.find({dim, physId});
        if (itName != m_gmshInfo.physicalNameMap.end())
            g.name = itName->second;

        auto cells = getCellsOfGroup(dim, physId);
        g.cellIds.assign(cells.begin(), cells.end());

        result.push_back(std::move(g));
    }

    return result;
}

std::vector<BCSelectedGroup> BoundaryConditions::takeSelections()
{
    auto result = getAllSelections();

    m_selectedGroups.clear();
    m_previewGroup       = std::make_pair(-1, -1);
    m_previewRemoveGroup = std::make_pair(-1, -1);

    updateSelectionColors();

    return result;
}

// ========== （兼容用）一次性 applySelection / takeSelectionOnce ==========

void BoundaryConditions::applySelection(BCSelectMode mode, int physId)
{
    std::vector<int> v(1, physId);
    applySelection(mode, v);
}

void BoundaryConditions::applySelection(BCSelectMode mode,
                                        const std::vector<int>& physIds)
{
    if (!m_private->m_fullGrid || !m_private->m_cellColors)
        return;

    // 这一套逻辑是“一次性”高亮 + 结果，不影响已选/预选/预删
    resetCellColors();
    m_currentSelection.clear();

    int dim = static_cast<int>(mode); // 0/1/2/3
    unsigned char pickColor[3] = { 255, 0, 0 }; // 红色

    for (int pid : physIds)
    {
        // 高亮
        highlightDimPhys(dim, pid, pickColor);

        // 记录结果
        BCSelectedGroup group;
        group.dim    = dim;
        group.physId = pid;

        auto itName = m_gmshInfo.physicalNameMap.find({dim, pid});
        if (itName != m_gmshInfo.physicalNameMap.end())
            group.name = itName->second;

        std::uint64_t key =
            (static_cast<std::uint64_t>(dim) << 32) |
            static_cast<std::uint32_t>(pid);

        auto it = m_private->m_dimPhysToCellIds.find(key);
        if (it != m_private->m_dimPhysToCellIds.end() && it->second)
        {
            auto ids = it->second;
            vtkIdType n = ids->GetNumberOfTuples();
            group.cellIds.reserve(static_cast<size_t>(n));
            for (vtkIdType i = 0; i < n; ++i)
                group.cellIds.push_back(ids->GetValue(i));
        }

        m_currentSelection.push_back(std::move(group));
    }
}

std::vector<BCSelectedGroup> BoundaryConditions::takeSelectionOnce()
{
    std::vector<BCSelectedGroup> result;
    result.swap(m_currentSelection);
    return result;
}

// ========== 按维度获取物理组列表 ==========

std::vector<PhysGroupInfo> BoundaryConditions::getGroupsForDim(int dim) const
{
    std::vector<PhysGroupInfo> result;
    for (const auto& kv : m_gmshInfo.physicalNameMap)
    {
        int d    = kv.first.first;
        int pid  = kv.first.second;
        if (d != dim) continue;

        PhysGroupInfo info;
        info.dim    = d;
        info.physId = pid;
        info.name   = kv.second;
        result.push_back(info);
    }
    std::sort(result.begin(), result.end(),
              [](const PhysGroupInfo& a, const PhysGroupInfo& b){
                  return a.physId < b.physId;
              });
    return result;
}

// ========== 父子关系查询 ==========

std::vector<int> BoundaryConditions::getFacesOfVolume(int volumePhysId) const
{
    std::vector<int> r;
    auto it = m_volumeToFaces.find(volumePhysId);
    if (it != m_volumeToFaces.end())
        r = it->second;
    return r;
}

std::vector<int> BoundaryConditions::getEdgesOfFace(int facePhysId) const
{
    std::vector<int> r;
    auto it = m_faceToEdges.find(facePhysId);
    if (it != m_faceToEdges.end())
        r = it->second;
    return r;
}

// ========== (dim, physId) -> cellIds ==========

std::vector<vtkIdType> BoundaryConditions::getCellsOfGroup(int dim, int physId) const
{
    std::vector<vtkIdType> r;
    std::uint64_t key =
        (static_cast<std::uint64_t>(dim) << 32) |
        static_cast<std::uint32_t>(physId);

    auto it = m_private->m_dimPhysToCellIds.find(key);
    if (it == m_private->m_dimPhysToCellIds.end() || !it->second)
        return r;

    auto ids = it->second;
    vtkIdType n = ids->GetNumberOfTuples();
    r.reserve(static_cast<size_t>(n));
    for (vtkIdType i = 0; i < n; ++i)
        r.push_back(ids->GetValue(i));
    return r;
}

// ========== static 回调 & handlePickEvent ==========

void BoundaryConditions::OnLeftButtonPress(vtkObject* caller,
                                           unsigned long eventId,
                                           void* clientData,
                                           void* callData)
{
    auto self = static_cast<BoundaryConditions*>(clientData);
    if (!self) return;
    self->handlePickEvent(caller, eventId, callData);
}

void BoundaryConditions::handlePickEvent(vtkObject* caller,
                                         unsigned long eventId,
                                         void* callData)
{
    if (!m_private->m_pickingEnabled)
        return;
    if (!m_private->m_fullGrid)
        return;

    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!iren)
        return;

    int* clickPos = iren->GetEventPosition();
    vtkRenderer* renderer =
        iren->GetRenderWindow()->GetRenderers()->GetFirstRenderer();
    if (!renderer)
        return;

    vtkSmartPointer<vtkCellPicker> picker =
        vtkSmartPointer<vtkCellPicker>::New();
    picker->SetTolerance(0.001);
    picker->Pick(clickPos[0], clickPos[1], 0, renderer);

    vtkIdType cellId = picker->GetCellId();
    if (cellId < 0)
        return;

    auto dimArray = vtkIntArray::SafeDownCast(
        m_private->m_fullGrid->GetCellData()->GetArray("gmsh_dim"));
    auto physIdArray = vtkIntArray::SafeDownCast(
        m_private->m_fullGrid->GetCellData()->GetArray("gmsh_physical_id"));
    auto physNameArray = vtkStringArray::SafeDownCast(
        m_private->m_fullGrid->GetCellData()->GetAbstractArray("gmsh_physical_name"));

    if (!dimArray || !physIdArray)
        return;

    int dim = dimArray->GetValue(cellId);
    int pid = physIdArray->GetValue(cellId);

    int targetDim = static_cast<int>(m_pickMode);
    if (dim != targetDim)
        return; // 当前模式只关心某一维

    std::string name;
    if (physNameArray)
        name = physNameArray->GetValue(cellId);

    std::cout << "[BC Pick] cellId=" << cellId
              << " dim=" << dim
              << " physId=" << pid
              << " name=" << name << std::endl;

    // 这里仍然使用“一次性”逻辑，不影响已选/预选/预删
    std::vector<int> ids(1, pid);
    applySelection(static_cast<BCSelectMode>(dim), ids);
}
