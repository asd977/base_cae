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
#include <vtkStringArray.h>

#include <QByteArray>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QHash>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <vtkCellTypes.h>
#include <exception>
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
static const QString kCategoryRegion = QStringLiteral("REGION");

static const QString kGlobalFilterCustomKey = QStringLiteral("mesh.display.filter.custom");
static const QString kGlobalFilterTopologyKey = QStringLiteral("mesh.display.filter.topology.checked");
static const QString kGlobalFilterKetypeKey = QStringLiteral("mesh.display.filter.ketype.checked");
static const QString kGlobalFilterMatypeKey = QStringLiteral("mesh.display.filter.matype.checked");
static const QString kGlobalFilterLayerKey = QStringLiteral("mesh.display.filter.layer.checked");
static const QString kGlobalFilterRegionKey = QStringLiteral("mesh.display.filter.region.checked");
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
        value > static_cast<int>(GridEditingWidget::ColorMode::Region))
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
    else if (category == kCategoryRegion)
        key = kGlobalFilterRegionKey;

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
    int vtkCellType = VTK_EMPTY_CELL;
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

static QVector<QVector<vtkIdType>> viewFaceDisplayPolys(const ViewFaceRef& face)
{
    QVector<QVector<vtkIdType>> polys;
    const QVector<vtkIdType>& p = face.pointIds;
    const int n = p.size();

    if (n < 3)
        return polys;

    auto addTri = [&](int a, int b, int c)
    {
        if (a < n && b < n && c < n)
            polys.push_back(QVector<vtkIdType>{p[a], p[b], p[c]});
    };

    auto addQuad = [&](int a, int b, int c, int d)
    {
        if (a < n && b < n && c < n && d < n)
            polys.push_back(QVector<vtkIdType>{p[a], p[b], p[c], p[d]});
    };

    if (face.vtkCellType == VTK_QUADRATIC_TRIANGLE ||
        face.vtkCellType == VTK_BIQUADRATIC_TRIANGLE ||
        n == 6)
    {
        addTri(0, 3, 5);
        addTri(3, 1, 4);
        addTri(5, 4, 2);
        addTri(3, 4, 5);
        return polys;
    }

    if (face.vtkCellType == VTK_QUADRATIC_QUAD || n == 8)
    {
        addTri(0, 4, 7);
        addTri(4, 1, 5);
        addTri(5, 2, 6);
        addTri(7, 6, 3);
        addQuad(4, 5, 6, 7);
        return polys;
    }

    if (face.vtkCellType == VTK_BIQUADRATIC_QUAD || n == 9)
    {
        addQuad(0, 4, 8, 7);
        addQuad(4, 1, 5, 8);
        addQuad(8, 5, 2, 6);
        addQuad(7, 8, 6, 3);
        return polys;
    }

    if (face.vtkCellType == VTK_QUADRATIC_LINEAR_QUAD && n >= 4)
    {
        addQuad(0, 1, 2, 3);
        return polys;
    }

    if (n == 3)
        polys.push_back(p);
    else if (n >= 4)
        addQuad(0, 1, 2, 3);

    return polys;
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

    const QVector<QVector<vtkIdType>> displayPolys = viewFaceDisplayPolys(face);
    for (const QVector<vtkIdType>& polyPts : displayPolys)
    {
        if (polyPts.size() < 3)
            continue;

        const vtkIdType newCellId = polys->GetNumberOfCells();
        polys->InsertNextCell(static_cast<vtkIdType>(polyPts.size()));
        for (vtkIdType pointId : polyPts)
            polys->InsertCellPoint(pointId);

        if (sourceCellData && out->GetCellData())
            out->GetCellData()->CopyData(sourceCellData, face.cellId, newCellId);

        originalCellIds->InsertNextValue(face.originalCellId);
        originalFaceIds->InsertNextValue(face.originalFaceId);
    }
}

static vtkSmartPointer<vtkPolyData> buildMappedViewSurface(vtkUnstructuredGrid* grid,
    const std::shared_ptr<std::atomic_bool>& cancelled = {})
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
        if ((cid & 4095) == 0 && cancelled && cancelled->load()) return nullptr;
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
            ref.vtkCellType = cell->GetCellType();
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
            one.vtkCellType = face->GetCellType();
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
    m_autoApplyTimer = new QTimer(this);
    m_autoApplyTimer->setSingleShot(true);
    m_autoApplyTimer->setInterval(0);
    connect(m_autoApplyTimer,&QTimer::timeout,this,&GridEditingWidget::applyFilterFromTree);

    if (ui->comboBoxColorMode)
    {
        const ColorMode initialMode = globalColorMode();
        const QSignalBlocker blocker(ui->comboBoxColorMode);
        ui->comboBoxColorMode->clear();
        ui->comboBoxColorMode->addItem(QStringLiteral("默认颜色"), static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->addItem(QStringLiteral("按网格拓扑"), static_cast<int>(ColorMode::Topology));
        ui->comboBoxColorMode->addItem(QStringLiteral("按求解器单元类型"), static_cast<int>(ColorMode::Ketype));
        ui->comboBoxColorMode->addItem(QStringLiteral("按材料类型"), static_cast<int>(ColorMode::Matype));
        ui->comboBoxColorMode->addItem(QStringLiteral("按填筑分层"), static_cast<int>(ColorMode::Layer));
        ui->comboBoxColorMode->addItem(QStringLiteral("按几何分域"), static_cast<int>(ColorMode::Region));
        const int initialIndex = ui->comboBoxColorMode->findData(
            static_cast<int>(initialMode));
        ui->comboBoxColorMode->setCurrentIndex(initialIndex >= 0 ? initialIndex : 0);
        m_colorMode = initialMode;
    }


    if (ui->checkBoxAutoApply)
    {
        // 默认即时应用；大型网格仍可关闭此选项后手动应用。
        ui->checkBoxAutoApply->setChecked(true);
        ui->checkBoxAutoApply->setToolTip(QStringLiteral("勾选后，筛选项变化会在后台更新显示。"));
        connect(ui->checkBoxAutoApply,&QCheckBox::toggled,this,[this](bool on) {
            if (on) m_autoApplyTimer->start();
            else m_autoApplyTimer->stop();
        });
    }

    if (ui->checkBoxShowBranding)
    {
        const QSignalBlocker blocker(ui->checkBoxShowBranding);
        ui->checkBoxShowBranding->setChecked(
            !m_visualWidgetService || m_visualWidgetService->isBrandingVisible());
    }
    connect(ui->checkBoxShowBranding, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                if (m_visualWidgetService)
                    m_visualWidgetService->setBrandingVisible(checked);
            });

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
    EventManager::instance().registerHandler(
        QStringLiteral("mesh.groups.changed"), this,
        [this](const QVariantMap&)
        {
            if (postResultFilterActive())
                return;
            auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            vtkUnstructuredGrid* latest = svc ? svc->getVtkUnstructuredGrid().GetPointer() : nullptr;
            if (!latest)
                return;
            m_fullGrid = latest;
            m_colorMode = globalColorMode();
            if (ui && ui->comboBoxColorMode)
            {
                const QSignalBlocker blocker(ui->comboBoxColorMode);
                const int idx = ui->comboBoxColorMode->findData(static_cast<int>(m_colorMode));
                if (idx >= 0)
                    ui->comboBoxColorMode->setCurrentIndex(idx);
            }
            buildFilterTreeFromGrid();
            refreshMeshActors();
            applyColorMode();
            if (m_visualWidgetService)
                m_visualWidgetService->render();
        });
    EventManager::instance().registerHandler(
        QStringLiteral("material.assignment.changed"), this,
        [this](const QVariantMap&)
        {
            if (postResultFilterActive())
                return;
            if (m_colorMode == ColorMode::None)
            {
                m_colorMode = ColorMode::Matype;
                setGlobalValue(kGlobalColorModeKey, static_cast<int>(m_colorMode));
                if (ui && ui->comboBoxColorMode)
                {
                    const QSignalBlocker blocker(ui->comboBoxColorMode);
                    const int idx = ui->comboBoxColorMode->findData(static_cast<int>(m_colorMode));
                    if (idx >= 0)
                        ui->comboBoxColorMode->setCurrentIndex(idx);
                }
                refreshMeshActors();
            }
            applyColorMode();
            if (m_visualWidgetService)
                m_visualWidgetService->render();
        });
    EventManager::instance().registerHandler(
        PublicConstants::kEventBrandingVisibilityChanged, this,
        [this](const QVariantMap& data)
        {
            if (!ui->checkBoxShowBranding)
                return;
            const QSignalBlocker blocker(ui->checkBoxShowBranding);
            ui->checkBoxShowBranding->setChecked(
                data.value(QStringLiteral("visible"), true).toBool());
        });
    EventManager::instance().registerHandler(QStringLiteral("scene.mesh.visibility.changed"),this,
        [this](const QVariantMap& data) {
            if(!data.value(QStringLiteral("visible")).toBool())cancelPendingFilter();
            else if(m_fullGrid && ui->checkBoxAutoApply->isChecked() && filterKey()!=m_appliedFilterKey)
                m_autoApplyTimer->start();
        });
    updateContextPresentation();
}

GridEditingWidget::~GridEditingWidget()
{
    cancelPendingFilter();
    for (const auto& thread : m_filterThreads) if(thread) thread->wait();
    delete ui;
}

void GridEditingWidget::clearData()
{
    cancelPendingFilter();
    m_cachedViews.clear();
    m_appliedFilterKey.clear();
    m_sourceGrid=nullptr;
    m_sourceMTime=0;
    m_fullGrid = nullptr;
    m_viewSurface = nullptr;
    m_viewLineGrid = nullptr;
    m_sourceName.clear();

    m_allTopologies.clear();
    m_allKetypes.clear();
    m_allMatypes.clear();
    m_allLayers.clear();
    m_allRegions.clear();
    m_regionNames.clear();

    m_filterTopologies.clear();
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();
    m_filterRegions.clear();

    m_colorMode = ColorMode::None;

    m_surfaceActor = nullptr;
    m_lineActor = nullptr;
    m_surfaceMapper = nullptr;
    m_lineMapper = nullptr;

    m_updatingFilterTree = false;
}

void GridEditingWidget::setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName)
{
    if (grid && m_sourceGrid.GetPointer()==grid && m_sourceMTime==grid->GetMTime()) {
        refreshMeshActors();
        if(!m_viewSurface) { captureViewInputs();m_appliedFilterKey=filterKey();cacheCurrentView(m_appliedFilterKey); }
        return;
    }
    clearData();
    m_sourceGrid=grid;
    m_sourceMTime=grid ? grid->GetMTime() : 0;
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
    captureViewInputs();
    m_appliedFilterKey=filterKey();
    cacheCurrentView(m_appliedFilterKey);
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
    const int region = tuple1i(cd->GetArray("src_part"), cellId, 0);

    auto allowed = [](const QSet<int>& allValues, const QSet<int>& checkedValues, int value) -> bool
    {
        if (allValues.isEmpty())
            return true;
        return checkedValues.contains(value);
    };

    return allowed(m_allTopologies, m_filterTopologies, topology) &&
           allowed(m_allKetypes, m_filterKetypes, ketype) &&
           allowed(m_allMatypes, m_filterMatypes, matype) &&
           allowed(m_allLayers, m_filterLayers, layer) &&
           allowed(m_allRegions, m_filterRegions, region);
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
    vtkUnstructuredGrid* grid, const vtkSmartPointer<vtkIdList>& ids)
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!grid || !ids || ids->GetNumberOfIds() <= 0)
        return out;

    auto source = copyWithFullCellIds(grid);
    if (ids->GetNumberOfIds()==grid->GetNumberOfCells()) { out->ShallowCopy(source); return out; }

    auto extractor = vtkSmartPointer<vtkExtractCells>::New();
    extractor->SetInputData(source);
    extractor->SetCellList(ids);
    extractor->SetAssumeSortedAndUniqueIds(true);
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

void GridEditingWidget::cancelPendingFilter()
{
    ++m_filterGeneration;
    if(m_autoApplyTimer)m_autoApplyTimer->stop();
    for(const auto& cancelled:m_filterCancellations)cancelled->store(true);
    m_filterRequestPending=false;
    setProperty("mesh.display.pending",false);
    if(ui && ui->pushButtonApplyFilter)
        ui->pushButtonApplyFilter->setText(QStringLiteral("应用筛选"));
}

QString GridEditingWidget::filterKey() const
{
    return intSetToString(m_filterTopologies)+'/'+intSetToString(m_filterKetypes)+'/'
        +intSetToString(m_filterMatypes)+'/'+intSetToString(m_filterLayers)+'/'+intSetToString(m_filterRegions);
}

void GridEditingWidget::cacheCurrentView(const QString& key)
{
    if(!m_viewSurface||!m_viewLineGrid)return;
    for(const auto& view:m_cachedViews)if(view.key==key)return;
    m_cachedViews.append({key,m_viewSurface,m_viewLineGrid});
    const QString allKey=intSetToString(m_allTopologies)+'/'+intSetToString(m_allKetypes)+'/'
        +intSetToString(m_allMatypes)+'/'+intSetToString(m_allLayers)+'/'+intSetToString(m_allRegions);
    const auto memory=[this] { unsigned long size=0;for(const auto& view:m_cachedViews)
        size+=view.surface->GetActualMemorySize()+view.lines->GetActualMemorySize();return size; };
    while(m_cachedViews.size()>3||(!m_cachedViews.isEmpty()&&memory()>256*1024))
        m_cachedViews.removeAt(m_cachedViews.size()>1&&m_cachedViews.first().key==allKey?1:0);
}

void GridEditingWidget::captureViewInputs()
{
    if(auto* mapper=vtkPolyDataMapper::SafeDownCast(m_surfaceMapper))m_viewSurface=mapper->GetInput();
    if(auto* mapper=vtkDataSetMapper::SafeDownCast(m_lineMapper))
        m_viewLineGrid=vtkUnstructuredGrid::SafeDownCast(mapper->GetInput());
    if(!m_viewLineGrid)m_viewLineGrid=vtkSmartPointer<vtkUnstructuredGrid>::New();
}

void GridEditingWidget::buildOrUpdateViewGrid()
{
    refreshMeshActors();
    if(!m_visualWidgetService||!m_fullGrid)return;
    const QString key=filterKey();
    if(m_viewSurface&&key==m_appliedFilterKey) {
        m_filterRequestPending=false;setProperty("mesh.display.pending",false);
        ui->pushButtonApplyFilter->setText(QStringLiteral("应用筛选"));return;
    }
    for(const auto& view:m_cachedViews)if(view.key==key) {
        m_viewSurface=view.surface;m_viewLineGrid=view.lines;m_appliedFilterKey=key;
        m_filterRequestPending=false;setProperty("mesh.display.pending",false);
        ui->pushButtonApplyFilter->setText(QStringLiteral("应用筛选"));presentView();return;
    }
    if(m_runningFilterGenerations.contains(m_filterGeneration)||m_runningFilterGenerations.size()>=2)return;
    const auto source=m_fullGrid;
    const auto generation=m_filterGeneration;
    const auto topologies=m_filterTopologies,ketypes=m_filterKetypes,matypes=m_filterMatypes,
        layers=m_filterLayers,regions=m_filterRegions;
    const bool byTopology=!m_allTopologies.isEmpty(),byKetype=!m_allKetypes.isEmpty(),
        byMatype=!m_allMatypes.isEmpty(),byLayer=!m_allLayers.isEmpty(),byRegion=!m_allRegions.isEmpty();
    struct Result { vtkSmartPointer<vtkPolyData> surface;vtkSmartPointer<vtkUnstructuredGrid> lines;QString error; };
    const auto result=std::make_shared<Result>();
    const auto cancelled=std::make_shared<std::atomic_bool>(false);
    auto* thread=QThread::create([source,result,cancelled,topologies,ketypes,matypes,layers,regions,
        byTopology,byKetype,byMatype,byLayer,byRegion] {
        try {
            if(cancelled->load())return;
            auto grid=vtkSmartPointer<vtkUnstructuredGrid>::New();grid->ShallowCopy(source);
            auto surfaceIds=vtkSmartPointer<vtkIdList>::New(),lineIds=vtkSmartPointer<vtkIdList>::New();
            auto* cd=grid->GetCellData();
            auto* topology=cd->GetArray("TOPOLOGY_TYPE");auto* ketype=cd->GetArray("KETYPE");
            auto* matype=cd->GetArray("MATYPE");auto* layer=cd->GetArray("LAYER");auto* region=cd->GetArray("src_part");
            const bool empty=(byTopology&&topologies.isEmpty())||(byKetype&&ketypes.isEmpty())
                ||(byMatype&&matypes.isEmpty())||(byLayer&&layers.isEmpty())||(byRegion&&regions.isEmpty());
            if(!empty)for(vtkIdType cid=0;cid<grid->GetNumberOfCells();++cid) {
                if((cid&4095)==0&&cancelled->load())return;
                if((byTopology&&!topologies.contains(tuple1i(topology,cid,grid->GetCellType(cid))))
                    ||(byKetype&&!ketypes.contains(tuple1i(ketype,cid)))
                    ||(byMatype&&!matypes.contains(tuple1i(matype,cid)))
                    ||(byLayer&&!layers.contains(tuple1i(layer,cid)))
                    ||(byRegion&&!regions.contains(tuple1i(region,cid))))continue;
                const int dim=vtkCellTypes::GetDimension(grid->GetCellType(cid));
                if(dim>=2)surfaceIds->InsertNextId(cid);else if(dim==1)lineIds->InsertNextId(cid);
            }
            if(cancelled->load())return;
            result->surface=surfaceIds->GetNumberOfIds()>0
                ? buildMappedViewSurface(extractCells(grid,surfaceIds),cancelled)
                : vtkSmartPointer<vtkPolyData>::New();
            if(cancelled->load())return;
            result->lines=extractCells(grid,lineIds);
        } catch(const std::exception& error) {result->error=QString::fromUtf8(error.what());}
        catch(...) {result->error=QStringLiteral("显示数据构造失败。");}
    });
    m_runningFilterGenerations.insert(generation);m_filterCancellations.append(cancelled);m_filterThreads.append(thread);
    setProperty("mesh.display.workerRunning",true);
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,thread,[thread]{thread->wait();},Qt::DirectConnection);
    connect(thread,&QThread::finished,this,[this,result,cancelled,source,generation,key,thread] {
        m_runningFilterGenerations.remove(generation);m_filterCancellations.removeOne(cancelled);m_filterThreads.removeAll(thread);
        setProperty("mesh.display.workerRunning",!m_runningFilterGenerations.isEmpty());
        if(source!=m_fullGrid||generation!=m_filterGeneration) {
            if(m_filterRequestPending)buildOrUpdateViewGrid();return;
        }
        m_filterRequestPending=false;setProperty("mesh.display.pending",false);
        ui->pushButtonApplyFilter->setText(QStringLiteral("应用筛选"));
        if(!result->error.isEmpty()||!result->surface||!result->lines) {
            qWarning()<<QStringLiteral("网格显示更新失败：")+result->error;return;
        }
        m_viewSurface=result->surface;m_viewLineGrid=result->lines;m_appliedFilterKey=key;
        cacheCurrentView(key);refreshMeshActors();presentView();
    },Qt::QueuedConnection);
    thread->start();
}

void GridEditingWidget::publishFilter()
{
    const bool custom=!setEquals(m_allTopologies,m_filterTopologies)||!setEquals(m_allKetypes,m_filterKetypes)
        ||!setEquals(m_allMatypes,m_filterMatypes)||!setEquals(m_allLayers,m_filterLayers)||!setEquals(m_allRegions,m_filterRegions);
    setGlobalValue(kGlobalFilterCustomKey,custom);
    setGlobalValue(kGlobalFilterTopologyKey,intSetToString(m_filterTopologies));
    setGlobalValue(kGlobalFilterKetypeKey,intSetToString(m_filterKetypes));
    setGlobalValue(kGlobalFilterMatypeKey,intSetToString(m_filterMatypes));
    setGlobalValue(kGlobalFilterLayerKey,intSetToString(m_filterLayers));
    setGlobalValue(kGlobalFilterRegionKey,intSetToString(m_filterRegions));
    setGlobalValue(kGlobalFilterProjectKey,PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey));
    QVariantMap event{{"custom",custom},{"topology",intSetToString(m_filterTopologies)},
        {"ketype",intSetToString(m_filterKetypes)},{"matype",intSetToString(m_filterMatypes)},
        {"layer",intSetToString(m_filterLayers)},{"region",intSetToString(m_filterRegions)}};
    EventManager::instance().sendEvent(QStringLiteral("mesh.display.filter.changed"),event);
}

void GridEditingWidget::presentView()
{
    if(!m_visualWidgetService)return;
    if(m_surfaceMapper)setMapperInput(m_surfaceMapper,m_viewSurface);
    if(m_lineMapper)setMapperInput(m_lineMapper,m_viewLineGrid);
    const bool postActive=PublicVariables::instance().get(QStringLiteral("post.display.view.active")).toBool();
    if (postActive)
    {
        m_visualWidgetService->setTemporaryVisible(kImportedMeshSurfacePropId, false, false);
        m_visualWidgetService->setTemporaryVisible(kImportedMeshLinePropId, false, false);
    }
    else
    {
        m_visualWidgetService->setVisible(kImportedMeshSurfacePropId,m_viewSurface&&m_viewSurface->GetNumberOfCells()>0,false);
        m_visualWidgetService->setVisible(kImportedMeshLinePropId,m_viewLineGrid&&m_viewLineGrid->GetNumberOfCells()>0,false);
    }
    publishFilter();applyColorMode();PickManager::instance().updateStructure();m_visualWidgetService->render();
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
    if (category == kCategoryRegion)
    {
        const QString name = m_regionNames.value(value);
        return name.isEmpty() ? (value == 0 ? QStringLiteral("未分域")
                                           : QStringLiteral("域 %1").arg(value)) : name;
    }
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
        switch (value)
        {
        case 1:
            return QStringLiteral("1（2节点线）");
        case 2:
            return QStringLiteral("2（3节点二次线）");
        case 3:
            return QStringLiteral("3（4节点四边形）");
        case 4:
            return QStringLiteral("4（6节点二次四边形）");
        case 5:
            return QStringLiteral("5（8节点二次四边形）");
        case 6:
            return QStringLiteral("6（8节点六面体）");
        case 31:
            return QStringLiteral("31（3节点三角形）");
        case 32:
            return QStringLiteral("32（6节点二次三角形）");
        case 35:
            return QStringLiteral("35（4节点四面体）");
        case 37:
            return QStringLiteral("37（20节点二次六面体）");
        case 38:
            return QStringLiteral("38（6节点三棱柱）");
        case 39:
            return QStringLiteral("39（5节点金字塔）");
        default:
            return QStringLiteral("%1（求解器单元类型）").arg(value);
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
    QMap<int, int> regionCounts;
    m_regionNames.clear();

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
        regionCounts = collectCellIntCounts("src_part");
        // Zero denotes cells without a source region. Show them only when
        // actual regions also exist, so mixed grids can still be filtered.
        if (regionCounts.size() == 1 && regionCounts.contains(0))
            regionCounts.clear();
        auto* cd = m_fullGrid ? m_fullGrid->GetCellData() : nullptr;
        auto* parts = cd ? cd->GetArray("src_part") : nullptr;
        auto* names = cd ? vtkStringArray::SafeDownCast(cd->GetAbstractArray("GroupName")) : nullptr;
        for (vtkIdType cid = 0; parts && names &&
             cid < qMin(parts->GetNumberOfTuples(), names->GetNumberOfValues()); ++cid)
        {
            const int region = tuple1i(parts, cid);
            if (m_regionNames.contains(region))
                continue;
            const QString name = QString::fromStdString(names->GetValue(cid)).trimmed();
            if (!name.isEmpty())
                m_regionNames.insert(region, name);
        }
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
    m_allRegions = mapKeysToSet(regionCounts);

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
        m_filterRegions = PublicVariables::instance().contains(kGlobalFilterRegionKey)
            ? globalCheckedValues(kCategoryRegion) : m_allRegions;
    }
    else
    {
        m_filterTopologies = m_allTopologies;
        m_filterKetypes = m_allKetypes;
        m_filterMatypes = m_allMatypes;
        m_filterLayers = m_allLayers;
        m_filterRegions = m_allRegions;
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
        if (category == kCategoryRegion)
            group->setToolTip(0, QStringLiteral("按几何区域分别显示；同一种材料可以包含多个几何分域。"));
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
                0, QStringLiteral("%1\n%2 = %3，单元数量：%4")
                       .arg(child->text(0), category)
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
            else if (category == kCategoryRegion)
                checked = m_filterRegions.contains(it.key());
            child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }

        updateParentCheckState(group);
    };

    if (!postMode)
    {
        if (showTopologyFilter)
            addGroup(kCategoryTopology, QStringLiteral("网格拓扑"), topologyCounts);
        addGroup(kCategoryKetype, QStringLiteral("求解器单元类型"), ketypeCounts);
        addGroup(kCategoryRegion, QStringLiteral("几何分域"), regionCounts);
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
        if (category == kCategoryRegion)
            return m_allRegions;
    }

    return values;
}

void GridEditingWidget::applyFilterFromTree()
{
    m_autoApplyTimer->stop();
    m_filterTopologies = checkedValuesForCategory(kCategoryTopology);
    m_filterKetypes = checkedValuesForCategory(kCategoryKetype);
    m_filterMatypes = checkedValuesForCategory(kCategoryMatype);
    m_filterLayers = checkedValuesForCategory(kCategoryLayer);
    m_filterRegions = checkedValuesForCategory(kCategoryRegion);

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

    cancelPendingFilter();
    if(!m_fullGrid||!m_visualWidgetService)return;
    m_filterRequestPending=true;
    setProperty("mesh.display.pending",true);
    ui->pushButtonApplyFilter->setText(QStringLiteral("更新中…"));
    buildOrUpdateViewGrid();
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
    m_filterRegions = checkedValuesForCategory(kCategoryRegion);

    cancelPendingFilter();
    if (ui->checkBoxAutoApply && ui->checkBoxAutoApply->isChecked())
        m_autoApplyTimer->start();
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
    case ColorMode::Region:
        arrayName = "src_part";
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
    refreshMeshActors();
    applyColorMode();
    if(m_visualWidgetService)m_visualWidgetService->render();
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
        cancelPendingFilter();
        // 云图只临时隔离网格，不能改写对象本身的显示选择。
        m_visualWidgetService->setTemporaryVisible(QStringLiteral("mesh.global.surf"), false, false);
        m_visualWidgetService->setTemporaryVisible(QStringLiteral("mesh.global.wire"), false, false);
        m_visualWidgetService->setTemporaryVisible(kImportedMeshSurfacePropId, false, false);
        m_visualWidgetService->setTemporaryVisible(kImportedMeshLinePropId, false, false);
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
