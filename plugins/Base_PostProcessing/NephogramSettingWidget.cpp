#include "NephogramSettingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkLookupTable.h>
#include <vtkMath.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTecplotReader.h>
#include <vtkTetra.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include "PostVisualUtils.h"
#include "ui_NephogramSettingWidget.h"

namespace
{
const QStringList kResultDirectories = {"SEEPCASE01"};
const QStringList kPreferredResultFiles = {"TEC_HP.DAT", "TEC_VELO.DAT"};
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

    vtkDataArray* arr = m_grid->GetPointData()->GetArray(m_currentArrayName.toUtf8().constData());
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
        applyPostLookupTableStyle(lut);
        lut->SetTableRange(range);
        lut->Build();
        applyPostScalarBarStyle(scalarBar, m_currentArrayName);

        m_mapper->SetLookupTable(lut);
        m_mapper->SetScalarRange(range);
        m_mapper->SetScalarModeToUsePointFieldData();
        m_mapper->SelectColorArray(m_currentArrayName.toUtf8().constData());
        m_mapper->SetUseLookupTableScalarRange(false);
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

    if (ui->stepComboBox->currentIndex() == -1)
    {
        m_initialized = false;
    }

    if (!m_initialized)
    {
        m_dataPath = selectedResultDirectoryPath();
        loadFileName(m_dataPath);
        ensureActorAttached();
        m_initialized = true;

        if (auto* ren = m_visualService->renderer())
            ren->ResetCamera();
        if (auto* rw = m_visualService->renderWindow())
            rw->Render();
        return;
    }

    if (m_compareEnabled)
    {
        enterCompareMode();
        updateCompare();
    }
    else
    {
        ensureActorAttached();
        syncScalarBarFromCurrent();
        m_actor->SetMapper(m_mapper);
    }

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void NephogramSettingWidget::beforeUnload()
{
    if (!m_visualService)
        return;
    if (m_compareEnabled)
    {
        exitCompareMode();
    }
    else if (m_actorAttached)
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
    for (const QString& name : kPreferredResultFiles)
    {
        const QString fullPath = directory.filePath(name);
        if (QFileInfo::exists(fullPath))
        {
            const QString stepName = QFileInfo(fullPath).completeBaseName();
            m_stepFileMap.insert(stepName, fullPath);
            ui->stepComboBox->addItem(stepName);
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

    vtkDataArray* scalarArray =
        m_grid->GetPointData()->GetArray(selectedScalarName.toUtf8().constData());
    if (!scalarArray)
    {
        qWarning() << "Scalar array not found:" << selectedScalarName;
        return;
    }

    m_currentArrayName = selectedScalarName;
    syncScalarBarFromCurrent();

    m_mapper->SetInputData(m_grid);
    applyPostMapperStyle(m_mapper);
    m_actor->SetMapper(m_mapper);
    applyPostActorStyle(m_actor);
    ensureActorAttached();

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
    ui->resultDirComboBox->addItems(kResultDirectories);
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
    const QString dirName = ui->resultDirComboBox->currentText().isEmpty()
                                ? QString("SEEPCASE01")
                                : ui->resultDirComboBox->currentText();
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
        lut->SetTableRange(minV, maxV);
        lut->Build();
        applyPostScalarBarStyle(scalarBar, m_currentArrayName);

        for (auto* m : {m_leftMapper.GetPointer(), m_rightMapper.GetPointer()})
        {
            if (!m)
                continue;
            m->SetLookupTable(lut);
            m->SetScalarRange(minV, maxV);
            m->SetScalarModeToUsePointFieldData();
            m->SelectColorArray(m_currentArrayName.toUtf8().constData());
            m->SetUseLookupTableScalarRange(false);
        }
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

    auto* leftArr = m_leftGrid->GetPointData()->GetArray(scalar.toUtf8().constData());
    auto* rightArr = m_rightGrid->GetPointData()->GetArray(scalar.toUtf8().constData());
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
    applyPostMapperStyle(m_leftMapper);
    applyPostMapperStyle(m_rightMapper);

    applyPostActorStyle(m_leftActor);
    applyPostActorStyle(m_rightActor);

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

void NephogramSettingWidget::on_compareEnableCheckBox_toggled(bool checked)
{
    if (checked == m_compareEnabled)
        return;
    m_compareEnabled = checked;

    if (m_compareEnabled)
    {
        enterCompareMode();
        updateCompare();
    }
    else
    {
        exitCompareMode();

        // 回到单图
        syncScalarBarFromCurrent();
        m_actor->SetMapper(m_mapper);
        ensureActorAttached();
        if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
            rw->Render();
    }
}

void NephogramSettingWidget::on_leftStepComboBox_currentIndexChanged(int)
{
    if (m_compareEnabled)
        updateCompare();
}

void NephogramSettingWidget::on_rightStepComboBox_currentIndexChanged(int)
{
    if (m_compareEnabled)
        updateCompare();
}

void NephogramSettingWidget::on_splitSlider_valueChanged(int value)
{
    if (!m_compareEnabled)
        return;
    m_splitRatio = std::min(std::max(value / 100.0, 0.05), 0.95);

    updateViewports(m_splitRatio);
    if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
        rw->Render();
}

void NephogramSettingWidget::test()
{
    qDebug() << "test";
}

void NephogramSettingWidget::on_pushButton_clicked()
{
    test();
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
    qDebug() << "load";
    qDebug() << obj.value("scalar").toString();
    qDebug() << obj.value("range_min").toDouble();
    qDebug() << obj.value("range_max").toDouble();
}

#include <vtkPoints.h>
#include <vtkPolyDataNormals.h>
#include <vtkSmartPointer.h>
#include <vtkTetra.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// 很简单的 trim
static inline void trim(std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos)
    {
        s.clear();
        return;
    }
    s = s.substr(b, e - b + 1);
}

// Nastran 小字段：每 8 字符一个 field
static inline void splitNas8(const std::string& line, std::vector<std::string>& fields)
{
    fields.clear();
    const std::size_t n = line.size();
    for (std::size_t i = 0; i < n; i += 8)
    {
        std::string f = line.substr(i, std::min<std::size_t>(8, n - i));
        trim(f);
        fields.push_back(f); // 可以允许空字符串保留或在这里 if(!f.empty()) 再 push
    }
}

// 只演示 GRID + CTETRA(4 节点)
vtkSmartPointer<vtkUnstructuredGrid> LoadNasToUnstructuredGrid(const std::string& filename)
{
    auto ug = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();

    // Nastran GRID id -> VTK point id
    std::unordered_map<int, vtkIdType> gridId2VtkId;

    std::ifstream in(filename);
    if (!in)
    {
        std::cerr << "Cannot open nas file: " << filename << std::endl;
        return nullptr;
    }

    std::string line;
    std::vector<std::string> fields;

    while (std::getline(in, line))
    {
        // 跳过空行和注释
        trim(line);
        if (line.empty())
            continue;
        if (line[0] == '$' || line[0] == '#')
            continue;

        // 切成 8 字段
        splitNas8(line, fields);
        if (fields.empty())
            continue;

        const std::string& card = fields[0];

        // ---- GRID 节点 ----
        if (card == "GRID")
        {
            // F1=GRID, F2=ID, F3=CP, F4=X, F5=Y, F6=Z
            if (fields.size() < 6)
            {
                std::cerr << "Invalid GRID line: " << line << std::endl;
                continue;
            }

            int id = std::stoi(fields[1]);
            // int cp  = std::stoi(fields[2]); // 暂时用不上
            double x = std::stod(fields[3]);
            double y = std::stod(fields[4]);
            double z = std::stod(fields[5]);

            vtkIdType pid = points->InsertNextPoint(x, y, z);
            gridId2VtkId[id] = pid;
        }
        // ---- CTETRA 四节点 ----
        else if (card == "CTETRA" || card == "CTETRA4")
        {
            // F1=CTETRA, F2=EID, F3=PID, F4=G1, F5=G2, F6=G3, F7=G4,
            // [F8..F10=中间节点]
            if (fields.size() < 7)
            {
                std::cerr << "Invalid CTETRA line: " << line << std::endl;
                continue;
            }

            int eid = std::stoi(fields[1]);
            int pid = std::stoi(fields[2]);
            (void)eid;
            (void)pid; // 先不用

            int nasIds[4];
            for (int i = 0; i < 4; ++i)
            {
                nasIds[i] = std::stoi(fields[3 + i]); // F4..F7
            }

            auto tet = vtkSmartPointer<vtkTetra>::New();
            bool ok = true;
            for (int i = 0; i < 4; ++i)
            {
                auto it = gridId2VtkId.find(nasIds[i]);
                if (it == gridId2VtkId.end())
                {
                    std::cerr << "CTETRA uses unknown GRID id: " << nasIds[i] << std::endl;
                    ok = false;
                    break;
                }
                tet->GetPointIds()->SetId(i, it->second);
            }
            if (!ok)
                continue;

            ug->InsertNextCell(tet->GetCellType(), tet->GetPointIds());
        }
        else
        {
            // 其他卡片不管，比如 PSOLID、MAT1 之类，如果有的话
        }
    }

    ug->SetPoints(points);
    return ug;
}

void NephogramSettingWidget::on_pushButton_4_clicked()
{
    std::string filename = "C:/project/cae_base_test/base_cae/build/tempGrid/mesh.nas";

    // 1. 读取 Nastran 网格（体单元）
    auto ug = LoadNasToUnstructuredGrid(filename);
    if (!ug || ug->GetNumberOfCells() == 0)
    {
        std::cerr << "Failed to load nas file or no cells: " << filename << std::endl;
        return;
    }

    std::cout << "UnstructuredGrid: points=" << ug->GetNumberOfPoints()
              << " cells=" << ug->GetNumberOfCells() << std::endl;

    // 2. 提取外表面
    auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(ug);
    surfaceFilter->Update();
    auto surface = surfaceFilter->GetOutput();

    std::cout << "Surface polys: " << surface->GetNumberOfCells() << std::endl;

    // 2.5 用法向 + Splitting 把锐边打断（按几何面切开）
    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(surface);
    normals->SplittingOn();         // 关键：沿锐边打断拓扑
    normals->SetFeatureAngle(45.0); // 锐边阈值，90°的棱肯定会被打断
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();
    auto facetedSurface = normals->GetOutput();

    // 3. 连通性分区：每个连通区域 = 一个“几何边界面”
    auto conn = vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
    conn->SetInputData(facetedSurface);
    conn->SetExtractionModeToAllRegions();
    conn->ColorRegionsOn(); // 生成 RegionId 标量
    conn->Update();

    auto coloredSurface = conn->GetOutput();
    int numRegions = conn->GetNumberOfExtractedRegions();
    std::cout << "Extracted boundary regions: " << numRegions << std::endl;

    // 4. 取出 RegionId 数组（CellData / PointData 里找）
    vtkDataArray* regionArray = coloredSurface->GetCellData()->GetArray("RegionId");
    bool useCellData = true;
    if (!regionArray)
    {
        regionArray = coloredSurface->GetPointData()->GetArray("RegionId");
        useCellData = false;
    }

    if (!regionArray)
    {
        qWarning() << "No RegionId array found on connectivity output!";
        return;
    }

    double range[2] = {0.0, 1.0};
    regionArray->GetRange(range); // 理论上就是 [0, numRegions-1]

    if (numRegions <= 0)
    {
        numRegions = static_cast<int>(range[1] - range[0] + 0.5);
        if (numRegions <= 0)
            numRegions = 1;
    }

    // 5. 建立 LookupTable，为每个 RegionId 分配不同颜色
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(std::max(numRegions, 1));
    lut->Build();

    for (int i = 0; i < numRegions; ++i)
    {
        // [0,1) 均匀取色相
        double hue = static_cast<double>(i) / std::max(numRegions, 1);
        double hsv[3] = {hue, 0.8, 0.9}; // H, S, V
        double rgb[3] = {0.0, 0.0, 0.0};

        vtkMath::HSVToRGB(hsv, rgb);
        lut->SetTableValue(i, rgb[0], rgb[1], rgb[2], 1.0);
    }

    // 6. Mapper：使用 RegionId 作为 scalar 着色
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(coloredSurface);

    if (useCellData)
    {
        coloredSurface->GetCellData()->SetScalars(regionArray);
        mapper->SetScalarModeToUseCellData();
    }
    else
    {
        coloredSurface->GetPointData()->SetScalars(regionArray);
        mapper->SetScalarModeToUsePointData();
    }

    mapper->SetLookupTable(lut);
    mapper->SetScalarRange(range);
    mapper->ScalarVisibilityOn();

    // 7. Actor & 加到你的后处理窗口
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->EdgeVisibilityOn();
    actor->GetProperty()->SetEdgeColor(0, 0, 0);

    addActorByService(m_visualService, actor); // 确保内部会 Render / ResetCamera
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
