#include "BlockSettingsWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkAppendFilter.h>
#include <vtkLookupTable.h>
#include <vtkCellDataToPointData.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QDebug>
#include <QJsonObject>
#include <QDir>
#include <QRegularExpression>
#include <QSignalBlocker>

#include "PostFieldUtils.h"
#include "ResultDisplayNames.h"
#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ui_BlockSettingsWidget.h"

#include <algorithm>
#include <cmath>

BlockSettingsWidget::BlockSettingsWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::BlockSettingsWidget), BasePage(PublicConstants::kPagePostProcessBlock),
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
        updateContourOverlay();
        if (m_visualService)
            m_visualService->render();
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
        if (editPostDisplaySettings(this, m_visualService, true, false,
                                    field.name, dataMin, dataMax))
        {
            ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            ui->showContourCheckBox->setChecked(postContourStyle().enabled);
            updateColoringFromUI();
            renderNow();
        }
    });

    m_plane = vtkSmartPointer<vtkPlane>::New();
    m_actor = vtkSmartPointer<vtkActor>::New();

    ui->blockSlider->setRange(0, 1000);
    ui->blockSlider->setTracking(false);
    ui->blockSlider->setValue(500);
    ui->blockPositionSpin->setKeyboardTracking(false);
    ui->flipCheckBox->setChecked(false);
    ui->flipCheckBox->setToolTip(QStringLiteral("勾选后保留切平面的另一侧"));

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_stepComboBox_currentIndexChanged);

    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->planeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_planeComboBox_currentIndexChanged);
    connect(ui->blockSlider, &QSlider::valueChanged, this,
            &BlockSettingsWidget::on_blockSlider_valueChanged);
    connect(ui->blockPositionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &BlockSettingsWidget::on_blockPositionSpin_valueChanged);
    connect(ui->flipCheckBox, &QCheckBox::toggled, this,
            &BlockSettingsWidget::on_flipCheckBox_toggled);
    connect(ui->analysisComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_analysisComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_resultDirComboBox_currentIndexChanged);
    connect(ui->resultTimeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_resultTimeComboBox_currentIndexChanged);

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
            updateColoringFromUI();
            updateContourOverlay();
            renderNow();
        });
}

BlockSettingsWidget::~BlockSettingsWidget()
{
    delete ui;
}

void BlockSettingsWidget::beforeLoad()
{
    setProperty("postDisplayActive", true);
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);
    refreshResultGroups();
    if (m_dataPath.isEmpty())
    {
        const QString workPath =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (m_dataPath.isEmpty())
            m_dataPath = PostResultAutoPrepare::firstResultDirectory(workPath);
    }

    if (ui->stepComboBox->count() == 0)
    {
        // 首次进入：列出 step，并触发一次读取
        loadFileName(m_dataPath);
        // buildClipPipelineIfNeeded/updateColoring/updatePlane 会在 on_step...
        // 和下面调用
        buildClipPipelineIfNeeded();
        updateColoringFromUI();
        updatePlaneFromUI();
        m_loadedOnce = true;
    }
    else
    {
        // 非首次：恢复 actor（如果之前被 beforeUnload 隐藏）
        if (m_actor && !m_isAttached)
        {
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("block")),
                          m_actor, QStringLiteral("切块"));
            m_isAttached = true;
        }
        // 确保 mapper/LUT/plane 与当前 UI 一致
        buildClipPipelineIfNeeded();
        updateColoringFromUI();
        updatePlaneFromUI();
    }

    applyPendingState();
    renderNow();
}

QJsonObject BlockSettingsWidget::saveJson()
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
    object.insert(QStringLiteral("positionCoordinate"), ui->blockPositionSpin->value());
    object.insert(QStringLiteral("flip"), ui->flipCheckBox->isChecked());
    return object;
}

void BlockSettingsWidget::loadJson(const QJsonObject& object)
{
    m_pendingState.clear();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingState.insert(it.key(), it.value().toVariant());
    m_hasPendingState = true;
    if (m_loadedOnce)
        applyPendingState();
}

void BlockSettingsWidget::applyPendingState()
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
            if (readTecplot(stepFile))
            {
                if (m_clipper)
                    m_clipper->SetInputData(m_grid);
            }
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
        ui->planeComboBox->setCurrentIndex(
            std::max(0, std::min(state.value(QStringLiteral("planeIndex"), 0).toInt(),
                                 std::max(0, ui->planeComboBox->count() - 1))));
    }
    double positionRatio = state.value(QStringLiteral("positionRatio"), -1.0).toDouble();
    if (positionRatio < 0.0)
        positionRatio = state.value(QStringLiteral("position"), 50).toDouble() / 100.0;
    updatePositionControls(std::max(0.0, std::min(positionRatio, 1.0)));
    {
        const QSignalBlocker flipBlocker(ui->flipCheckBox);
        ui->flipCheckBox->setChecked(state.value(QStringLiteral("flip"), false).toBool());
    }

    buildClipPipelineIfNeeded();
    updateColoringFromUI();
    updatePlaneFromUI();
}

void BlockSettingsWidget::refreshResultGroups()
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

void BlockSettingsWidget::on_analysisComboBox_currentIndexChanged(int)
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

void BlockSettingsWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void BlockSettingsWidget::on_resultTimeComboBox_currentIndexChanged(int)
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

void BlockSettingsWidget::beforeUnload()
{
    setPostViewActive(false);
    setProperty("postDisplayActive", false);
    clearPostDisplayContext();
    // 只移除，不销毁；保持状态与对象以便回来恢复
    if (m_actor && m_isAttached)
    {
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("block")));
        m_isAttached = false;
    }
    if (m_contourActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("contour")), false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
}

void BlockSettingsWidget::loadFileName(QString dataPath)
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

void BlockSettingsWidget::loadLatestData()
{
    QDir dir(m_dataPath);
    if (!dir.exists())
    {
        qWarning() << "data path not exist:" << m_dataPath;
        return;
    }
    QRegularExpression re("stress_strain(\\d+)-(\\d+)\\.txt");
    int bestG = -1, bestO = -1;
    QString bestFile;

    for (const QString& f : dir.entryList(QDir::Files))
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
            bestFile = dir.filePath(f);
        }
    }
    if (!bestFile.isEmpty())
        readTecplot(bestFile);
}

bool BlockSettingsWidget::readTecplot(const QString& fileName)
{
    QString errorMessage;
    auto newGrid = PostTecplotUtils::readGrid(fileName, &errorMessage);
    if (!newGrid || newGrid->GetNumberOfPoints() == 0 || newGrid->GetNumberOfCells() == 0)
    {
        qWarning() << "[Block] Failed to read grid:" << fileName << errorMessage;
        return false;
    }

    m_grid = newGrid;

    // 缓存范围
    double b[6];
    m_grid->GetBounds(b);
    m_XRange[0] = b[0];
    m_XRange[1] = b[1];
    m_YRange[0] = b[2];
    m_YRange[1] = b[3];
    m_ZRange[0] = b[4];
    m_ZRange[1] = b[5];
    updatePositionControls(currentNormalizedPosition());

    // 标量下拉同时区分节点数据与单元数据，并尽量保留之前的选择。
    PostFieldUtils::fillCombo(ui->scalarComboBox, m_grid);
    const int sharedFieldIndex = PostResultSelectionUtils::findSharedField(
        ui->scalarComboBox, PostFieldUtils::NameRole, PostFieldUtils::AssociationRole);
    if (sharedFieldIndex >= 0)
        ui->scalarComboBox->setCurrentIndex(sharedFieldIndex);
    return true;
}

void BlockSettingsWidget::buildClipPipelineIfNeeded()
{
    if (!m_grid)
        return;
    if (m_clipper)
        return;

    // 1) Clip
    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    m_clipper = vtkSmartPointer<vtkTableBasedClipDataSet>::New();
    m_clipper->SetInputData(displayGrid);
    m_clipper->SetClipFunction(m_plane);
    m_clipper->InsideOutOff();             // 默认保留法向一侧
    m_clipper->GenerateClippedOutputOff(); // 不输出反面
    m_clipper->Update();

    // 2) Mapper
    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_mapper->SetInputConnection(m_clipper->GetOutputPort());

    // 3) Actor
    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(m_mapper);
    applyPostActorRepresentation(m_actor);

    // 首次建管线时即挂入场景
    addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("block")),
                          m_actor, QStringLiteral("切块"));
    m_isAttached = true;
}

void BlockSettingsWidget::updatePlaneFromUI()
{
    if (!m_grid || !m_clipper)
        return;

    const int planeIndex = ui->planeComboBox->currentIndex();
    const double coordinate = ui->blockPositionSpin->value();

    double origin[3] = {0, 0, 0};
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

    if (ui->flipCheckBox->isChecked())
        m_clipper->InsideOutOn();
    else
        m_clipper->InsideOutOff();
    m_clipper->Update();
}

void BlockSettingsWidget::updateColoringFromUI()
{
    if (!m_grid || !m_mapper || !m_clipper)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        if (m_actor)
            m_actor->SetVisibility(false);
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }
    m_clipper->SetInputData(displayGrid);
    m_clipper->Update();
    if (m_actor)
        m_actor->SetVisibility(postSceneStyle().visible);

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(displayGrid, field);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        clearPostDisplayContext();
        if (m_actor)
            m_actor->SetVisibility(false);
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }

    const QByteArray encodedName = field.name.toUtf8();
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
        displayGrid->GetCellData()->SetActiveScalars(encodedName.constData());
    else
        displayGrid->GetPointData()->SetActiveScalars(encodedName.constData());

    double dataRange[2];
    arr->GetRange(dataRange);
    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(field.name, dataRange[0], dataRange[1], true, false);
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
    applyPostFieldLookupTable(lut, field.name, range[0], range[1]);

    m_mapper->SetLookupTable(lut);
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
        m_mapper->SetScalarModeToUseCellFieldData();
    else
        m_mapper->SetScalarModeToUsePointFieldData();
    m_mapper->SelectColorArray(encodedName.constData());
    m_mapper->SetScalarRange(range);
    m_mapper->ScalarVisibilityOn();

    if (m_visualService)
    {
        applyStandardPostScalarBar(
            m_visualService, ResultDisplayNames::scalarBarName(field.name));
        m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    }
    updateContourOverlay();
}

void BlockSettingsWidget::updateContourOverlay()
{
    if (m_contourActor)
    {
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("contour")), false);
        m_contourActor = nullptr;
    }
    if (!m_visualService || !m_grid || !m_clipper || !postContourStyle().enabled)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return;
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* array = PostFieldUtils::array(displayGrid, field);
    if (!array || array->GetNumberOfTuples() <= 0)
        return;
    double dataRange[2]{0.0, 1.0};
    array->GetRange(dataRange);
    double range[2]{dataRange[0], dataRange[1]};
    resolvePostDisplayRange(field.name, dataRange[0], dataRange[1], range[0], range[1]);

    // createPostContourActor() 统一负责“提当前可见表面 -> Contour”。
    // 这里仅在 CellData 场时做一次 CellData -> PointData，避免各页面重复维护 Surface 管线。
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
    {
        auto cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
        cellToPoint->SetInputConnection(m_clipper->GetOutputPort());
        cellToPoint->PassCellDataOn();
        m_contourActor = createPostContourActor(cellToPoint->GetOutputPort(), field.name,
                                                range[0], range[1]);
    }
    else
    {
        m_contourActor = createPostContourActor(m_clipper->GetOutputPort(), field.name,
                                                range[0], range[1]);
    }
    if (m_contourActor)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("contour")),
                          m_contourActor, QStringLiteral("等值线"), false);
}

void BlockSettingsWidget::renderNow()
{
    if (!m_visualService)
        return;
    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void BlockSettingsWidget::on_scalarComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox,
                                                     PostFieldUtils::NameRole,
                                                     PostFieldUtils::AssociationRole);
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    buildClipPipelineIfNeeded();
    updateColoringFromUI();
    renderNow();
}

void BlockSettingsWidget::on_planeComboBox_currentIndexChanged(int)
{
    const double position = currentNormalizedPosition();
    updatePositionControls(position);
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

void BlockSettingsWidget::on_blockSlider_valueChanged(int)
{
    const double ratio = currentNormalizedPosition();
    const int axis = ui->planeComboBox->currentIndex();
    const double minimum = axis == 0 ? m_XRange[0] : (axis == 1 ? m_YRange[0] : m_ZRange[0]);
    const double maximum = axis == 0 ? m_XRange[1] : (axis == 1 ? m_YRange[1] : m_ZRange[1]);
    {
        const QSignalBlocker blocker(ui->blockPositionSpin);
        ui->blockPositionSpin->setValue(minimum + ratio * (maximum - minimum));
    }
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

void BlockSettingsWidget::on_blockPositionSpin_valueChanged(double value)
{
    const int axis = ui->planeComboBox->currentIndex();
    const double minimum = axis == 0 ? m_XRange[0] : (axis == 1 ? m_YRange[0] : m_ZRange[0]);
    const double maximum = axis == 0 ? m_XRange[1] : (axis == 1 ? m_YRange[1] : m_ZRange[1]);
    const double span = maximum - minimum;
    const double ratio = span > 0.0 ? std::max(0.0, std::min((value - minimum) / span, 1.0)) : 0.5;
    {
        const QSignalBlocker blocker(ui->blockSlider);
        ui->blockSlider->setValue(qRound(ratio * ui->blockSlider->maximum()));
    }
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

double BlockSettingsWidget::currentNormalizedPosition() const
{
    if (!ui->blockSlider || ui->blockSlider->maximum() <= 0)
        return 0.5;
    return std::max(0.0, std::min(ui->blockSlider->value() / double(ui->blockSlider->maximum()), 1.0));
}

void BlockSettingsWidget::updatePositionControls(double normalizedPosition)
{
    normalizedPosition = std::max(0.0, std::min(normalizedPosition, 1.0));
    const int axis = ui->planeComboBox->currentIndex();
    const double minimum = axis == 0 ? m_XRange[0] : (axis == 1 ? m_YRange[0] : m_ZRange[0]);
    const double maximum = axis == 0 ? m_XRange[1] : (axis == 1 ? m_YRange[1] : m_ZRange[1]);
    const QString axisName = axis == 0 ? QStringLiteral("X") : (axis == 1 ? QStringLiteral("Y") : QStringLiteral("Z"));
    {
        const QSignalBlocker sliderBlocker(ui->blockSlider);
        const QSignalBlocker spinBlocker(ui->blockPositionSpin);
        ui->blockSlider->setValue(qRound(normalizedPosition * ui->blockSlider->maximum()));
        ui->blockPositionSpin->setRange(minimum, maximum);
        ui->blockPositionSpin->setValue(minimum + normalizedPosition * (maximum - minimum));
    }
    ui->blockRangeLabel->setText(
        QStringLiteral("范围：%1  %2 ～ %3")
            .arg(axisName, QString::number(minimum, 'g', 8), QString::number(maximum, 'g', 8)));
}

void BlockSettingsWidget::on_flipCheckBox_toggled(bool)
{
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

void BlockSettingsWidget::on_stepComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;

    const QString file = ui->stepComboBox->currentData().toString();
    if (file.isEmpty())
        return;

    // 读取失败时保留上一份有效结果，不继续把空输入送入 VTK 管线。
    if (!readTecplot(file))
        return;

    // 如已建管线，切换输入；否则现在就建
    if (!m_clipper)
    {
        buildClipPipelineIfNeeded();
    }
    else
    {
        m_clipper->SetInputData(postFilteredResultGrid(m_grid));
        m_clipper->Update();
    }

    // 刷新着色/切块位置（沿用当前 UI）
    updateColoringFromUI();
    updatePlaneFromUI();

    // 确保 actor 已挂
    if (m_actor && !m_isAttached)
    {
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("block")),
                          m_actor, QStringLiteral("切块"));
        m_isAttached = true;
    }

    renderNow();
}
