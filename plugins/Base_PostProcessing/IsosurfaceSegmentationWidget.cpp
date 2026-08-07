#include "IsosurfaceSegmentationWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkContourFilter.h>
#include <vtkDataObject.h> // FIELD_ASSOCIATION_*
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkThreshold.h>
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
#include "ui_IsosurfaceSegmentationWidget.h"

#include <algorithm>
#include <cmath>

IsosurfaceSegmentationWidget::IsosurfaceSegmentationWidget(
    VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::IsosurfaceSegmentationWidget),
      BasePage(PublicConstants::kPagePostProcessIsosurfaceSegmentation),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    configurePostResultCombo(ui->analysisComboBox);
    configurePostResultCombo(ui->resultDirComboBox);
    configurePostResultCombo(ui->resultTimeComboBox);
    configurePostResultCombo(ui->stepComboBox);

    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    connect(ui->showScalarBarCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        postScalarBarStyle().visible = checked;
        if (m_visualService)
        {
            m_visualService->showScalarBar(checked, false);
            m_visualService->render();
        }
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
        if (editPostDisplaySettings(this, m_visualService, false, false,
                                    field.name, dataMin, dataMax))
        {
            ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            updateColoringFromUI();
            renderNow();
        }
    });

    // UI 默认
    ui->percentModeRadioButton->setChecked(true);
    ui->isoSlider->setEnabled(true);
    ui->isoSpin->setEnabled(false);
    ui->isoSlider->setRange(0, 100);
    ui->isoSlider->setTracking(false);
    ui->isoSlider->setValue(50);
    ui->isoSpin->setDecimals(3);
    ui->isoSpin->setSingleStep(0.001);

    ui->showLowCheckBox->setChecked(true);
    ui->showHighCheckBox->setChecked(true);
    ui->showInterfaceCheckBox->setChecked(false);

    ui->lowOpacitySpinBox->setRange(10, 100);
    ui->highOpacitySpinBox->setRange(10, 100);
    ui->interfaceOpacitySpinBox->setRange(10, 100);
    ui->lowOpacitySpinBox->setValue(60);
    ui->highOpacitySpinBox->setValue(60);
    ui->interfaceOpacitySpinBox->setValue(100);
    ui->lowOpacitySpinBox->setEnabled(true);
    ui->highOpacitySpinBox->setEnabled(true);
    ui->interfaceOpacitySpinBox->setEnabled(false);

    // 信号（如用 Qt Designer 的自动连接，也可以省略）
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->percentModeRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSegmentationWidget::on_percentCheckBox_toggled);
    connect(ui->valueModeRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSegmentationWidget::on_percentCheckBox_toggled);
    connect(ui->isoSlider, &QSlider::valueChanged, this,
            &IsosurfaceSegmentationWidget::on_isoSlider_valueChanged);
    connect(ui->isoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &IsosurfaceSegmentationWidget::on_isoSpin_valueChanged);

    connect(ui->showLowCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_showLowCheckBox_toggled);
    connect(ui->showHighCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_showHighCheckBox_toggled);
    connect(ui->showInterfaceCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_showInterfaceCheckBox_toggled);

    connect(ui->lowOpacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &IsosurfaceSegmentationWidget::on_lowOpacitySlider_valueChanged);
    connect(ui->highOpacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &IsosurfaceSegmentationWidget::on_highOpacitySlider_valueChanged);
    connect(ui->interfaceOpacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &IsosurfaceSegmentationWidget::on_ifaceOpacitySlider_valueChanged);

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_stepComboBox_currentIndexChanged);
    connect(ui->analysisComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_analysisComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_resultDirComboBox_currentIndexChanged);
    connect(ui->resultTimeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_resultTimeComboBox_currentIndexChanged);
}

IsosurfaceSegmentationWidget::~IsosurfaceSegmentationWidget()
{
    delete ui;
}

void IsosurfaceSegmentationWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    refreshResultGroups();
    if (m_dataPath.isEmpty())
    {
        const QString projectDir =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (m_dataPath.isEmpty())
            m_dataPath = PostResultAutoPrepare::firstResultDirectory(projectDir);
    }

    if (ui->stepComboBox->count() == 0)
    {
        // 首次进入：列步号 -> 读取 -> 建管线 -> 绑定色标/阈值/显隐
        loadFileName(m_dataPath);
        buildPipelineIfNeeded();
        updateColoringFromUI();
        updateIsoFromUI();
        updateVisibilityFromUI();
        m_loadedOnce = true;
    }
    else
    {
        // 非首次：把 actor 挂回，并确保与当前 UI 同步
        attachActorsIfNeeded();
        buildPipelineIfNeeded();
        updateColoringFromUI();
        updateIsoFromUI();
        updateVisibilityFromUI();
    }
    applyPendingState();
    renderNow();
}

QJsonObject IsosurfaceSegmentationWidget::saveJson()
{
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    QJsonObject object;
    object.insert(QStringLiteral("analysisKey"), ui->analysisComboBox->currentData().toString());
    object.insert(QStringLiteral("resultDir"), ui->resultDirComboBox->currentData().toString());
    object.insert(QStringLiteral("resultTime"), ui->resultTimeComboBox->currentData().toString());
    object.insert(QStringLiteral("stepFile"), ui->stepComboBox->currentData().toString());
    object.insert(QStringLiteral("fieldName"), field.name);
    object.insert(QStringLiteral("fieldAssociation"), field.association);
    object.insert(QStringLiteral("percentMode"), ui->percentModeRadioButton->isChecked());
    object.insert(QStringLiteral("isoPercent"), ui->isoSlider->value());
    object.insert(QStringLiteral("isoValue"), ui->isoSpin->value());
    object.insert(QStringLiteral("showLow"), ui->showLowCheckBox->isChecked());
    object.insert(QStringLiteral("showHigh"), ui->showHighCheckBox->isChecked());
    object.insert(QStringLiteral("showInterface"), ui->showInterfaceCheckBox->isChecked());
    object.insert(QStringLiteral("lowOpacity"), ui->lowOpacitySpinBox->value());
    object.insert(QStringLiteral("highOpacity"), ui->highOpacitySpinBox->value());
    object.insert(QStringLiteral("interfaceOpacity"), ui->interfaceOpacitySpinBox->value());
    return object;
}

void IsosurfaceSegmentationWidget::loadJson(const QJsonObject& object)
{
    m_pendingState.clear();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingState.insert(it.key(), it.value().toVariant());
    m_hasPendingState = true;
    if (m_loadedOnce)
        applyPendingState();
}

void IsosurfaceSegmentationWidget::applyPendingState()
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
        const QSignalBlocker blocker(ui->scalarComboBox);
        PostFieldUtils::selectField(
            ui->scalarComboBox, state.value(QStringLiteral("fieldName")).toString(),
            state.value(QStringLiteral("fieldAssociation"),
                        vtkDataObject::FIELD_ASSOCIATION_POINTS).toInt());
    }
    setIsoSpinRangeFromScalar();

    {
        const QSignalBlocker percentBlocker(ui->percentModeRadioButton);
        const QSignalBlocker valueModeBlocker(ui->valueModeRadioButton);
        const QSignalBlocker sliderBlocker(ui->isoSlider);
        const QSignalBlocker spinBlocker(ui->isoSpin);
        const QSignalBlocker lowBlocker(ui->showLowCheckBox);
        const QSignalBlocker highBlocker(ui->showHighCheckBox);
        const QSignalBlocker interfaceBlocker(ui->showInterfaceCheckBox);
        const QSignalBlocker lowOpacityBlocker(ui->lowOpacitySpinBox);
        const QSignalBlocker highOpacityBlocker(ui->highOpacitySpinBox);
        const QSignalBlocker interfaceOpacityBlocker(ui->interfaceOpacitySpinBox);

        const bool percentMode = state.value(QStringLiteral("percentMode"), true).toBool();
        ui->percentModeRadioButton->setChecked(percentMode);
        ui->valueModeRadioButton->setChecked(!percentMode);
        ui->isoSlider->setValue(state.value(QStringLiteral("isoPercent"), 50).toInt());
        if (!percentMode)
            ui->isoSpin->setValue(state.value(QStringLiteral("isoValue"), ui->isoSpin->value()).toDouble());
        ui->showLowCheckBox->setChecked(state.value(QStringLiteral("showLow"), true).toBool());
        ui->showHighCheckBox->setChecked(state.value(QStringLiteral("showHigh"), true).toBool());
        ui->showInterfaceCheckBox->setChecked(
            state.value(QStringLiteral("showInterface"), false).toBool());
        ui->lowOpacitySpinBox->setValue(state.value(QStringLiteral("lowOpacity"), 60).toInt());
        ui->highOpacitySpinBox->setValue(state.value(QStringLiteral("highOpacity"), 60).toInt());
        ui->interfaceOpacitySpinBox->setValue(
            state.value(QStringLiteral("interfaceOpacity"), 100).toInt());
    }
    ui->isoSlider->setEnabled(ui->percentModeRadioButton->isChecked());
    ui->isoSpin->setEnabled(!ui->percentModeRadioButton->isChecked());
    ui->lowOpacitySpinBox->setEnabled(ui->showLowCheckBox->isChecked());
    ui->highOpacitySpinBox->setEnabled(ui->showHighCheckBox->isChecked());
    ui->interfaceOpacitySpinBox->setEnabled(ui->showInterfaceCheckBox->isChecked());

    buildPipelineIfNeeded();
    updateColoringFromUI();
    updateIsoFromUI();
    updateVisibilityFromUI();
    attachActorsIfNeeded();
}

void IsosurfaceSegmentationWidget::refreshResultGroups()
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

void IsosurfaceSegmentationWidget::on_analysisComboBox_currentIndexChanged(int)
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

void IsosurfaceSegmentationWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void IsosurfaceSegmentationWidget::on_resultTimeComboBox_currentIndexChanged(int)
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

void IsosurfaceSegmentationWidget::loadFileName(QString dataPath)
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

void IsosurfaceSegmentationWidget::beforeUnload()
{
    // 只移除，不销毁对象
    detachActorsIfAttached();
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
}

void IsosurfaceSegmentationWidget::readTecplotLatest()
{
    QDir dir(m_dataPath);
    if (!dir.exists())
        return;

    QRegularExpression re("stress_strain(\\d+)-(\\d+)\\.txt");
    int bestG = -1, bestO = -1;
    QString best;
    for (const auto& f : dir.entryList(QDir::Files))
    {
        auto m = re.match(f);
        if (!m.hasMatch())
            continue;
        int g = m.captured(1).toInt();
        int o = m.captured(2).toInt();
        if (g > bestG || (g == bestG && o > bestO))
        {
            bestG = g;
            bestO = o;
            best = dir.filePath(f);
        }
    }
    if (!best.isEmpty())
        readTecplot(best);
}

bool IsosurfaceSegmentationWidget::readTecplot(const QString& file)
{
    QString errorMessage;
    auto newGrid = PostTecplotUtils::readGrid(file, &errorMessage);
    if (!newGrid || newGrid->GetNumberOfPoints() == 0 || newGrid->GetNumberOfCells() == 0)
    {
        qWarning() << "[IsosurfaceSegmentation] Failed to read grid:" << file << errorMessage;
        return false;
    }
    m_grid = newGrid;
    return true;
}

void IsosurfaceSegmentationWidget::fillScalarCombo()
{
    PostFieldUtils::fillCombo(ui->scalarComboBox, m_grid);
    const int sharedFieldIndex = PostResultSelectionUtils::findSharedField(
        ui->scalarComboBox, PostFieldUtils::NameRole, PostFieldUtils::AssociationRole);
    if (sharedFieldIndex >= 0)
        ui->scalarComboBox->setCurrentIndex(sharedFieldIndex);
    setIsoSpinRangeFromScalar();
}

void IsosurfaceSegmentationWidget::buildPipelineIfNeeded()
{
    if (!m_grid)
        return;
    if (m_threshLow)
        return; // 已构建

    // 低值侧
    m_threshLow = vtkSmartPointer<vtkThreshold>::New();
    m_threshLow->SetInputData(m_grid);

    m_mapperLow = vtkSmartPointer<vtkDataSetMapper>::New();
    m_mapperLow->SetInputConnection(m_threshLow->GetOutputPort());

    m_actorLow = vtkSmartPointer<vtkActor>::New();
    m_actorLow->SetMapper(m_mapperLow);
    m_actorLow->GetProperty()->SetOpacity(0.6);

    // 高值侧
    m_threshHigh = vtkSmartPointer<vtkThreshold>::New();
    m_threshHigh->SetInputData(m_grid);

    m_mapperHigh = vtkSmartPointer<vtkDataSetMapper>::New();
    m_mapperHigh->SetInputConnection(m_threshHigh->GetOutputPort());

    m_actorHigh = vtkSmartPointer<vtkActor>::New();
    m_actorHigh->SetMapper(m_mapperHigh);
    m_actorHigh->GetProperty()->SetOpacity(0.6);

    // 等值面（中间界面）
    m_cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
    m_cellToPoint->PassCellDataOn();

    m_contourIso = vtkSmartPointer<vtkContourFilter>::New();
    m_contourIso->SetInputData(m_grid);

    m_mapperIso = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapperIso->SetInputConnection(m_contourIso->GetOutputPort());

    m_actorIso = vtkSmartPointer<vtkActor>::New();
    m_actorIso->SetMapper(m_mapperIso);
    m_actorIso->GetProperty()->SetOpacity(1.0);
    m_actorIso->GetProperty()->SetColor(1, 1, 1); // 纯白界面

    // 首次构建时：加入场景，并记挂载标志
    if (m_actorLow && !m_attachedLow)
    {
        addActorByService(m_visualService, m_actorLow, false);
        m_attachedLow = true;
    }
    if (m_actorHigh && !m_attachedHigh)
    {
        addActorByService(m_visualService, m_actorHigh, false);
        m_attachedHigh = true;
    }
    if (m_actorIso && !m_attachedIso)
    {
        addActorByService(m_visualService, m_actorIso, false);
        m_attachedIso = true;
    }
}

void IsosurfaceSegmentationWidget::setIsoSpinRangeFromScalar()
{
    if (!m_grid)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
        return;

    arr->GetRange(m_scalarRange);

    ui->isoSpin->blockSignals(true);
    ui->isoSpin->setRange(m_scalarRange[0], m_scalarRange[1]);
    ui->isoSpin->setValue((m_scalarRange[0] + m_scalarRange[1]) * 0.5);
    ui->isoSpin->blockSignals(false);
}

double IsosurfaceSegmentationWidget::currentScalarMin() const
{
    return m_scalarRange[0];
}
double IsosurfaceSegmentationWidget::currentScalarMax() const
{
    return m_scalarRange[1];
}

void IsosurfaceSegmentationWidget::updateIsoFromUI()
{
    if (!m_grid || !m_threshLow || !m_threshHigh || !m_contourIso)
        return;

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* selectedArray = PostFieldUtils::array(m_grid, field);
    if (!selectedArray)
        return;
    const QByteArray encodedName = field.name.toUtf8();
    const int assoc = field.association;
    QString contourArrayName = field.name;

    // 百分比/绝对值 -> iso
    double iso = 0.0;
    if (ui->percentModeRadioButton->isChecked())
    {
        const double t = ui->isoSlider->value() / 100.0;
        iso = currentScalarMin() + t * (currentScalarMax() - currentScalarMin());
        ui->isoSpin->blockSignals(true);
        ui->isoSpin->setValue(iso);
        ui->isoSpin->blockSignals(false);
    }
    else
    {
        iso = ui->isoSpin->value();
        double t = 0.0;
        if (currentScalarMax() > currentScalarMin())
            t = (iso - currentScalarMin()) / (currentScalarMax() - currentScalarMin());
        t = std::max(0.0, std::min(1.0, t));
        ui->isoSlider->blockSignals(true);
        ui->isoSlider->setValue(int(std::round(t * 100)));
        ui->isoSlider->blockSignals(false);
    }

    // —— 阈值分割 ——（用一个很小的 eps 避免两侧重叠）
    const double eps = 1e-6 * std::max(1.0, std::abs(currentScalarMax() - currentScalarMin()));

    // 低值侧：(-inf, iso - eps]
    m_threshLow->SetInputArrayToProcess(0, 0, 0, assoc, encodedName.constData());
#if VTK_VERSION_NUMBER < VTK_VERSION_CHECK(9, 1, 0)
    m_threshLow->ThresholdByLower(iso - eps);
#else
    m_threshLow->SetThresholdFunction(vtkThreshold::THRESHOLD_LOWER);
    m_threshLow->SetLowerThreshold(iso - eps);
#endif

    // 高值侧：[iso + eps, +inf)
    m_threshHigh->SetInputArrayToProcess(0, 0, 0, assoc, encodedName.constData());
#if VTK_VERSION_NUMBER < VTK_VERSION_CHECK(9, 1, 0)
    m_threshHigh->ThresholdByUpper(iso + eps);
#else
    m_threshHigh->SetThresholdFunction(vtkThreshold::THRESHOLD_UPPER);
    m_threshHigh->SetUpperThreshold(iso + eps);
#endif

    m_threshLow->Update();
    m_threshHigh->Update();

    // —— 中间等值面 ——
    // vtkContourFilter 需要节点标量；选择单元数据时先显式转换，避免界面为空或使用错误数组。
    if (assoc == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        auto preparedGrid = PostFieldUtils::prepareCellScalarForPointConversion(
            m_grid, field, &contourArrayName);
        if (!preparedGrid)
            return;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        m_contourIso->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        m_contourIso->SetInputData(m_grid);
    }
    const QByteArray contourArray = contourArrayName.toUtf8();
    m_contourIso->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                         contourArray.constData());
    m_contourIso->SetNumberOfContours(1);
    m_contourIso->SetValue(0, iso);
    m_contourIso->Update();
}

void IsosurfaceSegmentationWidget::updateColoringFromUI()
{
    if (!m_grid || !m_mapperLow || !m_mapperHigh || !m_mapperIso)
        return;

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
        return;
    const int assoc = field.association;
    const QByteArray encodedName = field.name.toUtf8();

    double dataRange[2];
    arr->GetRange(dataRange);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);

    if (!m_visualService)
        return;
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

    // 低值侧
    m_mapperLow->SetLookupTable(lut);
    if (assoc == vtkDataObject::FIELD_ASSOCIATION_POINTS)
        m_mapperLow->SetScalarModeToUsePointFieldData();
    else
        m_mapperLow->SetScalarModeToUseCellFieldData();
    m_mapperLow->SelectColorArray(encodedName.constData());
    m_mapperLow->SetScalarRange(range);

    // 高值侧
    m_mapperHigh->SetLookupTable(lut);
    if (assoc == vtkDataObject::FIELD_ASSOCIATION_POINTS)
        m_mapperHigh->SetScalarModeToUsePointFieldData();
    else
        m_mapperHigh->SetScalarModeToUseCellFieldData();
    m_mapperHigh->SelectColorArray(encodedName.constData());
    m_mapperHigh->SetScalarRange(range);

    // 等值面
    m_mapperIso->SetLookupTable(lut);
    m_mapperIso->SetScalarModeToUsePointFieldData(); // 等值面输出为点场
    const QByteArray contourArray =
        (assoc == vtkDataObject::FIELD_ASSOCIATION_CELLS
             ? PostFieldUtils::convertedCellArrayName(field)
             : field.name)
            .toUtf8();
    m_mapperIso->SelectColorArray(contourArray.constData());
    m_mapperIso->SetScalarRange(range);

    if (m_visualService)
    {
        applyStandardPostScalarBar(
            m_visualService, ResultDisplayNames::scalarBarName(field.name));
        m_visualService->showScalarBar(postScalarBarStyle().visible, false);
    }

    // 缓存给 Spin/Slider 使用
    m_scalarRange[0] = range[0];
    m_scalarRange[1] = range[1];
}

void IsosurfaceSegmentationWidget::updateVisibilityFromUI()
{
    if (m_actorLow)
        m_actorLow->SetVisibility(ui->showLowCheckBox->isChecked());
    if (m_actorHigh)
        m_actorHigh->SetVisibility(ui->showHighCheckBox->isChecked());
    if (m_actorIso)
        m_actorIso->SetVisibility(ui->showInterfaceCheckBox->isChecked());

    if (m_actorLow)
        m_actorLow->GetProperty()->SetOpacity(ui->lowOpacitySpinBox->value() / 100.0);
    if (m_actorHigh)
        m_actorHigh->GetProperty()->SetOpacity(ui->highOpacitySpinBox->value() / 100.0);
    if (m_actorIso)
        m_actorIso->GetProperty()->SetOpacity(ui->interfaceOpacitySpinBox->value() / 100.0);
}

void IsosurfaceSegmentationWidget::renderNow()
{
    if (!m_visualService)
        return;
    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void IsosurfaceSegmentationWidget::attachActorsIfNeeded()
{
    if (m_actorLow && !m_attachedLow)
    {
        addActorByService(m_visualService, m_actorLow, false);
        m_attachedLow = true;
    }
    if (m_actorHigh && !m_attachedHigh)
    {
        addActorByService(m_visualService, m_actorHigh, false);
        m_attachedHigh = true;
    }
    if (m_actorIso && !m_attachedIso)
    {
        addActorByService(m_visualService, m_actorIso, false);
        m_attachedIso = true;
    }
}

void IsosurfaceSegmentationWidget::detachActorsIfAttached()
{
    if (m_actorLow && m_attachedLow)
    {
        removeActorByService(m_visualService, m_actorLow, false);
        m_attachedLow = false;
    }
    if (m_actorHigh && m_attachedHigh)
    {
        removeActorByService(m_visualService, m_actorHigh, false);
        m_attachedHigh = false;
    }
    if (m_actorIso && m_attachedIso)
    {
        removeActorByService(m_visualService, m_actorIso, false);
        m_attachedIso = false;
    }
}

void IsosurfaceSegmentationWidget::on_scalarComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox,
                                                     PostFieldUtils::NameRole,
                                                     PostFieldUtils::AssociationRole);
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    buildPipelineIfNeeded(); // 首次保障已构建（后续无事发生）
    setIsoSpinRangeFromScalar();
    updateColoringFromUI();
    updateIsoFromUI();
    updateVisibilityFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_percentCheckBox_toggled(bool)
{
    const bool percentMode = ui->percentModeRadioButton->isChecked();
    ui->isoSlider->setEnabled(percentMode);
    ui->isoSpin->setEnabled(!percentMode);
    updateIsoFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_isoSlider_valueChanged(int)
{
    if (!ui->percentModeRadioButton->isChecked())
        return;
    updateIsoFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_isoSpin_valueChanged(double)
{
    if (ui->percentModeRadioButton->isChecked())
        return;
    updateIsoFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_showLowCheckBox_toggled(bool checked)
{
    ui->lowOpacitySpinBox->setEnabled(checked);
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_showHighCheckBox_toggled(bool checked)
{
    ui->highOpacitySpinBox->setEnabled(checked);
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_showInterfaceCheckBox_toggled(bool checked)
{
    ui->interfaceOpacitySpinBox->setEnabled(checked);
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_lowOpacitySlider_valueChanged(int)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_highOpacitySlider_valueChanged(int)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_ifaceOpacitySlider_valueChanged(int)
{
    updateVisibilityFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_stepComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;

    const QString file = ui->stepComboBox->currentData().toString();
    if (file.isEmpty())
        return;

    // 读取失败时保留当前有效结果，不把空网格送进已有管线。
    if (!readTecplot(file))
        return;

    // 重新填充（并尽量保留选择）
    fillScalarCombo();

    // 若已有管线，切换输入；否则首次构建
    if (!m_threshLow || !m_threshHigh || !m_contourIso)
    {
        buildPipelineIfNeeded();
    }
    else
    {
        m_threshLow->SetInputData(m_grid);
        m_threshHigh->SetInputData(m_grid);
        m_cellToPoint->SetInputData(m_grid);
    }

    // 按当前 UI 刷新色标/阈值/显隐
    updateColoringFromUI();
    updateIsoFromUI();
    updateVisibilityFromUI();

    // 确保 actor 已挂
    attachActorsIfNeeded();

    renderNow();
}
