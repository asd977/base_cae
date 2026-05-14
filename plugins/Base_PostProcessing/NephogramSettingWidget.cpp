#include "NephogramSettingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkCell.h>
#include <vtkPolyDataMapper.h>
#include <vtkSelectVisiblePoints.h>
#include <vtkContourFilter.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkMaskPoints.h>
#include <vtkLabeledDataMapper.h>
#include <vtkCellPicker.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkPoints.h>
#include <vtkDataSetMapper.h>
#include <vtkDataObject.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTecplotReader.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMap>
#include <QRegularExpression>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <set>

#include "PostVisualUtils.h"
#include "ui_NephogramSettingWidget.h"

namespace
{

struct ResultDirectoryConfig
{
    QString dirName;      // 实际文件夹名
    QString displayName;  // UI显示名
    QString description;  // 说明
};

struct ResultFileConfig
{
    QString fileName;     // 实际文件名
    QString displayName;  // UI显示名
    QString description;  // 说明
};

const QVector<ResultDirectoryConfig> kResultDirectoryConfigs = {
    {"SEEPCASE01",   "渗流结果", "渗流计算结果目录"},
    {"CASE01FILL01", "土石坝结果", "土石坝填筑/坝体计算结果目录"},
    {"GEOSTR", "地应力回归", "地应力回归计算结果目录"},
    {"CASE01JACA/J03","开挖结果J03","开挖结果J03"},
    {"CASE01","边坡稳定","边坡稳定"}



};

const QVector<ResultFileConfig> kPreferredResultFileConfigs = {
    {"TEC_HP.DAT",   "水头渗压", "水头、渗压结果"},
    {"TEC_VELO.DAT", "渗流速度", "渗流速度分量"},
    {"TEC_DISP.DAT", "坝体位移", "坝体位移结果"},
    {"TEC_PRS.DAT",  "主应力",   "主应力分量"},
    {"TEC_CMM.DAT",   "应力水平", "应力水平结果"}
};

} // namespace

NephogramSettingWidget::NephogramSettingWidget(VisualWidgetService* postProcessingWidget,
                                               QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessNephogram), ui(new Ui::NephogramSettingWidget)
{
    ui->setupUi(this);
    m_visualService = postProcessingWidget;

    // 单图对象
    m_actor = vtkSmartPointer<vtkActor>::New();
    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_contourActor = vtkSmartPointer<vtkActor>::New();
    m_contourLabelActor = vtkSmartPointer<vtkActor2D>::New();

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_stepComboBox_currentIndexChanged);
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_resultDirComboBox_currentIndexChanged);

    connect(ui->compareEnableCheckBox, &QCheckBox::toggled, this,
            &NephogramSettingWidget::on_compareEnableCheckBox_toggled);
    connect(ui->leftStepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_leftStepComboBox_currentIndexChanged);
    connect(ui->rightStepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_rightStepComboBox_currentIndexChanged);
    connect(ui->splitSlider, &QSlider::valueChanged, this,
            &NephogramSettingWidget::on_splitSlider_valueChanged);

    // 分屏滑块范围
    if (ui->splitSlider->minimum() != 5 || ui->splitSlider->maximum() != 95)
    {
        ui->splitSlider->setMinimum(5);
        ui->splitSlider->setMaximum(95);
        ui->splitSlider->setValue(50);
    }

    connect(ui->contourEnableCheckBox, &QCheckBox::toggled, this, [this](bool) {
        if (!m_compareEnabled)
            updateNephogram(ui->scalarComboBox->currentText());
    });
    connect(ui->contourLevelsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (!m_compareEnabled)
            updateNephogram(ui->scalarComboBox->currentText());
    });
    connect(ui->contourLabelCheckBox, &QCheckBox::toggled, this, [this](bool) {
        if (!m_compareEnabled)
            updateNephogram(ui->scalarComboBox->currentText());
    });

    // 按需求移除分屏对比入口：仅保留单图云图模式
    m_compareEnabled = false;
    ui->compareEnableCheckBox->setChecked(false);
    ui->compareEnableCheckBox->setEnabled(false);
    ui->pushButton_2->hide();
    ui->widget->hide();

    initResultDirectoryOptions();
}

NephogramSettingWidget::~NephogramSettingWidget()
{
    delete ui;
}

void NephogramSettingWidget::ensureActorAttached()
{
    if (!m_actorAttached)
    {
        addActorByService(m_visualService, m_actor);
        m_actorAttached = true;
    }
}

void NephogramSettingWidget::syncScalarBarFromCurrent()
{
    if (!m_grid || m_currentArrayName.isEmpty())
        return;

    bool usePointData = true;
    vtkDataArray* arr = findScalarArray(m_grid, m_currentArrayName, usePointData);
    if (!arr)
        return;

    double range[2];
    arr->GetRange(range);

    vtkScalarBarActor* scalarBar = m_visualService ? m_visualService->scalarBarActor() : nullptr;
    if (!scalarBar)
        return;

    if (m_visualService)
        m_visualService->showScalarBar(true, false);

    scalarBar->SetWidth(0.08);
    scalarBar->SetHeight(0.5);
    scalarBar->SetPosition(0.90, 0.25);
    scalarBar->GetTitleTextProperty()->SetColor(0.0, 0.0, 0.0);
    scalarBar->GetLabelTextProperty()->SetColor(0.0, 0.0, 0.0);

    if (auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()))
    {
        updateLookupTableRange(lut, range[0], range[1]);
        scalarBar->SetTitle(m_currentArrayName.toUtf8().constData());
        configureMapper(m_mapper, m_currentArrayName, usePointData, range[0], range[1], lut);
    }
}

void NephogramSettingWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
    if (!m_visualService)
    {
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
    }
    if (!m_visualService)
    {
        qWarning() << "Visual service is null in NephogramSettingWidget.";
        return;
    }

    // 每次进入页面都刷新结果文件列表，确保新产生的文件可见。
    const QString previousStep = ui->stepComboBox->currentText();
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);
    if (!previousStep.isEmpty())
    {
        const int previousIndex = ui->stepComboBox->findText(previousStep);
        if (previousIndex >= 0)
            ui->stepComboBox->setCurrentIndex(previousIndex);
    }

    if (ui->stepComboBox->currentIndex() == -1)
    {
        m_initialized = false;
    }

    if (!m_initialized)
    {
        ensureActorAttached();
        m_initialized = true;

        if (auto* ren = m_visualService->renderer())
            ren->ResetCamera();
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
        return;
    }

    ensureActorAttached();
    syncScalarBarFromCurrent();
    m_actor->SetMapper(m_mapper);

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void NephogramSettingWidget::beforeUnload()
{
    if (!m_visualService)
        return;
    clearContourOverlay();
    if (m_actorAttached)
    {
        removeActorByService(m_visualService, m_actor);
        m_actorAttached = false;
    }
    m_visualService->showScalarBar(false, false);

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void NephogramSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    const QString stepText = ui->stepComboBox->itemText(index);
    const QString filePath = m_stepFileMap.value(stepText);
    if (filePath.isEmpty())
    {
        qWarning() << "No data file mapped for step:" << stepText;
        return;
    }

    if (QFileInfo(filePath).suffix().compare("vtu", Qt::CaseInsensitive) == 0)
    {
        readVtu(filePath);
        return;
    }

    readTecplot(filePath);
}

void NephogramSettingWidget::loadFileName(const QString& dataPath)
{
    ui->stepComboBox->blockSignals(true);
    ui->stepComboBox->clear();
    m_stepFileMap.clear();

    QDir directory(dataPath);
    if (!directory.exists())
    {
        qWarning() << "Directory not found:" << dataPath;
        ui->stepComboBox->blockSignals(false);
        fillCompareCombosFromSteps();
        return;
    }

    // 优先加载指定文件名（后续扩展时只改这里）
    for (const auto& cfg : kPreferredResultFileConfigs)
    {
        const QString fullPath = directory.filePath(cfg.fileName);
        if (QFileInfo::exists(fullPath))
        {
            const QString stepName = cfg.displayName;

            m_stepFileMap.insert(stepName, fullPath);
            ui->stepComboBox->addItem(stepName);

            const int index = ui->stepComboBox->count() - 1;
            ui->stepComboBox->setItemData(
                index,
                QString("%1：%2").arg(cfg.fileName, cfg.description),
                Qt::ToolTipRole);
        }
    }

    ui->stepComboBox->blockSignals(false);

    if (ui->stepComboBox->count() > 0)
    {
        ui->stepComboBox->setCurrentIndex(0);
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());
    }
    else
    {
        ui->scalarComboBox->clear();
        qWarning() << "No configured result files found in" << dataPath;
    }

    fillCompareCombosFromSteps();
}

void NephogramSettingWidget::on_resultDirComboBox_currentIndexChanged(int)
{
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);
}

void NephogramSettingWidget::on_scalarComboBox_currentIndexChanged(int)
{
    if (m_compareEnabled)
    {
        updateCompare();
    }
    else
    {
        updateNephogram(ui->scalarComboBox->currentText());
    }
}

void NephogramSettingWidget::readTecplot(const QString& fileName)
{
    vtkSmartPointer<vtkTecplotReader> reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());
    reader->Update();

    vtkMultiBlockDataSet* multi = reader->GetOutput();
    if (!multi || multi->GetNumberOfBlocks() <= 0)
    {
        qWarning() << "Tecplot contains no readable blocks:" << fileName;
        return;
    }

    vtkSmartPointer<vtkAppendFilter> appender = vtkSmartPointer<vtkAppendFilter>::New();
    int addedBlocks = 0;
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i)
    {
        if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i)))
        {
            appender->AddInputData(block);
            ++addedBlocks;
        }
    }
    if (addedBlocks == 0)
    {
        qWarning() << "Tecplot has no unstructured grid blocks:" << fileName;
        return;
    }

    appender->Update();
    m_grid = appender->GetOutput();
    m_loadedFileName = fileName;

    if (!m_grid || m_grid->GetNumberOfPoints() == 0)
    {
        qWarning() << "Failed to combine blocks for" << fileName;
        return;
    }

    QStringList scalarNames;
    if (auto* pd = m_grid->GetPointData())
    {
        const int n = pd->GetNumberOfArrays();
        scalarNames.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const char* name = pd->GetArrayName(i);
            if (name && *name)
                scalarNames << QString::fromUtf8(name);
        }
    }

    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();
    ui->scalarComboBox->addItems(scalarNames);
    if (ui->scalarComboBox->count() > 0 && ui->scalarComboBox->currentIndex() < 0)
        ui->scalarComboBox->setCurrentIndex(0);
    ui->scalarComboBox->blockSignals(false);

    updateNephogram(ui->scalarComboBox->currentText());
}

void NephogramSettingWidget::updateNephogram(const QString& selectedScalarName)
{
    if (!m_grid)
    {
        qWarning() << "No grid to visualize.";
        return;
    }

    bool usePointData = true;
    vtkDataArray* scalarArray = findScalarArray(m_grid, selectedScalarName, usePointData);
    if (!scalarArray)
    {
        qWarning() << "Scalar array not found:" << selectedScalarName;
        return;
    }

    m_currentArrayName = selectedScalarName;
    syncScalarBarFromCurrent();

    m_mapper->SetInputData(m_grid);
    m_actor->SetMapper(m_mapper);
    m_actor->GetProperty()->SetRepresentationToSurface();
    ensureActorAttached();
    double contourRange[2];
    scalarArray->GetRange(contourRange);
    updateContourOverlay(selectedScalarName, usePointData, contourRange[0], contourRange[1]);

    if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
        rw->Render();
}

vtkSmartPointer<vtkUnstructuredGrid>
NephogramSettingWidget::loadGridFromTecplot(const QString& fileName)
{
    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());
    reader->Update();

    auto multi = reader->GetOutput();
    if (!multi || multi->GetNumberOfBlocks() <= 0)
        return nullptr;

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    int addedBlocks = 0;
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i)
    {
        if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i)))
        {
            appender->AddInputData(block);
            ++addedBlocks;
        }
    }
    if (addedBlocks == 0)
        return nullptr;

    appender->Update();
    return appender->GetOutput();
}

vtkSmartPointer<vtkUnstructuredGrid>
NephogramSettingWidget::loadGridFromFile(const QString& fileName)
{
    if (fileName.isEmpty())
        return nullptr;
    if (QFileInfo(fileName).suffix().compare("vtu", Qt::CaseInsensitive) == 0)
    {
        auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
        reader->SetFileName(fileName.toUtf8().constData());
        reader->Update();
        return reader->GetOutput();
    }
    return loadGridFromTecplot(fileName);
}

QString NephogramSettingWidget::resultDirectory() const
{
    return selectedResultDirectoryPath();
}

void NephogramSettingWidget::initResultDirectoryOptions()
{
    ui->resultDirComboBox->blockSignals(true);
    ui->resultDirComboBox->clear();

    for (const auto& cfg : kResultDirectoryConfigs)
    {
        ui->resultDirComboBox->addItem(cfg.displayName, cfg.dirName);

        const int index = ui->resultDirComboBox->count() - 1;
        ui->resultDirComboBox->setItemData(
            index,
            QString("%1：%2").arg(cfg.dirName, cfg.description),
            Qt::ToolTipRole);
    }

    if (ui->resultDirComboBox->count() > 0)
    {
        ui->resultDirComboBox->setCurrentIndex(0);
    }

    ui->resultDirComboBox->blockSignals(false);
}
QString NephogramSettingWidget::selectedResultDirectoryPath() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    QString dirName = ui->resultDirComboBox->currentData().toString();

    if (dirName.isEmpty())
    {
        dirName = "SEEPCASE01";
    }

    return QDir(projectDir).filePath(dirName);
}

void NephogramSettingWidget::enterCompareMode()
{
    if (m_leftRen && m_rightRen)
        return;

    auto* rw = m_visualService->renderWindow();
    if (!rw)
        return;

    // 1) 获取基础 renderer（持有 ScalarBar），并将其作为顶层 Overlay
    vtkRenderer* baseRen = rw->GetRenderers()->GetFirstRenderer();
    if (!baseRen)
        return;
    m_baseRenderer = baseRen;

    // 重要：两层渲染。底层画左右内容，顶层画色标等UI
    rw->SetNumberOfLayers(std::max(rw->GetNumberOfLayers(), 2));

    // 顶层不要清背景，否则会把底层内容盖住
    m_baseRenderer->SetLayer(1);
    m_baseRenderer->SetErase(false); // 仅绘制其 props（ScalarBar 等）

    // 2) 移除单图 actor，避免重叠
    clearContourOverlay();
    if (m_actorAttached)
    {
        removeActorByService(m_visualService, m_actor);
        m_actorAttached = false;
    }

    // 3) 创建左右 renderer，并共享相机
    m_leftRen = vtkSmartPointer<vtkRenderer>::New();
    m_rightRen = vtkSmartPointer<vtkRenderer>::New();

    m_leftRen->SetActiveCamera(m_baseRenderer->GetActiveCamera());
    m_rightRen->SetActiveCamera(m_baseRenderer->GetActiveCamera());

    double bg[3];
    m_baseRenderer->GetBackground(bg);
    m_leftRen->SetBackground(bg);
    m_rightRen->SetBackground(bg);

    // 底层
    m_leftRen->SetLayer(0);
    m_rightRen->SetLayer(0);

    rw->AddRenderer(m_leftRen);
    rw->AddRenderer(m_rightRen);

    // 4) 左右 actor/mapper
    m_leftActor = vtkSmartPointer<vtkActor>::New();
    m_rightActor = vtkSmartPointer<vtkActor>::New();
    m_leftMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_rightMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    m_leftActor->SetMapper(m_leftMapper);
    m_rightActor->SetMapper(m_rightMapper);

    m_leftRen->AddActor(m_leftActor);
    m_rightRen->AddActor(m_rightActor);

    updateViewports(m_splitRatio);
}

void NephogramSettingWidget::exitCompareMode()
{
    auto* rw = m_visualService->renderWindow();

    // 先恢复基础 renderer 为普通层，恢复清屏
    if (m_baseRenderer)
    {
        m_baseRenderer->SetLayer(0);
        m_baseRenderer->SetErase(true);
    }

    if (rw)
    {
        if (m_leftRen)
            rw->RemoveRenderer(m_leftRen);
        if (m_rightRen)
            rw->RemoveRenderer(m_rightRen);
        // 可选：恢复为单层
        //        rw->SetNumberOfLayers(1);
    }

    m_leftRen = nullptr;
    m_rightRen = nullptr;
    m_leftActor = nullptr;
    m_rightActor = nullptr;
    m_leftMapper = nullptr;
    m_rightMapper = nullptr;
    m_leftGrid = nullptr;
    m_rightGrid = nullptr;
    m_baseRenderer = nullptr;

    ensureActorAttached();
}

void NephogramSettingWidget::updateViewports(double ratio)
{
    ratio = std::min(std::max(ratio, 0.05), 0.95);
    if (m_leftRen)
        m_leftRen->SetViewport(0.0, 0.0, ratio, 1.0);
    if (m_rightRen)
        m_rightRen->SetViewport(ratio, 0.0, 1.0, 1.0);
}

void NephogramSettingWidget::syncScalarBarForCompare(vtkDataArray* leftArr, vtkDataArray* rightArr)
{
    if (!leftArr || !rightArr)
        return;

    double rL[2], rR[2];
    leftArr->GetRange(rL);
    rightArr->GetRange(rR);
    const double minV = std::min(rL[0], rR[0]);
    const double maxV = std::max(rL[1], rR[1]);

    vtkScalarBarActor* scalarBar = m_visualService ? m_visualService->scalarBarActor() : nullptr;
    if (!scalarBar)
        return;

    if (m_visualService)
        m_visualService->showScalarBar(true, false);

    scalarBar->SetWidth(0.08);
    scalarBar->SetHeight(0.5);
    scalarBar->SetPosition(0.90, 0.25);
    scalarBar->GetTitleTextProperty()->SetColor(0.0, 0.0, 0.0);
    scalarBar->GetLabelTextProperty()->SetColor(0.0, 0.0, 0.0);

    if (auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()))
    {
        updateLookupTableRange(lut, minV, maxV);
        scalarBar->SetTitle(m_currentArrayName.toUtf8().constData());

        bool usePointLeft = true;
        bool usePointRight = true;
        findScalarArray(m_leftGrid, m_currentArrayName, usePointLeft);
        findScalarArray(m_rightGrid, m_currentArrayName, usePointRight);
        configureMapper(m_leftMapper, m_currentArrayName, usePointLeft, minV, maxV, lut);
        configureMapper(m_rightMapper, m_currentArrayName, usePointRight, minV, maxV, lut);
    }
}

void NephogramSettingWidget::updateCompare()
{
    if (!m_leftRen || !m_rightRen)
        return;

    const QString scalar = ui->scalarComboBox->currentText();
    if (scalar.isEmpty())
        return;
    m_currentArrayName = scalar;

    auto fileByStep = [&](const QString& stepText) -> QString
    {
        return m_stepFileMap.value(stepText);
    };

    const QString leftStep = ui->leftStepComboBox->currentText();
    const QString rightStep = ui->rightStepComboBox->currentText();
    if (leftStep.isEmpty() || rightStep.isEmpty())
        return;

    m_leftGrid = loadGridFromFile(fileByStep(leftStep));
    m_rightGrid = loadGridFromFile(fileByStep(rightStep));
    if (!m_leftGrid || !m_rightGrid)
    {
        qWarning() << "Failed to load compare grids:" << leftStep << rightStep;
        return;
    }

    bool usePointLeft = true;
    bool usePointRight = true;
    auto* leftArr = findScalarArray(m_leftGrid, scalar, usePointLeft);
    auto* rightArr = findScalarArray(m_rightGrid, scalar, usePointRight);
    if (!leftArr || !rightArr)
    {
        qWarning() << "Scalar not found in left/right:" << scalar;
        return;
    }

    // 统一色标范围（左右并集）
    syncScalarBarForCompare(leftArr, rightArr);

    // 绑输入
    m_leftMapper->SetInputData(m_leftGrid);
    m_rightMapper->SetInputData(m_rightGrid);

    m_leftActor->GetProperty()->SetRepresentationToSurface();
    m_rightActor->GetProperty()->SetRepresentationToSurface();

    if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
        rw->Render();
}

void NephogramSettingWidget::fillCompareCombosFromSteps()
{
    QStringList steps;
    for (int i = 0; i < ui->stepComboBox->count(); ++i)
        steps << ui->stepComboBox->itemText(i);

    ui->leftStepComboBox->blockSignals(true);
    ui->rightStepComboBox->blockSignals(true);

    ui->leftStepComboBox->clear();
    ui->rightStepComboBox->clear();

    ui->leftStepComboBox->addItems(steps);
    ui->rightStepComboBox->addItems(steps);

    if (ui->leftStepComboBox->count() > 0)
        ui->leftStepComboBox->setCurrentIndex(0);
    if (ui->rightStepComboBox->count() > 1)
        ui->rightStepComboBox->setCurrentIndex(ui->rightStepComboBox->count() - 1);

    ui->leftStepComboBox->blockSignals(false);
    ui->rightStepComboBox->blockSignals(false);
}

void NephogramSettingWidget::on_compareEnableCheckBox_toggled(bool)
{
    // 分屏对比已下线，保留槽函数以兼容旧 UI 连接
    m_compareEnabled = false;
}

void NephogramSettingWidget::on_leftStepComboBox_currentIndexChanged(int)
{
}

void NephogramSettingWidget::on_rightStepComboBox_currentIndexChanged(int)
{
}

void NephogramSettingWidget::on_splitSlider_valueChanged(int)
{
}

vtkDataArray* NephogramSettingWidget::findScalarArray(vtkUnstructuredGrid* grid,
                                                      const QString& scalarName,
                                                      bool& usePointData) const
{
    usePointData = true;
    if (!grid || scalarName.isEmpty())
        return nullptr;

    vtkDataArray* arr = nullptr;
    if (auto* pointData = grid->GetPointData())
    {
        arr = pointData->GetArray(scalarName.toUtf8().constData());
    }
    if (arr)
    {
        usePointData = true;
        return arr;
    }

    if (auto* cellData = grid->GetCellData())
    {
        arr = cellData->GetArray(scalarName.toUtf8().constData());
        if (arr)
        {
            usePointData = false;
            return arr;
        }
    }
    return nullptr;
}

void NephogramSettingWidget::updateLookupTableRange(vtkLookupTable* lut, double minV,
                                                    double maxV) const
{
    if (!lut)
        return;

    if (std::abs(maxV - minV) < 1e-12)
    {
        maxV = minV + 1.0;
    }

    lut->SetNumberOfTableValues(256);
    lut->SetTableRange(minV, maxV);
    lut->SetHueRange(2.0 / 3.0, 0.0); // 常规云图：蓝 -> 青 -> 绿 -> 黄 -> 红
    lut->SetSaturationRange(1.0, 1.0);
    lut->SetValueRange(1.0, 1.0);
    lut->SetRampToLinear();
    lut->Build();
}

void NephogramSettingWidget::configureMapper(vtkDataSetMapper* mapper, const QString& scalarName,
                                             bool usePointData, double minV, double maxV,
                                             vtkLookupTable* lut) const
{
    if (!mapper)
        return;
    mapper->SetLookupTable(lut);
    mapper->SetScalarRange(minV, maxV);
    mapper->SelectColorArray(scalarName.toUtf8().constData());
    mapper->SetUseLookupTableScalarRange(false);
    mapper->ScalarVisibilityOn();
    if (usePointData)
        mapper->SetScalarModeToUsePointFieldData();
    else
        mapper->SetScalarModeToUseCellFieldData();
    mapper->InterpolateScalarsBeforeMappingOn();
}

void NephogramSettingWidget::on_pushButton_clicked()
{
    // 保留槽函数，暂无额外逻辑
}

QJsonObject NephogramSettingWidget::saveJson()
{
    QJsonObject obj;
    obj["scalar"] = 1;
    obj["range_min"] = 2;
    obj["range_max"] = 3;
    return obj;
}

void NephogramSettingWidget::loadJson(const QJsonObject& obj)
{
    qInfo() << "[Base_PostProcessing] Load nephogram settings";
    qDebug() << obj.value("scalar").toString();
    qDebug() << obj.value("range_min").toDouble();
    qDebug() << obj.value("range_max").toDouble();
}



void NephogramSettingWidget::readVtu(const QString& fileName)
{
    // 1) 使用 VTK 的 XML UnstructuredGrid Reader
    auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());

    // 若是 polydata 也兼容
    auto readerPoly = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    bool isUGrid = true;

    QFileInfo info(fileName);
    if (info.suffix().toLower() == "vtu")
    {
        reader->Update();
        if (!reader->GetOutput())
        {
            // 尝试 PolyData
            readerPoly->SetFileName(fileName.toUtf8().constData());
            readerPoly->Update();
            isUGrid = false;
        }
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid;

    if (isUGrid && reader->GetOutput())
    {
        grid = reader->GetOutput();
    }
    else if (readerPoly->GetOutput())
    {
        // 把 polydata 包成 unstructuredGrid
        auto pd = readerPoly->GetOutput();
        auto append = vtkSmartPointer<vtkAppendFilter>::New();
        append->AddInputData(pd);
        append->Update();
        grid = append->GetOutput();
    }
    else
    {
        qWarning() << "Failed to read VTU file:" << fileName;
        return;
    }

    m_grid = grid;
    m_loadedFileName = fileName;

    // 2) 填充 scalarComboBox
    QStringList scalarNames;
    if (auto* pd = grid->GetPointData())
    {
        const int n = pd->GetNumberOfArrays();
        for (int i = 0; i < n; ++i)
        {
            const char* name = pd->GetArrayName(i);
            if (name && *name)
                scalarNames << QString::fromUtf8(name);
        }
    }

    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();
    ui->scalarComboBox->addItems(scalarNames);
    if (ui->scalarComboBox->count() > 0)
        ui->scalarComboBox->setCurrentIndex(0);
    ui->scalarComboBox->blockSignals(false);

    updateNephogram(ui->scalarComboBox->currentText());
}

void NephogramSettingWidget::clearContourOverlay()
{
    if (!m_visualService)
        return;
    if (auto* ren = m_visualService->renderer())
    {
        ren->RemoveActor(m_contourActor);
        ren->RemoveActor2D(m_contourLabelActor);
    }
}

void NephogramSettingWidget::updateContourOverlay(const QString& scalarName, bool usePointData, double minV, double maxV)
{
    if (!m_visualService || !m_grid || !m_actorAttached)
        return;

    auto* ren = m_visualService->renderer();
    if (!ren)
        return;

    clearContourOverlay();

    if (!ui->contourEnableCheckBox->isChecked())
        return;

    auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(m_grid);

    auto contour = vtkSmartPointer<vtkContourFilter>::New();
    contour->SetInputConnection(surfaceFilter->GetOutputPort());
    contour->SetInputArrayToProcess(0, 0, 0,
                                    usePointData ? vtkDataObject::FIELD_ASSOCIATION_POINTS
                                                 : vtkDataObject::FIELD_ASSOCIATION_CELLS,
                                    scalarName.toUtf8().constData());
    const int levels = std::max(2, ui->contourLevelsSpinBox->value());
    contour->GenerateValues(levels, minV, maxV);
    contour->Update();

    auto* contourOutput = contour->GetOutput();
    if (!contourOutput || contourOutput->GetNumberOfCells() <= 0 ||
        contourOutput->GetNumberOfPoints() <= 0)
        return;

    auto contourMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    contourMapper->SetInputConnection(contour->GetOutputPort());
    contourMapper->ScalarVisibilityOff();
    m_contourActor->SetMapper(contourMapper);
    m_contourActor->GetProperty()->SetColor(0.0, 0.0, 0.0);
    m_contourActor->GetProperty()->SetLineWidth(1.6);
    ren->AddActor(m_contourActor);

    if (!ui->contourLabelCheckBox->isChecked())
        return;

    vtkDataArray* contourScalars =
        contourOutput->GetPointData() ? contourOutput->GetPointData()->GetScalars() : nullptr;
    if (!contourScalars)
        return;

    auto labelPoly = vtkSmartPointer<vtkPolyData>::New();
    auto labelPts = vtkSmartPointer<vtkPoints>::New();
    auto labelVals = vtkSmartPointer<vtkDoubleArray>::New();
    labelVals->SetName("ContourLabels");

    const int levelsToShow = std::max(2, ui->contourLevelsSpinBox->value());
    const double span = std::max(std::abs(maxV - minV), 1e-12);
    const double tol = span * 1e-6;
    QVector<double> addedLevels;
    addedLevels.reserve(levelsToShow);

    const vtkIdType cellCount = contourOutput->GetNumberOfCells();
    for (vtkIdType ci = 0; ci < cellCount; ++ci)
    {
        auto* cell = contourOutput->GetCell(ci);
        if (!cell)
            continue;

        auto* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() <= 0)
            continue;

        const vtkIdType midId = ids->GetId(ids->GetNumberOfIds() / 2);
        const double v = contourScalars->GetComponent(midId, 0);

        bool exists = false;
        for (double lv : addedLevels)
        {
            if (std::abs(lv - v) <= tol)
            {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;

        double p[3];
        contourOutput->GetPoint(midId, p);
        labelPts->InsertNextPoint(p);
        labelVals->InsertNextValue(v);
        addedLevels.push_back(v);
        if (addedLevels.size() >= levelsToShow)
            break;
    }

    if (labelPts->GetNumberOfPoints() <= 0)
        return;

    labelPoly->SetPoints(labelPts);
    labelPoly->GetPointData()->SetScalars(labelVals);

    auto visiblePoints = vtkSmartPointer<vtkSelectVisiblePoints>::New();
    visiblePoints->SetRenderer(ren);
    visiblePoints->SetInputData(labelPoly);
    visiblePoints->Update();

    auto labelMapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
    labelMapper->SetInputConnection(visiblePoints->GetOutputPort());
    labelMapper->SetLabelModeToLabelScalars();
    labelMapper->GetLabelTextProperty()->SetFontSize(12);
    labelMapper->GetLabelTextProperty()->SetColor(0.0, 0.0, 0.0);

    m_contourLabelActor->SetMapper(labelMapper);
    ren->AddActor2D(m_contourLabelActor);

}

