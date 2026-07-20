#include "IsosurfaceSettingsWidget.h"

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
#include <QSignalBlocker>

#include "PostFieldUtils.h"
#include "ResultDisplayNames.h"
#include "PostResultAutoPrepare.h"
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

    ui->singleIsoRadioButton->setChecked(true);
    ui->isoModeStackedWidget->setCurrentIndex(0);

    // UI 默认
    ui->percentCheckBox->setChecked(true);
    ui->isoSlider->setRange(0, 100);
    ui->isoSlider->setTracking(false);
    ui->isoSlider->setToolTip(QStringLiteral("松开滑块后更新等值面，避免拖动时连续重算"));
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

    // 连接信号
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->percentCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSettingsWidget::on_percentCheckBox_toggled);
    connect(ui->isoSlider, &QSlider::valueChanged, this,
            &IsosurfaceSettingsWidget::on_isoSlider_valueChanged);
    connect(ui->isoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &IsosurfaceSettingsWidget::on_isoSpin_valueChanged);

    connect(ui->confirmPushButton, &QPushButton::clicked, this,
            &IsosurfaceSettingsWidget::on_confirmPushButton_clicked);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSettingsWidget::on_resultDirComboBox_currentIndexChanged);

    connect(ui->singleIsoRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSettingsWidget::on_singleIsoGroupBox_toggled);
    connect(ui->multiIsoRadioButton, &QRadioButton::toggled, this,
            &IsosurfaceSettingsWidget::on_multiIsoGroupBox_toggled);
}

IsosurfaceSettingsWidget::~IsosurfaceSettingsWidget()
{
    delete ui;
}

void IsosurfaceSettingsWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
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
            m_visualService->showScalarBar(true, false);
            if (auto* scalarBar = m_visualService->scalarBarActor())
                scalarBar->SetTitle(ResultDisplayNames::scalarBarName(field.name).toUtf8().constData());
        }
    }
    renderNow();
}

QJsonObject IsosurfaceSettingsWidget::saveJson()
{
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    QJsonObject object;
    object.insert(QStringLiteral("resultDir"), ui->resultDirComboBox->currentData().toString());
    object.insert(QStringLiteral("stepFile"), ui->stepComboBox->currentData().toString());
    object.insert(QStringLiteral("fieldName"), field.name);
    object.insert(QStringLiteral("fieldAssociation"), field.association);
    object.insert(QStringLiteral("percentMode"), ui->percentCheckBox->isChecked());
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

    const QString resultDir = state.value(QStringLiteral("resultDir")).toString();
    if (!resultDir.isEmpty())
    {
        const int index = ui->resultDirComboBox->findData(resultDir);
        if (index >= 0)
        {
            const QSignalBlocker blocker(ui->resultDirComboBox);
            ui->resultDirComboBox->setCurrentIndex(index);
            m_dataPath = resultDir;
        }
    }
    loadFileName(m_dataPath);

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
        const QSignalBlocker percentBlocker(ui->percentCheckBox);
        const QSignalBlocker sliderBlocker(ui->isoSlider);
        const QSignalBlocker spinBlocker(ui->isoSpin);
        const QSignalBlocker singleBlocker(ui->singleIsoRadioButton);
        const QSignalBlocker multiBlocker(ui->multiIsoRadioButton);
        const QSignalBlocker countBlocker(ui->isoCountSpin);
        const QSignalBlocker endsBlocker(ui->includeEndsCheck);

        const bool percentMode = state.value(QStringLiteral("percentMode"), true).toBool();
        const bool singleMode = state.value(QStringLiteral("singleMode"), true).toBool();
        ui->percentCheckBox->setChecked(percentMode);
        ui->isoSlider->setValue(state.value(QStringLiteral("isoPercent"), 50).toInt());
        if (!percentMode)
            ui->isoSpin->setValue(state.value(QStringLiteral("isoValue"), ui->isoSpin->value()).toDouble());
        ui->singleIsoRadioButton->setChecked(singleMode);
        ui->multiIsoRadioButton->setChecked(!singleMode);
        ui->isoModeStackedWidget->setCurrentIndex(singleMode ? 0 : 1);
        ui->isoCountSpin->setValue(state.value(QStringLiteral("isoCount"), 5).toInt());
        ui->includeEndsCheck->setChecked(state.value(QStringLiteral("includeEnds"), false).toBool());
    }
    ui->isoSlider->setEnabled(ui->percentCheckBox->isChecked());
    ui->isoSpin->setEnabled(!ui->percentCheckBox->isChecked());

    detachAllFromScene();
    m_actorOverall = nullptr;
    clearMultiResults();
    m_singleDisplayed = false;
    m_multiDisplayed = false;
    updateOverallColoring();
    if (ui->singleIsoRadioButton->isChecked())
        updateSingleIsoRealtime();
    else if (m_actorOverall)
        addActorByService(m_visualService, m_actorOverall, false);
}

void IsosurfaceSettingsWidget::refreshResultGroups()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousPath = m_dataPath;
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);

    ui->resultDirComboBox->blockSignals(true);
    ui->resultDirComboBox->clear();
    for (const PostResultAutoPrepare::ResultGroup& group : groups)
        ui->resultDirComboBox->addItem(group.displayName, group.sourcePath);

    int index = previousPath.isEmpty() ? -1 : ui->resultDirComboBox->findData(previousPath);
    if (index < 0 && ui->resultDirComboBox->count() > 0)
        index = 0;
    ui->resultDirComboBox->setCurrentIndex(index);
    m_dataPath = index >= 0 ? ui->resultDirComboBox->itemData(index).toString() : QString();
    ui->resultDirComboBox->blockSignals(false);
}

void IsosurfaceSettingsWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void IsosurfaceSettingsWidget::beforeUnload()
{
    // 只移除，不销毁
    detachAllFromScene();
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
}

void IsosurfaceSettingsWidget::loadFileName(QString dataPath)
{
    ui->stepComboBox->blockSignals(true);
    ui->stepComboBox->clear();

    QDir directory(dataPath);
    if (!directory.exists())
    {
        qWarning() << "Directory not found:" << dataPath;
        ui->stepComboBox->blockSignals(false);
        return;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QVector<PostResultAutoPrepare::ResultFile> files =
        PostResultAutoPrepare::resultFiles(projectDir, dataPath, false);

    for (const PostResultAutoPrepare::ResultFile& file : files)
    {
        const QString itemName = file.displayName.trimmed().isEmpty()
                                     ? QStringLiteral("结果数据")
                                     : file.displayName.trimmed();
        ui->stepComboBox->addItem(itemName, file.filePath);
    }

    ui->stepComboBox->blockSignals(false);

    if (ui->stepComboBox->count() > 0)
    {
        ui->stepComboBox->setCurrentIndex(0);
        on_stepComboBox_currentIndexChanged(0);
    }
    else
    {
        qWarning() << "No configured result files found in" << dataPath;
    }
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
    setIsoSpinRangeFromScalar();
}

void IsosurfaceSettingsWidget::setIsoSpinRangeFromScalar()
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
    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
        return;
    const QByteArray encodedName = field.name.toUtf8();

    double range[2];
    arr->GetRange(range);
    auto* lut = ensureLUT();
    if (!lut)
        return;
    lut->SetTableRange(range);
    lut->Build();

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

    m_actorOverall = vtkSmartPointer<vtkActor>::New();
    m_actorOverall->SetMapper(mapper);
    m_actorOverall->GetProperty()->SetRepresentationToSurface();

    if (m_visualService)
    {
        m_visualService->showScalarBar(true, false);
        if (auto* scalarBar = m_visualService->scalarBarActor())
            scalarBar->SetTitle(ResultDisplayNames::scalarBarName(field.name).toUtf8().constData());
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
    if (ui->percentCheckBox->isChecked())
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

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
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
            m_grid, field, &contourArrayName);
        if (!preparedGrid)
            return false;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        m_contourSingle->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        m_grid->GetPointData()->SetActiveScalars(encodedName.constData());
        m_contourSingle->SetInputData(m_grid);
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

    double range[2];
    arr->GetRange(range);
    auto* lut = ensureLUT();
    if (!lut)
        return false;
    lut->SetTableRange(range);
    lut->Build();

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
        removeActorByService(m_visualService, m_actorOverall, false);
    hideMultiActorsFromScene();

    // 挂单等值面
    addActorByService(m_visualService, m_actorSingle, false);
    if (m_visualService)
    {
        const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
        m_visualService->showScalarBar(true, false);
        if (auto* scalarBar = m_visualService->scalarBarActor())
            scalarBar->SetTitle(ResultDisplayNames::scalarBarName(field.name).toUtf8().constData());
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

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
    {
        qWarning() << "[Isosurface] Scalar array not found for multi:" << field.name;
        return;
    }
    const QByteArray encodedName = field.name.toUtf8();
    QString contourArrayName = field.name;

    clearMultiResults();

    double range[2];
    arr->GetRange(range);
    auto* lut = ensureLUT();
    if (!lut)
        return;
    lut->SetTableRange(range);
    lut->Build();

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
            m_grid, field, &contourArrayName);
        if (!preparedGrid)
            return;
        m_cellToPoint->SetInputData(preparedGrid);
        m_cellToPoint->Update();
        contour->SetInputConnection(m_cellToPoint->GetOutputPort());
    }
    else
    {
        m_grid->GetPointData()->SetActiveScalars(encodedName.constData());
        contour->SetInputData(m_grid);
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
        removeActorByService(m_visualService, m_actorOverall, false);
    if (m_actorSingle)
        removeActorByService(m_visualService, m_actorSingle, false);
    addActorByService(m_visualService, actor, false);

    m_singleDisplayed = false;
    m_multiDisplayed = true;
    renderNow();
}

void IsosurfaceSettingsWidget::detachAllFromScene()
{
    if (m_actorOverall)
        removeActorByService(m_visualService, m_actorOverall, false);
    if (m_actorSingle)
        removeActorByService(m_visualService, m_actorSingle, false);
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
        for (auto& a : m_multiActors)
            if (a)
                addActorByService(m_visualService, a, false);
        return;
    }

    if (singleMode)
    {
        // 单等值面是实时显示：若已有 actor 直接挂回；否则先挂整体
        if (m_actorSingle)
        {
            addActorByService(m_visualService, m_actorSingle);
            m_singleDisplayed = true;
            return;
        }
        if (m_actorOverall)
            addActorByService(m_visualService, m_actorOverall);
        return;
    }

    // 其它情况：默认整体
    if (m_actorOverall)
        addActorByService(m_visualService, m_actorOverall);
}

void IsosurfaceSettingsWidget::hideMultiActorsFromScene()
{
    for (auto& a : m_multiActors)
        if (a)
            removeActorByService(m_visualService, a, false);
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
    if (!m_grid)
        return;

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
    {
        qWarning() << "[Isosurface] Scalar array not found:" << field.name;
        return;
    }
    arr->GetRange(m_scalarRange);
    if (m_actorOverall)
        removeActorByService(m_visualService, m_actorOverall, false);
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
            addActorByService(m_visualService, m_actorOverall);
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
    else
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_singleIsoGroupBox_toggled(bool checked)
{
    if (!checked)
        return;

    ui->isoModeStackedWidget->setCurrentIndex(0);
    hideMultiActorsFromScene();
    m_multiDisplayed = false;
    updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_multiIsoGroupBox_toggled(bool checked)
{
    if (!checked)
        return;

    ui->isoModeStackedWidget->setCurrentIndex(1);
    if (m_actorSingle)
        removeActorByService(m_visualService, m_actorSingle, false);
    m_singleDisplayed = false;

    hideMultiActorsFromScene();
    m_multiDisplayed = false;
    if (m_actorOverall)
        addActorByService(m_visualService, m_actorOverall);
    renderNow();
}

void IsosurfaceSettingsWidget::on_stepComboBox_currentIndexChanged(int index)
{
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
        removeActorByService(m_visualService, m_actorSingle, false);
    }
    if (m_actorOverall)
    {
        removeActorByService(m_visualService, m_actorOverall, false);
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
        addActorByService(m_visualService, m_actorOverall);

    renderNow();
}
