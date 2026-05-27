#include "GridEditingWidget.h"

#include "ui_GridEditingWidget.h"

#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include "MeshGridIO.h"

#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <QColor>
#include <QComboBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QVariant>

#include <algorithm>

namespace
{
static const QString kImportedMeshSurfacePropId = QStringLiteral("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QStringLiteral("mesh.imported.line");

static int tuple1i(vtkDataArray* array, vtkIdType tupleId, int defaultValue = 0)
{
    if (!array || tupleId < 0 || tupleId >= array->GetNumberOfTuples())
        return defaultValue;
    return static_cast<int>(array->GetTuple1(tupleId));
}

static QColor categoricalColor(int value)
{
    if (value <= 0)
        return QColor::fromRgbF(0.78, 0.78, 0.78);

    const int hue = ((value * 47) % 360 + 360) % 360;
    return QColor::fromHsv(hue, 170, 220);
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
        ui->comboBoxColorMode->addItem(QStringLiteral("默认"), static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->addItem(QStringLiteral("单元类型"), static_cast<int>(ColorMode::Ketype));
        ui->comboBoxColorMode->addItem(QStringLiteral("材料类型"), static_cast<int>(ColorMode::Matype));
        ui->comboBoxColorMode->addItem(QStringLiteral("填筑分层"), static_cast<int>(ColorMode::Layer));
        ui->comboBoxColorMode->setCurrentIndex(0);
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

    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    m_colorMode = ColorMode::None;

    m_highlightedMatypes.clear();
    m_selectedMatype = -1;
    m_previewMatype = -1;
    m_preDeleteMatype = -1;

    m_surfaceActor = nullptr;
    m_lineActor = nullptr;
    m_surfaceMapper = nullptr;
    m_lineMapper = nullptr;
}

void GridEditingWidget::setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName)
{
    clearData();
    m_sourceName = sourceName;

    if (ui && ui->comboBoxColorMode)
    {
        const QSignalBlocker blocker(ui->comboBoxColorMode);
        ui->comboBoxColorMode->setCurrentIndex(0);
    }

    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        updateFilterSummaryLabel();
        return;
    }

    m_fullGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_fullGrid->DeepCopy(grid);

    updateFilterCombosFromGrid();
    updateFilterSummaryLabel();
    buildOrUpdateViewGrid();
}

void GridEditingWidget::clearGrid()
{
    clearData();

    if (ui)
    {
        if (ui->comboBoxKetype)
            ui->comboBoxKetype->clear();
        if (ui->comboBoxMatype)
            ui->comboBoxMatype->clear();
        if (ui->comboBoxLayer)
            ui->comboBoxLayer->clear();
        if (ui->labelFilterSummary)
            ui->labelFilterSummary->setText(QStringLiteral("当前过滤：全部"));
    }
}

bool GridEditingWidget::hasFilter() const
{
    return !m_filterKetypes.isEmpty() || !m_filterMatypes.isEmpty() || !m_filterLayers.isEmpty();
}

bool GridEditingWidget::cellMatchesFilter(vtkIdType cellId) const
{
    if (!m_fullGrid || cellId < 0 || cellId >= m_fullGrid->GetNumberOfCells())
        return false;

    auto* cd = m_fullGrid->GetCellData();
    if (!cd)
        return false;

    const int ketype = tuple1i(cd->GetArray("KETYPE"), cellId, 0);
    const int matype = tuple1i(cd->GetArray("MATYPE"), cellId, 0);
    const int layer = tuple1i(cd->GetArray("LAYER"), cellId, 0);

    if (!m_filterKetypes.isEmpty() && !m_filterKetypes.contains(ketype))
        return false;
    if (!m_filterMatypes.isEmpty() && !m_filterMatypes.contains(matype))
        return false;
    if (!m_filterLayers.isEmpty() && !m_filterLayers.contains(layer))
        return false;

    return true;
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

    auto extractor = vtkSmartPointer<vtkExtractCells>::New();
    extractor->SetInputData(m_fullGrid);
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
    {
        dataSetMapper->SetInputData(dataSet);
        return;
    }
}

void GridEditingWidget::buildOrUpdateViewGrid()
{
    refreshMeshActors();

    if (!m_visualWidgetService || !m_fullGrid)
        return;

    auto surfaceCellIds = vtkSmartPointer<vtkIdList>::New();
    auto lineCellIds = vtkSmartPointer<vtkIdList>::New();

    const bool filtering = hasFilter();
    const vtkIdType cellCount = m_fullGrid->GetNumberOfCells();

    for (vtkIdType cellId = 0; cellId < cellCount; ++cellId)
    {
        vtkCell* cell = m_fullGrid->GetCell(cellId);
        if (!cell)
            continue;

        if (filtering && !cellMatchesFilter(cellId))
            continue;

        const int dim = cell->GetCellDimension();
        if (dim >= 2)
            surfaceCellIds->InsertNextId(cellId);
        else if (dim == 1)
            lineCellIds->InsertNextId(cellId);
    }

    m_viewSurface = nullptr;
    m_viewLineGrid = nullptr;

    if (surfaceCellIds->GetNumberOfIds() > 0)
    {
        auto surfaceSource = extractCells(surfaceCellIds);
        auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
        surfaceFilter->SetInputData(surfaceSource);
        surfaceFilter->PassThroughCellIdsOn();
        surfaceFilter->PassThroughPointIdsOn();
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
    m_visualWidgetService->render();
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

QSet<int> GridEditingWidget::collectUniqueCellInts(const char* arrayName) const
{
    return collectUniqueCellInts(m_fullGrid, arrayName);
}

QSet<int> GridEditingWidget::collectVisibleCellInts(const char* arrayName) const
{
    QSet<int> values = collectUniqueCellInts(m_viewSurface, arrayName);
    values.unite(collectUniqueCellInts(m_viewLineGrid, arrayName));
    return values;
}

void GridEditingWidget::updateFilterCombosFromGrid()
{
    if (!ui)
        return;

    auto fillCombo = [](QComboBox* combo, const QSet<int>& values)
    {
        if (!combo)
            return;

        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(QStringLiteral("全部"), QVariant());

        QList<int> list = values.values();
        std::sort(list.begin(), list.end());

        for (int value : list)
            combo->addItem(QString::number(value), value);

        combo->setCurrentIndex(0);
    };

    fillCombo(ui->comboBoxKetype, collectUniqueCellInts("KETYPE"));
    fillCombo(ui->comboBoxLayer, collectUniqueCellInts("LAYER"));

    if (ui->comboBoxMatype)
    {
        const QSignalBlocker blocker(ui->comboBoxMatype);
        ui->comboBoxMatype->clear();
        ui->comboBoxMatype->addItem(QStringLiteral("全部"), QVariant());

        QList<int> list = collectUniqueCellInts("MATYPE").values();
        std::sort(list.begin(), list.end());

        const MshPickMap pickMap = MeshGridIO::instance().getMeshMap();
        for (int value : list)
        {
            const QString name = pickMap.physicalName(value).trimmed();
            ui->comboBoxMatype->addItem(name.isEmpty() ? QString::number(value)
                                                       : QStringLiteral("%1 (%2)").arg(name).arg(value),
                                        value);
        }

        ui->comboBoxMatype->setCurrentIndex(0);
    }
}

void GridEditingWidget::updateFilterSummaryLabel()
{
    if (!ui || !ui->labelFilterSummary)
        return;

    auto describe = [](const QSet<int>& values)
    {
        if (values.isEmpty())
            return QStringLiteral("全部");

        QList<int> list = values.values();
        std::sort(list.begin(), list.end());

        QStringList items;
        for (int value : list)
            items << QString::number(value);

        return items.join(QStringLiteral(","));
    };

    ui->labelFilterSummary->setText(
        QStringLiteral("当前过滤：KETYPE(%1) | MATYPE(%2) | LAYER(%3)")
            .arg(describe(m_filterKetypes))
            .arg(describe(m_filterMatypes))
            .arg(describe(m_filterLayers)));
}

vtkSmartPointer<vtkLookupTable> GridEditingWidget::buildLookupTable(const QSet<int>& values) const
{
    if (values.isEmpty())
        return nullptr;

    int maxValue = -1;
    for (int value : values)
        maxValue = qMax(maxValue, value);

    if (maxValue < 0)
        return nullptr;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxValue + 1);
    lut->SetTableRange(0.0, maxValue);
    lut->Build();

    for (int i = 0; i <= maxValue; ++i)
    {
        const QColor color = values.contains(i) ? categoricalColor(i)
                                                : QColor::fromRgbF(0.78, 0.78, 0.78);
        lut->SetTableValue(i, color.redF(), color.greenF(), color.blueF(), 1.0);
    }

    return lut;
}

bool GridEditingWidget::hasMatypeHighlight() const
{
    return !m_highlightedMatypes.isEmpty() ||
           m_selectedMatype >= 0 ||
           m_previewMatype >= 0 ||
           m_preDeleteMatype >= 0;
}

vtkSmartPointer<vtkLookupTable> GridEditingWidget::buildMatypeHighlightLookupTable() const
{
    QSet<int> values = collectVisibleCellInts("MATYPE");
    values.unite(m_highlightedMatypes);

    if (m_selectedMatype >= 0)
        values.insert(m_selectedMatype);
    if (m_previewMatype >= 0)
        values.insert(m_previewMatype);
    if (m_preDeleteMatype >= 0)
        values.insert(m_preDeleteMatype);

    int maxValue = -1;
    for (int value : values)
        maxValue = qMax(maxValue, value);

    if (maxValue < 0)
        return nullptr;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxValue + 1);
    lut->SetTableRange(0.0, maxValue);
    lut->Build();

    for (int i = 0; i <= maxValue; ++i)
        lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);

    for (int matype : m_highlightedMatypes)
    {
        if (matype >= 0 && matype <= maxValue)
            lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
    }

    if (m_selectedMatype >= 0 && m_selectedMatype <= maxValue)
        lut->SetTableValue(m_selectedMatype, 0.90, 0.20, 0.20, 1.0);

    if (m_previewMatype >= 0 && m_previewMatype <= maxValue)
        lut->SetTableValue(m_previewMatype, 0.20, 0.40, 0.90, 1.0);

    if (m_preDeleteMatype >= 0 && m_preDeleteMatype <= maxValue)
        lut->SetTableValue(m_preDeleteMatype, 0.90, 0.55, 0.20, 1.0);

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

    if (!m_fullGrid)
        return;

    const char* arrayName = nullptr;
    vtkSmartPointer<vtkLookupTable> lut;

    if (hasMatypeHighlight())
    {
        arrayName = "MATYPE";
        lut = buildMatypeHighlightLookupTable();
    }
    else
    {
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

        lut = buildLookupTable(collectVisibleCellInts(arrayName));
    }

    if (!arrayName || !lut)
        return;

    applyScalarColor(m_surfaceMapper, m_viewSurface, arrayName, lut);
    applyScalarColor(m_lineMapper, m_viewLineGrid, arrayName, lut);
}

void GridEditingWidget::on_pushButtonApplyFilter_clicked()
{
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    auto readCombo = [](QComboBox* combo, QSet<int>& target)
    {
        if (!combo)
            return;

        const QVariant data = combo->currentData();
        if (data.isValid())
            target.insert(data.toInt());
    };

    readCombo(ui->comboBoxKetype, m_filterKetypes);
    readCombo(ui->comboBoxMatype, m_filterMatypes);
    readCombo(ui->comboBoxLayer, m_filterLayers);

    updateFilterSummaryLabel();
    buildOrUpdateViewGrid();
}

void GridEditingWidget::on_pushButtonResetFilter_clicked()
{
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    if (ui->comboBoxKetype)
        ui->comboBoxKetype->setCurrentIndex(0);
    if (ui->comboBoxMatype)
        ui->comboBoxMatype->setCurrentIndex(0);
    if (ui->comboBoxLayer)
        ui->comboBoxLayer->setCurrentIndex(0);

    updateFilterSummaryLabel();
    buildOrUpdateViewGrid();
}

void GridEditingWidget::on_comboBoxColorMode_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    if (!ui || !ui->comboBoxColorMode)
        return;

    const QVariant data = ui->comboBoxColorMode->currentData();
    m_colorMode = data.isValid() ? static_cast<ColorMode>(data.toInt()) : ColorMode::None;

    // 用户手动选择颜色模式时，退出外部 MATYPE 高亮状态。
    m_highlightedMatypes.clear();
    m_selectedMatype = -1;
    m_previewMatype = -1;
    m_preDeleteMatype = -1;

    applyColorMode();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void GridEditingWidget::highlightMatypes(const QSet<int>& matypes,
                                         int selectedMatype,
                                         int previewMatype,
                                         int preDeleteMatype)
{
    m_highlightedMatypes = matypes;
    m_selectedMatype = selectedMatype;
    m_previewMatype = previewMatype;
    m_preDeleteMatype = preDeleteMatype;

    applyColorMode();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void GridEditingWidget::beforeLoad()
{
}

void GridEditingWidget::beforeUnload()
{
}
