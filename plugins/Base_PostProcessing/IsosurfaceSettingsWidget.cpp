#include "IsosurfaceSettingsWidget.h"
#include "ui_IsosurfaceSettingsWidget.h"
#include "PostProcessingWidget.h"

#include <QDir>
#include <QRegularExpression>
#include <QDebug>

#include <vtkTecplotReader.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkAppendFilter.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPointData.h>
#include <vtkContourFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkDataObject.h>   // for SetInputArrayToProcess

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <algorithm>
#include <cmath>

static QVector<double> genUniformTs(int N, bool includeEnds)
{
    QVector<double> ts;
    N = std::max(1, N);
    ts.reserve(N);

    if (includeEnds) {
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

IsosurfaceSettingsWidget::IsosurfaceSettingsWidget(PostProcessingWidget* ppw,
                                                   QWidget *parent)
    : ui(new Ui::IsosurfaceSettingsWidget),
      BasePage(PublicConstants::kPagePostProcessIsosurface)
    , m_postProcessingWidget(ppw)
{
    ui->setupUi(this);

    // 两个 GroupBox 必须是 checkable，且互斥
    if (ui->singleIsoGroupBox) ui->singleIsoGroupBox->setCheckable(true);
    if (ui->multiIsoGroupBox)  ui->multiIsoGroupBox->setCheckable(true);
    if (ui->singleIsoGroupBox && ui->multiIsoGroupBox) {
        ui->singleIsoGroupBox->blockSignals(true);
        ui->multiIsoGroupBox->blockSignals(true);
        ui->singleIsoGroupBox->setChecked(true);
        ui->multiIsoGroupBox->setChecked(false);
        ui->singleIsoGroupBox->blockSignals(false);
        ui->multiIsoGroupBox->blockSignals(false);
    }

    // UI 默认
    ui->percentCheckBox->setChecked(true);
    ui->isoSlider->setRange(0,100);
    ui->isoSlider->setValue(50);
    ui->isoSpin->setDecimals(3);
    ui->isoSpin->setSingleStep(0.001);

    if (ui->isoCountSpin) { ui->isoCountSpin->setRange(1, 999); if (ui->isoCountSpin->value() < 1) ui->isoCountSpin->setValue(5); }
    if (ui->includeEndsCheck) ui->includeEndsCheck->setChecked(false);

    // 连接信号
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IsosurfaceSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->percentCheckBox, &QCheckBox::toggled,
            this, &IsosurfaceSettingsWidget::on_percentCheckBox_toggled);
    connect(ui->isoSlider, &QSlider::valueChanged,
            this, &IsosurfaceSettingsWidget::on_isoSlider_valueChanged);
    connect(ui->isoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &IsosurfaceSettingsWidget::on_isoSpin_valueChanged);

    connect(ui->confirmPushButton, &QPushButton::clicked,
            this, &IsosurfaceSettingsWidget::on_confirmPushButton_clicked);

    connect(ui->singleIsoGroupBox, &QGroupBox::toggled,
            this, &IsosurfaceSettingsWidget::on_singleIsoGroupBox_toggled);
    connect(ui->multiIsoGroupBox,  &QGroupBox::toggled,
            this, &IsosurfaceSettingsWidget::on_multiIsoGroupBox_toggled);
}

IsosurfaceSettingsWidget::~IsosurfaceSettingsWidget()
{
    delete ui;
}

void IsosurfaceSettingsWidget::beforeLoad()
{
    if(ui->stepComboBox->currentIndex() == -1)
    {
        m_loadedOnce = false;
    }
    if (!m_loadedOnce) {
        // <project>/data
        m_dataPath = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString() + "/data";
//        readTecplotLatest();
        loadFileName(m_dataPath);
//        fillScalarCombo();
        updateOverallColoring(); // 显示整体云图
        m_loadedOnce = true;
    }

    // 切回页面：按状态把之前的内容挂回去
    reattachAccordingToState();
    renderNow();
}

void IsosurfaceSettingsWidget::beforeUnload()
{
    // 只移除，不销毁
    detachAllFromScene();
}

void IsosurfaceSettingsWidget::loadFileName(QString dataPath)
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

}

void IsosurfaceSettingsWidget::readTecplotLatest()
{
    QDir dir(m_dataPath);
    if (!dir.exists()) return;

    QRegularExpression re("stress_strain(\\d+)-(\\d+)\\.txt");
    int bestG=-1, bestO=-1;
    QString best;
    for (const auto& f : dir.entryList(QDir::Files)) {
        auto m = re.match(f);
        if (!m.hasMatch()) continue;
        int g = m.captured(1).toInt();
        int o = m.captured(2).toInt();
        if (g > bestG || (g==bestG && o>bestO)) {
            bestG=g; bestO=o; best=dir.filePath(f);
        }
    }
    if (!best.isEmpty())
        readTecplot(best);
}

void IsosurfaceSettingsWidget::readTecplot(const QString& file)
{
    qDebug()<<file;
    vtkSmartPointer<vtkTecplotReader> reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(file.toUtf8().constData());
    reader->Update();

    vtkMultiBlockDataSet* multi = reader->GetOutput();

    vtkSmartPointer<vtkAppendFilter> appender = vtkSmartPointer<vtkAppendFilter>::New();
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i) {
        if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i))) {
            appender->AddInputData(block);
        }
    }
    appender->Update();

    m_grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_grid->ShallowCopy(appender->GetOutput());
}

void IsosurfaceSettingsWidget::fillScalarCombo()
{
    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();
    if (!m_grid) return;

    auto pd = m_grid->GetPointData();
    for (int i=0;i<pd->GetNumberOfArrays();++i)
        if (const char* n = pd->GetArrayName(i))
            ui->scalarComboBox->addItem(QString::fromUtf8(n));

    if (ui->scalarComboBox->count())
        ui->scalarComboBox->setCurrentIndex(0);

    ui->scalarComboBox->blockSignals(false);
    setIsoSpinRangeFromScalar();
}

void IsosurfaceSettingsWidget::setIsoSpinRangeFromScalar()
{
    if (!m_grid) return;
    const QString name = ui->scalarComboBox->currentText();
    auto arr = m_grid->GetPointData()->GetArray(name.toUtf8().data());
    if (!arr) return;

    arr->GetRange(m_scalarRange);
    ui->isoSpin->blockSignals(true);
    ui->isoSpin->setRange(m_scalarRange[0], m_scalarRange[1]);
    ui->isoSpin->setValue(0.5*(m_scalarRange[0]+m_scalarRange[1]));
    ui->isoSpin->blockSignals(false);
}

vtkLookupTable* IsosurfaceSettingsWidget::ensureLUT()
{
    auto scalarBar = m_postProcessingWidget->getScalarBarActor();
    auto lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
    if (!lut) {
        lut = vtkSmartPointer<vtkLookupTable>::New();
        scalarBar->SetLookupTable(lut);
    }
    return lut;
}

void IsosurfaceSettingsWidget::updateOverallColoring()
{
    if (!m_grid) return;
    const QString scalar = ui->scalarComboBox->currentText();
    auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data());
    if (!arr) return;

    double range[2]; arr->GetRange(range);
    auto lut = ensureLUT();
    lut->SetTableRange(range);
    lut->Build();

    vtkSmartPointer<vtkDataSetSurfaceFilter> surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(m_grid);
    surfaceFilter->Update();

    vtkSmartPointer<vtkPolyData> polyData = surfaceFilter->GetOutput();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyData);
    mapper->SetLookupTable(lut);
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray(scalar.toUtf8().data());
    mapper->SetScalarRange(range);

    m_actorOverall = vtkSmartPointer<vtkActor>::New();
    m_actorOverall->SetMapper(mapper);
    m_actorOverall->GetProperty()->SetRepresentationToSurface();

}

void IsosurfaceSettingsWidget::renderNow()
{
    if (auto rw = m_postProcessingWidget->getRenderWindow())
        rw->Render();
}

double IsosurfaceSettingsWidget::currentSingleIsoFromUI() const
{
    if (ui->percentCheckBox->isChecked()) {
        const double t = ui->isoSlider->value()/100.0;
        return scalarMin() + t*(scalarMax()-scalarMin());
    } else {
        return ui->isoSpin->value();
    }
}

QVector<double> IsosurfaceSettingsWidget::multiIsoValuesFromUI() const
{
    const int N = ui->isoCountSpin ? std::max(1, ui->isoCountSpin->value()) : 1;
    const bool includeEnds = ui->includeEndsCheck->isChecked();
    const auto ts = genUniformTs(N, includeEnds);

    QVector<double> vals; vals.reserve(N);
    const double lo = scalarMin(), hi = scalarMax();
    for (double t : ts) vals << (lo + t*(hi-lo));
    return vals;
}

void IsosurfaceSettingsWidget::ensureSinglePipeline()
{
    if (!m_grid) return;

    const QString scalar = ui->scalarComboBox->currentText();
    // 确保 active
    if (m_grid->GetPointData()->GetArray(scalar.toUtf8().data())) {
        m_grid->GetPointData()->SetActiveScalars(scalar.toUtf8().data());
    } else {
        qWarning() << "Scalar array not found for single pipeline:" << scalar;
        return;
    }

    if (!m_contourSingle) {
        m_contourSingle = vtkSmartPointer<vtkContourFilter>::New();
        m_contourSingle->SetInputData(m_grid);
        m_contourSingle->ComputeNormalsOn();
        m_contourSingle->SetNumberOfContours(1);
    }
    // 指定输入数组（点数据、名字为 scalar）
    m_contourSingle->SetInputArrayToProcess(
        0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, scalar.toUtf8().data());

    if (!m_mapperSingle) {
        m_mapperSingle = vtkSmartPointer<vtkPolyDataMapper>::New();
        m_mapperSingle->SetInputConnection(m_contourSingle->GetOutputPort());
    }
    if (!m_actorSingle) {
        m_actorSingle = vtkSmartPointer<vtkActor>::New();
        m_actorSingle->SetMapper(m_mapperSingle);
        m_actorSingle->GetProperty()->SetRepresentationToSurface();
    }

    // LUT/着色
    auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data());
    double range[2]; arr->GetRange(range);
    auto lut = ensureLUT();
    lut->SetTableRange(range);
    lut->Build();

    m_mapperSingle->SetLookupTable(lut);
    m_mapperSingle->SetScalarModeToUsePointFieldData();
    m_mapperSingle->SelectColorArray(scalar.toUtf8().data());
    m_mapperSingle->SetScalarRange(range);
}

void IsosurfaceSettingsWidget::updateSingleIsoRealtime()
{
    if (!m_grid) return;
    ensureSinglePipeline();

    const double iso = currentSingleIsoFromUI();
    m_contourSingle->SetValue(0, iso);
    m_contourSingle->Modified();
    m_contourSingle->Update();

    // 隐藏整体 & 多等值面
    if (m_actorOverall) m_postProcessingWidget->removeActor(m_actorOverall);
    hideMultiActorsFromScene();

    // 挂单等值面
    m_postProcessingWidget->addActor(m_actorSingle);
    m_singleDisplayed = true;
    m_multiDisplayed  = false;

    renderNow();
}

void IsosurfaceSettingsWidget::buildSingleIsoResult()
{
    // 现在单等值面走实时，这个函数保留以备需要时调用
    updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::buildMultiIsoResult()
{
    if (!m_grid) return;

    const QString scalar = ui->scalarComboBox->currentText();
    auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data());
    if (!arr) { qWarning() << "Scalar array not found for multi:" << scalar; return; }

    // 设 active（以防）
    m_grid->GetPointData()->SetActiveScalars(scalar.toUtf8().data());

    // 清空旧的
    clearMultiResults();

    // LUT
    double range[2]; arr->GetRange(range);
    auto lut = ensureLUT();
    lut->SetTableRange(range);
    lut->Build();

    // 值列表
    const auto values = multiIsoValuesFromUI();

    for (double v : values) {
        auto contour = vtkSmartPointer<vtkContourFilter>::New();
        contour->SetInputData(m_grid);
        contour->SetNumberOfContours(1);
        contour->SetValue(0, v);
        contour->ComputeNormalsOn();
        // 指定输入数组
        contour->SetInputArrayToProcess(
            0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, scalar.toUtf8().data());
        contour->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(contour->GetOutputPort());
        mapper->SetLookupTable(lut);
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray(scalar.toUtf8().data());
        mapper->SetScalarRange(range);

        auto actor  = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetRepresentationToSurface();

        m_postProcessingWidget->addActor(actor);

        m_multiContours << contour;
        m_multiMappers  << mapper;
        m_multiActors   << actor;
    }

    // 多面模式：隐藏整体、隐藏单等值面
    if (m_actorOverall) m_postProcessingWidget->removeActor(m_actorOverall);
    if (m_actorSingle)  m_postProcessingWidget->removeActor(m_actorSingle);
    m_singleDisplayed = false;
    m_multiDisplayed  = true;

    renderNow();
}

void IsosurfaceSettingsWidget::detachAllFromScene()
{
    if (m_actorOverall) m_postProcessingWidget->removeActor(m_actorOverall);
    if (m_actorSingle)  m_postProcessingWidget->removeActor(m_actorSingle);
    hideMultiActorsFromScene();
}

void IsosurfaceSettingsWidget::reattachAccordingToState()
{
    // 清空当前显示
    detachAllFromScene();

    const bool singleMode = ui->singleIsoGroupBox && ui->singleIsoGroupBox->isChecked();
    const bool multiMode  = ui->multiIsoGroupBox  && ui->multiIsoGroupBox->isChecked();

    if (m_multiDisplayed && multiMode && !m_multiActors.isEmpty()) {
        for (auto& a : m_multiActors) if (a) m_postProcessingWidget->addActor(a);
        return;
    }

    if (singleMode) {
        // 单等值面是实时显示：若已有 actor 直接挂回；否则先挂整体
        if (m_actorSingle) {
            m_postProcessingWidget->addActor(m_actorSingle);
            m_singleDisplayed = true;
            return;
        }
        if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);
        return;
    }

    // 其它情况：默认整体
    if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);
}

void IsosurfaceSettingsWidget::hideMultiActorsFromScene()
{
    for (auto& a : m_multiActors) if (a) m_postProcessingWidget->removeActor(a);
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
    if (!m_grid) return;

    // 设 ActiveScalars，并更新范围
    const QString scalar = ui->scalarComboBox->currentText();
    if (auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data())) {
        m_grid->GetPointData()->SetActiveScalars(scalar.toUtf8().data());
        arr->GetRange(m_scalarRange);
    } else {
        qWarning() << "Scalar array not found:" << scalar;
        return;
    }
    if (m_actorOverall) m_postProcessingWidget->removeActor(m_actorOverall);
    m_actorOverall = nullptr;
    setIsoSpinRangeFromScalar();
    updateOverallColoring();

    const bool singleMode = ui->singleIsoGroupBox->isChecked();
    const bool multiMode  = ui->multiIsoGroupBox->isChecked();

    if (singleMode)
    {
        updateSingleIsoRealtime();
    }
    else if (multiMode)
    {
        hideMultiActorsFromScene();
        if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);
        renderNow();
//        buildMultiIsoResult();
    }
}

void IsosurfaceSettingsWidget::on_percentCheckBox_toggled(bool)
{
    // 单等值面 = 实时更新
    if (ui->singleIsoGroupBox && ui->singleIsoGroupBox->isChecked())
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_isoSlider_valueChanged(int)
{
    // 单等值面模式下实时
    if (ui->singleIsoGroupBox && ui->singleIsoGroupBox->isChecked())
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_isoSpin_valueChanged(double)
{
    // 单等值面模式下实时
    if (ui->singleIsoGroupBox && ui->singleIsoGroupBox->isChecked())
        updateSingleIsoRealtime();
}

void IsosurfaceSettingsWidget::on_confirmPushButton_clicked()
{
    // 只有“多等值面模式”按钮才生效；单等值面忽略按钮
    if (ui->multiIsoGroupBox && ui->multiIsoGroupBox->isChecked()) {
        buildMultiIsoResult();
    }
}

void IsosurfaceSettingsWidget::on_singleIsoGroupBox_toggled(bool checked)
{
    if (!ui->multiIsoGroupBox) return;

    // 互斥
    ui->multiIsoGroupBox->blockSignals(true);
    ui->multiIsoGroupBox->setChecked(!checked);
    ui->multiIsoGroupBox->blockSignals(false);

    if (checked) {
        // 进入单等值面：实时显示
        updateSingleIsoRealtime();
    } else {
        // 退出单等值面：隐藏单面，显示整体
        if (m_actorSingle) m_postProcessingWidget->removeActor(m_actorSingle);
        m_singleDisplayed = false;
        if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);
        renderNow();
    }
}

void IsosurfaceSettingsWidget::on_multiIsoGroupBox_toggled(bool checked)
{
    if (!ui->singleIsoGroupBox) return;

    // 互斥
    ui->singleIsoGroupBox->blockSignals(true);
    ui->singleIsoGroupBox->setChecked(!checked);
    ui->singleIsoGroupBox->blockSignals(false);

    if (checked) {
        // 进入多等值面：暂不自动生成，显示整体；实时单面（若有）需隐藏
        if (m_actorSingle) m_postProcessingWidget->removeActor(m_actorSingle);
        m_singleDisplayed = false;

        hideMultiActorsFromScene();
        m_multiDisplayed = false;

        if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);
        renderNow();
    } else {
        // 退出多等值面：隐藏多面结果，回到单面实时或整体
        hideMultiActorsFromScene();
        m_multiDisplayed = false;

        if (ui->singleIsoGroupBox->isChecked()) {
            if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);
            updateSingleIsoRealtime();
        }
    }
}

void IsosurfaceSettingsWidget::on_stepComboBox_currentIndexChanged(int index)
{
    if (index < 0) return;

    const QString stepText = ui->stepComboBox->itemText(index).trimmed();
    QDir dir(m_dataPath);
    const QString fullName = dir.filePath(QString("stress_strain%1.txt").arg(stepText));

    if (!QFileInfo::exists(fullName)) {
        qWarning() << "[Isosurface] File not found:" << fullName;
        return;
    }

    // 读取新数据
    readTecplot(fullName);

    // 用新数据刷新 scalar 下拉与范围
    fillScalarCombo();            // 或者至少 setIsoSpinRangeFromScalar();

    // ——关键重置：把依赖旧 m_grid 的管线/actor 清掉——
    // 1) 单等值面：移除并标记为未显示（确保不会挡住整体云图）
    if (m_actorSingle) {
        m_postProcessingWidget->removeActor(m_actorSingle);
    }
    if (m_actorOverall) {
        m_postProcessingWidget->removeActor(m_actorOverall);
    }
    m_contourSingle = nullptr;
    m_mapperSingle  = nullptr;
    m_actorSingle = nullptr;
    m_actorOverall = nullptr;
    // m_actorSingle 可以复用也行，这里不置空也没问题
    m_singleDisplayed = false;

    // 2) 多等值面：完全清空，等待用户点“确认”重建
    clearMultiResults();
    m_multiDisplayed = false;

    // 根据当前模式重建/显示
    const bool singleMode = ui->singleIsoGroupBox->isChecked();
    const bool multiMode  = ui->multiIsoGroupBox->isChecked();

    // 多等值面模式下，尚未点“确认”之前显示整体云图
    updateOverallColoring();

    if (singleMode) {
        // 进入单等值面：立即用新 m_grid 实时更新
        updateSingleIsoRealtime();
        return;
    }


    if (m_actorOverall) m_postProcessingWidget->addActor(m_actorOverall);

    renderNow();
}
