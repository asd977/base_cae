#include "NephogramSettingWidget.h"
#include "ui_NephogramSettingWidget.h"

#include <QDebug>
#include <QDir>
#include <QRegularExpression>
#include <QMap>
#include <algorithm>

#include <vtkAppendFilter.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCamera.h>
#include <set>

#include <PublicVariables.h>
#include <PublicConstants.h>
#include <PostProcessingWidget.h>

#include <vtkSmartPointer.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkProperty.h>
#include <vtkCellPicker.h>
#include <vtkCellData.h>
#include <vtkStringArray.h>
#include <QFileDialog>

NephogramSettingWidget::NephogramSettingWidget(PostProcessingWidget *postProcessingWidget, QWidget *parent)
    : BasePage(PublicConstants::PAGE_POSTPROCESS_NEPHOGRAM),
      ui(new Ui::NephogramSettingWidget)
{
    ui->setupUi(this);
    m_postProcessingWidget = postProcessingWidget;

    // 单图对象
    m_actor  = vtkSmartPointer<vtkActor>::New();
    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();


    connect(ui->stepComboBox,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NephogramSettingWidget::on_stepComboBox_currentIndexChanged);
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NephogramSettingWidget::on_scalarComboBox_currentIndexChanged);


    connect(ui->compareEnableCheckBox, &QCheckBox::toggled,
            this, &NephogramSettingWidget::on_compareEnableCheckBox_toggled);
    connect(ui->leftStepComboBox,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NephogramSettingWidget::on_leftStepComboBox_currentIndexChanged);
    connect(ui->rightStepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NephogramSettingWidget::on_rightStepComboBox_currentIndexChanged);
    connect(ui->splitSlider, &QSlider::valueChanged,
            this, &NephogramSettingWidget::on_splitSlider_valueChanged);

    // 分屏滑块范围
    if (ui->splitSlider->minimum() != 5 || ui->splitSlider->maximum() != 95)
    {
        ui->splitSlider->setMinimum(5);
        ui->splitSlider->setMaximum(95);
        ui->splitSlider->setValue(50);
    }
}

NephogramSettingWidget::~NephogramSettingWidget()
{
    delete ui;
}

void NephogramSettingWidget::ensureActorAttached()
{
    if (!m_actorAttached)
    {
        m_postProcessingWidget->addActor(m_actor);
        m_actorAttached = true;
    }
}

void NephogramSettingWidget::syncScalarBarFromCurrent()
{
    if (!m_grid || m_currentArrayName.isEmpty()) return;

    vtkDataArray* arr = m_grid->GetPointData()->GetArray(m_currentArrayName.toUtf8().constData());
    if (!arr) return;

    double range[2];
    arr->GetRange(range);

    vtkScalarBarActor* scalarBar = m_postProcessingWidget->getScalarBarActor();
    if (auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable())) {
        lut->SetTableRange(range);
        lut->Build();
        scalarBar->SetTitle(m_currentArrayName.toUtf8().constData());

        m_mapper->SetLookupTable(lut);
        m_mapper->SetScalarRange(range);
        m_mapper->SetScalarModeToUsePointFieldData();
        m_mapper->SelectColorArray(m_currentArrayName.toUtf8().constData());
        m_mapper->SetUseLookupTableScalarRange(false);
    }
}

void NephogramSettingWidget::beforeLoad()
{
    if(ui->stepComboBox->currentIndex() == -1)
    {
        m_initialized = false;
    }
    if (!m_initialized)
    {
        const QString workPath = PublicVariables::instance().get(PublicConstants::VALUE_PROJECT_DIR).toString();

        m_dataPath = workPath + "/data";
        loadFileName(m_dataPath);
        fillCompareCombosFromSteps();
        ensureActorAttached();
        m_initialized = true;

        m_postProcessingWidget->getRender()->ResetCamera();
        m_postProcessingWidget->getRenderWindow()->Render();
        return;
    }

    if (m_compareEnabled)
    {
        enterCompareMode();
        updateCompare();
    } else {
        ensureActorAttached();
        syncScalarBarFromCurrent();
        m_actor->SetMapper(m_mapper);
    }
    m_postProcessingWidget->getRenderWindow()->Render();

}

void NephogramSettingWidget::beforeUnload()
{
    if (m_compareEnabled) {
        exitCompareMode();
    } else if (m_actorAttached) {
        m_postProcessingWidget->removeActor(m_actor);
        m_actorAttached = false;
    }
    m_postProcessingWidget->getRenderWindow()->Render();
}

void NephogramSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
    if (index < 0) return;
    const QString stepText = ui->stepComboBox->itemText(index);
    QDir dir(m_dataPath);
    const QString fullName = dir.filePath(QString("stress_strain%1.txt").arg(stepText));
    readTecplot(fullName);
}

void NephogramSettingWidget::loadFileName(QString dataPath)
{
    ui->stepComboBox->blockSignals(true);
    ui->stepComboBox->clear();

    QMap<int, QVector<int>> numbers;
    QDir directory(dataPath);
    if (!directory.exists()) {
        qWarning() << "Directory not found:" << dataPath;
    }

    const QString baseName = "stress_strain";
    const QString extension = ".txt";
    const QRegularExpression regex(baseName + "(\\d+)" "-" "(\\d+)" + extension);

    const QStringList files = directory.entryList(QDir::Files);
    for (const QString &filename : files) {
        const auto match = regex.match(filename);
        if (match.hasMatch()) {
            bool ok1=false, ok2=false;
            const int g = match.captured(1).toInt(&ok1);
            const int o = match.captured(2).toInt(&ok2);
            if (ok1 && ok2) numbers[g].append(o);
        }
    }

    for (auto it = numbers.begin(); it != numbers.end(); ++it) {
        auto &vec = it.value();
        std::sort(vec.begin(), vec.end());
        for (int num : vec) ui->stepComboBox->addItem(QString::number(it.key()) + "-" + QString::number(num));
    }

    if (ui->stepComboBox->count() > 0)
        ui->stepComboBox->setCurrentIndex(0);

    ui->stepComboBox->blockSignals(false);

    if (ui->stepComboBox->count() > 0)
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());

    fillCompareCombosFromSteps();
}

void NephogramSettingWidget::on_scalarComboBox_currentIndexChanged(int)
{
    if (m_compareEnabled) {
        updateCompare();
    } else {
        updateNephogram(ui->scalarComboBox->currentText());
    }
}

void NephogramSettingWidget::readTecplot(const QString &fileName)
{
    vtkSmartPointer<vtkTecplotReader> reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());
    reader->Update();

    vtkMultiBlockDataSet* multi = reader->GetOutput();

    vtkSmartPointer<vtkAppendFilter> appender = vtkSmartPointer<vtkAppendFilter>::New();
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i) {
        if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i))) {
            appender->AddInputData(block);
        }
    }
    appender->Update();

    m_grid = appender->GetOutput();
    m_loadedFileName = fileName;

    if (!m_grid) {
        qWarning() << "Failed to combine blocks for" << fileName;
        return;
    }

    QStringList scalarNames;
    if (auto* pd = m_grid->GetPointData()) {
        const int n = pd->GetNumberOfArrays();
        scalarNames.reserve(n);
        for (int i = 0; i < n; ++i) {
            const char* name = pd->GetArrayName(i);
            if (name && *name) scalarNames << QString::fromUtf8(name);
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
    if (!m_grid) {
        qWarning() << "No grid to visualize.";
        return;
    }

    vtkDataArray* scalarArray = m_grid->GetPointData()->GetArray(selectedScalarName.toUtf8().constData());
    if (!scalarArray) {
        qWarning() << "Scalar array not found:" << selectedScalarName;
        return;
    }

    m_currentArrayName = selectedScalarName;
    syncScalarBarFromCurrent();

    m_mapper->SetInputData(m_grid);
    m_actor->SetMapper(m_mapper);
    m_actor->GetProperty()->SetRepresentationToSurface();
    ensureActorAttached();

    m_postProcessingWidget->getRenderWindow()->Render();
}

vtkSmartPointer<vtkUnstructuredGrid> NephogramSettingWidget::loadGridFromTecplot(const QString &fileName)
{
    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());
    reader->Update();

    auto multi = reader->GetOutput();

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i) {
        if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i))) {
            appender->AddInputData(block);
        }
    }
    appender->Update();
    return appender->GetOutput();
}

void NephogramSettingWidget::enterCompareMode()
{
    if (m_leftRen && m_rightRen) return;

    auto* rw = m_postProcessingWidget->getRenderWindow();
    if (!rw) return;

    // 1) 获取基础 renderer（持有 ScalarBar），并将其作为顶层 Overlay
    vtkRenderer* baseRen = rw->GetRenderers()->GetFirstRenderer();
    if (!baseRen) return;
    m_baseRenderer = baseRen;

    // 重要：两层渲染。底层画左右内容，顶层画色标等UI
    rw->SetNumberOfLayers(std::max(rw->GetNumberOfLayers(), 2));

    // 顶层不要清背景，否则会把底层内容盖住
    m_baseRenderer->SetLayer(1);
    m_baseRenderer->SetErase(false); // 仅绘制其 props（ScalarBar 等）

    // 2) 移除单图 actor，避免重叠
    if (m_actorAttached) {
        m_postProcessingWidget->removeActor(m_actor);
        m_actorAttached = false;
    }

    // 3) 创建左右 renderer，并共享相机
    m_leftRen  = vtkSmartPointer<vtkRenderer>::New();
    m_rightRen = vtkSmartPointer<vtkRenderer>::New();

    m_leftRen->SetActiveCamera(m_baseRenderer->GetActiveCamera());
    m_rightRen->SetActiveCamera(m_baseRenderer->GetActiveCamera());

    double bg[3]; m_baseRenderer->GetBackground(bg);
    m_leftRen->SetBackground(bg);
    m_rightRen->SetBackground(bg);

    // 底层
    m_leftRen->SetLayer(0);
    m_rightRen->SetLayer(0);

    rw->AddRenderer(m_leftRen);
    rw->AddRenderer(m_rightRen);

    // 4) 左右 actor/mapper
    m_leftActor  = vtkSmartPointer<vtkActor>::New();
    m_rightActor = vtkSmartPointer<vtkActor>::New();
    m_leftMapper  = vtkSmartPointer<vtkDataSetMapper>::New();
    m_rightMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    m_leftActor->SetMapper(m_leftMapper);
    m_rightActor->SetMapper(m_rightMapper);

    m_leftRen->AddActor(m_leftActor);
    m_rightRen->AddActor(m_rightActor);

    updateViewports(m_splitRatio);
}

void NephogramSettingWidget::exitCompareMode()
{
    auto* rw = m_postProcessingWidget->getRenderWindow();

    // 先恢复基础 renderer 为普通层，恢复清屏
    if (m_baseRenderer) {
        m_baseRenderer->SetLayer(0);
        m_baseRenderer->SetErase(true);
    }

    if (rw) {
        if (m_leftRen)  rw->RemoveRenderer(m_leftRen);
        if (m_rightRen) rw->RemoveRenderer(m_rightRen);
        // 可选：恢复为单层
//        rw->SetNumberOfLayers(1);
    }

    m_leftRen  = nullptr;
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
    if (m_leftRen)  m_leftRen->SetViewport(0.0, 0.0, ratio, 1.0);
    if (m_rightRen) m_rightRen->SetViewport(ratio, 0.0, 1.0, 1.0);
}

void NephogramSettingWidget::syncScalarBarForCompare(vtkDataArray* leftArr, vtkDataArray* rightArr)
{
    if (!leftArr || !rightArr) return;

    double rL[2], rR[2];
    leftArr->GetRange(rL);
    rightArr->GetRange(rR);
    const double minV = std::min(rL[0], rR[0]);
    const double maxV = std::max(rL[1], rR[1]);

    vtkScalarBarActor* scalarBar = m_postProcessingWidget->getScalarBarActor();
    if (auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable())) {
        lut->SetTableRange(minV, maxV);
        lut->Build();
        scalarBar->SetTitle(m_currentArrayName.toUtf8().constData());

        for (auto* m : { m_leftMapper.GetPointer(), m_rightMapper.GetPointer() }) {
            if (!m) continue;
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
    if (!m_leftRen || !m_rightRen) return;

    const QString scalar = ui->scalarComboBox->currentText();
    if (scalar.isEmpty()) return;
    m_currentArrayName = scalar;

    auto makeFullPath = [&](const QString& stepText)->QString{
        QDir dir(m_dataPath);
        return dir.filePath(QString("stress_strain%1.txt").arg(stepText));
    };

    const QString leftStep  = ui->leftStepComboBox->currentText();
    const QString rightStep = ui->rightStepComboBox->currentText();
    if (leftStep.isEmpty() || rightStep.isEmpty()) return;

    m_leftGrid  = loadGridFromTecplot(makeFullPath(leftStep));
    m_rightGrid = loadGridFromTecplot(makeFullPath(rightStep));
    if (!m_leftGrid || !m_rightGrid) return;

    auto* leftArr  = m_leftGrid->GetPointData()->GetArray(scalar.toUtf8().constData());
    auto* rightArr = m_rightGrid->GetPointData()->GetArray(scalar.toUtf8().constData());
    if (!leftArr || !rightArr) {
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

    m_postProcessingWidget->getRenderWindow()->Render();
}

void NephogramSettingWidget::fillCompareCombosFromSteps()
{
    QStringList steps;
    for (int i=0; i<ui->stepComboBox->count(); ++i)
        steps << ui->stepComboBox->itemText(i);

    ui->leftStepComboBox->blockSignals(true);
    ui->rightStepComboBox->blockSignals(true);

    ui->leftStepComboBox->clear();
    ui->rightStepComboBox->clear();

    ui->leftStepComboBox->addItems(steps);
    ui->rightStepComboBox->addItems(steps);

    if (ui->leftStepComboBox->count()>0)  ui->leftStepComboBox->setCurrentIndex(0);
    if (ui->rightStepComboBox->count()>1) ui->rightStepComboBox->setCurrentIndex(ui->rightStepComboBox->count()-1);

    ui->leftStepComboBox->blockSignals(false);
    ui->rightStepComboBox->blockSignals(false);
}

void NephogramSettingWidget::on_compareEnableCheckBox_toggled(bool checked)
{
    if (checked == m_compareEnabled) return;
    m_compareEnabled = checked;

    if (m_compareEnabled) {
        enterCompareMode();
        updateCompare();
    } else {
        exitCompareMode();

        // 回到单图
        syncScalarBarFromCurrent();
        m_actor->SetMapper(m_mapper);
        ensureActorAttached();
        m_postProcessingWidget->getRenderWindow()->Render();
    }
}

void NephogramSettingWidget::on_leftStepComboBox_currentIndexChanged(int)
{
    if (m_compareEnabled) updateCompare();
}

void NephogramSettingWidget::on_rightStepComboBox_currentIndexChanged(int)
{
    if (m_compareEnabled) updateCompare();
}

void NephogramSettingWidget::on_splitSlider_valueChanged(int value)
{
    if (!m_compareEnabled) return;
    m_splitRatio = std::min(std::max(value / 100.0, 0.05), 0.95);

    updateViewports(m_splitRatio);
    m_postProcessingWidget->getRenderWindow()->Render();
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
    qDebug()<<"load";
    qDebug()<<obj.value("scalar").toString();
    qDebug()<<obj.value("range_min").toDouble();
    qDebug()<<obj.value("range_max").toDouble();
}

