#include "SliceSettingsWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkCutter.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
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

SliceSettingsWidget::SliceSettingsWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::SliceSettingsWidget), BasePage(PublicConstants::kPagePostProcessSlice),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    configurePostResultCombo(ui->analysisComboBox);
    configurePostResultCombo(ui->resultDirComboBox);
    configurePostResultCombo(ui->resultTimeComboBox);
    configurePostResultCombo(ui->stepComboBox);

    ui->sliceSlider->setRange(0, 1000);
    ui->sliceSlider->setValue(500);
    ui->sliceSlider->setTracking(false);
    ui->sliceSlider->setToolTip(QStringLiteral("松开滑块后直接更新切面结果"));
    ui->slicePositionSpin->setKeyboardTracking(false);
    ui->modeStackedWidget->setCurrentIndex(0);

    connect(ui->sliceSlider, &QSlider::valueChanged, this, &SliceSettingsWidget::showCutPlane);
    connect(ui->slicePositionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &SliceSettingsWidget::on_slicePositionSpin_valueChanged);
    connect(ui->planeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SliceSettingsWidget::on_planeComboBox_currentIndexChanged);
    connect(ui->analysisComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SliceSettingsWidget::on_analysisComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SliceSettingsWidget::on_resultDirComboBox_currentIndexChanged);
    connect(ui->resultTimeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SliceSettingsWidget::on_resultTimeComboBox_currentIndexChanged);
    connect(ui->confirmButton, &QPushButton::clicked, this,
            &SliceSettingsWidget::on_confirmButton_clicked);
    connect(ui->singleSliceRadioButton, &QRadioButton::toggled, this,
            &SliceSettingsWidget::on_singleSliceRadioButton_toggled);
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SliceSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SliceSettingsWidget::on_stepComboBox_currentIndexChanged);

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
    showPostProcessingOnly(m_visualService, false);
    refreshResultGroups();
    if (!m_loadedOnce)
    {
        // 初始化一次
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
            m_visualService->showScalarBar(true, false);
            applyStandardPostScalarBar(
                m_visualService, ResultDisplayNames::scalarBarName(field.name));
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

    // 范围
    double b[6];
    m_grid->GetBounds(b);
    m_XRange[0] = b[0];
    m_XRange[1] = b[1];
    m_YRange[0] = b[2];
    m_YRange[1] = b[3];
    m_ZRange[0] = b[4];
    m_ZRange[1] = b[5];

    // 同名节点/单元变量分别显示，避免选错数据关联。
    PostFieldUtils::fillCombo(ui->scalarComboBox, m_grid);

    // UI 不自动出结果
    ui->planeComboBox->setCurrentIndex(0);
    updatePositionControls(0.5);

    // 切步后：默认只显示整体云图
    m_singleDisplayed = false;
    m_multiDisplayed = false;

    // 清理结果显示
    if (m_sliceActor)
        removeActorByService(m_visualService, m_sliceActor, false);
    clearMultiSlicesAll();

    // 上色整体云图
    if (ui->scalarComboBox->count() > 0)
        on_scalarComboBox_currentIndexChanged(ui->scalarComboBox->currentIndex());

    reattachAccordingToDisplayState();
    if (m_visualService)
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
}

void SliceSettingsWidget::on_scalarComboBox_currentIndexChanged(int)
{
    if (!m_grid)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
    {
        qWarning() << "[Slice] Scalar array not found:" << field.name;
        return;
    }
    const QByteArray encodedName = field.name.toUtf8();

    // LUT
    double range[2];
    arr->GetRange(range);
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
    lut->SetTableRange(range);
    lut->Build();

    // 整体云图
    vtkSmartPointer<vtkDataSetSurfaceFilter> surfaceFilter =
        vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(m_grid);
    surfaceFilter->Update();

    vtkSmartPointer<vtkPolyData> polyData = surfaceFilter->GetOutput();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyData);
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
    m_actor->GetProperty()->SetRepresentationToSurface();
    applyStandardPostScalarBar(m_visualService, ResultDisplayNames::scalarBarName(field.name));
    m_visualService->showScalarBar(true, false);

    // 没有在显示结果时，才把整体云图挂上
    if (!m_singleDisplayed && !m_multiDisplayed)
        addActorByService(m_visualService, m_actor);

    // 若已经在显示结果，重建结果以复用新 LUT
    if (m_singleDisplayed)
    {
        if (m_sliceActor)
            removeActorByService(m_visualService, m_sliceActor, false);
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
        removeActorByService(m_visualService, m_actor, false);
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
        removeActorByService(m_visualService, m_actor, false);
    if (m_sliceActor)
        removeActorByService(m_visualService, m_sliceActor, false);

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
    }
    else
    {
        if (m_sliceActor)
            removeActorByService(m_visualService, m_sliceActor, false);
        m_singleDisplayed = false;
        if (m_actor)
            addActorByService(m_visualService, m_actor, false);
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

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
        return;
    const QByteArray encodedName = field.name.toUtf8();
    QString sliceArrayName = field.name;

    if (!m_visualService)
        return;
    auto* scalarBar = m_visualService->scalarBarActor();
    auto* lut = scalarBar ? vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()) : nullptr;
    if (!lut)
        return;

    double range[2]{0, 1};
    arr->GetRange(range);
    lut->SetTableRange(range);
    lut->Build();

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
            m_grid, field, &sliceArrayName);
        if (!preparedGrid)
            return;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        cutter->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        cutter->SetInputData(m_grid);
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
        removeActorByService(m_visualService, m_sliceActor, false);

    m_sliceActor = vtkSmartPointer<vtkActor>::New();
    m_sliceActor->SetMapper(mapper);
    m_sliceActor->GetProperty()->SetRepresentationToSurface();

    addActorByService(m_visualService, m_sliceActor, false);
    applyStandardPostScalarBar(m_visualService, ResultDisplayNames::scalarBarName(field.name));
    m_visualService->showScalarBar(true, false);
}

void SliceSettingsWidget::buildMultiSliceResult()
{
    if (!m_grid || !m_visualService)
        return;

    clearMultiSlicesAll();

    const int count = ui->sliceNumSpin ? std::max(1, ui->sliceNumSpin->value()) : 1;
    const bool includeEnds = ui->includeEndsCheck && ui->includeEndsCheck->isChecked();
    const int planeIndex = ui->planeComboBox->currentIndex();

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
        return;
    const QByteArray encodedName = field.name.toUtf8();
    QString sliceArrayName = field.name;

    if (!m_visualService)
        return;
    auto* scalarBar = m_visualService->scalarBarActor();
    auto* lut = scalarBar ? vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()) : nullptr;
    if (!lut)
        return;
    double range[2]{0, 1};
    arr->GetRange(range);
    lut->SetTableRange(range);
    lut->Build();

    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        if (!m_cellToPoint)
        {
            m_cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
            m_cellToPoint->PassCellDataOn();
        }
        auto preparedGrid = PostFieldUtils::prepareCellScalarForPointConversion(
            m_grid, field, &sliceArrayName);
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
            cutter->SetInputData(m_grid);
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
        addActorByService(m_visualService, actor, false);

        m_multiPlanes << plane;
        m_multiCutters << cutter;
        m_multiMappers << mapper;
        m_multiActors << actor;
    }

    applyStandardPostScalarBar(m_visualService, ResultDisplayNames::scalarBarName(field.name));
    m_visualService->showScalarBar(true, false);
}

void SliceSettingsWidget::hideMultiSlicesFromScene()
{
    for (auto& a : m_multiActors)
    {
        if (a)
            removeActorByService(m_visualService, a, false);
    }
}

void SliceSettingsWidget::clearMultiSlicesAll()
{
    hideMultiSlicesFromScene();
    m_multiPlanes.clear();
    m_multiCutters.clear();
    m_multiMappers.clear();
    m_multiActors.clear();
}

void SliceSettingsWidget::reattachAccordingToDisplayState()
{
    // 全部清空当前场景上的本页 actor
    detachAllFromScene();

    const bool singleMode = isSingleMode();
    const bool multiMode = !singleMode;

    if (m_multiDisplayed && multiMode && !m_multiActors.isEmpty())
    {
        for (auto& a : m_multiActors)
            if (a)
                addActorByService(m_visualService, a, false);
        return;
    }

    if (m_singleDisplayed && singleMode && m_sliceActor)
    {
        addActorByService(m_visualService, m_sliceActor);
        return;
    }

    // 默认：整体云图
    if (m_actor)
        addActorByService(m_visualService, m_actor);

    // 单切面模式没有单独预览状态，直接恢复/生成真实切面结果。
    if (singleMode && !m_singleDisplayed)
        showCutPlane();
}

void SliceSettingsWidget::detachAllFromScene()
{
    if (m_actor)
        removeActorByService(m_visualService, m_actor, false);
    if (m_sliceActor)
        removeActorByService(m_visualService, m_sliceActor, false);
    hideMultiSlicesFromScene();
}
