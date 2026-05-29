#include "GridEditingWidget.h"

#include "ui_GridEditingWidget.h"

#include <ServiceRegistry.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <services/MeshModuleControllerService.h>

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
#include <QSignalBlocker>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

#include <algorithm>
#include <limits>

namespace
{
static const QString kImportedMeshSurfacePropId = QStringLiteral("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QStringLiteral("mesh.imported.line");

static const QString kCategoryKetype = QStringLiteral("KETYPE");
static const QString kCategoryMatype = QStringLiteral("MATYPE");
static const QString kCategoryLayer = QStringLiteral("LAYER");

static const QString kGlobalFilterCustomKey = QStringLiteral("mesh.display.filter.custom");
static const QString kGlobalFilterKetypeKey = QStringLiteral("mesh.display.filter.ketype.checked");
static const QString kGlobalFilterMatypeKey = QStringLiteral("mesh.display.filter.matype.checked");
static const QString kGlobalFilterLayerKey = QStringLiteral("mesh.display.filter.layer.checked");

constexpr int kRoleCategory = Qt::UserRole + 1;
constexpr int kRoleValue = Qt::UserRole + 2;
constexpr int kRoleIsGroup = Qt::UserRole + 3;

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
    const QStringList parts = text.split(',', QString::SkipEmptyParts);
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
    return PublicVariables::instance().get(kGlobalFilterCustomKey).toBool();
}

static QSet<int> globalCheckedValues(const QString& category)
{
    QString key;
    if (category == kCategoryKetype)
        key = kGlobalFilterKetypeKey;
    else if (category == kCategoryMatype)
        key = kGlobalFilterMatypeKey;
    else if (category == kCategoryLayer)
        key = kGlobalFilterLayerKey;

    return intSetFromString(PublicVariables::instance().get(key).toString());
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
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (ui->comboBoxColorMode)
    {
        ui->comboBoxColorMode->clear();
        ui->comboBoxColorMode->addItem(QStringLiteral("默认颜色"), static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->addItem(QStringLiteral("按单元类型 KETYPE"), static_cast<int>(ColorMode::Ketype));
        ui->comboBoxColorMode->addItem(QStringLiteral("按材料类型 MATYPE"), static_cast<int>(ColorMode::Matype));
        ui->comboBoxColorMode->addItem(QStringLiteral("按填筑分层 LAYER"), static_cast<int>(ColorMode::Layer));
        ui->comboBoxColorMode->setCurrentIndex(0);
    }


    if (ui->checkBoxAutoApply)
    {
        ui->checkBoxAutoApply->setChecked(false);
        ui->checkBoxAutoApply->setToolTip(QStringLiteral("取消/勾选筛选项时只修改列表状态；需要立即重建显示时再勾选此项。复杂网格建议保持关闭，然后点击“应用筛选”。"));
    }

    if (ui->treeWidgetFilterValues)
    {
        ui->treeWidgetFilterValues->setColumnCount(2);
        ui->treeWidgetFilterValues->setHeaderLabels(QStringList()
                                                    << QStringLiteral("筛选项")
                                                    << QStringLiteral("数量"));
        ui->treeWidgetFilterValues->setRootIsDecorated(true);
        ui->treeWidgetFilterValues->setAlternatingRowColors(true);
        connect(ui->treeWidgetFilterValues,
                &QTreeWidget::itemChanged,
                this,
                &GridEditingWidget::onFilterTreeItemChanged);
    }

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

    m_allKetypes.clear();
    m_allMatypes.clear();
    m_allLayers.clear();

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

           // 颜色模式只在当前网格编辑页生效，不再保存/恢复为全局状态。
    m_colorMode = ColorMode::None;

    if (ui && ui->comboBoxColorMode)
    {
        const QSignalBlocker blocker(ui->comboBoxColorMode);
        const int idx = ui->comboBoxColorMode->findData(static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        buildFilterTreeFromGrid();
        return;
    }

    m_fullGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_fullGrid->ShallowCopy(grid);

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

    const int ketype = tuple1i(cd->GetArray("KETYPE"), cellId, 0);
    const int matype = tuple1i(cd->GetArray("MATYPE"), cellId, 0);
    const int layer = tuple1i(cd->GetArray("LAYER"), cellId, 0);

    auto allowed = [](const QSet<int>& allValues, const QSet<int>& checkedValues, int value) -> bool
    {
        if (allValues.isEmpty())
            return true;
        return checkedValues.contains(value);
    };

    return allowed(m_allKetypes, m_filterKetypes, ketype) &&
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

    auto surfaceCellIds = vtkSmartPointer<vtkIdList>::New();
    auto lineCellIds = vtkSmartPointer<vtkIdList>::New();

    for (vtkIdType cellId = 0; cellId < m_fullGrid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = m_fullGrid->GetCell(cellId);
        if (!cell)
            continue;

        if (!cellMatchesFilter(cellId))
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

    m_visualWidgetService->setVisible(kImportedMeshSurfacePropId, showSurface, false);
    m_visualWidgetService->setVisible(kImportedMeshLinePropId, showLine, false);

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

void GridEditingWidget::buildFilterTreeFromGrid()
{
    if (!ui || !ui->treeWidgetFilterValues)
        return;

    const QSignalBlocker blocker(ui->treeWidgetFilterValues);
    m_updatingFilterTree = true;
    ui->treeWidgetFilterValues->clear();

    const QMap<int, int> ketypeCounts = collectCellIntCounts("KETYPE");
    const QMap<int, int> matypeCounts = collectCellIntCounts("MATYPE");
    const QMap<int, int> layerCounts = collectCellIntCounts("LAYER");

    m_allKetypes = mapKeysToSet(ketypeCounts);
    m_allMatypes = mapKeysToSet(matypeCounts);
    m_allLayers = mapKeysToSet(layerCounts);

    if (globalFilterIsCustom())
    {
        m_filterKetypes = globalCheckedValues(kCategoryKetype);
        m_filterMatypes = globalCheckedValues(kCategoryMatype);
        m_filterLayers = globalCheckedValues(kCategoryLayer);
    }
    else
    {
        m_filterKetypes = m_allKetypes;
        m_filterMatypes = m_allMatypes;
        m_filterLayers = m_allLayers;
    }

    auto addGroup = [this](const QString& category, const QString& title, const QMap<int, int>& counts)
    {
        auto* group = new QTreeWidgetItem(ui->treeWidgetFilterValues);
        group->setText(0, title);
        group->setText(1, QString::number(counts.size()));
        group->setData(0, kRoleCategory, category);
        group->setData(0, kRoleIsGroup, true);
        group->setFlags((group->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsTristate);

        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        {
            auto* child = new QTreeWidgetItem(group);
            child->setText(0, displayTextForFilterValue(category, it.key()));
            child->setText(1, QString::number(it.value()));
            child->setData(0, kRoleCategory, category);
            child->setData(0, kRoleValue, it.key());
            child->setData(0, kRoleIsGroup, false);
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            child->setToolTip(0, QStringLiteral("%1 = %2，单元数量：%3")
                                     .arg(category)
                                     .arg(it.key())
                                     .arg(it.value()));

            bool checked = true;
            if (category == kCategoryKetype)
                checked = m_filterKetypes.contains(it.key());
            else if (category == kCategoryMatype)
                checked = m_filterMatypes.contains(it.key());
            else if (category == kCategoryLayer)
                checked = m_filterLayers.contains(it.key());
            child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }

        updateParentCheckState(group);
    };

    addGroup(kCategoryKetype, QStringLiteral("单元类型 KETYPE"), ketypeCounts);
    addGroup(kCategoryMatype, QStringLiteral("材料类型 MATYPE"), matypeCounts);
    addGroup(kCategoryLayer, QStringLiteral("填筑分层 LAYER"), layerCounts);

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

    for (int i = 0; i < ui->treeWidgetFilterValues->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* group = ui->treeWidgetFilterValues->topLevelItem(i);
        if (!group || group->data(0, kRoleCategory).toString() != category)
            continue;

        for (int c = 0; c < group->childCount(); ++c)
        {
            QTreeWidgetItem* child = group->child(c);
            if (child && child->checkState(0) == Qt::Checked)
                values.insert(child->data(0, kRoleValue).toInt());
        }
        break;
    }

    return values;
}

void GridEditingWidget::applyFilterFromTree()
{
    m_filterKetypes = checkedValuesForCategory(kCategoryKetype);
    m_filterMatypes = checkedValuesForCategory(kCategoryMatype);
    m_filterLayers = checkedValuesForCategory(kCategoryLayer);

    const bool customFilter =
        !setEquals(m_allKetypes, m_filterKetypes) ||
        !setEquals(m_allMatypes, m_filterMatypes) ||
        !setEquals(m_allLayers, m_filterLayers);

    setGlobalValue(kGlobalFilterCustomKey, customFilter);
    setGlobalValue(kGlobalFilterKetypeKey, intSetToString(m_filterKetypes));
    setGlobalValue(kGlobalFilterMatypeKey, intSetToString(m_filterMatypes));
    setGlobalValue(kGlobalFilterLayerKey, intSetToString(m_filterLayers));

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
    m_filterKetypes = checkedValuesForCategory(kCategoryKetype);
    m_filterMatypes = checkedValuesForCategory(kCategoryMatype);
    m_filterLayers = checkedValuesForCategory(kCategoryLayer);

    if (ui->checkBoxAutoApply && ui->checkBoxAutoApply->isChecked())
        applyFilterFromTree();
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

void GridEditingWidget::beforeLoad()
{
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
    // 筛选显示仍然保留为全局状态，但颜色显示只属于网格编辑页。
    // 离开页面时只关闭 mapper 的标量着色，不恢复筛选后的 mapper 输入。
    refreshMeshActors();

    m_colorMode = ColorMode::None;
    if (ui && ui->comboBoxColorMode)
    {
        const QSignalBlocker blocker(ui->comboBoxColorMode);
        const int idx = ui->comboBoxColorMode->findData(static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    if (m_surfaceMapper)
    {
        m_surfaceMapper->ScalarVisibilityOff();
        m_surfaceMapper->Modified();
    }
    if (m_lineMapper)
    {
        m_lineMapper->ScalarVisibilityOff();
        m_lineMapper->Modified();
    }

    if (m_visualWidgetService)
        m_visualWidgetService->render();

    clearData();
}
