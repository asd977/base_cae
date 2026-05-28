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
#include <vtkUnstructuredGrid.h>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
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

    auto fullIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullIds->SetName("vtkOriginalCellIds");
    fullIds->SetNumberOfComponents(1);
    fullIds->SetNumberOfTuples(grid->GetNumberOfCells());
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
        fullIds->SetValue(cid, cid);

    if (auto* cd = out->GetCellData())
    {
        cd->RemoveArray("vtkOriginalCellIds");
        cd->AddArray(fullIds);
    }

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

    updateFilterSummaryLabel();
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
        updateFilterSummaryLabel();
        return;
    }

    m_fullGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_fullGrid->ShallowCopy(grid);

    buildFilterTreeFromGrid();
    updateFilterSummaryLabel();
    refreshMeshActors();
}

void GridEditingWidget::clearGrid()
{
    clearData();

    if (ui && ui->treeWidgetFilterValues)
        ui->treeWidgetFilterValues->clear();

    updateFilterSummaryLabel();
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
        auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
        surfaceFilter->SetInputData(surfaceSource);
        surfaceFilter->Update();

        m_viewSurface = vtkSmartPointer<vtkPolyData>::New();
        m_viewSurface->ShallowCopy(surfaceFilter->GetOutput());
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

    updateFilterSummaryLabel();
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
    updateFilterSummaryLabel();

    if (ui->checkBoxAutoApply && ui->checkBoxAutoApply->isChecked())
        applyFilterFromTree();
}

void GridEditingWidget::updateFilterSummaryLabel()
{
    if (!ui || !ui->labelFilterSummary)
        return;

    auto describe = [](const QString& name,
                       const QSet<int>& allValues,
                       const QSet<int>& checkedValues) -> QString
    {
        if (allValues.isEmpty())
            return QStringLiteral("%1：无字段").arg(name);

        if (setEquals(allValues, checkedValues))
            return QStringLiteral("%1：全部 %2").arg(name).arg(allValues.size());

        if (checkedValues.isEmpty())
            return QStringLiteral("%1：无 0/%2").arg(name).arg(allValues.size());

        QList<int> list = checkedValues.values();
        std::sort(list.begin(), list.end());

        QStringList items;
        const int maxPreviewCount = 8;
        for (int i = 0; i < list.size() && i < maxPreviewCount; ++i)
            items << QString::number(list[i]);

        QString text = items.join(QStringLiteral(","));
        if (list.size() > maxPreviewCount)
            text += QStringLiteral(" ...");

        return QStringLiteral("%1：%2/%3 [%4]")
            .arg(name)
            .arg(checkedValues.size())
            .arg(allValues.size())
            .arg(text);
    };

    ui->labelFilterSummary->setText(
        QStringLiteral("全局筛选  |  %1  |  %2  |  %3")
            .arg(describe(QStringLiteral("KETYPE"), m_allKetypes, m_filterKetypes))
            .arg(describe(QStringLiteral("MATYPE"), m_allMatypes, m_filterMatypes))
            .arg(describe(QStringLiteral("LAYER"), m_allLayers, m_filterLayers)));
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
