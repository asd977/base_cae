#include "GridEditingWidget.h"
#include <EventManager.h>

#include "ui_GridEditingWidget.h"

#include <ServiceRegistry.h>
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <services/VisualWidgetService.h>
#include <services/MeshModuleControllerService.h>
#include <services/UiFrameService.h>

#include "MeshGridIO.h"
#include "PickManager.h"

#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkCellType.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkSmartPointer.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QHash>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTimer>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>
#include <initializer_list>

namespace
{
static const QString kImportedMeshSurfacePropId = QStringLiteral("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QStringLiteral("mesh.imported.line");

static const QString kCategoryTopology = QStringLiteral("TOPOLOGY_TYPE");
constexpr int kLegacyTopologyAbnormal = 1099;
constexpr int kTopologyAbnormal = -1;
// 兼容前一版已经写入工程/全局筛选状态的临时分类编号；界面不再显示这些分类。
constexpr int kLegacyTopologyDegenerateHex5 = 1005;
constexpr int kLegacyTopologyDegenerateHex6 = 1006;
constexpr int kLegacyTopologyDegenerateHex7 = 1007;
static const QString kCategoryKetype = QStringLiteral("KETYPE");
static const QString kCategoryMatype = QStringLiteral("MATYPE");
static const QString kCategoryLayer = QStringLiteral("LAYER");

static const QString kGlobalFilterCustomKey = QStringLiteral("mesh.display.filter.custom");
static const QString kGlobalFilterTopologyKey = QStringLiteral("mesh.display.filter.topology.checked");
static const QString kGlobalFilterKetypeKey = QStringLiteral("mesh.display.filter.ketype.checked");
static const QString kGlobalFilterMatypeKey = QStringLiteral("mesh.display.filter.matype.checked");
static const QString kGlobalFilterLayerKey = QStringLiteral("mesh.display.filter.layer.checked");
static const QString kGlobalFilterProjectKey = QStringLiteral("mesh.display.filter.project");
static const QString kGlobalColorModeKey = QStringLiteral("mesh.display.color.mode");

// 后处理使用独立的结果筛选状态。结果生成文件自身携带 MATYPE/LAYER，
// 因此进入后处理后这里只展示/修改结果域，不再修改 Mesh 的显示筛选。
static const QString kPostFilterCustomKey = QStringLiteral("post.result.filter.custom");
static const QString kPostFilterMatypeKey = QStringLiteral("post.result.filter.matype.checked");
static const QString kPostFilterLayerKey = QStringLiteral("post.result.filter.layer.checked");
static const QString kPostFilterMatypeCountsKey = QStringLiteral("post.result.filter.matype.counts");
static const QString kPostFilterLayerCountsKey = QStringLiteral("post.result.filter.layer.counts");
static const QString kPostFilterProjectKey = QStringLiteral("post.result.filter.project");
static const QString kPostFilterChangedEvent = QStringLiteral("post.result.filter.changed");
static const QString kPostFilterSourceChangedEvent = QStringLiteral("post.result.filter.source.changed");

static GridEditingWidget::ColorMode globalColorMode()
{
    const int value = PublicVariables::instance().get(kGlobalColorModeKey).toInt();
    if (value < static_cast<int>(GridEditingWidget::ColorMode::None) ||
        value > static_cast<int>(GridEditingWidget::ColorMode::Topology))
    {
        return GridEditingWidget::ColorMode::None;
    }
    return static_cast<GridEditingWidget::ColorMode>(value);
}

constexpr int kRoleCategory = Qt::UserRole + 1;
constexpr int kRoleValue = Qt::UserRole + 2;
constexpr int kRoleIsGroup = Qt::UserRole + 3;

static int canonicalTopologyFromLegacyPattern(int pattern)
{
    if (pattern < 0)
        return VTK_EMPTY_CELL;

    int patternSlots[8] = {};
    for (int i = 0; i < 8; ++i)
        patternSlots[i] = (pattern >> (i * 3)) & 0x7;

    auto equals = [&](std::initializer_list<int> expected) -> bool
    {
        if (static_cast<int>(expected.size()) != 8)
            return false;
        int i = 0;
        for (int value : expected)
        {
            if (patternSlots[i++] != value)
                return false;
        }
        return true;
    };

    if (equals({0, 1, 2, 2, 3, 3, 3, 3}))
        return VTK_TETRA;
    if (equals({0, 1, 2, 3, 4, 4, 4, 4}))
        return VTK_PYRAMID;
    if (equals({0, 1, 2, 2, 3, 4, 5, 5}))
        return VTK_WEDGE;
    if (equals({0, 1, 2, 3, 4, 5, 6, 7}))
        return VTK_HEXAHEDRON;
    return VTK_EMPTY_CELL;
}

static int tuple1i(vtkDataArray* array, vtkIdType tupleId, int defaultValue = 0)
{
    if (!array || tupleId < 0 || tupleId >= array->GetNumberOfTuples())
        return defaultValue;
    return static_cast<int>(array->GetTuple1(tupleId));
}

static QColor categoricalColor(int value)
{
    if (value == 0)
        return QColor::fromRgbF(0.78, 0.78, 0.78);

    const int hue = ((value * 47) % 360 + 360) % 360;
    return QColor::fromHsv(hue, 170, 220);
}

static QSet<int> mapKeysToSet(const QMap<int, int>& map)
{
    QSet<int> values;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        values.insert(it.key());
    return values;
}

static bool setEquals(const QSet<int>& a, const QSet<int>& b)
{
    if (a.size() != b.size())
        return false;

    for (int value : a)
    {
        if (!b.contains(value))
            return false;
    }
    return true;
}

static QString intSetToString(const QSet<int>& values)
{
    QList<int> list = values.values();
    std::sort(list.begin(), list.end());

    QStringList parts;
    for (int value : list)
        parts << QString::number(value);
    return parts.join(QStringLiteral(","));
}

static QSet<int> intSetFromString(const QString& text)
{
    QSet<int> values;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok)
            values.insert(value);
    }
    return values;
}

static bool globalFilterIsCustom()
{
    if (!PublicVariables::instance().get(kGlobalFilterCustomKey).toBool())
        return false;

    const QString storedProject =
        PublicVariables::instance().get(kGlobalFilterProjectKey).toString().trimmed();
    const QString currentProject =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString().trimmed();
    return storedProject.isEmpty() || currentProject.isEmpty() || storedProject == currentProject;
}

static QSet<int> globalCheckedValues(const QString& category)
{
    QString key;
    if (category == kCategoryTopology)
        key = kGlobalFilterTopologyKey;
    else if (category == kCategoryKetype)
        key = kGlobalFilterKetypeKey;
    else if (category == kCategoryMatype)
        key = kGlobalFilterMatypeKey;
    else if (category == kCategoryLayer)
        key = kGlobalFilterLayerKey;

    QSet<int> values = intSetFromString(PublicVariables::instance().get(key).toString());
    if (category == kCategoryTopology)
    {
        const bool hadLegacyDegenerateHex =
            values.remove(kLegacyTopologyDegenerateHex5) ||
            values.remove(kLegacyTopologyDegenerateHex6) ||
            values.remove(kLegacyTopologyDegenerateHex7);
        // 前一版把合法退化 Hex 拆成 1005/1006/1007；现在统一并回“六面体”。
        if (hadLegacyDegenerateHex)
            values.insert(VTK_HEXAHEDRON);

               // 1099 曾被临时用作“异常拓扑”的内部哨兵，容易和真实单元编号混淆。
               // 新版改为负值，并兼容迁移旧的全局筛选状态。
        if (values.remove(kLegacyTopologyAbnormal))
            values.insert(kTopologyAbnormal);
    }
    return values;
}


static bool postResultFilterActive()
{
    return PublicVariables::instance()
        .get(QStringLiteral("post.display.view.active"))
        .toBool();
}

static QMap<int, int> postResultFilterCounts(const QString& key)
{
    QMap<int, int> counts;
    const QVariantMap values = PublicVariables::instance().get(key).toMap();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
        bool ok = false;
        const int value = it.key().toInt(&ok);
        if (ok)
            counts.insert(value, it.value().toInt());
    }
    return counts;
}

static QSet<int> postResultCheckedValues(const QString& category)
{
    QString key;
    if (category == kCategoryMatype)
        key = kPostFilterMatypeKey;
    else if (category == kCategoryLayer)
        key = kPostFilterLayerKey;
    return key.isEmpty() ? QSet<int>{}
                         : intSetFromString(PublicVariables::instance().get(key).toString());
}

static void setGlobalValue(const QString& key, const QVariant& value)
{
    PublicVariables::instance().set(key, value, QStringLiteral("project_dir"));
}

static vtkSmartPointer<vtkUnstructuredGrid> copyWithFullCellIds(vtkUnstructuredGrid* grid)
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!grid)
        return out;

    out->ShallowCopy(grid);

           // 关键：筛选抽取后 cellId/pointId 会变成本地编号。
           // PickManager 需要通过 vtkOriginalCellIds / vtkOriginalPointIds 回到完整网格编号。
    auto fullCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullCellIds->SetName("vtkOriginalCellIds");
    fullCellIds->SetNumberOfComponents(1);
    fullCellIds->SetNumberOfTuples(grid->GetNumberOfCells());
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
        fullCellIds->SetValue(cid, cid);

    if (auto* cd = out->GetCellData())
    {
        cd->RemoveArray("vtkOriginalCellIds");
        cd->AddArray(fullCellIds);
    }

    auto fullPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullPointIds->SetName("vtkOriginalPointIds");
    fullPointIds->SetNumberOfComponents(1);
    fullPointIds->SetNumberOfTuples(grid->GetNumberOfPoints());
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
        fullPointIds->SetValue(pid, pid);

    if (auto* pd = out->GetPointData())
    {
        pd->RemoveArray("vtkOriginalPointIds");
        pd->AddArray(fullPointIds);
    }

    return out;
}

static int viewCellDimension(vtkUnstructuredGrid* grid, vtkIdType cellId)
{
    vtkCell* cell = (grid && cellId >= 0 && cellId < grid->GetNumberOfCells())
    ? grid->GetCell(cellId)
    : nullptr;
    return cell ? cell->GetCellDimension() : -1;
}

static vtkIdType originalCellIdOf(vtkDataSet* grid, vtkIdType cellId)
{
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return cellId;

    auto* arr = grid->GetCellData() ? grid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
    if (arr && cellId < arr->GetNumberOfTuples())
        return static_cast<vtkIdType>(arr->GetTuple1(cellId));
    return cellId;
}

static vtkIdType originalPointIdOf(vtkDataSet* grid, vtkIdType pointId)
{
    if (!grid || pointId < 0 || pointId >= grid->GetNumberOfPoints())
        return pointId;

    auto* arr = grid->GetPointData() ? grid->GetPointData()->GetArray("vtkOriginalPointIds") : nullptr;
    if (arr && pointId < arr->GetNumberOfTuples())
        return static_cast<vtkIdType>(arr->GetTuple1(pointId));
    return pointId;
}

static int cellIntValue(vtkDataSet* grid, vtkIdType cellId, const char* arrayName)
{
    auto* arr = (grid && grid->GetCellData()) ? grid->GetCellData()->GetArray(arrayName) : nullptr;
    if (!arr || cellId < 0 || cellId >= arr->GetNumberOfTuples())
        return 0;
    return static_cast<int>(arr->GetTuple1(cellId));
}

static QByteArray facePointKey(vtkIdList* pointIds)
{
    QVector<vtkIdType> ids;
    if (pointIds)
    {
        ids.reserve(static_cast<int>(pointIds->GetNumberOfIds()));
        for (vtkIdType i = 0; i < pointIds->GetNumberOfIds(); ++i)
            ids.push_back(pointIds->GetId(i));
    }

    std::sort(ids.begin(), ids.end());

    QByteArray key;
    key.reserve(ids.size() * int(sizeof(vtkIdType)));
    for (vtkIdType id : ids)
        key.append(reinterpret_cast<const char*>(&id), int(sizeof(vtkIdType)));
    return key;
}

struct ViewFaceRef
{
    vtkIdType cellId = -1;          // 筛选后 surfaceSource 的本地 cell id
    vtkIdType originalCellId = -1;  // 完整网格 cell id
    int originalFaceId = 15;        // 3D 单元局部面 id；2D 单元整体面固定为 15
    int matype = 0;
    int layer = 0;
    QVector<vtkIdType> pointIds;    // surfaceSource 的本地点 id，用于 polydata 组面
};

static bool differentVisibleDomain(const ViewFaceRef& a, const ViewFaceRef& b,
                                   bool hasMatype, bool hasLayer)
{
    if (hasLayer && a.layer != b.layer)
        return true;
    if (hasMatype && a.matype != b.matype)
        return true;
    return false;
}

static void addViewSurfaceFace(vtkPolyData* out,
                               vtkCellArray* polys,
                               vtkIdTypeArray* originalCellIds,
                               vtkIdTypeArray* originalFaceIds,
                               vtkCellData* sourceCellData,
                               const ViewFaceRef& face)
{
    if (!out || !polys || !originalCellIds || !originalFaceIds || face.pointIds.size() < 3)
        return;

    auto ids = vtkSmartPointer<vtkIdList>::New();
    ids->SetNumberOfIds(face.pointIds.size());
    for (int i = 0; i < face.pointIds.size(); ++i)
        ids->SetId(i, face.pointIds[i]);

    const vtkIdType newCellId = polys->GetNumberOfCells();
    polys->InsertNextCell(ids);

    if (sourceCellData && out->GetCellData())
        out->GetCellData()->CopyData(sourceCellData, face.cellId, newCellId);

    originalCellIds->InsertNextValue(face.originalCellId);
    originalFaceIds->InsertNextValue(face.originalFaceId);
}

static vtkSmartPointer<vtkPolyData> buildMappedViewSurface(vtkUnstructuredGrid* grid)
{
    auto out = vtkSmartPointer<vtkPolyData>::New();
    if (!grid || grid->GetNumberOfCells() <= 0 || !grid->GetPoints())
        return out;

    out->SetPoints(grid->GetPoints());

    auto polys = vtkSmartPointer<vtkCellArray>::New();
    out->SetPolys(polys);

    vtkCellData* srcCellData = grid->GetCellData();
    if (srcCellData)
        out->GetCellData()->CopyAllocate(srcCellData, grid->GetNumberOfCells());

    auto originalPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalPointIds->SetName("vtkOriginalPointIds");
    originalPointIds->SetNumberOfComponents(1);
    originalPointIds->SetNumberOfTuples(grid->GetNumberOfPoints());
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
        originalPointIds->SetValue(pid, originalPointIdOf(grid, pid));

    auto originalCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalCellIds->SetName("vtkOriginalCellIds");
    originalCellIds->SetNumberOfComponents(1);

    auto originalFaceIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalFaceIds->SetName("vtkOriginalFaceIds");
    originalFaceIds->SetNumberOfComponents(1);

    const bool hasMatype = srcCellData && srcCellData->GetArray("MATYPE");
    const bool hasLayer = srcCellData && srcCellData->GetArray("LAYER");

    QHash<QByteArray, QVector<ViewFaceRef>> faceMap;
    faceMap.reserve(static_cast<int>(grid->GetNumberOfCells() * 3));

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const int dim = viewCellDimension(grid, cid);
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;

        ViewFaceRef ref;
        ref.cellId = cid;
        ref.originalCellId = originalCellIdOf(grid, cid);
        ref.matype = cellIntValue(grid, cid, "MATYPE");
        ref.layer = cellIntValue(grid, cid, "LAYER");

        if (dim == 2)
        {
            ref.originalFaceId = 15;
            vtkIdList* pids = cell->GetPointIds();
            if (!pids || pids->GetNumberOfIds() < 3)
                continue;

            ref.pointIds.reserve(static_cast<int>(pids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                ref.pointIds.push_back(pids->GetId(i));

            addViewSurfaceFace(out, polys, originalCellIds, originalFaceIds, srcCellData, ref);
            continue;
        }

        if (dim != 3)
            continue;

        const int nFaces = cell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* face = cell->GetFace(fi);
            if (!face)
                continue;

            vtkIdList* pids = face->GetPointIds();
            if (!pids || pids->GetNumberOfIds() < 3)
                continue;

            ViewFaceRef one = ref;
            one.originalFaceId = fi;
            one.pointIds.reserve(static_cast<int>(pids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                one.pointIds.push_back(pids->GetId(i));

            faceMap[facePointKey(pids)].push_back(one);
        }
    }

    for (auto it = faceMap.constBegin(); it != faceMap.constEnd(); ++it)
    {
        const QVector<ViewFaceRef>& owners = it.value();
        if (owners.isEmpty())
            continue;

        if (owners.size() == 1)
        {
            addViewSurfaceFace(out, polys, originalCellIds, originalFaceIds, srcCellData, owners[0]);
            continue;
        }

               // 筛选视图里相邻单元同属同一材料/层时，内部面不显示；
               // 材料/分层不同的内部界面保留，和 MeshModuleController 主显示逻辑一致。
        for (int i = 0; i < owners.size(); ++i)
        {
            bool visibleInterface = false;
            for (int j = 0; j < owners.size(); ++j)
            {
                if (i == j)
                    continue;
                if (differentVisibleDomain(owners[i], owners[j], hasMatype, hasLayer))
                {
                    visibleInterface = true;
                    break;
                }
            }

            if (visibleInterface)
                addViewSurfaceFace(out, polys, originalCellIds, originalFaceIds, srcCellData, owners[i]);
        }
    }

    if (out->GetPointData())
    {
        out->GetPointData()->RemoveArray("vtkOriginalPointIds");
        out->GetPointData()->AddArray(originalPointIds);
    }

    if (out->GetCellData())
    {
        out->GetCellData()->RemoveArray("vtkOriginalCellIds");
        out->GetCellData()->RemoveArray("vtkOriginalFaceIds");
        out->GetCellData()->AddArray(originalCellIds);
        out->GetCellData()->AddArray(originalFaceIds);
    }

    out->Modified();
    return out;
}
} // namespace

GridEditingWidget::GridEditingWidget(QWidget* parent)
    : BasePage("GridEditingWidget"), ui(new Ui::GridEditingWidget)
{
    Q_UNUSED(parent);

    ui->setupUi(this);
    setProperty("visualControlPanel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (ui->comboBoxColorMode)
    {
        const ColorMode initialMode = globalColorMode();
        const QSignalBlocker blocker(ui->comboBoxColorMode);
        ui->comboBoxColorMode->clear();
        ui->comboBoxColorMode->addItem(QStringLiteral("默认颜色"), static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->addItem(QStringLiteral("按网格拓扑"), static_cast<int>(ColorMode::Topology));
        ui->comboBoxColorMode->addItem(QStringLiteral("按计算类型编号"), static_cast<int>(ColorMode::Ketype));
        ui->comboBoxColorMode->addItem(QStringLiteral("按材料类型"), static_cast<int>(ColorMode::Matype));
        ui->comboBoxColorMode->addItem(QStringLiteral("按填筑分层"), static_cast<int>(ColorMode::Layer));
        const int initialIndex = ui->comboBoxColorMode->findData(
            static_cast<int>(initialMode));
        ui->comboBoxColorMode->setCurrentIndex(initialIndex >= 0 ? initialIndex : 0);
        m_colorMode = initialMode;
    }


    if (ui->checkBoxAutoApply)
    {
        // 默认即时应用；大型网格仍可关闭此选项后手动应用。
        ui->checkBoxAutoApply->setChecked(true);
        ui->checkBoxAutoApply->setToolTip(QStringLiteral("勾选后，筛选项变化会立即更新显示。"));
    }

    if (ui->treeWidgetFilterValues)
    {
        ui->treeWidgetFilterValues->setColumnCount(2);
        ui->treeWidgetFilterValues->setHeaderLabels(QStringList()
                                                    << QStringLiteral("筛选项")
                                                    << QStringLiteral("数量"));
        ui->treeWidgetFilterValues->setRootIsDecorated(true);
        ui->treeWidgetFilterValues->setAlternatingRowColors(true);
        ui->treeWidgetFilterValues->setUniformRowHeights(true);
        ui->treeWidgetFilterValues->setTextElideMode(Qt::ElideRight);
        if (QHeaderView* header = ui->treeWidgetFilterValues->header())
        {
            header->setStretchLastSection(false);
            header->setMinimumSectionSize(48);
            header->setSectionResizeMode(0, QHeaderView::Stretch);
            header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        }
        connect(ui->treeWidgetFilterValues,
                &QTreeWidget::itemChanged,
                this,
                &GridEditingWidget::onFilterTreeItemChanged);
    }

    EventManager::instance().registerHandler(
        QStringLiteral("post.display.view.active.changed"), this,
        [this](const QVariantMap&)
        {
            updateContextPresentation();
        });
    EventManager::instance().registerHandler(
        kPostFilterSourceChangedEvent, this,
        [this](const QVariantMap&)
        {
            if (postResultFilterActive())
                buildFilterTreeFromGrid();
        });
    updateContextPresentation();
}

GridEditingWidget::~GridEditingWidget()
{
    delete ui;
}

void GridEditingWidget::clearData()
{
    m_fullGrid = nullptr;
    m_viewSurface = nullptr;
    m_viewLineGrid = nullptr;
    m_sourceName.clear();

    m_allTopologies.clear();
    m_allKetypes.clear();
    m_allMatypes.clear();
    m_allLayers.clear();

    m_filterTopologies.clear();
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    m_colorMode = ColorMode::None;

    m_surfaceActor = nullptr;
    m_lineActor = nullptr;
    m_surfaceMapper = nullptr;
    m_lineMapper = nullptr;

    m_updatingFilterTree = false;
}

void GridEditingWidget::setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName)
{
    clearData();
    m_sourceName = sourceName;

           // 颜色模式属于网格的全局显示状态。重新进入页面时保持导入后或用户选择的模式。
    m_colorMode = globalColorMode();

    if (ui && ui->comboBoxColorMode)
    {
        const QSignalBlocker blocker(ui->comboBoxColorMode);
        const int idx = ui->comboBoxColorMode->findData(static_cast<int>(m_colorMode));
        ui->comboBoxColorMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        buildFilterTreeFromGrid();
        return;
    }

    m_fullGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_fullGrid->ShallowCopy(grid);

           // “KETYPE”是求解器计算单元类型，不等于真实网格拓扑。
           // MeshGridIO 会把能等价恢复的旧 8 槽位单元转成标准拓扑；
           // 其余合法闭合的 Hex 塌缩体在筛选语义上并回“六面体”，
           // 只有真正非法的塌缩记录才使用“异常”编号。
           // 只有字段不存在或尺寸不对时才根据实际 VTK CellType 补建。
    if (m_fullGrid->GetCellData())
    {
        auto* existing = vtkIntArray::SafeDownCast(
            m_fullGrid->GetCellData()->GetArray("TOPOLOGY_TYPE"));
        if (!existing || existing->GetNumberOfTuples() != m_fullGrid->GetNumberOfCells())
        {
            auto topology = vtkSmartPointer<vtkIntArray>::New();
            topology->SetName("TOPOLOGY_TYPE");
            topology->SetNumberOfComponents(1);
            topology->SetNumberOfTuples(m_fullGrid->GetNumberOfCells());
            for (vtkIdType cid = 0; cid < m_fullGrid->GetNumberOfCells(); ++cid)
                topology->SetValue(cid, m_fullGrid->GetCellType(cid));
            m_fullGrid->GetCellData()->RemoveArray("TOPOLOGY_TYPE");
            m_fullGrid->GetCellData()->AddArray(topology);
        }
        else
        {
            bool needsMigration = false;
            for (vtkIdType cid = 0; cid < existing->GetNumberOfTuples(); ++cid)
            {
                const int value = existing->GetValue(cid);
                if (value == kLegacyTopologyDegenerateHex5 ||
                    value == kLegacyTopologyDegenerateHex6 ||
                    value == kLegacyTopologyDegenerateHex7 ||
                    value == kLegacyTopologyAbnormal)
                {
                    needsMigration = true;
                    break;
                }
            }

            if (needsMigration)
            {
                auto topology = vtkSmartPointer<vtkIntArray>::New();
                topology->DeepCopy(existing);
                topology->SetName("TOPOLOGY_TYPE");
                for (vtkIdType cid = 0; cid < topology->GetNumberOfTuples(); ++cid)
                {
                    const int value = topology->GetValue(cid);
                    if (value == kLegacyTopologyDegenerateHex5 ||
                        value == kLegacyTopologyDegenerateHex6 ||
                        value == kLegacyTopologyDegenerateHex7)
                    {
                        topology->SetValue(cid, VTK_HEXAHEDRON);
                    }
                    else if (value == kLegacyTopologyAbnormal)
                    {
                        // 兼容上一版已经缓存到工程里的“异常=1099”。
                        // 优先根据原 IIIN 连接模式重新归类，避免 1099 号这类
                        // A B C C D E F F 的标准三棱柱继续被旧状态锁死为异常。
                        int migrated = VTK_EMPTY_CELL;
                        if (auto* patternArray =
                            m_fullGrid->GetCellData()->GetArray("IIIN_CONNECTIVITY_PATTERN"))
                        {
                            if (cid < patternArray->GetNumberOfTuples())
                            {
                                const int pattern = static_cast<int>(
                                    std::llround(patternArray->GetTuple1(cid)));
                                migrated = canonicalTopologyFromLegacyPattern(pattern);
                            }
                        }

                        if (migrated == VTK_EMPTY_CELL)
                        {
                            const int currentType = m_fullGrid->GetCellType(cid);
                            if (currentType == VTK_TETRA || currentType == VTK_PYRAMID ||
                                currentType == VTK_WEDGE || currentType == VTK_HEXAHEDRON)
                            {
                                migrated = currentType;
                            }
                        }
                        topology->SetValue(
                            cid, migrated != VTK_EMPTY_CELL ? migrated : kTopologyAbnormal);
                    }
                }
                m_fullGrid->GetCellData()->RemoveArray("TOPOLOGY_TYPE");
                m_fullGrid->GetCellData()->AddArray(topology);
            }
        }
    }

    buildFilterTreeFromGrid();
    refreshMeshActors();
}

void GridEditingWidget::clearGrid()
{
    clearData();

    if (ui && ui->treeWidgetFilterValues)
        ui->treeWidgetFilterValues->clear();

}

bool GridEditingWidget::cellMatchesFilter(vtkIdType cellId) const
{
    if (!m_fullGrid || cellId < 0 || cellId >= m_fullGrid->GetNumberOfCells())
        return false;

    auto* cd = m_fullGrid->GetCellData();
    if (!cd)
        return true;

    const int topology = tuple1i(cd->GetArray("TOPOLOGY_TYPE"), cellId,
                                 m_fullGrid->GetCellType(cellId));
    const int ketype = tuple1i(cd->GetArray("KETYPE"), cellId, 0);
    const int matype = tuple1i(cd->GetArray("MATYPE"), cellId, 0);
    const int layer = tuple1i(cd->GetArray("LAYER"), cellId, 0);

    auto allowed = [](const QSet<int>& allValues, const QSet<int>& checkedValues, int value) -> bool
    {
        if (allValues.isEmpty())
            return true;
        return checkedValues.contains(value);
    };

    return allowed(m_allTopologies, m_filterTopologies, topology) &&
           allowed(m_allKetypes, m_filterKetypes, ketype) &&
           allowed(m_allMatypes, m_filterMatypes, matype) &&
           allowed(m_allLayers, m_filterLayers, layer);
}

void GridEditingWidget::refreshMeshActors()
{
    m_surfaceActor = nullptr;
    m_lineActor = nullptr;
    m_surfaceMapper = nullptr;
    m_lineMapper = nullptr;

    if (!m_visualWidgetService)
        m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visualWidgetService)
        return;

    m_surfaceActor = vtkActor::SafeDownCast(m_visualWidgetService->getProp(kImportedMeshSurfacePropId));
    m_lineActor = vtkActor::SafeDownCast(m_visualWidgetService->getProp(kImportedMeshLinePropId));

    m_surfaceMapper = m_surfaceActor ? m_surfaceActor->GetMapper() : nullptr;
    m_lineMapper = m_lineActor ? m_lineActor->GetMapper() : nullptr;
}

vtkSmartPointer<vtkUnstructuredGrid> GridEditingWidget::extractCells(
    const vtkSmartPointer<vtkIdList>& ids) const
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!m_fullGrid || !ids || ids->GetNumberOfIds() <= 0)
        return out;

    auto source = copyWithFullCellIds(m_fullGrid);

    auto extractor = vtkSmartPointer<vtkExtractCells>::New();
    extractor->SetInputData(source);
    extractor->SetCellList(ids);
    extractor->Update();

    out->ShallowCopy(extractor->GetOutput());
    return out;
}

void GridEditingWidget::setMapperInput(vtkMapper* mapper, vtkDataSet* dataSet) const
{
    if (!mapper || !dataSet)
        return;

    if (auto* polyMapper = vtkPolyDataMapper::SafeDownCast(mapper))
    {
        if (auto* poly = vtkPolyData::SafeDownCast(dataSet))
            polyMapper->SetInputData(poly);
        return;
    }

    if (auto* dataSetMapper = vtkDataSetMapper::SafeDownCast(mapper))
        dataSetMapper->SetInputData(dataSet);
}

void GridEditingWidget::buildOrUpdateViewGrid()
{
    refreshMeshActors();

    if (!m_visualWidgetService || !m_fullGrid)
        return;

           // “全部隐藏”是高频操作，大网格没必要再扫描几十万个 Cell、抽取并重建表面。
           // 直接隐藏 Actor；重新勾选后再按当前条件重建即可。
    const bool definitelyEmpty =
        (!m_allTopologies.isEmpty() && m_filterTopologies.isEmpty()) ||
        (!m_allKetypes.isEmpty() && m_filterKetypes.isEmpty()) ||
        (!m_allMatypes.isEmpty() && m_filterMatypes.isEmpty()) ||
        (!m_allLayers.isEmpty() && m_filterLayers.isEmpty());
    if (definitelyEmpty)
    {
        m_visualWidgetService->setVisible(kImportedMeshSurfacePropId, false, false);
        m_visualWidgetService->setVisible(kImportedMeshLinePropId, false, false);
        m_visualWidgetService->render();
        return;
    }

    auto surfaceCellIds = vtkSmartPointer<vtkIdList>::New();
    auto lineCellIds = vtkSmartPointer<vtkIdList>::New();

    for (vtkIdType cellId = 0; cellId < m_fullGrid->GetNumberOfCells(); ++cellId)
    {
        // 先做纯数组筛选，只有命中的 Cell 才构造 vtkCell，减少大网格筛选时的对象开销。
        if (!cellMatchesFilter(cellId))
            continue;

        vtkCell* cell = m_fullGrid->GetCell(cellId);
        if (!cell)
            continue;

        const int dim = cell->GetCellDimension();
        if (dim >= 2)
            surfaceCellIds->InsertNextId(cellId);
        else if (dim == 1)
            lineCellIds->InsertNextId(cellId);
    }

    if (surfaceCellIds->GetNumberOfIds() > 0)
    {
        auto surfaceSource = extractCells(surfaceCellIds);
        // 不再用 vtkDataSetSurfaceFilter 直接抽壳。
        // 直接抽壳只保证 vtkOriginalCellIds，无法稳定提供 originalFaceIds，
        // 高阶单元/内部界面/筛选后局部点编号都会导致 PickManager 反推错面。
        m_viewSurface = buildMappedViewSurface(surfaceSource);
    }
    else
    {
        m_viewSurface = vtkSmartPointer<vtkPolyData>::New();
    }

    if (lineCellIds->GetNumberOfIds() > 0)
        m_viewLineGrid = extractCells(lineCellIds);
    else
        m_viewLineGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();

    if (m_surfaceMapper)
        setMapperInput(m_surfaceMapper, m_viewSurface);
    if (m_lineMapper)
        setMapperInput(m_lineMapper, m_viewLineGrid);

    const bool showSurface = m_viewSurface && m_viewSurface->GetNumberOfCells() > 0;
    const bool showLine = m_viewLineGrid && m_viewLineGrid->GetNumberOfCells() > 0;

    const bool postViewActive =
        PublicVariables::instance().get(QStringLiteral("post.display.view.active")).toBool();
    m_visualWidgetService->setVisible(kImportedMeshSurfacePropId,
                                      !postViewActive && showSurface, false);
    m_visualWidgetService->setVisible(kImportedMeshLinePropId,
                                      !postViewActive && showLine, false);

    applyColorMode();
    PickManager::instance().updateStructure();
    m_visualWidgetService->render();
}

QMap<int, int> GridEditingWidget::collectCellIntCounts(const char* arrayName) const
{
    QMap<int, int> counts;
    if (!m_fullGrid || !arrayName)
        return counts;

    auto* cd = m_fullGrid->GetCellData();
    auto* array = cd ? cd->GetArray(arrayName) : nullptr;
    if (!array)
        return counts;

    for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
    {
        const int value = static_cast<int>(array->GetTuple1(i));
        counts[value] = counts.value(value) + 1;
    }

    return counts;
}

QSet<int> GridEditingWidget::collectUniqueCellInts(vtkDataSet* dataSet, const char* arrayName) const
{
    QSet<int> values;
    if (!dataSet || !arrayName)
        return values;

    auto* cd = dataSet->GetCellData();
    auto* array = cd ? cd->GetArray(arrayName) : nullptr;
    if (!array)
        return values;

    for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
        values.insert(static_cast<int>(array->GetTuple1(i)));

    return values;
}

QSet<int> GridEditingWidget::collectVisibleCellInts(const char* arrayName) const
{
    QSet<int> values = collectUniqueCellInts(m_viewSurface, arrayName);
    values.unite(collectUniqueCellInts(m_viewLineGrid, arrayName));
    return values;
}

QString GridEditingWidget::displayTextForFilterValue(const QString& category, int value) const
{
    if (category == kCategoryTopology)
    {
        switch (value)
        {
        case VTK_LINE:
            return QStringLiteral("线单元");
        case VTK_TRIANGLE:
            return QStringLiteral("三角形");
        case VTK_QUAD:
            return QStringLiteral("四边形");
        case VTK_TETRA:
            return QStringLiteral("四面体");
        case VTK_HEXAHEDRON:
            return QStringLiteral("六面体");
        case VTK_WEDGE:
            return QStringLiteral("三棱柱");
        case VTK_PYRAMID:
            return QStringLiteral("金字塔");
        case VTK_POLYHEDRON:
            return QStringLiteral("多面体");
        case kTopologyAbnormal:
            return QStringLiteral("异常");
        default:
            return QStringLiteral("类型 %1").arg(value);
        }
    }

    if (category == kCategoryKetype)
    {
        // 当前求解器 KEATT_CON/KEATT_VAR 中：
        // KETYPE=1 为 2 节点线类计算单元；KETYPE=6 为 8 槽位三维实体计算单元。
        // 这里只标注已经明确的类型，未知编号仍保留原值，避免误命名。
        switch (value)
        {
        case 1:
            return QStringLiteral("1（线单元）");
        case 6:
            return QStringLiteral("6（三维实体单元）");
        default:
            return QStringLiteral("%1（计算类型）").arg(value);
        }
    }

    if (category == kCategoryMatype)
    {
        const MshPickMap pickMap = MeshGridIO::instance().getMeshMap();
        QString name = pickMap.physicalName(value, 3).trimmed();
        if (name.isEmpty())
            name = pickMap.physicalName(value).trimmed();
        if (!name.isEmpty())
            return QStringLiteral("%1 (%2)").arg(name).arg(value);
    }

    return QString::number(value);
}

static bool isDegenerateLegacyConnectivityPattern(int pattern)
{
    if (pattern < 0)
        return false;

    bool seen[8] = {false, false, false, false, false, false, false, false};
    int uniqueCount = 0;
    for (int slot = 0; slot < 8; ++slot)
    {
        const int local = (pattern >> (slot * 3)) & 0x7;
        if (!seen[local])
        {
            seen[local] = true;
            ++uniqueCount;
        }
    }
    return uniqueCount < 8;
}

void GridEditingWidget::buildFilterTreeFromGrid()
{
    if (!ui || !ui->treeWidgetFilterValues)
        return;

    const QSignalBlocker blocker(ui->treeWidgetFilterValues);
    m_updatingFilterTree = true;
    ui->treeWidgetFilterValues->clear();

    const bool postMode = postResultFilterActive();

    QMap<int, int> topologyCounts;
    QMap<int, int> ketypeCounts;
    QMap<int, int> matypeCounts;
    QMap<int, int> layerCounts;

    if (postMode)
    {
        // 后处理筛选项来自当前结果文件自身的 CellData，不再使用 Mesh 计数。
        matypeCounts = postResultFilterCounts(kPostFilterMatypeCountsKey);
        layerCounts = postResultFilterCounts(kPostFilterLayerCountsKey);
    }
    else
    {
        topologyCounts = collectCellIntCounts("TOPOLOGY_TYPE");
        ketypeCounts = collectCellIntCounts("KETYPE");
        matypeCounts = collectCellIntCounts("MATYPE");
        layerCounts = collectCellIntCounts("LAYER");
    }

    bool showTopologyFilter = false;
    if (!postMode && m_fullGrid && m_fullGrid->GetCellData())
    {
        auto* cd = m_fullGrid->GetCellData();
        auto* patternArray = cd->GetArray("IIIN_CONNECTIVITY_PATTERN");
        auto* ketypeArray = cd->GetArray("KETYPE");
        if (patternArray && ketypeArray)
        {
            const vtkIdType n = qMin<vtkIdType>(
                m_fullGrid->GetNumberOfCells(),
                qMin<vtkIdType>(patternArray->GetNumberOfTuples(),
                                ketypeArray->GetNumberOfTuples()));
            for (vtkIdType cid = 0; cid < n; ++cid)
            {
                const int pattern =
                    static_cast<int>(std::llround(patternArray->GetTuple1(cid)));
                const int ketype =
                    static_cast<int>(std::llround(ketypeArray->GetTuple1(cid)));
                if (ketype == 6 && isDegenerateLegacyConnectivityPattern(pattern))
                {
                    showTopologyFilter = true;
                    break;
                }
            }
        }
    }

    m_allTopologies = mapKeysToSet(topologyCounts);
    m_allKetypes = mapKeysToSet(ketypeCounts);
    m_allMatypes = mapKeysToSet(matypeCounts);
    m_allLayers = mapKeysToSet(layerCounts);

    if (postMode)
    {
        m_filterTopologies.clear();
        m_filterKetypes.clear();

        const QString storedProject =
            PublicVariables::instance().get(kPostFilterProjectKey).toString().trimmed();
        const QString currentProject =
            PublicVariables::instance()
                .get(PublicConstants::kProjectDirectoryKey)
                .toString()
                .trimmed();
        const bool validState = !storedProject.isEmpty() &&
                                storedProject == currentProject;

        if (validState)
        {
            m_filterMatypes = postResultCheckedValues(kCategoryMatype);
            m_filterLayers = postResultCheckedValues(kCategoryLayer);
        }
        else
        {
            m_filterMatypes = m_allMatypes;
            m_filterLayers = m_allLayers;
        }
    }
    else if (globalFilterIsCustom())
    {
        m_filterTopologies = globalCheckedValues(kCategoryTopology);
        if (m_filterTopologies.isEmpty() && !m_allTopologies.isEmpty() &&
            PublicVariables::instance()
                .get(kGlobalFilterTopologyKey)
                .toString()
                .trimmed()
                .isEmpty())
        {
            m_filterTopologies = m_allTopologies;
        }
        m_filterKetypes = globalCheckedValues(kCategoryKetype);
        m_filterMatypes = globalCheckedValues(kCategoryMatype);
        m_filterLayers = globalCheckedValues(kCategoryLayer);
    }
    else
    {
        m_filterTopologies = m_allTopologies;
        m_filterKetypes = m_allKetypes;
        m_filterMatypes = m_allMatypes;
        m_filterLayers = m_allLayers;
    }

    if (!postMode && !showTopologyFilter)
        m_filterTopologies = m_allTopologies;

    auto addGroup =
        [this](const QString& category, const QString& title,
               const QMap<int, int>& counts)
    {
        if (counts.isEmpty())
            return;

        auto* group = new QTreeWidgetItem(ui->treeWidgetFilterValues);
        group->setText(0, title);
        group->setText(1, QString::number(counts.size()));
        group->setData(0, kRoleCategory, category);
        group->setData(0, kRoleIsGroup, true);
        group->setFlags((group->flags() | Qt::ItemIsUserCheckable) &
                        ~Qt::ItemIsAutoTristate);

        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        {
            auto* child = new QTreeWidgetItem(group);
            child->setText(0, displayTextForFilterValue(category, it.key()));
            child->setText(1, QString::number(it.value()));
            child->setData(0, kRoleCategory, category);
            child->setData(0, kRoleValue, it.key());
            child->setData(0, kRoleIsGroup, false);
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            child->setToolTip(
                0, QStringLiteral("%1 = %2，结果单元数量：%3")
                       .arg(category)
                       .arg(it.key())
                       .arg(it.value()));

            bool checked = true;
            if (category == kCategoryTopology)
                checked = m_filterTopologies.contains(it.key());
            else if (category == kCategoryKetype)
                checked = m_filterKetypes.contains(it.key());
            else if (category == kCategoryMatype)
                checked = m_filterMatypes.contains(it.key());
            else if (category == kCategoryLayer)
                checked = m_filterLayers.contains(it.key());
            child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }

        updateParentCheckState(group);
    };

    if (!postMode)
    {
        if (showTopologyFilter)
            addGroup(kCategoryTopology, QStringLiteral("网格拓扑"), topologyCounts);
        addGroup(kCategoryKetype, QStringLiteral("计算类型编号"), ketypeCounts);
    }

    // 后处理模式只显示结果域：材料 + 分层。网格类型不参与联动。
    addGroup(kCategoryMatype, QStringLiteral("材料类型"), matypeCounts);
    addGroup(kCategoryLayer, QStringLiteral("填筑分层"), layerCounts);

    ui->treeWidgetFilterValues->expandAll();
    ui->treeWidgetFilterValues->resizeColumnToContents(0);

    m_updatingFilterTree = false;
}

void GridEditingWidget::setFilterTreeChecked(bool checked)
{
    if (!ui || !ui->treeWidgetFilterValues)
        return;

    const QSignalBlocker blocker(ui->treeWidgetFilterValues);
    m_updatingFilterTree = true;

    const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;

    for (int i = 0; i < ui->treeWidgetFilterValues->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* group = ui->treeWidgetFilterValues->topLevelItem(i);
        if (!group)
            continue;

        for (int c = 0; c < group->childCount(); ++c)
        {
            QTreeWidgetItem* child = group->child(c);
            if (child)
                child->setCheckState(0, state);
        }
        updateParentCheckState(group);
    }

    m_updatingFilterTree = false;
}

void GridEditingWidget::updateParentCheckState(QTreeWidgetItem* parentItem)
{
    if (!parentItem)
        return;

    int checkedCount = 0;
    int partialCount = 0;
    const int childCount = parentItem->childCount();

    for (int i = 0; i < childCount; ++i)
    {
        QTreeWidgetItem* child = parentItem->child(i);
        if (!child)
            continue;

        const Qt::CheckState state = child->checkState(0);
        if (state == Qt::Checked)
            ++checkedCount;
        else if (state == Qt::PartiallyChecked)
            ++partialCount;
    }

    if (childCount == 0 || (checkedCount == childCount && partialCount == 0))
        parentItem->setCheckState(0, Qt::Checked);
    else if (checkedCount == 0 && partialCount == 0)
        parentItem->setCheckState(0, Qt::Unchecked);
    else
        parentItem->setCheckState(0, Qt::PartiallyChecked);
}

QSet<int> GridEditingWidget::checkedValuesForCategory(const QString& category) const
{
    QSet<int> values;
    if (!ui || !ui->treeWidgetFilterValues)
        return values;

    bool found = false;
    for (int i = 0; i < ui->treeWidgetFilterValues->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* group = ui->treeWidgetFilterValues->topLevelItem(i);
        if (!group || group->data(0, kRoleCategory).toString() != category)
            continue;

        found = true;
        for (int c = 0; c < group->childCount(); ++c)
        {
            QTreeWidgetItem* child = group->child(c);
            if (child && child->checkState(0) == Qt::Checked)
                values.insert(child->data(0, kRoleValue).toInt());
        }
        break;
    }

           // 某一筛选组没有显示，不等于“全部取消”。尤其普通网格没有“网格拓扑”组时，
           // 必须把拓扑视为全选，否则执行一次“隐藏整体”后再勾选其它组会永远匹配不到 Cell。
    if (!found)
    {
        if (category == kCategoryTopology)
            return m_allTopologies;
        if (category == kCategoryKetype)
            return m_allKetypes;
        if (category == kCategoryMatype)
            return m_allMatypes;
        if (category == kCategoryLayer)
            return m_allLayers;
    }

    return values;
}

void GridEditingWidget::applyFilterFromTree()
{
    m_filterTopologies = checkedValuesForCategory(kCategoryTopology);
    m_filterKetypes = checkedValuesForCategory(kCategoryKetype);
    m_filterMatypes = checkedValuesForCategory(kCategoryMatype);
    m_filterLayers = checkedValuesForCategory(kCategoryLayer);

    if (postResultFilterActive())
    {
        const bool customFilter =
            !setEquals(m_allMatypes, m_filterMatypes) ||
            !setEquals(m_allLayers, m_filterLayers);

        // 后处理模式只写结果筛选状态，不修改 Mesh 的筛选，也不重建 Mesh ViewGrid。
        setGlobalValue(kPostFilterCustomKey, customFilter);
        setGlobalValue(kPostFilterMatypeKey, intSetToString(m_filterMatypes));
        setGlobalValue(kPostFilterLayerKey, intSetToString(m_filterLayers));
        setGlobalValue(kPostFilterProjectKey,
                       PublicVariables::instance()
                           .get(PublicConstants::kProjectDirectoryKey));

        QVariantMap filterEvent;
        filterEvent.insert(QStringLiteral("custom"), customFilter);
        filterEvent.insert(QStringLiteral("matype"), intSetToString(m_filterMatypes));
        filterEvent.insert(QStringLiteral("layer"), intSetToString(m_filterLayers));
        EventManager::instance().sendEvent(kPostFilterChangedEvent, filterEvent);
        return;
    }

    const bool customFilter =
        !setEquals(m_allTopologies, m_filterTopologies) ||
        !setEquals(m_allKetypes, m_filterKetypes) ||
        !setEquals(m_allMatypes, m_filterMatypes) ||
        !setEquals(m_allLayers, m_filterLayers);

    setGlobalValue(kGlobalFilterCustomKey, customFilter);
    setGlobalValue(kGlobalFilterTopologyKey, intSetToString(m_filterTopologies));
    setGlobalValue(kGlobalFilterKetypeKey, intSetToString(m_filterKetypes));
    setGlobalValue(kGlobalFilterMatypeKey, intSetToString(m_filterMatypes));
    setGlobalValue(kGlobalFilterLayerKey, intSetToString(m_filterLayers));
    setGlobalValue(kGlobalFilterProjectKey,
                   PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey));

    buildOrUpdateViewGrid();

    QVariantMap filterEvent;
    filterEvent.insert(QStringLiteral("custom"), customFilter);
    filterEvent.insert(QStringLiteral("topology"), intSetToString(m_filterTopologies));
    filterEvent.insert(QStringLiteral("ketype"), intSetToString(m_filterKetypes));
    filterEvent.insert(QStringLiteral("matype"), intSetToString(m_filterMatypes));
    filterEvent.insert(QStringLiteral("layer"), intSetToString(m_filterLayers));
    EventManager::instance().sendEvent(QStringLiteral("mesh.display.filter.changed"), filterEvent);
}

void GridEditingWidget::onFilterTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingFilterTree || !item || column != 0 || !ui || !ui->treeWidgetFilterValues)
        return;

    {
        const QSignalBlocker blocker(ui->treeWidgetFilterValues);
        m_updatingFilterTree = true;

        const bool isGroup = item->data(0, kRoleIsGroup).toBool();
        if (isGroup)
        {
            const Qt::CheckState state =
                (item->checkState(0) == Qt::Unchecked) ? Qt::Unchecked : Qt::Checked;

            item->setCheckState(0, state);
            for (int i = 0; i < item->childCount(); ++i)
            {
                QTreeWidgetItem* child = item->child(i);
                if (child)
                    child->setCheckState(0, state);
            }
        }
        else
        {
            updateParentCheckState(item->parent());
        }

        m_updatingFilterTree = false;
    }

           // 先只同步页面状态，不重建树、不改回用户刚点击的勾选状态。
    m_filterTopologies = checkedValuesForCategory(kCategoryTopology);
    m_filterKetypes = checkedValuesForCategory(kCategoryKetype);
    m_filterMatypes = checkedValuesForCategory(kCategoryMatype);
    m_filterLayers = checkedValuesForCategory(kCategoryLayer);

    if (ui->checkBoxAutoApply && ui->checkBoxAutoApply->isChecked())
    {
        // 大网格连续勾选时合并短时间内的多次重建请求，避免每一下都完整抽面。
        const int generation = property("meshFilterApplyGeneration").toInt() + 1;
        setProperty("meshFilterApplyGeneration", generation);
        QTimer::singleShot(60, this, [this, generation]()
                           {
                               if (property("meshFilterApplyGeneration").toInt() != generation)
                                   return;
                               applyFilterFromTree();
                           });
    }
}

vtkSmartPointer<vtkLookupTable> GridEditingWidget::buildLookupTable(const QSet<int>& values) const
{
    if (values.isEmpty())
        return nullptr;

    int minValue = std::numeric_limits<int>::max();
    int maxValue = std::numeric_limits<int>::min();
    for (int value : values)
    {
        minValue = qMin(minValue, value);
        maxValue = qMax(maxValue, value);
    }

    if (minValue > maxValue)
        return nullptr;

    const int tableSize = maxValue - minValue + 1;
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(tableSize);
    lut->SetTableRange(static_cast<double>(minValue), static_cast<double>(maxValue));
    lut->Build();

    for (int i = 0; i < tableSize; ++i)
    {
        const int value = minValue + i;
        const QColor color = values.contains(value) ? categoricalColor(value)
                                                    : QColor::fromRgbF(0.78, 0.78, 0.78);
        lut->SetTableValue(i, color.redF(), color.greenF(), color.blueF(), 1.0);
    }

    return lut;
}

void GridEditingWidget::applyScalarColor(vtkMapper* mapper,
                                         vtkDataSet* dataSet,
                                         const char* arrayName,
                                         vtkLookupTable* lut) const
{
    if (!mapper || !dataSet || !arrayName || !lut)
        return;

    auto* cd = dataSet->GetCellData();
    if (!cd || !cd->GetArray(arrayName))
    {
        mapper->ScalarVisibilityOff();
        return;
    }

    mapper->SetScalarModeToUseCellFieldData();
    mapper->SelectColorArray(arrayName);
    mapper->SetLookupTable(lut);
    mapper->UseLookupTableScalarRangeOn();
    mapper->ScalarVisibilityOn();
}

void GridEditingWidget::applyColorMode()
{
    if (m_surfaceMapper)
        m_surfaceMapper->ScalarVisibilityOff();
    if (m_lineMapper)
        m_lineMapper->ScalarVisibilityOff();

    const char* arrayName = nullptr;
    switch (m_colorMode)
    {
    case ColorMode::Topology:
        arrayName = "TOPOLOGY_TYPE";
        break;
    case ColorMode::Ketype:
        arrayName = "KETYPE";
        break;
    case ColorMode::Matype:
        arrayName = "MATYPE";
        break;
    case ColorMode::Layer:
        arrayName = "LAYER";
        break;
    case ColorMode::None:
    default:
        return;
    }

    auto lut = buildLookupTable(collectVisibleCellInts(arrayName));
    if (!lut)
        return;

    applyScalarColor(m_surfaceMapper, m_viewSurface, arrayName, lut);
    applyScalarColor(m_lineMapper, m_viewLineGrid, arrayName, lut);
}

void GridEditingWidget::on_pushButtonApplyFilter_clicked()
{
    applyFilterFromTree();
}

void GridEditingWidget::on_pushButtonResetFilter_clicked()
{
    setFilterTreeChecked(true);
    applyFilterFromTree();
}

void GridEditingWidget::on_pushButtonUncheckAll_clicked()
{
    setFilterTreeChecked(false);
    applyFilterFromTree();
}

void GridEditingWidget::on_comboBoxColorMode_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    if (!ui || !ui->comboBoxColorMode)
        return;

    const QVariant data = ui->comboBoxColorMode->currentData();
    m_colorMode = data.isValid() ? static_cast<ColorMode>(data.toInt()) : ColorMode::None;
    setGlobalValue(kGlobalColorModeKey, static_cast<int>(m_colorMode));
    buildOrUpdateViewGrid();
}

void GridEditingWidget::highlightMatypes(const QSet<int>& matypes,
                                         int selectedMatype,
                                         int previewMatype,
                                         int preDeleteMatype)
{
    Q_UNUSED(matypes);
    Q_UNUSED(selectedMatype);
    Q_UNUSED(previewMatype);
    Q_UNUSED(preDeleteMatype);
}

void GridEditingWidget::updateContextPresentation()
{
    if (!ui)
        return;

    const bool postViewActive =
        PublicVariables::instance().get(QStringLiteral("post.display.view.active")).toBool();

    if (ui->labelColorMode)
        ui->labelColorMode->setVisible(!postViewActive);
    if (ui->comboBoxColorMode)
        ui->comboBoxColorMode->setVisible(!postViewActive);
    if (ui->treeWidgetFilterValues && ui->treeWidgetFilterValues->headerItem())
        ui->treeWidgetFilterValues->headerItem()->setText(
            0, postViewActive ? QStringLiteral("结果筛选项") : QStringLiteral("筛选项"));

           // 后处理激活时，这个面板只负责筛选结果区域，不负责把网格本体叠加出来。
           // 主动压住所有常用网格 Actor，避免其它显示路径把网格重新打开。
    if (postViewActive && m_visualWidgetService)
    {
        m_visualWidgetService->setVisible(QStringLiteral("mesh.global.surf"), false, false);
        m_visualWidgetService->setVisible(QStringLiteral("mesh.global.wire"), false, false);
        m_visualWidgetService->setVisible(kImportedMeshSurfacePropId, false, false);
        m_visualWidgetService->setVisible(kImportedMeshLinePropId, false, false);
        m_visualWidgetService->render();
    }

    buildFilterTreeFromGrid();
}

void GridEditingWidget::beforeLoad()
{
    if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
        uiFrame->setWidgetVisible(this, true);

    updateContextPresentation();

    if (postResultFilterActive())
    {
        buildFilterTreeFromGrid();
        return;
    }

    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        clearGrid();
        return;
    }

    setEditableGrid(grid, QString());
}

void GridEditingWidget::beforeUnload()
{
  // The dock panel remains active while users work on other settings pages.
}
