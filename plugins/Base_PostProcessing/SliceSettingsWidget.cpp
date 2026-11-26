#include <QFileInfo>
#include <QDateTime>
#include "SliceSettingsWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkAppendFilter.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkCutter.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkIdList.h>
#include <vtkLabeledDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPlane.h>
#include <vtkPlaneSource.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalBlocker>

#include "PostFieldUtils.h"
#include "ResultDisplayNames.h"
#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ui_SliceSettingsWidget.h"

#include <vtkDataSetSurfaceFilter.h>

#include <algorithm>
#include <cmath>

static QVector<double> genUniformTs(int N, bool includeEnds)
{
    QVector<double> ts;
    N = std::max(1, N);
    ts.reserve(N);

    const double eps = 1e-6; // 防止 t=0 或 1 处某些单元切不到

    if (includeEnds)
    {
        if (N == 1)
        {
            ts << 0.5; // 如果你想 N=1 且勾选时生成两条端点切面，可改成
                       // ts<<eps<<1.0-eps
        }
        else
        {
            for (int i = 0; i < N; ++i)
            {
                double t = double(i) / double(N - 1); // [0,1]
                if (t <= 0.0)
                    t = eps;
                if (t >= 1.0)
                    t = 1.0 - eps;
                ts << t;
            }
        }
    }
    else
    {
        if (N == 1)
        {
            ts << 0.5;
        }
        else
        {
            const double step = 1.0 / double(N + 1);
            for (int i = 0; i < N; ++i)
                ts << (i + 1) * step; // (0,1)
        }
    }
    return ts;
}

static bool resolveSliceContourRange(vtkPolyData* sliceOutput,
                                     const QString& scalarName,
                                     const double displayRange[2],
                                     double contourRange[2])
{
    if (!sliceOutput || scalarName.isEmpty() || !displayRange || !contourRange ||
        sliceOutput->GetNumberOfPoints() <= 0 || sliceOutput->GetNumberOfCells() <= 0)
        return false;

    vtkPointData* pointData = sliceOutput->GetPointData();
    const QByteArray encodedName = scalarName.toUtf8();
    vtkDataArray* scalarArray = pointData ? pointData->GetArray(encodedName.constData()) : nullptr;
    if (!scalarArray || scalarArray->GetNumberOfComponents() != 1 ||
        scalarArray->GetNumberOfTuples() < sliceOutput->GetNumberOfPoints())
        return false;

    double sliceRange[2]{0.0, 0.0};
    scalarArray->GetRange(sliceRange);
    if (!std::isfinite(sliceRange[0]) || !std::isfinite(sliceRange[1]) ||
        !std::isfinite(displayRange[0]) || !std::isfinite(displayRange[1]))
        return false;

    const double displayMin = std::min(displayRange[0], displayRange[1]);
    const double displayMax = std::max(displayRange[0], displayRange[1]);
    contourRange[0] = std::max(sliceRange[0], displayMin);
    contourRange[1] = std::min(sliceRange[1], displayMax);
    return contourRange[1] > contourRange[0];
}

static vtkSmartPointer<vtkActor2D> createSliceContourLabelActor(
    const vtkSmartPointer<vtkActor>& contourActor,
    const QString& scalarName,
    double minV,
    double maxV)
{
    if (!contourActor || scalarName.isEmpty() || !postContourStyle().labels)
        return nullptr;

    auto* contourMapper = vtkPolyDataMapper::SafeDownCast(contourActor->GetMapper());
    if (!contourMapper)
        return nullptr;
    contourMapper->Update();

    vtkPolyData* contourData = contourMapper->GetInput();
    const QByteArray scalarNameUtf8 = scalarName.toUtf8();
    vtkDataArray* contourScalars = contourData && contourData->GetPointData()
                                       ? contourData->GetPointData()->GetArray(
                                             scalarNameUtf8.constData())
                                       : nullptr;
    if (!contourData || !contourScalars || contourData->GetNumberOfCells() <= 0 ||
        contourScalars->GetNumberOfTuples() < contourData->GetNumberOfPoints())
        return nullptr;

    struct LabelCandidate
    {
        double value{0.0};
        vtkIdType pointId{-1};
        double length{0.0};
    };

    QVector<LabelCandidate> candidates;
    candidates.reserve(qMax(2, postContourStyle().levels));
    const double tolerance = std::max(std::abs(maxV - minV), 1.0e-12) * 1.0e-5;

    for (vtkIdType cellIndex = 0; cellIndex < contourData->GetNumberOfCells(); ++cellIndex)
    {
        vtkCell* cell = contourData->GetCell(cellIndex);
        vtkIdList* ids = cell ? cell->GetPointIds() : nullptr;
        if (!ids || ids->GetNumberOfIds() < 2)
            continue;

        const vtkIdType midpointId = ids->GetId(ids->GetNumberOfIds() / 2);
        if (midpointId < 0 || midpointId >= contourData->GetNumberOfPoints())
            continue;
        const double value = contourScalars->GetComponent(midpointId, 0);
        if (!std::isfinite(value))
            continue;

        double lineLength = 0.0;
        double previousPoint[3]{0.0, 0.0, 0.0};
        contourData->GetPoint(ids->GetId(0), previousPoint);
        for (vtkIdType pointIndex = 1; pointIndex < ids->GetNumberOfIds(); ++pointIndex)
        {
            double point[3]{0.0, 0.0, 0.0};
            contourData->GetPoint(ids->GetId(pointIndex), point);
            const double dx = point[0] - previousPoint[0];
            const double dy = point[1] - previousPoint[1];
            const double dz = point[2] - previousPoint[2];
            lineLength += std::sqrt(dx * dx + dy * dy + dz * dz);
            previousPoint[0] = point[0];
            previousPoint[1] = point[1];
            previousPoint[2] = point[2];
        }

        int existingIndex = -1;
        for (int i = 0; i < candidates.size(); ++i)
        {
            if (std::abs(candidates[i].value - value) <= tolerance)
            {
                existingIndex = i;
                break;
            }
        }

        const LabelCandidate candidate{value, midpointId, lineLength};
        if (existingIndex < 0)
            candidates.push_back(candidate);
        else if (lineLength > candidates[existingIndex].length)
            candidates[existingIndex] = candidate;
    }

    if (candidates.isEmpty())
        return nullptr;

    std::sort(candidates.begin(), candidates.end(),
              [](const LabelCandidate& left, const LabelCandidate& right) {
                  return left.value < right.value;
              });

    auto labelPoints = vtkSmartPointer<vtkPoints>::New();
    auto labelValues = vtkSmartPointer<vtkDoubleArray>::New();
    labelValues->SetName("ContourLabels");
    for (const LabelCandidate& candidate : candidates)
    {
        double point[3]{0.0, 0.0, 0.0};
        contourData->GetPoint(candidate.pointId, point);
        labelPoints->InsertNextPoint(point);
        labelValues->InsertNextValue(candidate.value);
    }

    auto labelData = vtkSmartPointer<vtkPolyData>::New();
    labelData->SetPoints(labelPoints);
    labelData->GetPointData()->SetScalars(labelValues);

    auto labelMapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
    labelMapper->SetInputData(labelData);
    labelMapper->SetLabelModeToLabelScalars();
    const QByteArray labelFormat =
        QStringLiteral("%.%1g").arg(postScalarBarStyle().precision).toLatin1();
    labelMapper->SetLabelFormat(labelFormat.constData());
    vtkTextProperty* textProperty = labelMapper->GetLabelTextProperty();
    textProperty->SetFontSize(postContourStyle().labelFont);
    textProperty->SetColor(0.08, 0.08, 0.08);
    textProperty->SetBold(0);
    textProperty->SetShadow(0);
    textProperty->SetBackgroundColor(1.0, 1.0, 1.0);
    textProperty->SetBackgroundOpacity(0.72);
    textProperty->SetJustificationToCentered();
    textProperty->SetVerticalJustificationToCentered();

    auto labelActor = vtkSmartPointer<vtkActor2D>::New();
    labelActor->SetMapper(labelMapper);
    labelActor->PickableOff();
    return labelActor;
}

static vtkSmartPointer<vtkActor> createContextModelActor(vtkUnstructuredGrid* displayGrid)
{
    return createPostContextModelActor(displayGrid);
}

static vtkSmartPointer<vtkActor> createCutPlaneIndicatorActor(vtkPlane* plane, const double bounds[6])
{
    if (!plane || !bounds)
        return {};
    double n[3]{0.0, 0.0, 1.0};
    double orig[3]{0.0, 0.0, 0.0};
    plane->GetNormal(n);
    plane->GetOrigin(orig);
    const double nLen = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nLen <= 1.0e-12)
        return {};
    n[0] /= nLen;
    n[1] /= nLen;
    n[2] /= nLen;

    const double cx = 0.5 * (bounds[0] + bounds[1]);
    const double cy = 0.5 * (bounds[2] + bounds[3]);
    const double cz = 0.5 * (bounds[4] + bounds[5]);
    const double dx = std::max(bounds[1] - bounds[0], 1.0e-6);
    const double dy = std::max(bounds[3] - bounds[2], 1.0e-6);
    const double dz = std::max(bounds[5] - bounds[4], 1.0e-6);

    // Project bounding-box center onto the cutting plane so the indicator quad covers the model.
    const double dist = (cx - orig[0]) * n[0] + (cy - orig[1]) * n[1] + (cz - orig[2]) * n[2];
    const double pc[3]{cx - dist * n[0], cy - dist * n[1], cz - dist * n[2]};

    double u[3]{0.0, 0.0, 0.0};
    double v[3]{0.0, 0.0, 0.0};
    double sizeU = 1.0;
    double sizeV = 1.0;
    if (std::abs(n[0]) >= std::abs(n[1]) && std::abs(n[0]) >= std::abs(n[2]))
    {
        u[1] = 1.0;
        v[2] = 1.0;
        sizeU = dy * 1.06;
        sizeV = dz * 1.06;
    }
    else if (std::abs(n[1]) >= std::abs(n[0]) && std::abs(n[1]) >= std::abs(n[2]))
    {
        u[0] = 1.0;
        v[2] = 1.0;
        sizeU = dx * 1.06;
        sizeV = dz * 1.06;
    }
    else
    {
        u[0] = 1.0;
        v[1] = 1.0;
        sizeU = dx * 1.06;
        sizeV = dy * 1.06;
    }

    auto planeSource = vtkSmartPointer<vtkPlaneSource>::New();
    planeSource->SetOrigin(pc[0] - 0.5 * sizeU * u[0] - 0.5 * sizeV * v[0],
                           pc[1] - 0.5 * sizeU * u[1] - 0.5 * sizeV * v[1],
                           pc[2] - 0.5 * sizeU * u[2] - 0.5 * sizeV * v[2]);
    planeSource->SetPoint1(pc[0] + 0.5 * sizeU * u[0] - 0.5 * sizeV * v[0],
                           pc[1] + 0.5 * sizeU * u[1] - 0.5 * sizeV * v[1],
                           pc[2] + 0.5 * sizeU * u[2] - 0.5 * sizeV * v[2]);
    planeSource->SetPoint2(pc[0] - 0.5 * sizeU * u[0] + 0.5 * sizeV * v[0],
                           pc[1] - 0.5 * sizeU * u[1] + 0.5 * sizeV * v[1],
                           pc[2] - 0.5 * sizeU * u[2] + 0.5 * sizeV * v[2]);
    planeSource->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(planeSource->GetOutputPort());
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->SetColor(0.95, 0.48, 0.10);
    actor->GetProperty()->SetOpacity(0.14);
    actor->GetProperty()->EdgeVisibilityOn();
    actor->GetProperty()->SetEdgeColor(0.95, 0.45, 0.08);
    actor->GetProperty()->SetLineWidth(1.8);
    actor->GetProperty()->LightingOff();
    return actor;
}

SliceSettingsWidget::SliceSettingsWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::SliceSettingsWidget), BasePage(PublicConstants::kPagePostProcessSlice),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);
    ui->displayGroupBox->setVisible(false);

    configurePostResultCombo(ui->analysisComboBox);
    configurePostResultCombo(ui->resultDirComboBox);
    configurePostResultCombo(ui->resultTimeComboBox);
    configurePostResultCombo(ui->stepComboBox);

    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    connect(ui->showScalarBarCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        postScalarBarStyle().visible = checked;
        if (m_visualService)
        {
            m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
            m_visualService->render();
        }
    });
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);
    connect(ui->showContourCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        postContourStyle().enabled = checked;
        if (m_singleDisplayed)
            buildSingleSliceResult();
        else if (m_multiDisplayed)
            buildMultiSliceResult();
    });
    connect(ui->displaySettingsButton, &QPushButton::clicked, this, [this]() {
        const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
        double dataMin = 0.0;
        double dataMax = 1.0;
        if (m_grid)
        {
            if (vtkDataArray* array = PostFieldUtils::array(m_grid, field))
            {
                double range[2]{0.0, 1.0};
                array->GetRange(range);
                dataMin = range[0];
                dataMax = range[1];
            }
        }
        if (!editPostDisplaySettings(this, m_visualService, true, false,
                                     field.name, dataMin, dataMax))
            return;
        ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
        ui->showContourCheckBox->setChecked(postContourStyle().enabled);
        if (m_singleDisplayed)
            buildSingleSliceResult();
        else if (m_multiDisplayed)
            buildMultiSliceResult();
        else if (ui->scalarComboBox->currentIndex() >= 0)
            on_scalarComboBox_currentIndexChanged(ui->scalarComboBox->currentIndex());
    });

    ui->sliceSlider->setRange(0, 1000);
    ui->sliceSlider->setValue(500);
    ui->sliceSlider->setTracking(false);
    ui->slicePositionSpin->setKeyboardTracking(false);
    ui->modeStackedWidget->setCurrentIndex(0);

    connect(ui->sliceSlider, &QSlider::valueChanged, this, &SliceSettingsWidget::showCutPlane);

    EventManager::instance().registerHandler(
        postDisplaySettingsChangedEvent(), this,
        [this](const QVariantMap&)
        {
            if (!property("postDisplayActive").toBool())
                return;
            {
                const QSignalBlocker scalarBarBlocker(ui->showScalarBarCheckBox);
                const QSignalBlocker contourBlocker(ui->showContourCheckBox);
                ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
                ui->showContourCheckBox->setChecked(postContourStyle().enabled);
            }
            if (ui->scalarComboBox->currentIndex() >= 0)
                on_scalarComboBox_currentIndexChanged(ui->scalarComboBox->currentIndex());
        });

    ui->sliceNumSpin->setRange(1, 100);
    ui->sliceNumSpin->setValue(5);
    ui->confirmButton->setVisible(false); // 单切面实时更新；仅多切面需要生成按钮
}

SliceSettingsWidget::~SliceSettingsWidget()
{
    delete ui;
}

void SliceSettingsWidget::beforeLoad()
{
    const QSignalBlocker legendBlocker(ui->showScalarBarCheckBox);
    const QSignalBlocker contourBlocker(ui->showContourCheckBox);
    setProperty("postDisplayActive", true);
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);
    refreshResultGroups();
    const QString curStepFile = ui->stepComboBox->currentData().toString();
    const QFileInfo stepInfo(curStepFile.isEmpty() ? m_loadedFilePath : curStepFile);
    const bool fileStale = !m_loadedOnce || !m_grid || m_loadedFilePath.isEmpty() || !stepInfo.isFile() ||
                           QDir::cleanPath(stepInfo.absoluteFilePath()) != QDir::cleanPath(m_loadedFilePath) ||
                           stepInfo.size() != m_loadedFileSize ||
                           stepInfo.lastModified().toMSecsSinceEpoch() != m_loadedFileModified;
    if (fileStale)
    {
        if (!m_plane)
            m_plane = vtkSmartPointer<vtkPlane>::New();

        const QString workPath =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (m_dataPath.isEmpty())
            m_dataPath = PostResultAutoPrepare::firstResultDirectory(workPath);
        loadFileName(m_dataPath);

        m_loadedOnce = true;
    }

    applyPendingState();

    // 切回页面：根据状态挂回
    reattachAccordingToDisplayState();
    if (m_visualService)
    {
        const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
        if (!field.name.isEmpty())
        {
            m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
            applyStandardPostScalarBar(
                m_visualService, ResultDisplayNames::scalarBarName(field.name));
        }
    }

    if (m_grid)
    {
        const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
        if (vtkDataArray* array = PostFieldUtils::array(m_grid, field))
        {
            double dataRange[2]{0.0, 1.0};
            array->GetRange(dataRange);
            if (property("postDisplayActive").toBool())
                publishPostDisplayContext(field.name, dataRange[0], dataRange[1], true, true);
        }
    }

    if (m_visualService)
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
}

QJsonObject SliceSettingsWidget::saveJson()
{
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    QJsonObject object;
    object.insert(QStringLiteral("analysisKey"), ui->analysisComboBox->currentData().toString());
    object.insert(QStringLiteral("resultDir"), ui->resultDirComboBox->currentData().toString());
    object.insert(QStringLiteral("resultTime"), ui->resultTimeComboBox->currentData().toString());
    object.insert(QStringLiteral("stepFile"), ui->stepComboBox->currentData().toString());
    object.insert(QStringLiteral("fieldName"), field.name);
    object.insert(QStringLiteral("fieldAssociation"), field.association);
    object.insert(QStringLiteral("planeIndex"), ui->planeComboBox->currentIndex());
    object.insert(QStringLiteral("position"),
                  qRound(currentNormalizedPosition() * 100.0)); // 兼容旧工程
    object.insert(QStringLiteral("positionRatio"), currentNormalizedPosition());
    object.insert(QStringLiteral("positionCoordinate"), ui->slicePositionSpin->value());
    object.insert(QStringLiteral("singleMode"), isSingleMode());
    object.insert(QStringLiteral("sliceCount"), ui->sliceNumSpin->value());
    object.insert(QStringLiteral("includeEnds"), ui->includeEndsCheck->isChecked());
    return object;
}

void SliceSettingsWidget::loadJson(const QJsonObject& object)
{
    m_pendingState.clear();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingState.insert(it.key(), it.value().toVariant());
    m_hasPendingState = true;
    if (m_loadedOnce)
        applyPendingState();
}

void SliceSettingsWidget::applyPendingState()
{
    if (!m_hasPendingState)
        return;

    const QVariantMap state = m_pendingState;
    m_pendingState.clear();
    m_hasPendingState = false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString resultDir = state.value(QStringLiteral("resultDir")).toString();
    const QString resolvedResultDir =
        PostResultSelectionUtils::absoluteSourcePath(projectDir, resultDir);
    QString analysisKey = state.value(QStringLiteral("analysisKey")).toString();
    if (analysisKey.isEmpty() && !resolvedResultDir.isEmpty())
        analysisKey =
            PostResultSelectionUtils::analysisKeyForSource(projectDir, resolvedResultDir);

    if (!analysisKey.isEmpty())
    {
        const int analysisIndex = ui->analysisComboBox->findData(analysisKey);
        if (analysisIndex >= 0)
        {
            const QSignalBlocker blocker(ui->analysisComboBox);
            ui->analysisComboBox->setCurrentIndex(analysisIndex);
        }
    }
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, projectDir,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        resolvedResultDir);
    if (!resolvedResultDir.isEmpty())
    {
        const int index = PostResultSelectionUtils::sourceComboIndex(
            ui->resultDirComboBox, projectDir, resolvedResultDir);
        if (index >= 0)
        {
            const QSignalBlocker blocker(ui->resultDirComboBox);
            ui->resultDirComboBox->setCurrentIndex(index);
        }
    }
    m_dataPath = ui->resultDirComboBox->currentData().toString();
    loadFileName(m_dataPath);

    const QString resultTime = state.value(QStringLiteral("resultTime")).toString();
    if (!resultTime.isNull())
    {
        const int timeIndex = ui->resultTimeComboBox->findData(resultTime);
        if (timeIndex >= 0)
        {
            {
                const QSignalBlocker blocker(ui->resultTimeComboBox);
                ui->resultTimeComboBox->setCurrentIndex(timeIndex);
            }
            on_resultTimeComboBox_currentIndexChanged(timeIndex);
        }
    }

    const QString stepFile = state.value(QStringLiteral("stepFile")).toString();
    if (!stepFile.isEmpty())
    {
        const int index = ui->stepComboBox->findData(stepFile);
        if (index >= 0)
        {
            const QSignalBlocker blocker(ui->stepComboBox);
            ui->stepComboBox->setCurrentIndex(index);
            on_stepComboBox_currentIndexChanged(index);
        }
    }

    {
        const QSignalBlocker fieldBlocker(ui->scalarComboBox);
        PostFieldUtils::selectField(
            ui->scalarComboBox, state.value(QStringLiteral("fieldName")).toString(),
            state.value(QStringLiteral("fieldAssociation"),
                        vtkDataObject::FIELD_ASSOCIATION_POINTS).toInt());
    }
    {
        const QSignalBlocker planeBlocker(ui->planeComboBox);
        const int maximumIndex = std::max(0, ui->planeComboBox->count() - 1);
        ui->planeComboBox->setCurrentIndex(std::max(0, std::min(state.value(QStringLiteral("planeIndex"), 0).toInt(),
                                  maximumIndex)));
    }
    {
        const QSignalBlocker singleBlocker(ui->singleSliceRadioButton);
        const QSignalBlocker multiBlocker(ui->multiSliceRadioButton);
        const QSignalBlocker countBlocker(ui->sliceNumSpin);
        const QSignalBlocker endsBlocker(ui->includeEndsCheck);
        const bool singleMode = state.value(QStringLiteral("singleMode"), true).toBool();
        ui->singleSliceRadioButton->setChecked(singleMode);
        ui->multiSliceRadioButton->setChecked(!singleMode);
        ui->modeStackedWidget->setCurrentIndex(singleMode ? 0 : 1);
        ui->sliceNumSpin->setValue(state.value(QStringLiteral("sliceCount"), 5).toInt());
        ui->includeEndsCheck->setChecked(state.value(QStringLiteral("includeEnds"), false).toBool());
    }
    double positionRatio = state.value(QStringLiteral("positionRatio"), -1.0).toDouble();
    if (positionRatio < 0.0)
        positionRatio = state.value(QStringLiteral("position"), 50).toDouble() / 100.0;
    updatePositionControls(std::max(0.0, std::min(positionRatio, 1.0)));

    detachAllFromScene();
    m_singleDisplayed = false;
    m_multiDisplayed = false;
    clearMultiSlicesAll();
    on_scalarComboBox_currentIndexChanged(ui->scalarComboBox->currentIndex());
    if (isSingleMode())
        showCutPlane();
}

void SliceSettingsWidget::refreshResultGroups()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);
    QString previousAnalysis = ui->analysisComboBox->currentData().toString();
    const QString previousPath = m_dataPath;
    if (previousAnalysis.isEmpty() && !previousPath.isEmpty())
        previousAnalysis =
            PostResultSelectionUtils::analysisKeyForSource(groups, previousPath);

    PostResultSelectionUtils::populateAnalysisCombo(
        ui->analysisComboBox, groups, previousAnalysis);
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, groups,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        previousPath);

    m_dataPath = ui->resultDirComboBox->currentData().toString();
}

void SliceSettingsWidget::on_analysisComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousPath = m_dataPath;
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, projectDir,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        previousPath);
    m_dataPath = ui->resultDirComboBox->currentData().toString();
    loadFileName(m_dataPath);
}

void SliceSettingsWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void SliceSettingsWidget::on_resultTimeComboBox_currentIndexChanged(int)
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousName = ui->stepComboBox->currentText();
    PostResultSelectionUtils::populateResultCombo(
        ui->stepComboBox, projectDir, m_dataPath,
        PostResultSelectionUtils::selectedFrameKey(ui->resultTimeComboBox),
        QString(), false);

    const int sameName = ui->stepComboBox->findText(previousName);
    if (sameName >= 0)
        ui->stepComboBox->setCurrentIndex(sameName);
    if (ui->stepComboBox->count() > 0)
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());
}

void SliceSettingsWidget::beforeUnload()
{
    setPostViewActive(false);
    setProperty("postDisplayActive", false);
    clearPostDisplayContext();
    // 只移除，不销毁
    detachAllFromScene();
    if (m_visualService)
    {
        m_visualService->showScalarBar(false, false);
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
    }
}

void SliceSettingsWidget::loadFileName(QString dataPath)
{
    QDir directory(dataPath);
    if (!directory.exists())
    {
        ui->resultTimeComboBox->clear();
        ui->stepComboBox->clear();
        qWarning() << "Directory not found:" << dataPath;
        return;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousTime = ui->resultTimeComboBox->currentData().toString();
    const QString previousFile = ui->stepComboBox->currentData().toString();

    PostResultSelectionUtils::populateTimeCombo(
        ui->resultTimeComboBox, projectDir, dataPath, previousTime, false);
    PostResultSelectionUtils::populateResultCombo(
        ui->stepComboBox, projectDir, dataPath,
        PostResultSelectionUtils::selectedFrameKey(ui->resultTimeComboBox),
        previousFile, false);

    if (ui->stepComboBox->count() > 0)
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());
    else
        qWarning() << "No configured result files found in" << dataPath;
}

void SliceSettingsWidget::on_stepComboBox_currentIndexChanged(int idx)
{
    if (idx < 0)
        return;
    const QString file = ui->stepComboBox->currentData().toString();
    if (file.isEmpty())
        return;

    QString errorMessage;
    auto newGrid = PostTecplotUtils::readGrid(file, &errorMessage);
    if (!newGrid || newGrid->GetNumberOfPoints() == 0 || newGrid->GetNumberOfCells() == 0)
    {
        qWarning() << "[Slice] Failed to read grid:" << file << errorMessage;
        return;
    }
    m_grid = newGrid;
    const QFileInfo fi(file);
    m_loadedFilePath = fi.absoluteFilePath();
    m_loadedFileSize = fi.size();
    m_loadedFileModified = fi.lastModified().toMSecsSinceEpoch();

    // 范围
    double b[6];
    m_grid->GetBounds(b);
    m_XRange[0] = b[0];
    m_XRange[1] = b[1];
    m_YRange[0] = b[2];
    m_YRange[1] = b[3];
    m_ZRange[0] = b[4];
    m_ZRange[1] = b[5];

    {
        const QSignalBlocker scalarBlocker(ui->scalarComboBox);
        const QSignalBlocker planeBlocker(ui->planeComboBox);
        // 同名节点/单元变量分别显示，避免选错数据关联。
        PostFieldUtils::fillCombo(ui->scalarComboBox, m_grid);
        const int sharedFieldIndex = PostResultSelectionUtils::findSharedField(
            ui->scalarComboBox, PostFieldUtils::NameRole, PostFieldUtils::AssociationRole);
        if (sharedFieldIndex >= 0)
            ui->scalarComboBox->setCurrentIndex(sharedFieldIndex);

        // UI 不自动出结果
        ui->planeComboBox->setCurrentIndex(0);
        updatePositionControls(0.5);
    }

    // 切步后：默认只显示整体云图
    m_singleDisplayed = false;
    m_multiDisplayed = false;

    // 清理结果显示
    if (m_sliceActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")), false);
    clearMultiSlicesAll();

    // 上色整体云图
    if (ui->scalarComboBox->count() > 0)
    {
        on_scalarComboBox_currentIndexChanged(ui->scalarComboBox->currentIndex());
        return;
    }

    reattachAccordingToDisplayState();
    if (m_visualService)
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
}

void SliceSettingsWidget::on_scalarComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox,
                                                     PostFieldUtils::NameRole,
                                                     PostFieldUtils::AssociationRole);
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (!m_grid)
        return;
    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid, true);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        detachAllFromScene();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    displayGrid = postApplyZeroHideFilter(displayGrid, field.name, true);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        qWarning() << "[Slice] Scalar array not found:" << field.name;
        clearPostDisplayContext();
        detachAllFromScene();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }
    const QByteArray encodedName = field.name.toUtf8();

    // LUT
    double dataRange[2];
    arr->GetRange(dataRange);
    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(field.name, dataRange[0], dataRange[1], true, true);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);
    auto* scalarBar = m_visualService->scalarBarActor();
    if (!scalarBar)
        return;
    auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
    if (!lut)
    {
        auto newLut = vtkSmartPointer<vtkLookupTable>::New();
        scalarBar->SetLookupTable(newLut);
        lut = newLut;
    }
    applyPostFieldLookupTable(lut, field.name, range[0], range[1]);

    // 整体结果直接显示当前材料/分层筛选后的体网格。
    // 切面页的“整体云图”不展开内部域界面，避免筛选后出现碎片薄面；
    // 真正的切面仍由 vtkCutter 直接切完整筛选体网格。
    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(displayGrid);
    mapper->SetLookupTable(lut);
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
        mapper->SetScalarModeToUseCellFieldData();
    else
        mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray(encodedName.constData());
    mapper->SetScalarRange(range);

    if (!m_actor)
        m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(mapper);
    const bool monochromeContours = postContourStyle().enabled && postContourStyle().monochrome;
    if (monochromeContours)
    {
        mapper->ScalarVisibilityOff();
        m_actor->GetProperty()->SetColor(0.97, 0.97, 0.97);
    }
    else
    {
        mapper->ScalarVisibilityOn();
        m_actor->GetProperty()->SetColor(1.0, 1.0, 1.0);
    }
    applyPostActorRepresentation(m_actor);
    applyStandardPostScalarBar(m_visualService, ResultDisplayNames::scalarBarName(field.name));
    m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);

    // 没有在显示结果时，才把整体云图挂上
    if (!m_singleDisplayed && !m_multiDisplayed)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")),
                          m_actor, QStringLiteral("结果表面"), false);

    // 若已经在显示结果，重建结果以复用新 LUT
    if (m_singleDisplayed)
    {
        if (m_sliceActor)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")), false);
        buildSingleSliceResult();
    }
    else if (m_multiDisplayed)
    {
        hideMultiSlicesFromScene();
        buildMultiSliceResult();
    }

    if (m_visualService)
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
}

void SliceSettingsWidget::on_planeComboBox_currentIndexChanged(int)
{
    const double position = currentNormalizedPosition();
    updatePositionControls(position);
    if (isSingleMode())
        showCutPlane();
}

void SliceSettingsWidget::showCutPlane(int)
{
    if (!m_grid || !m_visualService)
        return;

    // 单切面实时更新；多切面由按钮统一生成
    if (!isSingleMode())
        return;

    const int planeIndex = ui->planeComboBox->currentIndex();
    const double minimum = planeIndex == 0 ? m_XRange[0]
                                           : (planeIndex == 1 ? m_YRange[0] : m_ZRange[0]);
    const double maximum = planeIndex == 0 ? m_XRange[1]
                                           : (planeIndex == 1 ? m_YRange[1] : m_ZRange[1]);
    if (sender() == ui->sliceSlider)
    {
        const QSignalBlocker blocker(ui->slicePositionSpin);
        ui->slicePositionSpin->setValue(
            minimum + currentNormalizedPosition() * (maximum - minimum));
    }
    const double coordinate = std::max(minimum, std::min(ui->slicePositionSpin->value(), maximum));

    if (!m_plane)
        m_plane = vtkSmartPointer<vtkPlane>::New();

    double origin[3] = {0.0, 0.0, 0.0};
    if (planeIndex == 0)
    {
        origin[0] = coordinate;
        m_plane->SetNormal(1, 0, 0);
    }
    else if (planeIndex == 1)
    {
        origin[1] = coordinate;
        m_plane->SetNormal(0, 1, 0);
    }
    else
    {
        origin[2] = coordinate;
        m_plane->SetNormal(0, 0, 1);
    }
    m_plane->SetOrigin(origin);

    // 位置变化后直接显示真实切面结果。
    if (m_actor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")), false);
    hideMultiSlicesFromScene();

    buildSingleSliceResult();
    m_singleDisplayed = true;
    m_multiDisplayed = false;

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void SliceSettingsWidget::on_confirmButton_clicked()
{
    if (!m_visualService)
        return;

    if (isSingleMode())
    {
        showCutPlane();
        return;
    }

    // 多切面计算量更大，仍由“生成多切面”按钮显式触发。
    if (m_actor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")), false);
    if (m_sliceActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")), false);

    buildMultiSliceResult();
    m_singleDisplayed = false;
    m_multiDisplayed = true;

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void SliceSettingsWidget::on_singleSliceRadioButton_toggled(bool checked)
{
    ui->modeStackedWidget->setCurrentIndex(checked ? 0 : 1);
    ui->confirmButton->setVisible(!checked);
    ui->confirmButton->setText(QStringLiteral("生成多切面"));

    if (checked)
    {
        hideMultiSlicesFromScene();
        m_multiDisplayed = false;
        showCutPlane();
        return;
    }
    else
    {
        if (m_sliceActor)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")), false);
        if (m_cutIndicatorActor)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("cut-indicator")), false);
        if (m_contextActor)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")), false);
        if (m_contextOutlineActor)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")), false);
        if (m_sliceContourActor)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice-contour")), false);
        if (m_sliceContourLabelActor)
            removePostPropByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("slice-contour-labels")),
                m_sliceContourLabelActor, false);
        m_singleDisplayed = false;
        if (m_actor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")),
                          m_actor, QStringLiteral("结果表面"), false);
    }

    if (m_visualService)
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
}

void SliceSettingsWidget::on_slicePositionSpin_valueChanged(double value)
{
    const int axis = ui->planeComboBox->currentIndex();
    const double minimum = axis == 0 ? m_XRange[0] : (axis == 1 ? m_YRange[0] : m_ZRange[0]);
    const double maximum = axis == 0 ? m_XRange[1] : (axis == 1 ? m_YRange[1] : m_ZRange[1]);
    const double span = maximum - minimum;
    const double ratio = span > 0.0 ? std::max(0.0, std::min((value - minimum) / span, 1.0)) : 0.5;
    {
        const QSignalBlocker blocker(ui->sliceSlider);
        ui->sliceSlider->setValue(qRound(ratio * ui->sliceSlider->maximum()));
    }
    showCutPlane();
}

bool SliceSettingsWidget::isSingleMode() const
{
    return ui->singleSliceRadioButton && ui->singleSliceRadioButton->isChecked();
}

double SliceSettingsWidget::currentNormalizedPosition() const
{
    if (!ui->sliceSlider || ui->sliceSlider->maximum() <= 0)
        return 0.5;
    return std::max(0.0, std::min(ui->sliceSlider->value() / double(ui->sliceSlider->maximum()), 1.0));
}

void SliceSettingsWidget::updatePositionControls(double normalizedPosition)
{
    normalizedPosition = std::max(0.0, std::min(normalizedPosition, 1.0));
    const int axis = ui->planeComboBox->currentIndex();
    const double minimum = axis == 0 ? m_XRange[0] : (axis == 1 ? m_YRange[0] : m_ZRange[0]);
    const double maximum = axis == 0 ? m_XRange[1] : (axis == 1 ? m_YRange[1] : m_ZRange[1]);
    const QString axisName = axis == 0 ? QStringLiteral("X") : (axis == 1 ? QStringLiteral("Y") : QStringLiteral("Z"));

    {
        const QSignalBlocker sliderBlocker(ui->sliceSlider);
        const QSignalBlocker spinBlocker(ui->slicePositionSpin);
        ui->sliceSlider->setValue(qRound(normalizedPosition * ui->sliceSlider->maximum()));
        ui->slicePositionSpin->setRange(minimum, maximum);
        ui->slicePositionSpin->setValue(minimum + normalizedPosition * (maximum - minimum));
    }
    ui->sliceRangeLabel->setText(
        QStringLiteral("范围：%1  %2 ～ %3")
            .arg(axisName, QString::number(minimum, 'g', 8), QString::number(maximum, 'g', 8)));
}

void SliceSettingsWidget::buildSingleSliceResult()
{
    if (!m_grid || !m_visualService)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid, true);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    displayGrid = postApplyZeroHideFilter(displayGrid, field.name, true);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
        return;
    const QByteArray encodedName = field.name.toUtf8();
    QString sliceArrayName = field.name;

    if (!m_visualService)
        return;
    auto* scalarBar = m_visualService->scalarBarActor();
    auto* lut = scalarBar ? vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()) : nullptr;
    if (!lut)
        return;

    double dataRange[2]{0, 1};
    arr->GetRange(dataRange);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);
    applyPostFieldLookupTable(lut, field.name, range[0], range[1]);

    auto cutter = vtkSmartPointer<vtkCutter>::New();
    cutter->SetCutFunction(m_plane);
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        if (!m_cellToPoint)
        {
            m_cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
            m_cellToPoint->PassCellDataOn();
        }
        auto preparedGrid = PostFieldUtils::prepareCellScalarForPointConversion(
            displayGrid, field, &sliceArrayName);
        if (!preparedGrid)
            return;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        cutter->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        cutter->SetInputData(displayGrid);
    }
    cutter->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(cutter->GetOutputPort());
    mapper->SetLookupTable(lut);
    mapper->SetScalarModeToUsePointFieldData();
    const QByteArray sliceArray = sliceArrayName.toUtf8();
    mapper->SelectColorArray(sliceArray.constData());
    mapper->SetScalarRange(range);
    mapper->ScalarVisibilityOn();

    if (m_sliceActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")), false);
    if (m_cutIndicatorActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("cut-indicator")), false);
    if (m_contextActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")), false);
    if (m_contextOutlineActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")), false);
    if (m_sliceContourActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice-contour")), false);
    if (m_sliceContourLabelActor)
        removePostPropByService(
            m_visualService,
            postActorId(getUuid(), QStringLiteral("slice-contour-labels")),
            m_sliceContourLabelActor, false);
    m_cutIndicatorActor = nullptr;
    m_contextActor = nullptr;
    m_contextOutlineActor = nullptr;
    m_sliceContourActor = nullptr;
    m_sliceContourLabelActor = nullptr;

    m_sliceActor = vtkSmartPointer<vtkActor>::New();
    m_sliceActor->SetMapper(mapper);
    if (postContourStyle().enabled && postContourStyle().monochrome)
    {
        mapper->ScalarVisibilityOff();
        m_sliceActor->GetProperty()->SetColor(0.97, 0.97, 0.97);
    }
    else
    {
        mapper->ScalarVisibilityOn();
        m_sliceActor->GetProperty()->SetColor(1.0, 1.0, 1.0);
    }
    applyPostActorRepresentation(m_sliceActor);

    addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")),
                      m_sliceActor, QStringLiteral("切面"), false);
    if (postSceneStyle().showContextOutline)
    {
        m_contextActor = createContextModelActor(displayGrid);
        if (m_contextActor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")),
                              m_contextActor, QStringLiteral("整体模型参考"), false);
    }
    if (postSceneStyle().showContextLines)
    {
        m_contextOutlineActor = createPostContextOutlineActor(displayGrid);
        if (m_contextOutlineActor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")),
                              m_contextOutlineActor, QStringLiteral("轮廓线"), false);
    }
    double bounds[6]{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    displayGrid->GetBounds(bounds);
    m_cutIndicatorActor = postSceneStyle().showContextOutline
        ? createCutPlaneIndicatorActor(m_plane, bounds) : nullptr;
    if (m_cutIndicatorActor)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("cut-indicator")),
                          m_cutIndicatorActor, QStringLiteral("切割位置"), false);
    double contourRange[2]{0.0, 0.0};
    if (postContourStyle().enabled &&
        resolveSliceContourRange(cutter->GetOutput(), sliceArrayName, range, contourRange))
        m_sliceContourActor = createPostContourActor(cutter->GetOutputPort(), sliceArrayName,
                                                      contourRange[0], contourRange[1]);
    if (m_sliceContourActor)
    {
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice-contour")),
                          m_sliceContourActor, QStringLiteral("切面等值线"), false);
        m_sliceContourLabelActor = createSliceContourLabelActor(
            m_sliceContourActor, sliceArrayName, contourRange[0], contourRange[1]);
    }
    if (m_sliceContourLabelActor)
        addPostPropByService(
            m_visualService,
            postActorId(getUuid(), QStringLiteral("slice-contour-labels")),
            m_sliceContourLabelActor, QStringLiteral("切面等值线数值"), false);
    applyStandardPostScalarBar(m_visualService, ResultDisplayNames::scalarBarName(field.name));
    m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
}

void SliceSettingsWidget::buildMultiSliceResult()
{
    if (!m_grid || !m_visualService)
        return;

    clearMultiSlicesAll();

    const int count = ui->sliceNumSpin ? std::max(1, ui->sliceNumSpin->value()) : 1;
    const bool includeEnds = ui->includeEndsCheck && ui->includeEndsCheck->isChecked();
    const int planeIndex = ui->planeComboBox->currentIndex();

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid, true);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    displayGrid = postApplyZeroHideFilter(displayGrid, field.name, true);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
        return;
    const QByteArray encodedName = field.name.toUtf8();
    QString sliceArrayName = field.name;

    if (!m_visualService)
        return;
    auto* scalarBar = m_visualService->scalarBarActor();
    auto* lut = scalarBar ? vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()) : nullptr;
    if (!lut)
        return;
    double dataRange[2]{0, 1};
    arr->GetRange(dataRange);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);
    applyPostFieldLookupTable(lut, field.name, range[0], range[1]);

    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        if (!m_cellToPoint)
        {
            m_cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
            m_cellToPoint->PassCellDataOn();
        }
        auto preparedGrid = PostFieldUtils::prepareCellScalarForPointConversion(
            displayGrid, field, &sliceArrayName);
        if (!preparedGrid)
            return;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
    }

    const QVector<double> positions = genUniformTs(count, includeEnds);
    double bounds[6];
    m_grid->GetBounds(bounds);
    const double dx = bounds[1] - bounds[0];
    const double dy = bounds[3] - bounds[2];
    const double dz = bounds[5] - bounds[4];

    for (double position : positions)
    {
        auto plane = vtkSmartPointer<vtkPlane>::New();
        double origin[3] = {0, 0, 0};
        if (planeIndex == 0)
        {
            origin[0] = bounds[0] + position * dx;
            plane->SetNormal(1, 0, 0);
        }
        else if (planeIndex == 1)
        {
            origin[1] = bounds[2] + position * dy;
            plane->SetNormal(0, 1, 0);
        }
        else
        {
            origin[2] = bounds[4] + position * dz;
            plane->SetNormal(0, 0, 1);
        }
        plane->SetOrigin(origin);

        auto cutter = vtkSmartPointer<vtkCutter>::New();
        cutter->SetCutFunction(plane);
        if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
            cutter->SetInputConnection(m_cellToPoint->GetOutputPort());
        else
            cutter->SetInputData(displayGrid);
        cutter->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(cutter->GetOutputPort());
        mapper->SetLookupTable(lut);
        mapper->SetScalarModeToUsePointFieldData();
        const QByteArray sliceArray = sliceArrayName.toUtf8();
        mapper->SelectColorArray(sliceArray.constData());
        mapper->SetScalarRange(range);
        mapper->ScalarVisibilityOn();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        if (postContourStyle().enabled && postContourStyle().monochrome)
        {
            mapper->ScalarVisibilityOff();
            actor->GetProperty()->SetColor(0.97, 0.97, 0.97);
        }
        else
        {
            mapper->ScalarVisibilityOn();
            actor->GetProperty()->SetColor(1.0, 1.0, 1.0);
        }
        const int sliceIndex = m_multiActors.size();
        addActorByService(m_visualService,
                          postActorId(getUuid(), QStringLiteral("slice-%1").arg(sliceIndex)),
                          actor, QStringLiteral("切面 %1").arg(sliceIndex + 1), false);

        vtkSmartPointer<vtkActor> contourActor;
        vtkSmartPointer<vtkActor2D> contourLabelActor;
        double contourRange[2]{0.0, 0.0};
        if (postContourStyle().enabled &&
            resolveSliceContourRange(cutter->GetOutput(), sliceArrayName, range, contourRange))
            contourActor = createPostContourActor(cutter->GetOutputPort(), sliceArrayName,
                                                   contourRange[0], contourRange[1]);
        if (contourActor)
        {
            addActorByService(m_visualService,
                              postActorId(getUuid(), QStringLiteral("slice-contour-%1").arg(sliceIndex)),
                              contourActor, QStringLiteral("切面等值线 %1").arg(sliceIndex + 1), false);
            contourLabelActor = createSliceContourLabelActor(
                contourActor, sliceArrayName, contourRange[0], contourRange[1]);
        }
        if (contourLabelActor)
            addPostPropByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("slice-contour-labels-%1").arg(sliceIndex)),
                contourLabelActor, QStringLiteral("切面等值线数值 %1").arg(sliceIndex + 1), false);

        m_multiPlanes << plane;
        m_multiCutters << cutter;
        m_multiMappers << mapper;
        m_multiActors << actor;
        if (auto indicator = postSceneStyle().showContextOutline
                ? createCutPlaneIndicatorActor(plane, bounds) : vtkSmartPointer<vtkActor>{})
        {
            addActorByService(m_visualService,
                              postActorId(getUuid(), QStringLiteral("cut-indicator-%1").arg(sliceIndex)),
                              indicator, QStringLiteral("切割位置 %1").arg(sliceIndex + 1), false);
            m_multiCutIndicatorActors << indicator;
        }
        else
        {
            m_multiCutIndicatorActors << nullptr;
        }
        // 保持与 m_multiActors 的索引严格对齐；空切面/常量切面没有真实等值线。
        m_multiContourActors << contourActor;
        m_multiContourLabelActors << contourLabelActor;
    }

    m_contextActor = nullptr;
    m_contextOutlineActor = nullptr;
    if (postSceneStyle().showContextOutline)
    {
        m_contextActor = createContextModelActor(displayGrid);
        if (m_contextActor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")),
                              m_contextActor, QStringLiteral("整体模型参考"), false);
    }
    if (postSceneStyle().showContextLines)
    {
        m_contextOutlineActor = createPostContextOutlineActor(displayGrid);
        if (m_contextOutlineActor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")),
                              m_contextOutlineActor, QStringLiteral("轮廓线"), false);
    }

    applyStandardPostScalarBar(m_visualService, ResultDisplayNames::scalarBarName(field.name));
    m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
}

void SliceSettingsWidget::hideMultiSlicesFromScene()
{
    if (m_contextActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")), false);
    if (m_contextOutlineActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")), false);
    for (int i = 0; i < m_multiActors.size(); ++i)
    {
        if (m_multiActors[i])
            removeActorByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("slice-%1").arg(i)), false);
    }
    for (int i = 0; i < m_multiCutIndicatorActors.size(); ++i)
    {
        if (m_multiCutIndicatorActors[i])
            removeActorByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("cut-indicator-%1").arg(i)), false);
    }
    for (int i = 0; i < m_multiContourActors.size(); ++i)
    {
        if (m_multiContourActors[i])
            removeActorByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("slice-contour-%1").arg(i)), false);
    }
    for (int i = 0; i < m_multiContourLabelActors.size(); ++i)
    {
        if (m_multiContourLabelActors[i])
            removePostPropByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("slice-contour-labels-%1").arg(i)),
                m_multiContourLabelActors[i], false);
    }
}

void SliceSettingsWidget::clearMultiSlicesAll()
{
    hideMultiSlicesFromScene();
    m_multiPlanes.clear();
    m_multiCutters.clear();
    m_multiMappers.clear();
    m_multiActors.clear();
    m_multiCutIndicatorActors.clear();
    m_multiContourActors.clear();
    m_multiContourLabelActors.clear();
}

void SliceSettingsWidget::reattachAccordingToDisplayState()
{
    // 全部清空当前场景上的本页 actor
    detachAllFromScene();

    const bool singleMode = isSingleMode();
    const bool multiMode = !singleMode;

    if (m_multiDisplayed && multiMode && !m_multiActors.isEmpty())
    {
        for (int i = 0; i < m_multiActors.size(); ++i)
            if (m_multiActors[i])
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("slice-%1").arg(i)),
                    m_multiActors[i], QStringLiteral("切面 %1").arg(i + 1), false);
        for (int i = 0; i < m_multiCutIndicatorActors.size(); ++i)
            if (postSceneStyle().showContextOutline && m_multiCutIndicatorActors[i])
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("cut-indicator-%1").arg(i)),
                    m_multiCutIndicatorActors[i], QStringLiteral("切割位置 %1").arg(i + 1), false);
        if (postSceneStyle().showContextOutline && m_contextActor)
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("context-model")),
                    m_contextActor, QStringLiteral("整体模型参考"), false);
        if (postSceneStyle().showContextLines && m_contextOutlineActor)
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("context-outline")),
                    m_contextOutlineActor, QStringLiteral("轮廓线"), false);
        for (int i = 0; i < m_multiContourActors.size(); ++i)
            if (m_multiContourActors[i])
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("slice-contour-%1").arg(i)),
                    m_multiContourActors[i], QStringLiteral("切面等值线 %1").arg(i + 1), false);
        for (int i = 0; i < m_multiContourLabelActors.size(); ++i)
            if (m_multiContourLabelActors[i])
                addPostPropByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("slice-contour-labels-%1").arg(i)),
                    m_multiContourLabelActors[i],
                    QStringLiteral("切面等值线数值 %1").arg(i + 1), false);
        return;
    }

    if (m_singleDisplayed && singleMode && m_sliceActor)
    {
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")),
                      m_sliceActor, QStringLiteral("切面"), false);
        if (postSceneStyle().showContextOutline && m_contextActor)
                addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")),
                                  m_contextActor, QStringLiteral("整体模型参考"), false);
        if (postSceneStyle().showContextLines && m_contextOutlineActor)
                addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")),
                                  m_contextOutlineActor, QStringLiteral("轮廓线"), false);
        if (postSceneStyle().showContextOutline && m_cutIndicatorActor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("cut-indicator")),
                              m_cutIndicatorActor, QStringLiteral("切割位置"), false);
        if (m_sliceContourActor)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice-contour")),
                          m_sliceContourActor, QStringLiteral("切面等值线"), false);
        if (m_sliceContourLabelActor)
            addPostPropByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("slice-contour-labels")),
                m_sliceContourLabelActor, QStringLiteral("切面等值线数值"), false);
        if (m_visualService)
            m_visualService->render();
        return;
    }

    // 默认：整体云图
    if (m_actor)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")),
                          m_actor, QStringLiteral("结果表面"), false);

    // 单切面模式没有单独预览状态，直接恢复/生成真实切面结果。
    if (singleMode && !m_singleDisplayed)
        showCutPlane();
}

void SliceSettingsWidget::detachAllFromScene()
{
    if (m_actor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")), false);
    if (m_contextActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-model")), false);
    if (m_contextOutlineActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("context-outline")), false);
    if (m_sliceActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice")), false);
    if (m_cutIndicatorActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("cut-indicator")), false);
    if (m_sliceContourActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("slice-contour")), false);
    if (m_sliceContourLabelActor)
        removePostPropByService(
            m_visualService,
            postActorId(getUuid(), QStringLiteral("slice-contour-labels")),
            m_sliceContourLabelActor, false);
    hideMultiSlicesFromScene();
}
