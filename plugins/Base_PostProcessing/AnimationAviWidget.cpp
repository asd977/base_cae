#include "AnimationAviWidget.h"

#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWindowToImageFilter.h>

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>

#include "PostVisualUtils.h"
#include "ui_AnimationAviWidget.h"
// #include <vtkAVIWriter.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkPoints.h>
#include <vtkProperty.h>

#include <algorithm>
#include <cmath>

AnimationAviWidget::AnimationAviWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::AnimationAviWidget), BasePage(PublicConstants::kPagePostProcessAnimation),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    ui->fpsSpin->setRange(1.0, 60.0);
    ui->fpsSpin->setSingleStep(1.0);
    ui->fpsSpin->setValue(10.0);
    ui->loopCheckBox->setChecked(true);
    ui->fixRangeCheckBox->setChecked(true);
    ui->interpCheckBox->setChecked(false);
    ui->interpCountSpin->setRange(0, 60);
    ui->interpCountSpin->setValue(0);

    connect(&m_timer, &QTimer::timeout, this, &AnimationAviWidget::onTick);

    connect(ui->prevButton, &QPushButton::clicked, this,
            &AnimationAviWidget::on_prevButton_clicked);
    connect(ui->nextButton, &QPushButton::clicked, this,
            &AnimationAviWidget::on_nextButton_clicked);
    connect(ui->frameSlider, &QSlider::valueChanged, this,
            &AnimationAviWidget::on_frameSlider_valueChanged);
    connect(ui->fpsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &AnimationAviWidget::on_fpsSpin_valueChanged);
    connect(ui->loopCheckBox, &QCheckBox::toggled, this,
            &AnimationAviWidget::on_loopCheckBox_toggled);

    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AnimationAviWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->fixRangeCheckBox, &QCheckBox::toggled, this,
            &AnimationAviWidget::on_fixRangeCheckBox_toggled);

    connect(ui->saveAviButton, &QPushButton::clicked, this,
            &AnimationAviWidget::on_saveAviButton_clicked);
}

AnimationAviWidget::~AnimationAviWidget()
{
    delete ui;
}

void AnimationAviWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
    if (m_actor)
    {
        removeActorByService(m_visualService, m_actor);
        addActorByService(m_visualService, m_actor);
        renderNow();
    }
}

void AnimationAviWidget::beforeUnload()
{
    stopTimer();
    if (m_actor)
        removeActorByService(m_visualService, m_actor);
}

void AnimationAviWidget::scanFrames()
{
    m_frames.clear();
    QDir dir(m_dataPath);
    QRegularExpression re(R"(stress_strain(\d+)-(\d+)\.txt)");

    for (const QString& f : dir.entryList(QDir::Files))
    {
        auto m = re.match(f);
        if (!m.hasMatch())
            continue;
        FrameItem it;
        it.group = m.captured(1).toInt();
        it.order = m.captured(2).toInt();
        it.filePath = dir.filePath(f);
        m_frames.push_back(it);
    }
    std::sort(m_frames.begin(), m_frames.end(),
              [](const FrameItem& a, const FrameItem& b)
              {
                  if (a.group != b.group)
                      return a.group < b.group;
                  return a.order < b.order;
              });
}

vtkSmartPointer<vtkUnstructuredGrid> AnimationAviWidget::loadGridFromFile(const QString& file)
{
    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(file.toUtf8().constData());
    reader->Update();

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    auto mb = reader->GetOutput();
    for (int i = 0; i < mb->GetNumberOfBlocks(); ++i)
        if (auto g = vtkUnstructuredGrid::SafeDownCast(mb->GetBlock(i)))
            appender->AddInputData(g);
    appender->Update();

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->ShallowCopy(appender->GetOutput());
    return grid;
}

bool AnimationAviWidget::loadFrame(int index)
{
    if (index < 0 || index >= m_frames.size())
        return false;
    m_grid = loadGridFromFile(m_frames[index].filePath);
    if (!m_grid)
        return false;

    m_mapper->SetInputData(m_grid);
    applyPostMapperStyle(m_mapper);

    // 应用色标
    if (ui->scalarComboBox->count() > 0)
    {
        const QString s = ui->scalarComboBox->currentText();
        double range[2]{0, 1};
        if (m_hasFixedRange)
        {
            range[0] = m_fixedRange[0];
            range[1] = m_fixedRange[1];
        }
        else if (auto arr = m_grid->GetPointData()->GetArray(s.toUtf8().data()))
        {
            arr->GetRange(range);
        }
        applyScalarAndLUT(s, range);
    }

    m_currentIndex = index;
    m_subStep = 0;

    ui->frameSlider->blockSignals(true);
    ui->frameSlider->setValue(index);
    ui->frameSlider->blockSignals(false);

    updateFrameLabel();
    renderNow();
    return true;
}

void AnimationAviWidget::fillScalarComboFromCurrentGrid()
{
    QString prev = ui->scalarComboBox->currentText();
    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();

    if (m_grid)
    {
        auto pd = m_grid->GetPointData();
        for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
            if (const char* n = pd->GetArrayName(i))
                ui->scalarComboBox->addItem(QString::fromUtf8(n));
    }

    int keep = ui->scalarComboBox->findText(prev);
    if (keep >= 0)
        ui->scalarComboBox->setCurrentIndex(keep);
    else if (ui->scalarComboBox->count() > 0)
        ui->scalarComboBox->setCurrentIndex(0);

    ui->scalarComboBox->blockSignals(false);
}

bool AnimationAviWidget::computeGlobalRange(const QString& scalar, double outRange[2])
{
    if (m_frames.isEmpty())
        return false;
    bool inited = false;
    double gmin = 0, gmax = 0;

    for (const auto& f : m_frames)
    {
        auto g = loadGridFromFile(f.filePath);
        if (!g)
            continue;
        if (auto arr = g->GetPointData()->GetArray(scalar.toUtf8().data()))
        {
            double r[2];
            arr->GetRange(r);
            if (!inited)
            {
                gmin = r[0];
                gmax = r[1];
                inited = true;
            }
            else
            {
                gmin = std::min(gmin, r[0]);
                gmax = std::max(gmax, r[1]);
            }
        }
    }
    if (!inited)
        return false;
    outRange[0] = gmin;
    outRange[1] = gmax;
    return true;
}

void AnimationAviWidget::applyScalarAndLUT(const QString& scalar, const double range[2])
{
    auto scalarBar = m_visualService->scalarBarActor();
    auto lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
    if (!lut)
    {
        lut = vtkSmartPointer<vtkLookupTable>::New();
        scalarBar->SetLookupTable(lut);
    }
    applyPostLookupTableStyle(lut);
    lut->SetTableRange(range);
    lut->Build();
    applyPostScalarBarStyle(scalarBar, scalar);

    m_mapper->SetLookupTable(lut);
    m_mapper->SetScalarModeToUsePointFieldData();
    m_mapper->SelectColorArray(scalar.toUtf8().data());
    m_mapper->SetScalarRange(range);
}

void AnimationAviWidget::buildPipelineIfNeeded()
{
    if (m_mapper)
        return;
    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(m_mapper);
    applyPostActorStyle(m_actor);
    addActorByService(m_visualService, m_actor);
}

void AnimationAviWidget::updateFrameLabel()
{
    QString base = QString("%1 / %2").arg(m_currentIndex + 1).arg(m_frames.size());
    QString fn = QFileInfo(m_frames[m_currentIndex].filePath).fileName();
    if (ui->interpCheckBox->isChecked() && ui->interpCountSpin->value() > 0)
    {
        base += QString("  (子步 %1/%2)").arg(m_subStep).arg(ui->interpCountSpin->value());
    }
    ui->frameLabel->setText(base + "  " + fn);
}

void AnimationAviWidget::renderNow()
{
    if (auto rw = m_visualService->renderWindow())
        rw->Render();
}

void AnimationAviWidget::startTimer()
{
    const double fps = ui->fpsSpin->value();
    int intervalMs = int(std::round(1000.0 / std::max(1.0, fps)));
    m_timer.start(intervalMs);
    m_isPlaying = true;
    ui->playPauseButton->setText("暂停");
}
void AnimationAviWidget::stopTimer()
{
    m_timer.stop();
    m_isPlaying = false;
    ui->playPauseButton->setText("播放");
}

bool AnimationAviWidget::canInterpolateBetween(int i, int j) const
{
    if (i < 0 || j < 0 || i >= m_frames.size() || j >= m_frames.size())
        return false;
    auto g0 = const_cast<AnimationAviWidget*>(this)->loadGridFromFile(m_frames[i].filePath);
    auto g1 = const_cast<AnimationAviWidget*>(this)->loadGridFromFile(m_frames[j].filePath);
    if (!g0 || !g1)
        return false;
    // 基础要求：点数一致（更严格可比 cell 数/类型）
    return g0->GetNumberOfPoints() == g1->GetNumberOfPoints();
}

vtkSmartPointer<vtkUnstructuredGrid> AnimationAviWidget::interpolateGrids(vtkUnstructuredGrid* g0,
                                                                          vtkUnstructuredGrid* g1,
                                                                          double alpha) const
{
    // 单元拓扑假设一致
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    out->DeepCopy(g0); // 拷贝单元/拓扑及数组壳

    // 点坐标
    vtkIdType npts = g0->GetNumberOfPoints();
    auto pts = vtkSmartPointer<vtkPoints>::New();
    pts->SetNumberOfPoints(npts);
    double p0[3], p1[3], p[3];
    for (vtkIdType i = 0; i < npts; ++i)
    {
        g0->GetPoint(i, p0);
        g1->GetPoint(i, p1);
        p[0] = (1 - alpha) * p0[0] + alpha * p1[0];
        p[1] = (1 - alpha) * p0[1] + alpha * p1[1];
        p[2] = (1 - alpha) * p0[2] + alpha * p1[2];
        pts->SetPoint(i, p);
    }
    out->SetPoints(pts);

    // 点标量数组：同名同分量线性插值（不存在则跳过）
    auto pd0 = g0->GetPointData();
    auto pd1 = g1->GetPointData();
    auto pdO = out->GetPointData();

    for (int a = 0; a < pd0->GetNumberOfArrays(); ++a)
    {
        auto arr0 = pd0->GetArray(a);
        if (!arr0)
            continue;
        const char* name = arr0->GetName();
        auto arr1 = pd1->GetArray(name);
        auto arrO = pdO->GetArray(name);
        if (!arr1 || !arrO)
            continue;
        int ncomp = arr0->GetNumberOfComponents();
        for (vtkIdType i = 0; i < npts; ++i)
        {
            for (int c = 0; c < ncomp; ++c)
            {
                double v0 = arr0->GetComponent(i, c);
                double v1 = arr1->GetComponent(i, c);
                arrO->SetComponent(i, c, (1 - alpha) * v0 + alpha * v1);
            }
        }
    }

    // 单元标量：可选（若有需要再加，通常着色用点数据）
    return out;
}

void AnimationAviWidget::onTick()
{
    const bool doInterp = ui->interpCheckBox->isChecked() && (ui->interpCountSpin->value() > 0);
    const int N = ui->interpCountSpin->value();

    int nextIndex = m_currentIndex + 1;
    if (nextIndex >= m_frames.size())
    {
        if (ui->loopCheckBox->isChecked())
            nextIndex = 0;
        else
        {
            stopTimer();
            return;
        }
    }

    if (doInterp && canInterpolateBetween(m_currentIndex, nextIndex))
    {
        // 如果还有子步，就插值；否则切换到下一原始帧
        if (m_subStep < N)
        {
            double alpha = double(m_subStep + 1) / double(N + 1);
            auto g0 = loadGridFromFile(m_frames[m_currentIndex].filePath);
            auto g1 = loadGridFromFile(m_frames[nextIndex].filePath);
            auto gI = interpolateGrids(g0, g1, alpha);
            if (gI)
            {
                m_mapper->SetInputData(gI);
                // LUT 范围
                if (ui->scalarComboBox->count() > 0)
                {
                    const QString s = ui->scalarComboBox->currentText();
                    double range[2]{0, 1};
                    if (m_hasFixedRange)
                    {
                        range[0] = m_fixedRange[0];
                        range[1] = m_fixedRange[1];
                    }
                    else if (auto arr = gI->GetPointData()->GetArray(s.toUtf8().data()))
                    {
                        arr->GetRange(range);
                    }
                    applyScalarAndLUT(s, range);
                }
                m_subStep++;
                updateFrameLabel();
                renderNow();
                return;
            }
        }
    }

    // 走到这里：进入下一原始帧
    loadFrame(nextIndex);
}

void AnimationAviWidget::on_playPauseButton_clicked()
{
    qDebug() << "m_isPlaying " << m_isPlaying;
    if (m_isPlaying)
        stopTimer();
    else
        startTimer();
}
void AnimationAviWidget::on_prevButton_clicked()
{
    stopTimer();
    int prev = m_currentIndex - 1;
    if (prev < 0)
        prev = ui->loopCheckBox->isChecked() ? (m_frames.size() - 1) : 0;
    loadFrame(prev);
}
void AnimationAviWidget::on_nextButton_clicked()
{
    stopTimer();
    int next = m_currentIndex + 1;
    if (next >= m_frames.size())
        next = ui->loopCheckBox->isChecked() ? 0 : (m_frames.size() - 1);
    loadFrame(next);
}
void AnimationAviWidget::on_frameSlider_valueChanged(int v)
{
    stopTimer();
    loadFrame(v);
}
void AnimationAviWidget::on_fpsSpin_valueChanged(double)
{
    if (m_isPlaying)
        startTimer(); // 重置 interval
}
void AnimationAviWidget::on_loopCheckBox_toggled(bool)
{
}

void AnimationAviWidget::on_scalarComboBox_currentIndexChanged(int)
{
    if (ui->scalarComboBox->count() == 0)
        return;
    const QString s = ui->scalarComboBox->currentText();

    if (ui->fixRangeCheckBox->isChecked())
    {
        double r[2];
        if (computeGlobalRange(s, r))
        {
            m_fixedRange[0] = r[0];
            m_fixedRange[1] = r[1];
            m_hasFixedRange = true;
            applyScalarAndLUT(s, m_fixedRange);
        }
    }
    else
    {
        m_hasFixedRange = false;
        double r[2]{0, 1};
        if (auto arr = m_grid->GetPointData()->GetArray(s.toUtf8().data()))
            arr->GetRange(r);
        applyScalarAndLUT(s, r);
    }
    renderNow();
}
void AnimationAviWidget::on_fixRangeCheckBox_toggled(bool)
{
    on_scalarComboBox_currentIndexChanged(0);
}

bool AnimationAviWidget::exportAVI(const QString& fileName, double fps)
{
    //     if (m_frames.isEmpty()) return false;
    //     auto rw = m_visualService->renderWindow();

    //     // 捕获器
    //     auto w2i = vtkSmartPointer<vtkWindowToImageFilter>::New();
    //     w2i->SetInput(rw);
    //     w2i->SetInputBufferTypeToRGB();
    //     w2i->ReadFrontBufferOff();
    //     w2i->ShouldRerenderOff();

    //     // 写 AVI
    //     auto avi = vtkSmartPointer<vtkAVIWriter>::New();
    //     avi->SetFileName(fileName.toUtf8().constData());
    // #if defined(VTK_AVIWRITER_HAS_RATE) || 1
    //     avi->SetRate(int(std::round(fps))); // 大多数版本支持；如不支持可移除
    // #endif
    //     avi->SetInputConnection(w2i->GetOutputPort());
    //     avi->Start();

    //     // 停止实时播放，逐帧写出
    //     bool looping = ui->loopCheckBox->isChecked();
    //     stopTimer();

    //     const bool doInterp = ui->interpCheckBox->isChecked() &&
    //     (ui->interpCountSpin->value() > 0); const int N =
    //     ui->interpCountSpin->value();

    //     for (int i=0; i<m_frames.size(); ++i)
    //     {
    //         // —— 原始帧 ——
    //         loadFrame(i);
    //         rw->Render();
    //         w2i->Modified(); w2i->Update();
    //         avi->Write();

    //         // —— 插帧 ——
    //         int j = (i+1 < m_frames.size()) ? (i+1) : (looping ? 0 : -1);
    //         if (doInterp && j >= 0 && canInterpolateBetween(i, j)) {
    //             auto g0 = loadGridFromFile(m_frames[i].filePath);
    //             auto g1 = loadGridFromFile(m_frames[j].filePath);
    //             for (int k=0; k<N; ++k) {
    //                 double alpha = double(k+1)/double(N+1);
    //                 auto gI = interpolateGrids(g0, g1, alpha);
    //                 if (!gI) break;
    //                 m_mapper->SetInputData(gI);
    //                 if (ui->scalarComboBox->count() > 0) {
    //                     const QString s = ui->scalarComboBox->currentText();
    //                     double range[2]{0,1};
    //                     if (m_hasFixedRange) { range[0]=m_fixedRange[0];
    //                     range[1]=m_fixedRange[1]; } else if (auto arr =
    //                     gI->GetPointData()->GetArray(s.toUtf8().data())) {
    //                     arr->GetRange(range); } applyScalarAndLUT(s, range);
    //                 }
    //                 rw->Render();
    //                 w2i->Modified(); w2i->Update();
    //                 avi->Write();
    //             }
    //         }
    //     }

    //     avi->End();
    return true;
}

void AnimationAviWidget::on_saveAviButton_clicked()
{
    QString fn = QFileDialog::getSaveFileName(this, "保存 AVI", QString(), "AVI (*.avi)");
    if (fn.isEmpty())
        return;

    if (exportAVI(fn, ui->fpsSpin->value()))
        QMessageBox::information(this, "完成", "AVI 导出成功。");
    else
        QMessageBox::warning(this, "失败", "AVI 导出失败。");
}

void AnimationAviWidget::on_loadPushButton_clicked()
{
    // <project>/data
    m_dataPath =
        QDir(PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString())
            .filePath("data");

    scanFrames();
    if (m_frames.isEmpty())
    {
        QMessageBox::warning(this, "提示", "未找到结果文件。");
        return;
    }

    ui->frameSlider->setRange(0, m_frames.size() - 1);
    m_currentIndex = 0;
    m_subStep = 0;

    buildPipelineIfNeeded();
    if (!loadFrame(0))
        return;

    fillScalarComboFromCurrentGrid();

    // 固定色标（默认开）
    if (ui->fixRangeCheckBox->isChecked() && ui->scalarComboBox->count() > 0)
    {
        const QString s = ui->scalarComboBox->currentText();
        double r[2];
        if (computeGlobalRange(s, r))
        {
            m_fixedRange[0] = r[0];
            m_fixedRange[1] = r[1];
            m_hasFixedRange = true;
            applyScalarAndLUT(s, m_fixedRange);
            renderNow();
        }
    }
}
