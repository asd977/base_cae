#include "SliceSettingsWidget.h"
#include "ui_SliceSettingsWidget.h"
#include "PostProcessingWidget.h"

#include <QDir>
#include <QRegularExpression>
#include <QDebug>

#include <vtkTecplotReader.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkAppendFilter.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPointData.h>
#include <vtkCutter.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkPlane.h>

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkDataSetSurfaceFilter.h>

#include <algorithm>
#include <cmath>

static QVector<double> genUniformTs(int N, bool includeEnds)
{
    QVector<double> ts;
    N = std::max(1, N);
    ts.reserve(N);

    const double eps = 1e-6; // 防止 t=0 或 1 处某些单元切不到

    if (includeEnds) {
        if (N == 1) {
            ts << 0.5; // 如果你想 N=1 且勾选时生成两条端点切面，可改成 ts<<eps<<1.0-eps
        } else {
            for (int i = 0; i < N; ++i) {
                double t = double(i) / double(N - 1); // [0,1]
                if (t <= 0.0) t = eps;
                if (t >= 1.0) t = 1.0 - eps;
                ts << t;
            }
        }
    } else {
        if (N == 1) {
            ts << 0.5;
        } else {
            const double step = 1.0 / double(N + 1);
            for (int i = 0; i < N; ++i)
                ts << (i + 1) * step; // (0,1)
        }
    }
    return ts;
}

SliceSettingsWidget::SliceSettingsWidget(PostProcessingWidget* postProcessingWidget,
                                         QWidget* parent)
    : ui(new Ui::SliceSettingsWidget),
      BasePage(PublicConstants::kPagePostProcessSlice)
    , m_postProcessingWidget(postProcessingWidget)
{
    ui->setupUi(this);

    // 互斥：两组 GroupBox 必须是 checkable
    if (ui->singleSliceGroupBox) ui->singleSliceGroupBox->setCheckable(true);
    if (ui->multiSliceGroupBox)  ui->multiSliceGroupBox->setCheckable(true);

    // 默认进入单切面模式
    if (ui->singleSliceGroupBox && ui->multiSliceGroupBox)
    {
        ui->singleSliceGroupBox->blockSignals(true);
        ui->multiSliceGroupBox->blockSignals(true);
        ui->singleSliceGroupBox->setChecked(true);
        ui->multiSliceGroupBox->setChecked(false);
        ui->singleSliceGroupBox->blockSignals(false);
        ui->multiSliceGroupBox->blockSignals(false);
    }

    // 单切面预览（滑条/方向改变时）
    connect(ui->sliceSlider,  &QSlider::valueChanged,
            this,             &SliceSettingsWidget::showCutPlane);
    connect(ui->planeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &SliceSettingsWidget::on_planeComboBox_currentIndexChanged);

    // 确认按钮（不自动出结果，只有点确认才生成）
    connect(ui->confirmButton, &QPushButton::clicked,
            this,              &SliceSettingsWidget::on_confirmButton_clicked);

    // 互斥切换
    connect(ui->singleSliceGroupBox, &QGroupBox::toggled,
            this, &SliceSettingsWidget::on_singleSliceGroupBox_toggled);
    connect(ui->multiSliceGroupBox,  &QGroupBox::toggled,
            this, &SliceSettingsWidget::on_multiSliceGroupBox_toggled);

    // 标量/步号
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SliceSettingsWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->stepComboBox,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SliceSettingsWidget::on_stepComboBox_currentIndexChanged);

    // 多切面数量默认
    if (ui->sliceNumSpin) { ui->sliceNumSpin->setRange(1, 999); if (ui->sliceNumSpin->value() < 1) ui->sliceNumSpin->setValue(5); }
}

SliceSettingsWidget::~SliceSettingsWidget()
{
    delete ui;
}

void SliceSettingsWidget::beforeLoad()
{
    if (!m_loadedOnce) {
        // 初始化一次
        if (!m_plane) m_plane = vtkSmartPointer<vtkPlane>::New();

        const QString workPath =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        m_dataPath = QDir(workPath).filePath("data");
        loadFileName(m_dataPath);

        m_loadedOnce = true;
    }

    // 切回页面：根据状态挂回
    reattachAccordingToDisplayState();

    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::beforeUnload()
{
    // 只移除，不销毁
    detachAllFromScene();
    m_postProcessingWidget->getRenderWindow()->Render();
}

void SliceSettingsWidget::loadFileName(QString dataPath)
{
    ui->stepComboBox->blockSignals(true);
    ui->stepComboBox->clear();

    QMap<int, QVector<int>> groups;
    const QDir dir(dataPath);
    const QRegularExpression re("stress_strain(\\d+)-(\\d+)\\.txt");

    for (const QString& fname : dir.entryList(QDir::Files)) {
        const auto m = re.match(fname);
        if (!m.hasMatch()) continue;
        const int g = m.captured(1).toInt();
        const int o = m.captured(2).toInt();
        groups[g]  << o;
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        QVector<int> orders = it.value();
        std::sort(orders.begin(), orders.end());
        for (int o : orders)
            ui->stepComboBox->addItem(QString("%1-%2").arg(it.key()).arg(o));
    }
    ui->stepComboBox->blockSignals(false);

    if (ui->stepComboBox->count())
        on_stepComboBox_currentIndexChanged(0);
}

void SliceSettingsWidget::on_stepComboBox_currentIndexChanged(int idx)
{
    if (idx < 0) return;
    const QString step = ui->stepComboBox->itemText(idx);
    const QString file = QDir(m_dataPath).filePath(QString("stress_strain%1.txt").arg(step));

    // 读网格
    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(file.toUtf8().constData());
    reader->Update();

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    auto mb = reader->GetOutput();
    for (int i = 0; i < mb->GetNumberOfBlocks(); ++i)
        if (auto g = vtkUnstructuredGrid::SafeDownCast(mb->GetBlock(i)))
            appender->AddInputData(g);
    appender->Update();

    m_grid = appender->GetOutput();
    if (!m_grid) { qWarning() << "append failed"; return; }

    // 范围
    double b[6]; m_grid->GetBounds(b);
    m_XRange[0]=b[0]; m_XRange[1]=b[1];
    m_YRange[0]=b[2]; m_YRange[1]=b[3];
    m_ZRange[0]=b[4]; m_ZRange[1]=b[5];

    // 填标量
    QStringList scalars;
    auto pd = m_grid->GetPointData();
    for (int i=0;i<pd->GetNumberOfArrays();++i)
        if (const char* n = pd->GetArrayName(i))
            scalars << QString::fromUtf8(n);
    ui->scalarComboBox->clear();
    ui->scalarComboBox->addItems(scalars);

    // UI 不自动出结果
    ui->planeComboBox->setCurrentIndex(0);
    ui->sliceSlider->setValue(50);
    ui->sliceLineEdit->setText("0.5");

    // 切步后：默认只显示整体云图
    m_singleDisplayed = false;
    m_multiDisplayed  = false;

    // 清理结果显示
    if (m_sliceActor)      m_postProcessingWidget->removeActor(m_sliceActor);
    if (m_slicePlaneActor) m_postProcessingWidget->removeActor(m_slicePlaneActor);
    clearMultiSlicesAll();

    // 上色整体云图
    if (ui->scalarComboBox->count() > 0)
        on_scalarComboBox_currentIndexChanged(ui->scalarComboBox->currentIndex());

    reattachAccordingToDisplayState();
    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::on_scalarComboBox_currentIndexChanged(int)
{
    if (!m_grid) return;
    const QString scalar = ui->scalarComboBox->currentText();
    auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data());
    if (!arr) { qWarning() << "no scalar" << scalar; return; }

    // LUT
    double range[2]; arr->GetRange(range);
    auto lut = vtkLookupTable::SafeDownCast(
        m_postProcessingWidget->getScalarBarActor()->GetLookupTable());
    if (!lut) {
        lut = vtkSmartPointer<vtkLookupTable>::New();
        m_postProcessingWidget->getScalarBarActor()->SetLookupTable(lut);
    }
    lut->SetTableRange(range);
    lut->Build();

    // 整体云图
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

    if (!m_actor) m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(mapper);
    m_actor->GetProperty()->SetRepresentationToSurface();

    // 没有在显示结果时，才把整体云图挂上
    if (!m_singleDisplayed && !m_multiDisplayed)
        m_postProcessingWidget->addActor(m_actor);

    // 若已经在显示结果，重建结果以复用新 LUT
    if (m_singleDisplayed) {
        if (m_sliceActor) m_postProcessingWidget->removeActor(m_sliceActor);
        buildSingleSliceResult();
    } else if (m_multiDisplayed) {
        hideMultiSlicesFromScene();
        buildMultiSliceResult();
    }

    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::on_planeComboBox_currentIndexChanged(int)
{
    // 仅单切面模式预览
    if (ui->singleSliceGroupBox && ui->singleSliceGroupBox->isChecked())
        showCutPlane();
}

void SliceSettingsWidget::showCutPlane(int)
{
    if (!m_grid) return;

    // 仅单切面模式预览；多切面模式不预览
    if (!(ui->singleSliceGroupBox && ui->singleSliceGroupBox->isChecked()))
        return;

    const double t = ui->sliceSlider->value()/100.0;
    ui->sliceLineEdit->setText(QString::number(t,'f',3));
    const QString dir = ui->planeComboBox->currentText();

    double b[6]; m_grid->GetBounds(b);
    const double dx=b[1]-b[0], dy=b[3]-b[2], dz=b[5]-b[4];
    const double ex=0.525*dx,  ey=0.525*dy,  ez=0.525*dz;

    double O[3]={0,0,0}, P1[3]={0,0,0}, P2[3]={0,0,0};
    if (!m_plane) m_plane = vtkSmartPointer<vtkPlane>::New();

    if (dir=="YZ") {
        const double x=b[0]+t*dx;
        O[0]=x; O[1]=b[2]-ey; O[2]=b[4]-ez;
        P1[0]=x; P1[1]=b[3]+ey; P1[2]=b[4]-ez;
        P2[0]=x; P2[1]=b[2]-ey; P2[2]=b[5]+ez;
        m_plane->SetNormal(1,0,0);
    } else if (dir=="XZ") {
        const double y=b[2]+t*dy;
        O[0]=b[0]-ex; O[1]=y; O[2]=b[4]-ez;
        P1[0]=b[1]+ex; P1[1]=y; P1[2]=b[4]-ez;
        P2[0]=b[0]-ex; P2[1]=y; P2[2]=b[5]+ez;
        m_plane->SetNormal(0,1,0);
    } else { // XY
        const double z=b[4]+t*dz;
        O[0]=b[0]-ex; O[1]=b[2]-ey; O[2]=z;
        P1[0]=b[1]+ex; P1[1]=b[2]-ey; P1[2]=z;
        P2[0]=b[0]-ex; P2[1]=b[3]+ey; P2[2]=z;
        m_plane->SetNormal(0,0,1);
    }
    m_plane->SetOrigin(O);

    if (!m_slicePlaneSource) {
        m_slicePlaneSource = vtkSmartPointer<vtkPlaneSource>::New();
        m_slicePlaneMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        m_slicePlaneMapper->SetInputConnection(m_slicePlaneSource->GetOutputPort());
        m_slicePlaneActor  = vtkSmartPointer<vtkActor>::New();
        m_slicePlaneActor->SetMapper(m_slicePlaneMapper);
        m_slicePlaneActor->GetProperty()->SetColor(1,0,1);
        m_slicePlaneActor->GetProperty()->SetOpacity(0.25);
        m_slicePlaneActor->GetProperty()->SetLighting(false);
    }

    m_slicePlaneSource->SetOrigin(O);
    m_slicePlaneSource->SetPoint1(P1);
    m_slicePlaneSource->SetPoint2(P2);
    m_slicePlaneSource->Update();

    // 确保挂上（从多切面切回单切面时尤为关键）
    m_postProcessingWidget->addActor(m_slicePlaneActor);

    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::on_confirmButton_clicked()
{
    const bool singleMode = ui->singleSliceGroupBox && ui->singleSliceGroupBox->isChecked();
    const bool multiMode  = ui->multiSliceGroupBox  && ui->multiSliceGroupBox->isChecked();

    if (multiMode) {
        // 多切面：隐藏整体和单切面预览/结果，生成多切面结果
        if (m_actor)           m_postProcessingWidget->removeActor(m_actor);
        if (m_slicePlaneActor) m_postProcessingWidget->removeActor(m_slicePlaneActor);
        if (m_sliceActor)      m_postProcessingWidget->removeActor(m_sliceActor);

        buildMultiSliceResult();

        m_singleDisplayed = false;
        m_multiDisplayed  = true;
    } else if (singleMode) {
        // 单切面：隐藏整体和预览，生成单切面结果
        if (m_actor)           m_postProcessingWidget->removeActor(m_actor);
        if (m_slicePlaneActor) m_postProcessingWidget->removeActor(m_slicePlaneActor);

        buildSingleSliceResult();

        m_singleDisplayed = true;
        m_multiDisplayed  = false;

        // 顺手把多切面（若曾经生成过）隐藏
        hideMultiSlicesFromScene();
    } else {
        // 若两个都没勾（理论上不会），回退到整体
        m_singleDisplayed = false;
        m_multiDisplayed  = false;
        if (m_actor) m_postProcessingWidget->addActor(m_actor);
    }

    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::on_singleSliceGroupBox_toggled(bool checked)
{
    if (!ui->multiSliceGroupBox) return;

    // 互斥：改动另一组的勾选状态（避免递归）
    ui->multiSliceGroupBox->blockSignals(true);
    ui->multiSliceGroupBox->setChecked(!checked);
    ui->multiSliceGroupBox->blockSignals(false);

    if (checked) {
        // 进入单切面模式：隐藏多切面结果，显示整体并预览紫面
        hideMultiSlicesFromScene();
        m_multiDisplayed = false;

        if (m_actor) m_postProcessingWidget->addActor(m_actor);
        showCutPlane(); // 创建/更新并挂载预览
    } else {
        // 退出单切面模式：隐藏预览与单切面结果，仅显示整体
        if (m_slicePlaneActor) m_postProcessingWidget->removeActor(m_slicePlaneActor);
        if (m_sliceActor)      m_postProcessingWidget->removeActor(m_sliceActor);
        m_singleDisplayed = false;

        if (m_actor) m_postProcessingWidget->addActor(m_actor);
    }

    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::on_multiSliceGroupBox_toggled(bool checked)
{
    if (!ui->singleSliceGroupBox) return;

    // 互斥：改动另一组的勾选状态（避免递归）
    ui->singleSliceGroupBox->blockSignals(true);
    ui->singleSliceGroupBox->setChecked(!checked);
    ui->singleSliceGroupBox->blockSignals(false);

    if (checked) {
        // 进入多切面模式：隐藏单切面预览/结果，仅显示整体（不自动出结果）
        if (m_slicePlaneActor) m_postProcessingWidget->removeActor(m_slicePlaneActor);
        if (m_sliceActor)      m_postProcessingWidget->removeActor(m_sliceActor);
        m_singleDisplayed = false;

        if (m_actor) m_postProcessingWidget->addActor(m_actor);
    } else {
        // 退出多切面模式：隐藏多切面结果，仅显示整体，允许单切面预览
        hideMultiSlicesFromScene();
        m_multiDisplayed = false;

        if (m_actor) m_postProcessingWidget->addActor(m_actor);
        showCutPlane(); // 恢复预览（如果单切面组处于勾选状态）
    }

    if (auto rw = m_postProcessingWidget->getRenderWindow()) rw->Render();
}

void SliceSettingsWidget::buildSingleSliceResult()
{
    if (!m_grid) return;
    const QString scalar = ui->scalarComboBox->currentText();
    if (scalar.isEmpty()) return;

    auto lut = vtkLookupTable::SafeDownCast(
        m_postProcessingWidget->getScalarBarActor()->GetLookupTable());
    if (!lut) return;

    double range[2]{0,1};
    if (auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data()))
        arr->GetRange(range);
    lut->SetTableRange(range);
    lut->Build();

    auto cutter = vtkSmartPointer<vtkCutter>::New();
    cutter->SetCutFunction(m_plane);
    cutter->SetInputData(m_grid);
    cutter->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(cutter->GetOutputPort());
    mapper->SetLookupTable(lut);
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray(scalar.toUtf8().data());
    mapper->SetScalarRange(range);

    if (m_sliceActor)
        m_postProcessingWidget->removeActor(m_sliceActor);

    m_sliceActor = vtkSmartPointer<vtkActor>::New();
    m_sliceActor->SetMapper(mapper);
    m_sliceActor->GetProperty()->SetRepresentationToSurface();

    m_postProcessingWidget->addActor(m_sliceActor);
}

void SliceSettingsWidget::buildMultiSliceResult()
{
    if (!m_grid) return;

    clearMultiSlicesAll(); // 清空旧的多切面

    const int  N  = ui->sliceNumSpin ? std::max(1, ui->sliceNumSpin->value()) : 1;
    const bool includeEnds = (ui->includeEndsCheck ? ui->includeEndsCheck->isChecked() : false);
    const QString dir = ui->planeComboBox->currentText().isEmpty() ? "XY" : ui->planeComboBox->currentText();

    // LUT/标量
    auto lut = vtkLookupTable::SafeDownCast(
        m_postProcessingWidget->getScalarBarActor()->GetLookupTable());
    const QString scalar = ui->scalarComboBox->currentText();
    double range[2]{0,1};
    if (lut && !scalar.isEmpty()) {
        if (auto arr = m_grid->GetPointData()->GetArray(scalar.toUtf8().data()))
            arr->GetRange(range);
        lut->SetTableRange(range);
        lut->Build();
    }

    // t 分布：包含端点的话，两个端点都包含（并向内收 eps）
    const auto ts = genUniformTs(N, includeEnds);

    // 包围盒
    double b[6]; m_grid->GetBounds(b);
    const double dx=b[1]-b[0], dy=b[3]-b[2], dz=b[5]-b[4];

    for (double t : ts) {
        auto plane = vtkSmartPointer<vtkPlane>::New();
        double origin[3] = {0,0,0};
        if (dir=="YZ")      { origin[0]=b[0]+t*dx; plane->SetNormal(1,0,0); }
        else if (dir=="XZ") { origin[1]=b[2]+t*dy; plane->SetNormal(0,1,0); }
        else                { origin[2]=b[4]+t*dz; plane->SetNormal(0,0,1); }
        plane->SetOrigin(origin);

        auto cutter = vtkSmartPointer<vtkCutter>::New();
        cutter->SetCutFunction(plane);
        cutter->SetInputData(m_grid);
        cutter->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(cutter->GetOutputPort());
        if (lut && !scalar.isEmpty()) {
            mapper->SetLookupTable(lut);
            mapper->SetScalarModeToUsePointFieldData();
            mapper->SelectColorArray(scalar.toUtf8().data());
            mapper->SetScalarRange(range);
        }

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);

        m_postProcessingWidget->addActor(actor);

        m_multiPlanes  << plane;
        m_multiCutters << cutter;
        m_multiMappers << mapper;
        m_multiActors  << actor;
    }
}

void SliceSettingsWidget::hideMultiSlicesFromScene()
{
    for (auto& a : m_multiActors) {
        if (a) m_postProcessingWidget->removeActor(a);
    }
}

void SliceSettingsWidget::clearMultiSlicesAll()
{
    hideMultiSlicesFromScene();
    m_multiPlanes.clear();
    m_multiCutters.clear();
    m_multiMappers.clear();
    m_multiActors.clear();
}

void SliceSettingsWidget::reattachAccordingToDisplayState()
{
    // 全部清空当前场景上的本页 actor
    detachAllFromScene();

    const bool singleMode = ui->singleSliceGroupBox && ui->singleSliceGroupBox->isChecked();
    const bool multiMode  = ui->multiSliceGroupBox  && ui->multiSliceGroupBox->isChecked();

    if (m_multiDisplayed && multiMode && !m_multiActors.isEmpty()) {
        for (auto& a : m_multiActors) if (a) m_postProcessingWidget->addActor(a);
        return;
    }

    if (m_singleDisplayed && singleMode && m_sliceActor) {
        m_postProcessingWidget->addActor(m_sliceActor);
        return;
    }

    // 默认：整体云图
    if (m_actor) m_postProcessingWidget->addActor(m_actor);

    // 单切面模式下，如果没有结果在显示，恢复紫色预览
    if (singleMode && !m_singleDisplayed) {
        if (m_slicePlaneSource && m_slicePlaneActor) {
            m_postProcessingWidget->addActor(m_slicePlaneActor);
        } else {
            showCutPlane(); // 会创建并添加
        }
    }
}

void SliceSettingsWidget::detachAllFromScene()
{
    if (m_actor)           m_postProcessingWidget->removeActor(m_actor);
    if (m_slicePlaneActor) m_postProcessingWidget->removeActor(m_slicePlaneActor);
    if (m_sliceActor)      m_postProcessingWidget->removeActor(m_sliceActor);
    hideMultiSlicesFromScene();
}
