#include "IsosurfaceSettingsWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkDataArray.h>
#include <vtkContourFilter.h>
#include <vtkDataObject.h> // for SetInputArrayToProcess
#include <vtkDataSetSurfaceFilter.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QRadioButton>
#include <QSignalBlocker>

#include "PostFieldUtils.h"
#include "ResultDisplayNames.h"
#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ui_IsosurfaceSettingsWidget.h"

#include <vtkActor.h>

#include <algorithm>
#include <cmath>

static QVector<double> genUniformTs(int N, bool includeEnds)
{
    QVector<double> ts;
    N = std::max(1, N);
    ts.reserve(N);

    if (includeEnds)
    {
        if (N == 1)
        {
            ts << 0.5;
        }
        else
        {
            for (int i = 0; i < N; ++i)
                ts << double(i) / double(N - 1);
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
                ts << (i + 1) * step;
        }
    }
    return ts;
}

IsosurfaceSettingsWidget::IsosurfaceSettingsWidget(VisualWidgetService* ppw, QWidget* parent)
    : ui(new Ui::IsosurfaceSettingsWidget), BasePage(PublicConstants::kPagePostProcessIsosurface),
      m_visualService(ppw)
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
            updateOverallColoring();
            if (m_singleDisplayed)
                updateSingleIsoRealtime();
            else if (m_multiDisplayed)
                buildMultiIsoResult();
            renderNow();
        }
    });

    ui->singleIsoRadioButton->setChecked(true);
    ui->isoModeStackedWidget->setCurrentIndex(0);

    // UI 默认
    ui->percentModeRadioButton->setChecked(true);
    ui->valueModeRadioButton->setChecked(false);
    ui->isoSlider->setRange(0, 100);
    ui->isoSlider->setTracking(false);
    ui->isoSlider->setValue(50);
    ui->isoSpin->setDecimals(6);
    ui->isoSpin->setSingleStep(0.001);
    ui->isoSlider->setEnabled(true);
    ui->isoSpin->setEnabled(false);

    if (ui->isoCountSpin)
    {
        ui->isoCountSpin->setRange(1, 100);
        if (ui->isoCountSpin->value() < 1)
            ui->isoCountSpin->setValue(5);
    }
    if (ui->includeEndsCheck)
        ui->includeEndsCheck->setChecked(false);
    ui->confirmPushButton->setText(QStringLiteral("生成多等值面"));
    ui->confirmPushButton->setVisible(false);

    // 连接信号
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->percentModeRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSettingsWidget::on_percentCheckBox_toggled);
    connect(ui->isoSlider, &QSlider::valueChanged, this,
            &IsosurfaceSettingsWidget::on_isoSlider_valueChanged);
    connect(ui->isoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &IsosurfaceSettingsWidget::on_isoSpin_valueChanged);

    connect(ui->confirmPushButton, &QPushButton::clicked, this,
            &IsosurfaceSettingsWidget::on_confirmPushButton_clicked);
    connect(ui->analysisComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSettingsWidget::on_analysisComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSettingsWidget::on_resultDirComboBox_currentIndexChanged);
    connect(ui->resultTimeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSettingsWidget::on_resultTimeComboBox_currentIndexChanged);

    connect(ui->singleIsoRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSettingsWidget::on_singleIsoGroupBox_toggled);
    connect(ui->multiIsoRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSettingsWidget::on_multiIsoGroupBox_toggled);

    EventManager::instance().registerHandler(
        postDisplaySettingsChangedEvent(), this,
        [this](const QVariantMap& data)
        {
            if (!property("postDisplayActive").toBool())
                return;
            {
                const QSignalBlocker scalarBarBlocker(ui->showScalarBarCheckBox);
                ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            }
            if (data.value(QStringLiteral("reason")).toString() == QStringLiteral("mesh-filter"))
                setIsoSpinRangeFromScalar();
            updateOverallColoring();
            if (m_singleDisplayed)
                updateSingleIsoRealtime();
            else if (m_multiDisplayed)
                buildMultiIsoResult();
            renderNow();
        });
}

IsosurfaceSettingsWidget::~IsosurfaceSettingsWidget()
{
    delete ui;
}

void IsosurfaceSettingsWidget::beforeLoad()
{
    setProperty("postDisplayActive", true);
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    refreshResultGroups();
    if (ui->stepComboBox->currentIndex() == -1)
    {
        m_loadedOnce = false;
    }
    if (!m_loadedOnce)
    {
        // <project>/data
        const QString projectDir =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (m_dataPath.isEmpty())
            m_dataPath = PostResultAutoPrepare::firstResultDirectory(projectDir);
        //        readTecplotLatest();
        loadFileName(m_dataPath);
        //        fillScalarCombo();
        updateOverallColoring(); // 显示整体云图
        m_loadedOnce = true;
    }

    applyPendingState();

    // 切回页面：按状态把之前的内容挂回去
    reattachAccordingToState();
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
    renderNow();
}

QJsonObject IsosurfaceSettingsWidget::saveJson()
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
    object.insert(QStringLiteral("singleMode"), ui->singleIsoRadioButton->isChecked());
    object.insert(QStringLiteral("isoCount"), ui->isoCountSpin->value());
    object.insert(QStringLiteral("includeEnds"), ui->includeEndsCheck->isChecked());
    return object;
}

void IsosurfaceSettingsWidget::loadJson(const QJsonObject& object)
{
    m_pendingState.clear();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingState.insert(it.key(), it.value().toVariant());
    m_hasPendingState = true;
    if (m_loadedOnce)
        applyPendingState();
}

void IsosurfaceSettingsWidget::applyPendingState()
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
        const QSignalBlocker singleBlocker(ui->singleIsoRadioButton);
        const QSignalBlocker multiBlocker(ui->multiIsoRadioButton);
        const QSignalBlocker countBlocker(ui->isoCountSpin);
        const QSignalBlocker endsBlocker(ui->includeEndsCheck);

        const bool percentMode = state.value(QStringLiteral("percentMode"), true).toBool();
        const bool singleMode = state.value(QStringLiteral("singleMode"), true).toBool();
        ui->percentModeRadioButton->setChecked(percentMode);
        ui->valueModeRadioButton->setChecked(!percentMode);
        ui->isoSlider->setValue(state.value(QStringLiteral("isoPercent"), 50).toInt());
        if (!percentMode)
            ui->isoSpin->setValue(state.value(QStringLiteral("isoValue"), ui->isoSpin->value()).toDouble());
        ui->singleIsoRadioButton->setChecked(singleMode);
        ui->multiIsoRadioButton->setChecked(!singleMode);
        ui->isoModeStackedWidget->setCurrentIndex(singleMode ? 0 : 1);
        ui->isoCountSpin->setValue(state.value(QStringLiteral("isoCount"), 5).toInt());
        ui->includeEndsCheck->setChecked(state.value(QStringLiteral("includeEnds"), false).toBool());
    }
    ui->isoSlider->setEnabled(ui->percentModeRadioButton->isChecked());
    ui->isoSpin->setEnabled(!ui->percentModeRadioButton->isChecked());

    detachAllFromScene();
    m_actorOverall = nullptr;
    clearMultiResults();
    m_singleDisplayed = false;
    m_multiDisplayed = false;
    updateOverallColoring();
    if (ui->singleIsoRadioButton->isChecked())
        updateSingleIsoRealtime();
    else if (m_actorOverall)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")),
                          m_actorOverall, QStringLiteral("结果表面"), false);
}

void IsosurfaceSettingsWidget::refreshResultGroups()
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

void IsosurfaceSettingsWidget::on_analysisComboBox_currentIndexChanged(int)
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

void IsosurfaceSettingsWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void IsosurfaceSettingsWidget::on_resultTimeComboBox_currentIndexChanged(int)
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


void IsosurfaceSettingsWidget::beforeUnload()
{
    setPostViewActive(false);
    setProperty("postDisplayActive", false);
    clearPostDisplayContext();
    // 只移除，不销毁
    detachAllFromScene();
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
}

void IsosurfaceSettingsWidget::loadFileName(QString dataPath)
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

void IsosurfaceSettingsWidget::readTecplotLatest()
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

bool IsosurfaceSettingsWidget::readTecplot(const QString& file)
{
    QString errorMessage;
    auto newGrid = PostTecplotUtils::readGrid(file, &errorMessage);
    if (!newGrid || newGrid->GetNumberOfPoints() == 0 || newGrid->GetNumberOfCells() == 0)
    {
        qWarning() << "[Isosurface] Failed to read grid:" << file << errorMessage;
        return false;
    }
    m_grid = newGrid;
    return true;
}

void IsosurfaceSettingsWidget::fillScalarCombo()
{
    // QSignalBlocker 可保证所有提前返回路径都会恢复信号状态。
    PostFieldUtils::fillCombo(ui->scalarComboBox, m_grid);
    const int sharedFieldIndex = PostResultSelectionUtils::findSharedField(
        ui->scalarComboBox, PostFieldUtils::NameRole, PostFieldUtils::AssociationRole);
    if (sharedFieldIndex >= 0)
        ui->scalarComboBox->setCurrentIndex(sharedFieldIndex);
    setIsoSpinRangeFromScalar();
}

void IsosurfaceSettingsWidget::setIsoSpinRangeFromScalar()
{
    if (!m_grid)
        return;
    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        detachAllFromScene();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        if (property("postDisplayActive").toBool())
            clearPostDisplayContext();
        return;
    }

    arr->GetRange(m_scalarRange);
    ui->isoSpin->blockSignals(true);
    ui->isoSpin->setRange(m_scalarRange[0], m_scalarRange[1]);
    ui->isoSpin->setValue(0.5 * (m_scalarRange[0] + m_scalarRange[1]));
    ui->isoSpin->blockSignals(false);
}

vtkLookupTable* IsosurfaceSettingsWidget::ensureLUT()
{
    if (!m_visualService)
        return nullptr;
    auto* scalarBar = m_visualService->scalarBarActor();
    if (!scalarBar)
        return nullptr;

    auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
    if (!lut)
    {
        auto newLut = vtkSmartPointer<vtkLookupTable>::New();
        scalarBar->SetLookupTable(newLut);
        lut = newLut;
    }
    return lut;
}

void IsosurfaceSettingsWidget::updateOverallColoring()
{
    if (!m_grid)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        detachAllFromScene();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        clearPostDisplayContext();
        detachAllFromScene();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }
    const QByteArray encodedName = field.name.toUtf8();

    double dataRange[2];
    arr->GetRange(dataRange);
    m_scalarRange[0] = dataRange[0];
    m_scalarRange[1] = dataRange[1];
    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(field.name, dataRange[0], dataRange[1], false, false);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);
    auto* lut = ensureLUT();
    if (!lut)
        return;
    applyStandardPostLookupTable(lut, range[0], range[1]);

    vtkSmartPointer<vtkDataSetSurfaceFilter> surfaceFilter =
        vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(displayGrid);
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

    m_actorOverall = vtkSmartPointer<vtkActor>::New();
    m_actorOverall->SetMapper(mapper);
    m_actorOverall->GetProperty()->SetRepresentationToSurface();

    if (m_visualService)
    {
        applyStandardPostScalarBar(
            m_visualService, ResultDisplayNames::scalarBarName(field.name));
        m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    }
}

void IsosurfaceSettingsWidget::renderNow()
{
    if (!m_visualService)
        return;
    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

double IsosurfaceSettingsWidget::currentSingleIsoFromUI() const
{
    if (ui->percentModeRadioButton->isChecked())
    {
        const double t = ui->isoSlider->value() / 100.0;
        return scalarMin() + t * (scalarMax() - scalarMin());
    }
    else
    {
        return ui->isoSpin->value();
    }
}

QVector<double> IsosurfaceSettingsWidget::multiIsoValuesFromUI() const
{
    const int N = ui->isoCountSpin ? std::max(1, ui->isoCountSpin->value()) : 1;
    const bool includeEnds = ui->includeEndsCheck->isChecked();
    const auto ts = genUniformTs(N, includeEnds);

    QVector<double> vals;
    vals.reserve(N);
    const double lo = scalarMin(), hi = scalarMax();
    for (double t : ts)
        vals << (lo + t * (hi - lo));
    return vals;
}

bool IsosurfaceSettingsWidget::ensureSinglePipeline()
{
    if (!m_grid)
        return false;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return false;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        qWarning() << "[Isosurface] Scalar array not found:" << field.name;
        return false;
    }
    const QByteArray encodedName = field.name.toUtf8();
    QString contourArrayName = field.name;

    if (!m_contourSingle)
    {
        m_contourSingle = vtkSmartPointer<vtkContourFilter>::New();
        m_contourSingle->ComputeNormalsOn();
        m_contourSingle->SetNumberOfContours(1);
    }

    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        if (!m_cellToPoint)
        {
            m_cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
            m_cellToPoint->PassCellDataOn();
        }
        auto preparedGrid = PostFieldUtils::prepareCellScalarForPointConversion(
            displayGrid, field, &contourArrayName);
        if (!preparedGrid)
            return false;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        m_contourSingle->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        displayGrid->GetPointData()->SetActiveScalars(encodedName.constData());
        m_contourSingle->SetInputData(displayGrid);
    }
    const QByteArray contourArray = contourArrayName.toUtf8();
    m_contourSingle->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                            contourArray.constData());

    if (!m_mapperSingle)
    {
        m_mapperSingle = vtkSmartPointer<vtkPolyDataMapper>::New();
        m_mapperSingle->SetInputConnection(m_contourSingle->GetOutputPort());
    }
    if (!m_actorSingle)
    {
        m_actorSingle = vtkSmartPointer<vtkActor>::New();
        m_actorSingle->SetMapper(m_mapperSingle);
        m_actorSingle->GetProperty()->SetRepresentationToSurface();
    }

    double dataRange[2];
    arr->GetRange(dataRange);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);
    auto* lut = ensureLUT();
    if (!lut)
        return false;
    applyStandardPostLookupTable(lut, range[0], range[1]);

    m_mapperSingle->SetLookupTable(lut);
    m_mapperSingle->SetScalarModeToUsePointFieldData();
    m_mapperSingle->SelectColorArray(contourArray.constData());
    m_mapperSingle->SetScalarRange(range);
    m_mapperSingle->ScalarVisibilityOn();
    return true;
}

void IsosurfaceSettingsWidget::updateSingleIsoRealtime()
{
    if (!m_grid)
        return;
    if (!ensureSinglePipeline() || !m_contourSingle || !m_actorSingle)
        return;

    const double iso = currentSingleIsoFromUI();
    m_contourSingle->SetValue(0, iso);
    m_contourSingle->Modified();
    m_contourSingle->Update();

    // 隐藏整体 & 多等值面
    if (m_actorOverall)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")), false);
    hideMultiActorsFromScene();

    // 挂单等值面
    addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("single")),
                      m_actorSingle, QStringLiteral("等值面"), false);
    if (m_visualService)
    {
        const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
        m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
        applyStandardPostScalarBar(
            m_visualService, ResultDisplayNames::scalarBarName(field.name));
    }
    m_singleDisplayed = true;
    m_multiDisplayed = false;

    renderNow();
}

void IsosurfaceSettingsWidget::buildSingleIsoResult()
{
    // 现在单等值面走实时，这个函数保留以备需要时调用
    updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::buildMultiIsoResult()
{
    if (!m_grid)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        qWarning() << "[Isosurface] Scalar array not found for multi:" << field.name;
        return;
    }
    const QByteArray encodedName = field.name.toUtf8();
    QString contourArrayName = field.name;

    clearMultiResults();

    double dataRange[2];
    arr->GetRange(dataRange);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);
    auto* lut = ensureLUT();
    if (!lut)
        return;
    applyStandardPostLookupTable(lut, range[0], range[1]);

    const QVector<double> values = multiIsoValuesFromUI();
    auto contour = vtkSmartPointer<vtkContourFilter>::New();
    contour->ComputeNormalsOn();
    contour->SetNumberOfContours(values.size());
    for (int i = 0; i < values.size(); ++i)
        contour->SetValue(i, values.at(i));

    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        if (!m_cellToPoint)
        {
            m_cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
            m_cellToPoint->PassCellDataOn();
        }
        auto preparedGrid = PostFieldUtils::prepareCellScalarForPointConversion(
            displayGrid, field, &contourArrayName);
        if (!preparedGrid)
            return;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        contour->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        displayGrid->GetPointData()->SetActiveScalars(encodedName.constData());
        contour->SetInputData(displayGrid);
    }
    const QByteArray contourArray = contourArrayName.toUtf8();
    contour->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    contourArray.constData());
    contour->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(contour->GetOutputPort());
    mapper->SetLookupTable(lut);
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray(contourArray.constData());
    mapper->SetScalarRange(range);
    mapper->ScalarVisibilityOn();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetRepresentationToSurface();

    m_multiContours << contour;
    m_multiMappers << mapper;
    m_multiActors << actor;

    if (m_actorOverall)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")), false);
    if (m_actorSingle)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("single")), false);
    const int multiIndex = m_multiActors.size() - 1;
    addActorByService(m_visualService,
                      postActorId(getUuid(), QStringLiteral("multi-%1").arg(multiIndex)),
                      actor, QStringLiteral("多等值面"), false);

    m_singleDisplayed = false;
    m_multiDisplayed = true;
    renderNow();
}

void IsosurfaceSettingsWidget::detachAllFromScene()
{
    if (m_actorOverall)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")), false);
    if (m_actorSingle)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("single")), false);
    hideMultiActorsFromScene();
}

void IsosurfaceSettingsWidget::reattachAccordingToState()
{
    // 清空当前显示
    detachAllFromScene();

    const bool singleMode = ui->singleIsoRadioButton && ui->singleIsoRadioButton->isChecked();
    const bool multiMode = ui->multiIsoRadioButton && ui->multiIsoRadioButton->isChecked();

    if (m_multiDisplayed && multiMode && !m_multiActors.isEmpty())
    {
        for (int i = 0; i < m_multiActors.size(); ++i)
            if (m_multiActors[i])
                addActorByService(
                    m_visualService, postActorId(getUuid(), QStringLiteral("multi-%1").arg(i)),
                    m_multiActors[i], QStringLiteral("多等值面"), false);
        return;
    }

    if (singleMode)
    {
        // 单等值面是实时显示：若已有 actor 直接挂回；否则先挂整体
        if (m_actorSingle)
        {
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("single")),
                              m_actorSingle, QStringLiteral("等值面"));
            m_singleDisplayed = true;
            return;
        }
        if (m_actorOverall)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")),
                          m_actorOverall, QStringLiteral("结果表面"));
        return;
    }

    // 其它情况：默认整体
    if (m_actorOverall)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")),
                          m_actorOverall, QStringLiteral("结果表面"));
}

void IsosurfaceSettingsWidget::hideMultiActorsFromScene()
{
    for (int i = 0; i < m_multiActors.size(); ++i)
        if (m_multiActors[i])
            removeActorByService(
                m_visualService, postActorId(getUuid(), QStringLiteral("multi-%1").arg(i)), false);
}

void IsosurfaceSettingsWidget::clearMultiResults()
{
    hideMultiActorsFromScene();
    m_multiContours.clear();
    m_multiMappers.clear();
    m_multiActors.clear();
}

void IsosurfaceSettingsWidget::on_scalarComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox,
                                                     PostFieldUtils::NameRole,
                                                     PostFieldUtils::AssociationRole);
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (!m_grid)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        qWarning() << "[Isosurface] Scalar array not found:" << field.name;
        return;
    }
    arr->GetRange(m_scalarRange);
    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(field.name, m_scalarRange[0], m_scalarRange[1], false, false);
    if (m_actorOverall)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")), false);
    m_actorOverall = nullptr;
    setIsoSpinRangeFromScalar();
    updateOverallColoring();

    const bool singleMode = ui->singleIsoRadioButton->isChecked();
    const bool multiMode = ui->multiIsoRadioButton->isChecked();

    if (singleMode)
    {
        updateSingleIsoRealtime();
    }
    else if (multiMode)
    {
        hideMultiActorsFromScene();
        if (m_actorOverall)
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")),
                          m_actorOverall, QStringLiteral("结果表面"));
        renderNow();
        //        buildMultiIsoResult();
    }
}

void IsosurfaceSettingsWidget::on_percentCheckBox_toggled(bool checked)
{
    ui->isoSlider->setEnabled(checked);
    ui->isoSpin->setEnabled(!checked);
    // 单等值面 = 实时更新
    if (ui->singleIsoRadioButton && ui->singleIsoRadioButton->isChecked())
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_isoSlider_valueChanged(int)
{
    // 单等值面模式下实时
    if (ui->singleIsoRadioButton && ui->singleIsoRadioButton->isChecked())
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_isoSpin_valueChanged(double)
{
    // 单等值面模式下实时
    if (ui->singleIsoRadioButton && ui->singleIsoRadioButton->isChecked())
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_confirmPushButton_clicked()
{
    if (ui->multiIsoRadioButton->isChecked())
        buildMultiIsoResult();
}

void IsosurfaceSettingsWidget::on_singleIsoGroupBox_toggled(bool checked)
{
    if (!checked)
        return;

    ui->isoModeStackedWidget->setCurrentIndex(0);
    ui->confirmPushButton->setVisible(false);
    hideMultiActorsFromScene();
    m_multiDisplayed = false;
    updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_multiIsoGroupBox_toggled(bool checked)
{
    if (!checked)
        return;

    ui->isoModeStackedWidget->setCurrentIndex(1);
    ui->confirmPushButton->setVisible(true);
    ui->confirmPushButton->setText(QStringLiteral("生成多等值面"));
    if (m_actorSingle)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("single")), false);
    m_singleDisplayed = false;

    hideMultiActorsFromScene();
    m_multiDisplayed = false;
    if (m_actorOverall)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")),
                          m_actorOverall, QStringLiteral("结果表面"));
    renderNow();
}

void IsosurfaceSettingsWidget::on_stepComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;

    const QString fullName = ui->stepComboBox->currentData().toString();
    if (fullName.isEmpty())
        return;

    if (!QFileInfo::exists(fullName))
    {
        qWarning() << "[Isosurface] File not found:" << fullName;
        return;
    }

    // 读取失败时保留当前有效结果，避免后续解引用空网格。
    if (!readTecplot(fullName))
        return;

    // 用新数据刷新 scalar 下拉与范围
    fillScalarCombo(); // 或者至少 setIsoSpinRangeFromScalar();

    // ——关键重置：把依赖旧 m_grid 的管线/actor 清掉——
    // 1) 单等值面：移除并标记为未显示（确保不会挡住整体云图）
    if (m_actorSingle)
    {
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("single")), false);
    }
    if (m_actorOverall)
    {
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")), false);
    }
    m_contourSingle = nullptr;
    m_mapperSingle = nullptr;
    m_actorSingle = nullptr;
    m_actorOverall = nullptr;
    // m_actorSingle 可以复用也行，这里不置空也没问题
    m_singleDisplayed = false;

    // 2) 多等值面：完全清空，等待用户点“确认”重建
    clearMultiResults();
    m_multiDisplayed = false;

    // 根据当前模式重建/显示
    const bool singleMode = ui->singleIsoRadioButton->isChecked();
    const bool multiMode = ui->multiIsoRadioButton->isChecked();

    // 多等值面模式下，尚未点“确认”之前显示整体云图
    updateOverallColoring();

    if (singleMode)
    {
        // 进入单等值面：立即用新 m_grid 实时更新
        updateSingleIsoRealtime();
        return;
    }

    if (m_actorOverall)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("overall")),
                          m_actorOverall, QStringLiteral("结果表面"));

    renderNow();
}
