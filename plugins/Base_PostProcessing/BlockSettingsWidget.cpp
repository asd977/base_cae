#include "BlockSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkAppendFilter.h>
#include <vtkLookupTable.h>
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

    m_plane = vtkSmartPointer<vtkPlane>::New();
    m_actor = vtkSmartPointer<vtkActor>::New();

    ui->blockSlider->setRange(0, 100);
    ui->blockSlider->setTracking(false);
    ui->blockSlider->setToolTip(QStringLiteral("松开滑块后更新切块，避免连续重算"));
    ui->blockSlider->setValue(50);
    ui->blockLineEdit->setText(QString("0.50"));
    ui->flipCheckBox->setChecked(false);

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_stepComboBox_currentIndexChanged);

    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->planeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_planeComboBox_currentIndexChanged);
    connect(ui->blockSlider, &QSlider::valueChanged, this,
            &BlockSettingsWidget::on_blockSlider_valueChanged);
    connect(ui->flipCheckBox, &QCheckBox::toggled, this,
            &BlockSettingsWidget::on_flipCheckBox_toggled);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BlockSettingsWidget::on_resultDirComboBox_currentIndexChanged);
}

BlockSettingsWidget::~BlockSettingsWidget()
{
    delete ui;
}

void BlockSettingsWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
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
            addActorByService(m_visualService, m_actor);
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
    object.insert(QStringLiteral("resultDir"), ui->resultDirComboBox->currentData().toString());
    object.insert(QStringLiteral("stepFile"), ui->stepComboBox->currentData().toString());
    object.insert(QStringLiteral("fieldName"), field.name);
    object.insert(QStringLiteral("fieldAssociation"), field.association);
    object.insert(QStringLiteral("planeIndex"), ui->planeComboBox->currentIndex());
    object.insert(QStringLiteral("position"), ui->blockSlider->value());
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
    {
        const QSignalBlocker positionBlocker(ui->blockSlider);
        ui->blockSlider->setValue(state.value(QStringLiteral("position"), 50).toInt());
    }
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

void BlockSettingsWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}
void BlockSettingsWidget::beforeUnload()
{
    // 只移除，不销毁；保持状态与对象以便回来恢复
    if (m_actor && m_isAttached)
    {
        removeActorByService(m_visualService, m_actor);
        m_isAttached = false;
    }
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
}

void BlockSettingsWidget::loadFileName(QString dataPath)
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

    // 标量下拉同时区分节点数据与单元数据，并尽量保留之前的选择。
    PostFieldUtils::fillCombo(ui->scalarComboBox, m_grid);
    return true;
}

void BlockSettingsWidget::buildClipPipelineIfNeeded()
{
    if (!m_grid)
        return;
    if (m_clipper)
        return;

    // 1) Clip
    m_clipper = vtkSmartPointer<vtkTableBasedClipDataSet>::New();
    m_clipper->SetInputData(m_grid);
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
    m_actor->GetProperty()->SetRepresentationToSurface();

    // 首次建管线时即挂入场景
    addActorByService(m_visualService, m_actor);
    m_isAttached = true;
}

void BlockSettingsWidget::updatePlaneFromUI()
{
    if (!m_grid || !m_clipper)
        return;

    const QString dir = ui->planeComboBox->currentText(); // "XY"/"XZ"/"YZ"
    const double t = ui->blockSlider->value() / 100.0;
    ui->blockLineEdit->setText(QString::number(t, 'f', 3));

    double origin[3] = {0, 0, 0};
    if (dir == "YZ")
    { // X=const
        origin[0] = m_XRange[0] + t * (m_XRange[1] - m_XRange[0]);
        m_plane->SetNormal(1, 0, 0);
    }
    else if (dir == "XZ")
    { // Y=const
        origin[1] = m_YRange[0] + t * (m_YRange[1] - m_YRange[0]);
        m_plane->SetNormal(0, 1, 0);
    }
    else
    { // XY: Z=const
        origin[2] = m_ZRange[0] + t * (m_ZRange[1] - m_ZRange[0]);
        m_plane->SetNormal(0, 0, 1);
    }
    m_plane->SetOrigin(origin);

    // Flip：InsideOut = 取反
    if (ui->flipCheckBox->isChecked())
        m_clipper->InsideOutOn();
    else
        m_clipper->InsideOutOff();

    m_clipper->Update();
}

void BlockSettingsWidget::updateColoringFromUI()
{
    if (!m_grid || !m_mapper)
        return;

    const PostFieldUtils::Field field = PostFieldUtils::selectedField(ui->scalarComboBox);
    vtkDataArray* arr = PostFieldUtils::array(m_grid, field);
    if (!arr)
        return;

    const QByteArray encodedName = field.name.toUtf8();
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
        m_grid->GetCellData()->SetActiveScalars(encodedName.constData());
    else
        m_grid->GetPointData()->SetActiveScalars(encodedName.constData());

    double range[2];
    arr->GetRange(range);

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
        m_visualService->showScalarBar(true, false);
        if (auto* scalarBar = m_visualService->scalarBarActor())
            scalarBar->SetTitle(ResultDisplayNames::displayName(field.name).toUtf8().constData());
    }
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
    buildClipPipelineIfNeeded();
    updateColoringFromUI();
    renderNow();
}

void BlockSettingsWidget::on_planeComboBox_currentIndexChanged(int)
{
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

void BlockSettingsWidget::on_blockSlider_valueChanged(int)
{
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

void BlockSettingsWidget::on_flipCheckBox_toggled(bool)
{
    buildClipPipelineIfNeeded();
    updatePlaneFromUI();
    renderNow();
}

void BlockSettingsWidget::on_stepComboBox_currentIndexChanged(int index)
{
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
        m_clipper->SetInputData(m_grid);
        m_clipper->Update();
    }

    // 刷新着色/切块位置（沿用当前 UI）
    updateColoringFromUI();
    updatePlaneFromUI();

    // 确保 actor 已挂
    if (m_actor && !m_isAttached)
    {
        addActorByService(m_visualService, m_actor);
        m_isAttached = true;
    }

    renderNow();
}
