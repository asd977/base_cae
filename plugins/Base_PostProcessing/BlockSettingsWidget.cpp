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

#include <QDebug>
#include <QDir>
#include <QRegularExpression>

#include "PostResultAutoPrepare.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ui_BlockSettingsWidget.h"

#include <cmath>

BlockSettingsWidget::BlockSettingsWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::BlockSettingsWidget), BasePage(PublicConstants::kPagePostProcessBlock),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    m_plane = vtkSmartPointer<vtkPlane>::New();
    m_actor = vtkSmartPointer<vtkActor>::New();

    ui->blockSlider->setRange(0, 100);
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

    renderNow();
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

void BlockSettingsWidget::readTecplot(const QString& fileName)
{
    m_grid = PostTecplotUtils::readGrid(fileName);

    if (!m_grid || m_grid->GetNumberOfPoints() == 0)
    {
        qWarning() << "clip: no grid";
        return;
    }

    // 缓存范围
    double b[6];
    m_grid->GetBounds(b);
    m_XRange[0] = b[0];
    m_XRange[1] = b[1];
    m_YRange[0] = b[2];
    m_YRange[1] = b[3];
    m_ZRange[0] = b[4];
    m_ZRange[1] = b[5];

    // 标量下拉：尽量保留之前的选择
    const QString prev = ui->scalarComboBox->currentText();
    QStringList scalars;
    auto pd = m_grid->GetPointData();
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
        if (const char* n = pd->GetArrayName(i))
            scalars << QString::fromUtf8(n);
    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();
    ui->scalarComboBox->addItems(scalars);
    int keep = ui->scalarComboBox->findText(prev);
    ui->scalarComboBox->setCurrentIndex(keep >= 0 ? keep : (scalars.isEmpty() ? -1 : 0));
    ui->scalarComboBox->blockSignals(false);
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

    const QString scalar = ui->scalarComboBox->currentText();
    if (scalar.isEmpty())
        return;

    auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data());
    if (!arr)
        return;

    // 设为 active，保证后续 mapper/过滤器行为一致
    m_grid->GetPointData()->SetActiveScalars(scalar.toUtf8().data());

    double range[2];
    arr->GetRange(range);

    auto lut = vtkLookupTable::SafeDownCast(m_visualService->scalarBarActor()->GetLookupTable());
    if (!lut)
    {
        lut = vtkSmartPointer<vtkLookupTable>::New();
        m_visualService->scalarBarActor()->SetLookupTable(lut);
    }
    lut->SetTableRange(range);
    lut->Build();

    m_mapper->SetLookupTable(lut);
    m_mapper->SetScalarModeToUsePointFieldData();
    m_mapper->SelectColorArray(scalar.toUtf8().data());
    m_mapper->SetScalarRange(range);
}

void BlockSettingsWidget::renderNow()
{
    if (auto rw = m_visualService->renderWindow())
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

    // 读取新数据
    readTecplot(file);

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
