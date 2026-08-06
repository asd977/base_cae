#include "SeepageBoundaryPage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHeaderView>
#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QTableWidget>
#include <QTextStream>

#include <vtkActor.h>
#include <vtkArrowSource.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkGlyph3D.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "ui_SeepageBoundaryPage.h"
#include "SolverUiLayoutHelper.h"

namespace
{
constexpr int kWaterLevelSliderSteps = 1000;

bool finitePair(const QPair<double, double>& point)
{
    return std::isfinite(point.first) && std::isfinite(point.second);
}

struct FluxFaceRow
{
    int elementId = 0;
    QVector<int> nodeIds;
    double center[3] = {0.0, 0.0, 0.0};
    double outward[3] = {0.0, 0.0, 0.0};
};

struct FluxFaceCandidate
{
    int occurrences = 0;
    FluxFaceRow row;
};

struct FluxFaceCollection
{
    QVector<FluxFaceRow> rows;
    int nodeCount = 0;
    QString error;
};

vtkDataArray* findPointIdArray(vtkUnstructuredGrid* grid)
{
    if (!grid || !grid->GetPointData())
        return nullptr;
    if (auto* array = grid->GetPointData()->GetArray("node_id"))
        return array;
    return grid->GetPointData()->GetArray("NodeId");
}

vtkDataArray* findElementIdArray(vtkUnstructuredGrid* grid)
{
    if (!grid || !grid->GetCellData())
        return nullptr;
    if (auto* array = grid->GetCellData()->GetArray("elem_id"))
        return array;
    return grid->GetCellData()->GetArray("ElementId");
}

QString pointSetKey(vtkIdList* ids)
{
    QVector<vtkIdType> sorted;
    sorted.reserve(int(ids->GetNumberOfIds()));
    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
        sorted.push_back(ids->GetId(i));
    std::sort(sorted.begin(), sorted.end());

    QStringList parts;
    parts.reserve(sorted.size());
    for (vtkIdType id : sorted)
        parts.push_back(QString::number(static_cast<qlonglong>(id)));
    return parts.join(QLatin1Char(','));
}

FluxFaceCollection collectFluxFaces(const QString& domain, const QString& uuid)
{
    FluxFaceCollection result;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
    {
        result.error = QStringLiteral("网格服务不可用。");
        return result;
    }

    vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
    if (!grid)
        grid = meshSvc->getVtkUnstructuredGrid();
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        result.error = QStringLiteral("没有可用的实体网格。");
        return result;
    }

    const QVector<vtkIdType> selectedVector =
        PickManager::instance().getSelectedPointIds(domain, uuid, GridKind::Solid);
    QSet<vtkIdType> selected;
    selected.reserve(selectedVector.size());
    for (vtkIdType id : selectedVector)
        selected.insert(id);
    if (selected.isEmpty())
    {
        result.error = QStringLiteral("未选择边界节点。");
        return result;
    }

    vtkDataArray* nodeIdArray = findPointIdArray(grid);
    vtkDataArray* elementIdArray = findElementIdArray(grid);
    if (!nodeIdArray)
    {
        result.error = QStringLiteral("网格缺少节点编号。");
        return result;
    }

    QHash<QString, FluxFaceCandidate> candidates;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() < 2)
            continue;

        double cellCenter[3] = {0.0, 0.0, 0.0};
        vtkIdList* cellPointIds = cell->GetPointIds();
        if (!cellPointIds || cellPointIds->GetNumberOfIds() <= 0)
            continue;
        for (vtkIdType i = 0; i < cellPointIds->GetNumberOfIds(); ++i)
        {
            double point[3];
            grid->GetPoint(cellPointIds->GetId(i), point);
            for (int axis = 0; axis < 3; ++axis)
                cellCenter[axis] += point[axis];
        }
        for (double& value : cellCenter)
            value /= double(cellPointIds->GetNumberOfIds());

        const bool volumeCell = cell->GetCellDimension() == 3;
        const int subEntityCount = volumeCell ? cell->GetNumberOfFaces()
                                              : cell->GetNumberOfEdges();
        for (int localId = 0; localId < subEntityCount; ++localId)
        {
            vtkCell* subEntity = volumeCell ? cell->GetFace(localId)
                                            : cell->GetEdge(localId);
            if (!subEntity || !subEntity->GetPointIds())
                continue;
            vtkIdList* ids = subEntity->GetPointIds();
            const vtkIdType count = ids->GetNumberOfIds();
            if (count < 2)
                continue;

            bool allSelected = true;
            for (vtkIdType i = 0; i < count; ++i)
            {
                if (!selected.contains(ids->GetId(i)))
                {
                    allSelected = false;
                    break;
                }
            }
            if (!allSelected)
                continue;

            const QString key = pointSetKey(ids);
            FluxFaceCandidate& candidate = candidates[key];
            ++candidate.occurrences;
            if (candidate.occurrences != 1)
                continue;

            candidate.row.elementId = elementIdArray
                ? int(std::lround(elementIdArray->GetTuple1(cellId)))
                : int(cellId + 1);
            candidate.row.nodeIds.reserve(int(count));
            for (vtkIdType i = 0; i < count; ++i)
            {
                const vtkIdType pointId = ids->GetId(i);
                candidate.row.nodeIds.push_back(
                    int(std::lround(nodeIdArray->GetTuple1(pointId))));
                double point[3];
                grid->GetPoint(pointId, point);
                for (int axis = 0; axis < 3; ++axis)
                    candidate.row.center[axis] += point[axis];
            }
            for (int axis = 0; axis < 3; ++axis)
                candidate.row.center[axis] /= double(count);

            double norm2 = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                candidate.row.outward[axis] = candidate.row.center[axis] - cellCenter[axis];
                norm2 += candidate.row.outward[axis] * candidate.row.outward[axis];
            }
            if (norm2 > 1.0e-24)
            {
                const double invNorm = 1.0 / std::sqrt(norm2);
                for (double& value : candidate.row.outward)
                    value *= invNorm;
            }
        }
    }

    for (auto it = candidates.constBegin(); it != candidates.constEnd(); ++it)
    {
        if (it.value().occurrences != 1)
            continue;
        const FluxFaceRow& row = it.value().row;
        const int nodeCount = row.nodeIds.size();
        if (nodeCount < 2 || nodeCount > 4)
        {
            result.error = QStringLiteral("所选区域包含暂不支持的边界面，请重新选择。");
            result.rows.clear();
            return result;
        }
        if (result.nodeCount == 0)
            result.nodeCount = nodeCount;
        else if (result.nodeCount != nodeCount)
        {
            result.error = QStringLiteral("所选区域包含不同类型的边界面，请分开设置。");
            result.rows.clear();
            return result;
        }
        result.rows.push_back(row);
    }

    std::sort(result.rows.begin(), result.rows.end(),
              [](const FluxFaceRow& lhs, const FluxFaceRow& rhs)
              {
                  if (lhs.elementId != rhs.elementId)
                      return lhs.elementId < rhs.elementId;
                  return std::lexicographical_compare(
                      lhs.nodeIds.cbegin(), lhs.nodeIds.cend(),
                      rhs.nodeIds.cbegin(), rhs.nodeIds.cend());
              });
    if (result.rows.isEmpty() && result.error.isEmpty())
        result.error = QStringLiteral("未从所选节点识别出外边界面。");
    return result;
}
}

SeepageBoundaryPage::SeepageBoundaryPage(const QString& pageType,
                                         const QString& boundaryTitle,
                                         const QString& exportFileName,
                                         QWidget* parent,
                                         bool removeFileWhenEmpty)
    : BasePage(pageType), ui(new Ui::SeepageBoundaryPage),
      m_boundaryTitle(boundaryTitle), m_exportFileName(exportFileName),
      m_removeFileWhenEmpty(removeFileWhenEmpty)
{
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    if (pageType == QStringLiteral("SeepageBoundaryUpstreamWater"))
        m_hydraulicRole = HydraulicRole::Upstream;
    else if (pageType == QStringLiteral("SeepageBoundaryDownstreamWater"))
        m_hydraulicRole = HydraulicRole::Downstream;

    ui->labelBoundaryTitle->setText(QStringLiteral("未选择节点"));
    ui->boxSelectHintLabel->setText(QStringLiteral("Shift 框选，Ctrl 取消。"));
    ui->tableNodes->hide();

    setupBatchGroupCombo();
    configureHydraulicUi();

    connect(ui->btnClear, &QPushButton::clicked,
            this, &SeepageBoundaryPage::onClearClicked);
    connect(ui->checkEnablePointPick, &QCheckBox::toggled,
            this, &SeepageBoundaryPage::onPointPickToggled);
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind)
            {
                if (domain != pickDomain() || uuid != m_uuid || mode != PickMode::Node)
                    return;
                updateSelectionSummary();
                updateWaterLevelSliderRange();
                updateWaterLevelPreview();
            });

    updateSelectionSummary();
    updateHydraulicSummary();
}

SeepageBoundaryPage::~SeepageBoundaryPage()
{
    clearWaterLevelPreview();
    delete ui;
}

QString SeepageBoundaryPage::pickDomain()
{
    // 同一工程可包含多个渗流工况。选择域加入页面 UUID，
    // 允许不同工况在相同物理边界上保存各自的节点集合。
    return QStringLiteral("%1/%2").arg(getPageType(), m_uuid);
}

void SeepageBoundaryPage::beforeLoad()
{
    PickManager& picker = PickManager::instance();
    picker.updateStructure();
    refreshGroupCombo();
    picker.activate(pickDomain(), m_uuid, PickMode::Node);

    {
        const QSignalBlocker blocker(ui->checkEnablePointPick);
        ui->checkEnablePointPick->setChecked(false);
    }
    picker.setClickPickEnabled(pickDomain(), m_uuid, false);

    m_pageActive = true;
    updateSelectionSummary();
    updateWaterLevelSliderRange();
    updateWaterLevelPreview();
}

void SeepageBoundaryPage::beforeUnload()
{
    m_pageActive = false;
    clearWaterLevelPreview();
    PickManager& picker = PickManager::instance();
    picker.clearPreviewSelection(pickDomain(), m_uuid, PickMode::Node);
    picker.setClickPickEnabled(pickDomain(), m_uuid, true);
    picker.deactivate(pickDomain(), m_uuid, PickMode::Node);
}

QVector<int> SeepageBoundaryPage::selectedNodeIds()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return {};

    auto nodeIdArray = [](vtkUnstructuredGrid* grid) -> vtkIntArray*
    {
        if (!grid || !grid->GetPointData())
            return nullptr;
        if (auto* array = vtkIntArray::SafeDownCast(
                grid->GetPointData()->GetArray("node_id")))
            return array;
        return vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("NodeId"));
    };

    QSet<int> merged;
    auto collect = [&](GridKind kind, vtkUnstructuredGrid* grid)
    {
        vtkIntArray* ids = nodeIdArray(grid);
        if (!grid || !ids)
            return;
        const QVector<vtkIdType> selected =
            PickManager::instance().getSelectedPointIds(pickDomain(), m_uuid, kind);
        for (vtkIdType pointId : selected)
        {
            if (pointId < 0 || pointId >= grid->GetNumberOfPoints())
                continue;
            merged.insert(ids->GetValue(int(pointId)));
        }
    };

    collect(GridKind::Solid, meshSvc->getVtkUnstructuredGridSolid());
    collect(GridKind::Line, meshSvc->getVtkUnstructuredGridLine());

    QVector<int> result;
    result.reserve(merged.size());
    for (int id : merged)
        result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

int SeepageBoundaryPage::selectionCount()
{
    return selectedNodeIds().size();
}

SeepageBoundaryPage::HydraulicRole SeepageBoundaryPage::hydraulicRole() const
{
    return m_hydraulicRole;
}

bool SeepageBoundaryPage::hasHydraulicParameters() const
{
    return m_hydraulicRole != HydraulicRole::None;
}

bool SeepageBoundaryPage::hasHydraulicState() const
{
    return m_hasHydraulicState;
}

SeepageBoundaryPage::BoundaryConditionType
SeepageBoundaryPage::boundaryConditionType() const
{
    if (!hasHydraulicParameters())
        return BoundaryConditionType::WaterLevel;
    return static_cast<BoundaryConditionType>(
        ui->comboBoundaryConditionType->currentData().toInt());
}

bool SeepageBoundaryPage::usesWaterLevel() const
{
    return hasHydraulicParameters()
           && boundaryConditionType() == BoundaryConditionType::WaterLevel;
}

bool SeepageBoundaryPage::usesFlux() const
{
    return hasHydraulicParameters()
           && boundaryConditionType() == BoundaryConditionType::Flux;
}

double SeepageBoundaryPage::waterLevel() const
{
    return hasHydraulicParameters() ? ui->spinWaterLevel->value() : 0.0;
}

double SeepageBoundaryPage::flux() const
{
    return hasHydraulicParameters() ? ui->spinFlux->value() : 0.0;
}

double SeepageBoundaryPage::effectiveFlux() const
{
    return usesFlux() ? flux() : 0.0;
}

QVector<QPair<double, double>> SeepageBoundaryPage::waterLevelCurve() const
{
    return m_waterLevelCurve;
}

void SeepageBoundaryPage::setLegacyHydraulicState(
    double level, double flux, const QVector<QPair<double, double>>& curve)
{
    if (!hasHydraulicParameters() || m_hasHydraulicState)
        return;

    const QSignalBlocker levelBlocker(ui->spinWaterLevel);
    const QSignalBlocker fluxBlocker(ui->spinFlux);
    ui->spinWaterLevel->setValue(level);
    ui->spinFlux->setValue(flux);
    if (std::abs(flux) > 1.0e-30)
        ui->comboBoundaryConditionType->setCurrentIndex(
            ui->comboBoundaryConditionType->findData(
                int(BoundaryConditionType::Flux)));
    m_waterLevelCurve = curve;
    m_hasHydraulicState = true;
    updateBoundaryConditionUi();
    refreshWaterLevelCurveTable();
    updateWaterLevelSliderRange();
    updateHydraulicSummary();
    updateWaterLevelPreview();
}

void SeepageBoundaryPage::setTransientCurveEnabled(bool enabled,
                                                   double startTime,
                                                   double endTime)
{
    const bool hadDefaultCurve = m_waterLevelCurve.size() == 2
        && std::abs(m_waterLevelCurve[0].first - m_transientStart) <= 1.0e-9
        && std::abs(m_waterLevelCurve[1].first - m_transientEnd) <= 1.0e-9
        && std::abs(m_waterLevelCurve[0].second - m_waterLevelCurve[1].second) <= 1.0e-9;
    const double previousConstantLevel = hadDefaultCurve
                                             ? m_waterLevelCurve[0].second
                                             : waterLevel();

    m_transientCurveEnabled = enabled;
    m_transientStart = startTime;
    m_transientEnd = endTime;

    const bool visible = enabled && hasHydraulicParameters() && usesWaterLevel();
    ui->labelCurveTitle->setVisible(visible);
    ui->tableWaterLevelCurve->setVisible(visible);
    ui->widgetCurveButtons->setVisible(visible);
    ui->labelCurveHint->setVisible(visible);

    if (visible && m_waterLevelCurve.isEmpty())
        m_waterLevelCurve = defaultWaterLevelCurve();
    else if (visible && hadDefaultCurve)
        m_waterLevelCurve = {qMakePair(m_transientStart, previousConstantLevel),
                             qMakePair(m_transientEnd > m_transientStart
                                           ? m_transientEnd
                                           : m_transientStart + 1.0,
                                       previousConstantLevel)};

    updateBoundaryConditionUi();
    refreshWaterLevelCurveTable();
    updateHydraulicSummary();
}

bool SeepageBoundaryPage::validateHydraulicInputs(bool transient,
                                                  double startTime,
                                                  double endTime,
                                                  QString* errorMessage)
{
    if (!hasHydraulicParameters())
        return true;

    const auto fail = [errorMessage](const QString& text)
    {
        if (errorMessage)
            *errorMessage = text;
        return false;
    };

    if (selectionCount() <= 0)
        return fail(QStringLiteral("请选择%1边界。").arg(hydraulicRoleName()));

    if (usesFlux())
    {
        if (!std::isfinite(flux()))
            return fail(QStringLiteral("%1边界通量不是有效数值。").arg(hydraulicRoleName()));
        if (std::abs(flux()) <= 1.0e-30)
            return fail(QStringLiteral("%1通量边界需要输入非零通量。").arg(hydraulicRoleName()));
        const FluxFaceCollection faces = collectFluxFaces(pickDomain(), m_uuid);
        if (faces.rows.isEmpty())
            return fail(QStringLiteral("%1通量边界：%2")
                            .arg(hydraulicRoleName(), faces.error));
        return true;
    }

    if (!std::isfinite(waterLevel()))
        return fail(QStringLiteral("%1水位不是有效数值。").arg(hydraulicRoleName()));
    if (!transient)
        return true;

    if (m_waterLevelCurve.size() < 2)
        return fail(QStringLiteral("%1水位过程至少需要 2 个点。").arg(hydraulicRoleName()));

    double previous = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < m_waterLevelCurve.size(); ++i)
    {
        const auto& point = m_waterLevelCurve.at(i);
        if (!finitePair(point))
            return fail(QStringLiteral("%1水位过程第 %2 个点包含无效数值。")
                            .arg(hydraulicRoleName()).arg(i + 1));
        if (point.first <= previous)
            return fail(QStringLiteral("%1水位过程时间需递增。")
                            .arg(hydraulicRoleName()));
        previous = point.first;
    }

    if (m_waterLevelCurve.first().first > startTime)
        return fail(QStringLiteral("%1水位过程需覆盖起始时间 %2 s。")
                        .arg(hydraulicRoleName()).arg(startTime, 0, 'g', 12));
    if (m_waterLevelCurve.last().first < endTime)
        return fail(QStringLiteral("%1水位过程需覆盖终止时间 %2 s。")
                        .arg(hydraulicRoleName()).arg(endTime, 0, 'g', 12));
    return true;
}

void SeepageBoundaryPage::updateSelectionSummary()
{
    const int count = selectionCount();
    ui->labelBoundaryTitle->setText(
        count > 0 ? QStringLiteral("已选择 %1 个节点").arg(count)
                  : QStringLiteral("未选择节点"));

    double bounds[6];
    if (selectedPointBounds(bounds))
    {
        auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        int verticalAxis = meshSvc ? meshSvc->verticalAxis() - 1 : 1;
        if (verticalAxis < 0 || verticalAxis > 2)
            verticalAxis = 1;
        ui->labelElevationRangeValue->setText(
            QStringLiteral("%1 ～ %2 m")
                .arg(bounds[2 * verticalAxis], 0, 'g', 10)
                .arg(bounds[2 * verticalAxis + 1], 0, 'g', 10));
    }
    else
    {
        ui->labelElevationRangeValue->setText(QStringLiteral("-"));
    }

    emit selectionCountChanged(count);
    updateHydraulicSummary();
}

bool SeepageBoundaryPage::exportDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    // 水位边界写节点约束；通量边界只写面载荷，避免同一边界同时施加
    // Dirichlet（水头）和 Neumann（通量）条件。
    const QVector<int> ids = (!hasHydraulicParameters() || usesWaterLevel())
                                 ? selectedNodeIds() : QVector<int>();
    const QString outputPath = inputDir.filePath(m_exportFileName);
    if (ids.isEmpty() && m_removeFileWhenEmpty)
    {
        if (QFile::exists(outputPath) && !QFile::remove(outputPath))
            return false;
    }
    else
    {
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream os(&file);
        os << ids.size() << "\n";
        for (int i = 0; i < ids.size(); ++i)
            os << ids[i] << " " << (i + 1) << "\n";
    }

    if (!hasHydraulicParameters())
        return true;

    const QString faceName = fluxFaceFileName();
    if (faceName.isEmpty())
        return true;
    const QString facePath = inputDir.filePath(faceName);
    if (!usesFlux())
        return !QFile::exists(facePath) || QFile::remove(facePath);

    const FluxFaceCollection faces = collectFluxFaces(pickDomain(), m_uuid);
    if (faces.rows.isEmpty() || faces.nodeCount <= 0)
        return false;

    QFile faceFile(facePath);
    if (!faceFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&faceFile);
    const int makod = faces.nodeCount + 2;
    os << QStringLiteral("%1        %2     NJX   MAKOD\n")
              .arg(faces.rows.size()).arg(makod);
    if (faces.nodeCount == 4)
        os << "    KJX     KE1     KE2     JD1     JD2     JD3     JD4  JAT\n";
    else if (faces.nodeCount == 3)
        os << "    KJX     KE1     KE2     JD1     JD2     JD3  JAT\n";
    else
        os << "    KJX     KE1     KE2     JD1     JD2  JAT\n";

    for (int i = 0; i < faces.rows.size(); ++i)
    {
        const FluxFaceRow& row = faces.rows.at(i);
        os << QStringLiteral("%1 %2 %3")
                  .arg(i + 1, 9).arg(row.elementId, 9).arg(0, 9);
        for (int nodeId : row.nodeIds)
            os << QStringLiteral(" %1").arg(nodeId, 9);
        os << QStringLiteral(" %1\n").arg(makod, 9);
    }
    return true;
}

void SeepageBoundaryPage::onPointPickToggled(bool enabled)
{
    PickManager::instance().setClickPickEnabled(pickDomain(), m_uuid, enabled);
    ui->boxSelectHintLabel->setText(
        enabled ? QStringLiteral("单击点选；Shift 框选，Ctrl 取消。")
                : QStringLiteral("Shift 框选，Ctrl 取消。"));
}

void SeepageBoundaryPage::onBoundaryConditionTypeChanged(int)
{
    m_hasHydraulicState = true;
    updateBoundaryConditionUi();
    updateHydraulicSummary();
    updateWaterLevelPreview();
    emit hydraulicParametersChanged();
}

void SeepageBoundaryPage::onClearClicked()
{
    PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid, PickMode::Node);
    PickManager::instance().clearSelection(pickDomain(), m_uuid, PickMode::Node);
    updateSelectionSummary();
    updateWaterLevelSliderRange();
    updateWaterLevelPreview();
}

void SeepageBoundaryPage::setupBatchGroupCombo()
{
    if (!ui)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(tr("全部边界分组"), -1);
    ui->comboBoxDim->addItem(tr("点分组"), 0);
    ui->comboBoxDim->addItem(tr("线分组"), 1);
    ui->comboBoxDim->addItem(tr("面分组"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SeepageBoundaryPage::onDimChanged, Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    if (row < 0 || !ui)
                        return;
                    auto* meshSvc =
                        ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;

                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    const int dim = map.physicalDim(physId);
                    QVector<int> source;
                    if (dim == 0)
                        source = map.physicalPointIds(physId, 0);
                    else if (dim == 1)
                        source = map.physicalPointIds(physId, 1);
                    else if (dim == 2)
                        source = map.physicalPointIds(physId, 2);
                    else
                        return;

                    QVector<vtkIdType> ids;
                    ids.reserve(source.size());
                    for (int id : source)
                        ids.push_back(vtkIdType(id));
                    PickManager::instance().setPreviewPointIds(
                        pickDomain(), m_uuid, GridKind::Solid, ids);
                }, Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(
                        pickDomain(), m_uuid, PickMode::Node);
                }, Qt::UniqueConnection);
    }

    connect(ui->btnBatchAdd, &QPushButton::clicked, this,
            [this]() { onGroupChanged(ui->comboBoxGroup->currentIndex()); },
            Qt::UniqueConnection);
    refreshGroupCombo();
}

void SeepageBoundaryPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || !ui)
        return;

    const int previousPhysId = ui->comboBoxGroup->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();

    const QSignalBlocker blocker(ui->comboBoxGroup);
    ui->comboBoxGroup->clear();
    const auto nameMap = map.physicalNameMap();
    for (auto it = nameMap.constBegin(); it != nameMap.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physicalDim(physId);
        if (dim < 0 || dim > 2 || (dimFilter >= 0 && dim != dimFilter))
            continue;
        ui->comboBoxGroup->addItem(it.value(), physId);
    }

    const int restoredIndex = ui->comboBoxGroup->findData(previousPhysId);
    if (restoredIndex >= 0)
        ui->comboBoxGroup->setCurrentIndex(restoredIndex);
}

void SeepageBoundaryPage::onDimChanged(int)
{
    PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid, PickMode::Node);
    refreshGroupCombo();
}

void SeepageBoundaryPage::onGroupChanged(int index)
{
    if (index < 0 || !ui)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physicalDim(physId);
    QVector<int> source;
    if (dim == 0)
        source = map.physicalPointIds(physId, 0);
    else if (dim == 1)
        source = map.physicalPointIds(physId, 1);
    else if (dim == 2)
        source = map.physicalPointIds(physId, 2);
    else
        return;

    QVector<vtkIdType> ids;
    ids.reserve(source.size());
    for (int id : source)
        ids.push_back(vtkIdType(id));

    PickManager::instance().setSelectedPointIds(
        pickDomain(), m_uuid, GridKind::Solid, ids, false);
    PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid, PickMode::Node);
    updateSelectionSummary();
    updateWaterLevelSliderRange();
    updateWaterLevelPreview();
}

void SeepageBoundaryPage::configureHydraulicUi()
{
    const bool hydraulic = hasHydraulicParameters();
    ui->groupBoxHydraulic->setVisible(hydraulic);
    if (!hydraulic)
        return;

    ui->comboBoundaryConditionType->addItem(
        QStringLiteral("水位边界"), int(BoundaryConditionType::WaterLevel));
    ui->comboBoundaryConditionType->addItem(
        QStringLiteral("通量边界"), int(BoundaryConditionType::Flux));
    ui->labelWaterLevel->setText(QStringLiteral("%1水位").arg(hydraulicRoleName()));
    ui->labelFlux->setText(QStringLiteral("%1边界通量").arg(hydraulicRoleName()));
    ui->spinWaterLevel->setSuffix(QStringLiteral(" m"));
    ui->spinFlux->setSuffix(QStringLiteral(" m/s"));
    ui->sliderWaterLevel->setRange(0, kWaterLevelSliderSteps);

    connect(ui->comboBoundaryConditionType,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SeepageBoundaryPage::onBoundaryConditionTypeChanged);
    connect(ui->spinWaterLevel, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double)
            {
                m_hasHydraulicState = true;
                updateWaterLevelSliderRange();
                updateHydraulicSummary();
                updateWaterLevelPreview();
                emit hydraulicParametersChanged();
            });
    connect(ui->spinFlux, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double)
            {
                m_hasHydraulicState = true;
                updateHydraulicSummary();
                updateWaterLevelPreview();
                emit hydraulicParametersChanged();
            });
    connect(ui->sliderWaterLevel, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (m_sliderMaxLevel <= m_sliderMinLevel)
                    return;
                const double ratio = double(value) / double(kWaterLevelSliderSteps);
                ui->spinWaterLevel->setValue(
                    m_sliderMinLevel + ratio * (m_sliderMaxLevel - m_sliderMinLevel));
            });
    connect(ui->checkShowWaterLevelPreview, &QCheckBox::toggled, this,
            [this](bool)
            {
                updateHydraulicSummary();
                updateWaterLevelPreview();
            });
    ui->tableWaterLevelCurve->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWaterLevelCurve->verticalHeader()->setDefaultSectionSize(26);
    connect(ui->btnAddWaterLevelPoint, &QPushButton::clicked,
            this, &SeepageBoundaryPage::onAddWaterLevelPointClicked);
    connect(ui->btnRemoveWaterLevelPoint, &QPushButton::clicked,
            this, &SeepageBoundaryPage::onRemoveWaterLevelPointClicked);
    connect(ui->btnResetWaterLevelCurve, &QPushButton::clicked,
            this, &SeepageBoundaryPage::onResetWaterLevelCurveClicked);

    updateBoundaryConditionUi();
    setTransientCurveEnabled(false, 0.0, 0.0);
}

void SeepageBoundaryPage::updateBoundaryConditionUi()
{
    if (!hasHydraulicParameters())
        return;

    const bool head = usesWaterLevel();
    ui->labelWaterLevel->setVisible(head);
    ui->spinWaterLevel->setVisible(head);
    ui->labelWaterLevelSlider->setVisible(head);
    ui->sliderWaterLevel->setVisible(head);
    ui->labelFlux->setVisible(!head);
    ui->spinFlux->setVisible(!head);
    ui->labelFluxHint->setText(
        head ? QStringLiteral("通量由水头差计算。")
             : QStringLiteral("正值流出，负值流入；计算中保持不变。"));
    ui->checkShowWaterLevelPreview->setText(
        head ? QStringLiteral("显示当前水位")
             : QStringLiteral("显示通量方向"));

    const bool curveVisible = head && m_transientCurveEnabled;
    ui->labelCurveTitle->setVisible(curveVisible);
    ui->tableWaterLevelCurve->setVisible(curveVisible);
    ui->widgetCurveButtons->setVisible(curveVisible);
    ui->labelCurveHint->setVisible(curveVisible);
}

void SeepageBoundaryPage::updateHydraulicSummary()
{
    if (!hasHydraulicParameters())
        return;

    double bounds[6];
    if (!selectedPointBounds(bounds))
    {
        ui->labelHydraulicStatus->setText(
            QStringLiteral("请选择%1边界。").arg(hydraulicRoleName()));
        return;
    }

    if (usesFlux())
    {
        const double value = flux();
        if (std::abs(value) <= 1.0e-30)
            ui->labelHydraulicStatus->setText(QStringLiteral("请输入非零通量。"));
        else
            ui->labelHydraulicStatus->setText(
                QStringLiteral("%1 %2 m/s。")
                    .arg(value > 0.0 ? QStringLiteral("流出") : QStringLiteral("流入"))
                    .arg(std::abs(value), 0, 'g', 10));
        return;
    }

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    int verticalAxis = meshSvc ? meshSvc->verticalAxis() - 1 : 1;
    if (verticalAxis < 0 || verticalAxis > 2)
        verticalAxis = 1;
    const double minLevel = bounds[2 * verticalAxis];
    const double maxLevel = bounds[2 * verticalAxis + 1];
    const double level = waterLevel();
    QString relation;
    if (level < minLevel)
        relation = QStringLiteral("水位低于边界");
    else if (level > maxLevel)
        relation = QStringLiteral("边界位于水位以下");
    else
        relation = QStringLiteral("水位穿过边界");
    QString status = QStringLiteral("高程 %1 ～ %2 m；%3。")
                         .arg(minLevel, 0, 'g', 10)
                         .arg(maxLevel, 0, 'g', 10)
                         .arg(relation);

    if (meshSvc)
    {
        vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
        if (!grid)
            grid = meshSvc->getVtkUnstructuredGrid();
        if (grid && grid->GetNumberOfPoints() > 0)
        {
            double modelBounds[6];
            grid->GetBounds(modelBounds);
            if (level < modelBounds[2 * verticalAxis]
                || level > modelBounds[2 * verticalAxis + 1])
                status += QStringLiteral(" 水位超出模型范围。");
        }
    }
    ui->labelHydraulicStatus->setText(status);
}

void SeepageBoundaryPage::updateWaterLevelSliderRange()
{
    if (!hasHydraulicParameters())
        return;

    double bounds[6];
    bool hasBounds = selectedPointBounds(bounds);
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    int verticalAxis = meshSvc ? meshSvc->verticalAxis() - 1 : 1;
    if (verticalAxis < 0 || verticalAxis > 2)
        verticalAxis = 1;

    if (!hasBounds && meshSvc)
    {
        vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
        if (!grid)
            grid = meshSvc->getVtkUnstructuredGrid();
        if (grid && grid->GetNumberOfPoints() > 0)
        {
            grid->GetBounds(bounds);
            hasBounds = true;
        }
    }

    if (!hasBounds)
    {
        m_sliderMinLevel = waterLevel() - 1.0;
        m_sliderMaxLevel = waterLevel() + 1.0;
    }
    else
    {
        m_sliderMinLevel = bounds[2 * verticalAxis];
        m_sliderMaxLevel = bounds[2 * verticalAxis + 1];
        if (std::abs(m_sliderMaxLevel - m_sliderMinLevel) < 1.0e-12)
        {
            m_sliderMinLevel -= 0.5;
            m_sliderMaxLevel += 0.5;
        }
    }

    const double ratio = std::clamp(
        (waterLevel() - m_sliderMinLevel) / (m_sliderMaxLevel - m_sliderMinLevel),
        0.0, 1.0);
    const QSignalBlocker blocker(ui->sliderWaterLevel);
    ui->sliderWaterLevel->setValue(int(std::lround(ratio * kWaterLevelSliderSteps)));
}

bool SeepageBoundaryPage::selectedPointBounds(double bounds[6])
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return false;

    bool initialized = false;
    auto includeSelection = [&](GridKind kind, vtkUnstructuredGrid* grid)
    {
        if (!grid)
            return;
        const QVector<vtkIdType> ids = PickManager::instance().getSelectedPointIds(
            pickDomain(), m_uuid, kind);
        for (vtkIdType pid : ids)
        {
            if (pid < 0 || pid >= grid->GetNumberOfPoints())
                continue;
            double point[3];
            grid->GetPoint(pid, point);
            if (!initialized)
            {
                bounds[0] = bounds[1] = point[0];
                bounds[2] = bounds[3] = point[1];
                bounds[4] = bounds[5] = point[2];
                initialized = true;
            }
            else
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    bounds[2 * axis] = std::min(bounds[2 * axis], point[axis]);
                    bounds[2 * axis + 1] = std::max(bounds[2 * axis + 1], point[axis]);
                }
            }
        }
    };

    includeSelection(GridKind::Solid, meshSvc->getVtkUnstructuredGridSolid());
    includeSelection(GridKind::Line, meshSvc->getVtkUnstructuredGridLine());
    return initialized;
}

void SeepageBoundaryPage::clearWaterLevelPreview()
{
    auto* visualSvc = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visualSvc)
        return;
    visualSvc->removeProp(
        QStringLiteral("seepage_boundary_level_%1").arg(m_uuid), false);
    visualSvc->removeProp(
        QStringLiteral("seepage_boundary_flux_%1").arg(m_uuid), true);
}

void SeepageBoundaryPage::updateWaterLevelPreview()
{
    clearWaterLevelPreview();
    if (!m_pageActive || !hasHydraulicParameters()
        || !ui->checkShowWaterLevelPreview->isChecked())
        return;

    auto* visualSvc = ServiceRegistry::instance().getService<VisualWidgetService>();
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!visualSvc || !meshSvc)
        return;

    vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
    if (!grid)
        grid = meshSvc->getVtkUnstructuredGrid();
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return;

    if (usesFlux())
    {
        if (std::abs(flux()) <= 1.0e-30)
            return;
        const FluxFaceCollection faces = collectFluxFaces(pickDomain(), m_uuid);
        if (faces.rows.isEmpty())
            return;

        double modelBounds[6];
        grid->GetBounds(modelBounds);
        const double modelSpan = std::max({modelBounds[1] - modelBounds[0],
                                           modelBounds[3] - modelBounds[2],
                                           modelBounds[5] - modelBounds[4], 1.0});
        const int maximumArrows = 64;
        const int stride = std::max(1, int(std::ceil(
            double(faces.rows.size()) / double(maximumArrows))));

        auto points = vtkSmartPointer<vtkPoints>::New();
        auto vectors = vtkSmartPointer<vtkDoubleArray>::New();
        vectors->SetName("FluxDirection");
        vectors->SetNumberOfComponents(3);
        const double sign = flux() > 0.0 ? 1.0 : -1.0;
        for (int i = 0; i < faces.rows.size(); i += stride)
        {
            const FluxFaceRow& face = faces.rows.at(i);
            points->InsertNextPoint(face.center);
            vectors->InsertNextTuple3(sign * face.outward[0],
                                      sign * face.outward[1],
                                      sign * face.outward[2]);
        }

        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->GetPointData()->SetVectors(vectors);
        auto arrow = vtkSmartPointer<vtkArrowSource>::New();
        arrow->SetTipResolution(12);
        arrow->SetShaftResolution(12);
        auto glyph = vtkSmartPointer<vtkGlyph3D>::New();
        glyph->SetInputData(poly);
        glyph->SetSourceConnection(arrow->GetOutputPort());
        glyph->SetVectorModeToUseVector();
        glyph->OrientOn();
        glyph->SetScaleModeToDataScalingOff();
        glyph->SetScaleFactor(modelSpan * 0.035);
        glyph->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(glyph->GetOutputPort());
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        if (flux() > 0.0)
            actor->GetProperty()->SetColor(0.90, 0.25, 0.12);
        else
            actor->GetProperty()->SetColor(0.10, 0.48, 0.95);
        actor->GetProperty()->LightingOff();
        actor->PickableOff();
        visualSvc->addProp(
            QStringLiteral("seepage_boundary_flux_%1").arg(m_uuid), actor,
            QStringLiteral("渗流通量预览"),
            QStringLiteral("%1%2").arg(hydraulicRoleName(),
                flux() > 0.0 ? QStringLiteral("流出") : QStringLiteral("流入")),
            false, false);
        visualSvc->render();
        return;
    }

    double bounds[6];
    if (!selectedPointBounds(bounds))
        return;

    int verticalAxis = meshSvc->verticalAxis() - 1;
    if (verticalAxis < 0 || verticalAxis > 2)
        verticalAxis = 1;
    const double level = waterLevel();

    int lineAxis = -1;
    double largestSpan = -1.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis == verticalAxis)
            continue;
        const double axisSpan = std::abs(bounds[2 * axis + 1] - bounds[2 * axis]);
        if (axisSpan > largestSpan)
        {
            largestSpan = axisSpan;
            lineAxis = axis;
        }
    }
    if (lineAxis < 0)
        return;
    const int otherAxis = 3 - verticalAxis - lineAxis;

    double modelBounds[6];
    grid->GetBounds(modelBounds);
    const double modelSpan = std::max({modelBounds[1] - modelBounds[0],
                                       modelBounds[3] - modelBounds[2],
                                       modelBounds[5] - modelBounds[4], 1.0});
    const double tolerance = modelSpan * 1.0e-8;
    if (level < modelBounds[2 * verticalAxis] - tolerance
        || level > modelBounds[2 * verticalAxis + 1] + tolerance)
    {
        updateHydraulicSummary();
        return;
    }

    const double minimumLength = modelSpan * 0.04;
    double start = bounds[2 * lineAxis];
    double end = bounds[2 * lineAxis + 1];
    if (std::abs(end - start) < minimumLength)
    {
        const double center = 0.5 * (start + end);
        start = center - 0.5 * minimumLength;
        end = center + 0.5 * minimumLength;
    }

    double p0[3] = {0.5 * (bounds[0] + bounds[1]),
                    0.5 * (bounds[2] + bounds[3]),
                    0.5 * (bounds[4] + bounds[5])};
    double p1[3] = {p0[0], p0[1], p0[2]};
    p0[verticalAxis] = p1[verticalAxis] = level;
    p0[lineAxis] = start;
    p1[lineAxis] = end;
    p0[otherAxis] = p1[otherAxis] = 0.5 * (bounds[2 * otherAxis]
                                           + bounds[2 * otherAxis + 1]);

    auto points = vtkSmartPointer<vtkPoints>::New();
    vtkIdType ids[2] = {points->InsertNextPoint(p0), points->InsertNextPoint(p1)};
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    lines->InsertNextCell(2, ids);
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);
    poly->SetLines(lines);
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(poly);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    const bool upstream = m_hydraulicRole == HydraulicRole::Upstream;
    actor->GetProperty()->SetColor(upstream ? 0.12 : 0.05,
                                   upstream ? 0.45 : 0.72,
                                   upstream ? 0.95 : 0.82);
    actor->GetProperty()->SetLineWidth(4.0);
    actor->GetProperty()->LightingOff();
    actor->PickableOff();
    visualSvc->addProp(QStringLiteral("seepage_boundary_level_%1").arg(m_uuid),
                       actor, QStringLiteral("渗流水位预览"),
                       QStringLiteral("%1水位").arg(hydraulicRoleName()), false, false);
    visualSvc->render();
    updateHydraulicSummary();
}

QVector<QPair<double, double>> SeepageBoundaryPage::defaultWaterLevelCurve() const
{
    const double start = m_transientStart;
    const double end = m_transientEnd > start ? m_transientEnd : start + 1.0;
    return {qMakePair(start, waterLevel()), qMakePair(end, waterLevel())};
}

void SeepageBoundaryPage::appendWaterLevelCurveRow(double time, double level)
{
    const int row = ui->tableWaterLevelCurve->rowCount();
    ui->tableWaterLevelCurve->insertRow(row);

    auto* timeSpin = new QDoubleSpinBox(ui->tableWaterLevelCurve);
    timeSpin->setDecimals(6);
    timeSpin->setRange(-1.0e12, 1.0e12);
    timeSpin->setKeyboardTracking(false);
    timeSpin->setValue(time);

    auto* levelSpin = new QDoubleSpinBox(ui->tableWaterLevelCurve);
    levelSpin->setDecimals(4);
    levelSpin->setRange(-1.0e6, 1.0e6);
    levelSpin->setKeyboardTracking(false);
    levelSpin->setValue(level);

    ui->tableWaterLevelCurve->setCellWidget(row, 0, timeSpin);
    ui->tableWaterLevelCurve->setCellWidget(row, 1, levelSpin);

    connect(timeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { syncWaterLevelCurveFromTable(); });
    connect(levelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { syncWaterLevelCurveFromTable(); });
}

void SeepageBoundaryPage::refreshWaterLevelCurveTable()
{
    if (!hasHydraulicParameters())
        return;

    m_updatingCurveTable = true;
    ui->tableWaterLevelCurve->setRowCount(0);
    for (const auto& point : m_waterLevelCurve)
        appendWaterLevelCurveRow(point.first, point.second);
    m_updatingCurveTable = false;

    ui->btnRemoveWaterLevelPoint->setEnabled(m_waterLevelCurve.size() > 2);
    updateWaterLevelCurveHint();
}

void SeepageBoundaryPage::syncWaterLevelCurveFromTable()
{
    if (m_updatingCurveTable)
        return;

    QVector<QPair<double, double>> curve;
    curve.reserve(ui->tableWaterLevelCurve->rowCount());
    for (int row = 0; row < ui->tableWaterLevelCurve->rowCount(); ++row)
    {
        auto* timeSpin = qobject_cast<QDoubleSpinBox*>(
            ui->tableWaterLevelCurve->cellWidget(row, 0));
        auto* levelSpin = qobject_cast<QDoubleSpinBox*>(
            ui->tableWaterLevelCurve->cellWidget(row, 1));
        if (!timeSpin || !levelSpin)
            continue;
        curve.push_back(qMakePair(timeSpin->value(), levelSpin->value()));
    }

    m_waterLevelCurve = curve;
    m_hasHydraulicState = true;
    ui->btnRemoveWaterLevelPoint->setEnabled(m_waterLevelCurve.size() > 2);
    updateWaterLevelCurveHint();
    emit hydraulicParametersChanged();
}

void SeepageBoundaryPage::updateWaterLevelCurveHint()
{
    if (!m_transientCurveEnabled || !hasHydraulicParameters())
        return;

    if (m_waterLevelCurve.size() < 2)
    {
        ui->labelCurveHint->setText(QStringLiteral("至少需要 2 个点。"));
        return;
    }

    for (int i = 1; i < m_waterLevelCurve.size(); ++i)
    {
        if (m_waterLevelCurve[i].first <= m_waterLevelCurve[i - 1].first)
        {
            ui->labelCurveHint->setText(QStringLiteral("时间需递增。"));
            return;
        }
    }

    if (m_waterLevelCurve.first().first > m_transientStart
        || m_waterLevelCurve.last().first < m_transientEnd)
    {
        ui->labelCurveHint->setText(
            QStringLiteral("需覆盖 %1 ～ %2 s。")
                .arg(m_transientStart, 0, 'g', 12)
                .arg(m_transientEnd, 0, 'g', 12));
        return;
    }

    ui->labelCurveHint->setText(
        QStringLiteral("%1 个点，%2 ～ %3 s。")
            .arg(m_waterLevelCurve.size())
            .arg(m_waterLevelCurve.first().first, 0, 'g', 12)
            .arg(m_waterLevelCurve.last().first, 0, 'g', 12));
}

void SeepageBoundaryPage::onAddWaterLevelPointClicked()
{
    if (m_waterLevelCurve.isEmpty())
    {
        m_waterLevelCurve = defaultWaterLevelCurve();
    }
    else if (m_waterLevelCurve.size() == 1)
    {
        const double end = m_transientEnd > m_waterLevelCurve.first().first
                               ? m_transientEnd
                               : m_waterLevelCurve.first().first + 1.0;
        m_waterLevelCurve.push_back(qMakePair(end, m_waterLevelCurve.first().second));
    }
    else
    {
        int insertAt = m_waterLevelCurve.size();
        double bestGap = 0.0;
        double time = m_waterLevelCurve.last().first + 1.0;
        double level = m_waterLevelCurve.last().second;
        for (int i = 1; i < m_waterLevelCurve.size(); ++i)
        {
            const double gap = m_waterLevelCurve[i].first - m_waterLevelCurve[i - 1].first;
            if (gap > bestGap)
            {
                bestGap = gap;
                insertAt = i;
                time = 0.5 * (m_waterLevelCurve[i - 1].first
                              + m_waterLevelCurve[i].first);
                level = 0.5 * (m_waterLevelCurve[i - 1].second
                               + m_waterLevelCurve[i].second);
            }
        }
        m_waterLevelCurve.insert(insertAt, qMakePair(time, level));
    }

    m_hasHydraulicState = true;
    refreshWaterLevelCurveTable();
    emit hydraulicParametersChanged();
}

void SeepageBoundaryPage::onRemoveWaterLevelPointClicked()
{
    if (m_waterLevelCurve.size() <= 2)
        return;
    const int row = ui->tableWaterLevelCurve->currentRow();
    if (row < 0 || row >= m_waterLevelCurve.size())
        return;

    m_waterLevelCurve.removeAt(row);
    m_hasHydraulicState = true;
    refreshWaterLevelCurveTable();
    emit hydraulicParametersChanged();
}

void SeepageBoundaryPage::onResetWaterLevelCurveClicked()
{
    m_waterLevelCurve = defaultWaterLevelCurve();
    m_hasHydraulicState = true;
    refreshWaterLevelCurveTable();
    emit hydraulicParametersChanged();
}

QString SeepageBoundaryPage::hydraulicRoleName() const
{
    return m_hydraulicRole == HydraulicRole::Upstream
               ? QStringLiteral("上游")
               : m_hydraulicRole == HydraulicRole::Downstream
                     ? QStringLiteral("下游") : QString();
}

QString SeepageBoundaryPage::fluxFaceFileName() const
{
    if (m_hydraulicRole == HydraulicRole::Upstream)
        return QStringLiteral("FA_UPSEEP.DAT");
    if (m_hydraulicRole == HydraulicRole::Downstream)
        return QStringLiteral("FA_DWSEEP.DAT");
    return QString();
}

QJsonObject SeepageBoundaryPage::saveJson()
{
    QJsonObject obj;
    obj[QStringLiteral("dim_index")] = ui->comboBoxDim->currentIndex();
    obj[QStringLiteral("group_phys_id")] = ui->comboBoxGroup->currentData().toInt();

    auto toJson = [](const QVector<vtkIdType>& ids)
    {
        QJsonArray array;
        for (vtkIdType id : ids)
            array.append(static_cast<qint64>(id));
        return array;
    };
    obj[QStringLiteral("picked_solid")] = toJson(
        PickManager::instance().getSelectedPointIds(pickDomain(), m_uuid, GridKind::Solid));
    obj[QStringLiteral("picked_line")] = toJson(
        PickManager::instance().getSelectedPointIds(pickDomain(), m_uuid, GridKind::Line));

    if (hasHydraulicParameters())
    {
        obj[QStringLiteral("boundary_condition_type")] =
            usesFlux() ? QStringLiteral("flux") : QStringLiteral("head");
        obj[QStringLiteral("water_level")] = waterLevel();
        obj[QStringLiteral("flux")] = flux();
        obj[QStringLiteral("show_water_level_preview")] =
            ui->checkShowWaterLevelPreview->isChecked();
        QJsonArray curve;
        for (const auto& point : m_waterLevelCurve)
        {
            QJsonObject row;
            row[QStringLiteral("time")] = point.first;
            row[QStringLiteral("head")] = point.second;
            curve.append(row);
        }
        obj[QStringLiteral("water_level_curve")] = curve;
    }
    return obj;
}

void SeepageBoundaryPage::loadJson(const QJsonObject& obj)
{
    const int dimIndex = obj.value(QStringLiteral("dim_index")).toInt(0);
    ui->comboBoxDim->setCurrentIndex(dimIndex);

    const int physId = obj.value(QStringLiteral("group_phys_id")).toInt(-1);
    for (int i = 0; i < ui->comboBoxGroup->count(); ++i)
    {
        if (ui->comboBoxGroup->itemData(i).toInt() == physId)
        {
            const QSignalBlocker blocker(ui->comboBoxGroup);
            ui->comboBoxGroup->setCurrentIndex(i);
            break;
        }
    }

    auto fromJson = [](const QJsonArray& array)
    {
        QVector<vtkIdType> ids;
        ids.reserve(array.size());
        for (const auto& value : array)
            ids.push_back(static_cast<vtkIdType>(value.toVariant().toLongLong()));
        return ids;
    };
    PickManager::instance().setSelectedPointIds(
        pickDomain(), m_uuid, GridKind::Solid,
        fromJson(obj.value(QStringLiteral("picked_solid")).toArray()), true);
    PickManager::instance().setSelectedPointIds(
        pickDomain(), m_uuid, GridKind::Line,
        fromJson(obj.value(QStringLiteral("picked_line")).toArray()), true);

    if (hasHydraulicParameters())
    {
        m_hasHydraulicState = obj.contains(QStringLiteral("water_level"))
                              || obj.contains(QStringLiteral("flux"))
                              || obj.contains(QStringLiteral("water_level_curve"))
                              || obj.contains(QStringLiteral("boundary_condition_type"));
        QString conditionType = obj.value(QStringLiteral("boundary_condition_type")).toString();
        if (conditionType.isEmpty()
            && std::abs(obj.value(QStringLiteral("flux")).toDouble()) > 1.0e-30)
            conditionType = QStringLiteral("flux");
        const int conditionData = conditionType == QStringLiteral("flux")
                                      ? int(BoundaryConditionType::Flux)
                                      : int(BoundaryConditionType::WaterLevel);
        const int conditionIndex = ui->comboBoundaryConditionType->findData(conditionData);
        if (conditionIndex >= 0)
        {
            const QSignalBlocker conditionBlocker(ui->comboBoundaryConditionType);
            ui->comboBoundaryConditionType->setCurrentIndex(conditionIndex);
        }
        const QSignalBlocker levelBlocker(ui->spinWaterLevel);
        const QSignalBlocker fluxBlocker(ui->spinFlux);
        ui->spinWaterLevel->setValue(
            obj.value(QStringLiteral("water_level")).toDouble(ui->spinWaterLevel->value()));
        ui->spinFlux->setValue(
            obj.value(QStringLiteral("flux")).toDouble(ui->spinFlux->value()));
        ui->checkShowWaterLevelPreview->setChecked(
            obj.value(QStringLiteral("show_water_level_preview")).toBool(true));
        m_waterLevelCurve.clear();
        for (const auto& value : obj.value(QStringLiteral("water_level_curve")).toArray())
        {
            const QJsonObject row = value.toObject();
            m_waterLevelCurve.push_back({row.value(QStringLiteral("time")).toDouble(),
                                         row.value(QStringLiteral("head")).toDouble()});
        }
    }

    updateBoundaryConditionUi();
    refreshWaterLevelCurveTable();
    updateSelectionSummary();
    updateWaterLevelSliderRange();
    updateHydraulicSummary();
    updateWaterLevelPreview();
}
