#include "IsosurfaceSegmentationWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkContourFilter.h>
#include <vtkDataObject.h> // FIELD_ASSOCIATION_*
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTecplotReader.h>
#include <vtkThreshold.h>
#include <vtkUnstructuredGrid.h>

#include <QDebug>
#include <QDir>
#include <QRegularExpression>

#include "PostResultAutoPrepare.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ui_IsosurfaceSegmentationWidget.h"

#include <algorithm>
#include <cmath>

IsosurfaceSegmentationWidget::IsosurfaceSegmentationWidget(
    VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::IsosurfaceSegmentationWidget),
      BasePage(PublicConstants::kPagePostProcessIsosurfaceSegmentation),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    // UI 默认
    ui->percentCheckBox->setChecked(true);
    ui->isoSlider->setRange(0, 100);
    ui->isoSlider->setValue(50);
    ui->isoSpin->setDecimals(3);
    ui->isoSpin->setSingleStep(0.001);

    ui->showLowCheckBox->setChecked(true);
    ui->showHighCheckBox->setChecked(true);
    ui->showInterfaceCheckBox->setChecked(false);

    ui->lowOpacitySlider->setRange(10, 100);
    ui->highOpacitySlider->setRange(10, 100);
    ui->ifaceOpacitySlider->setRange(10, 100);
    ui->lowOpacitySlider->setValue(60);
    ui->highOpacitySlider->setValue(60);
    ui->ifaceOpacitySlider->setValue(100);

    // 信号（如用 Qt Designer 的自动连接，也可以省略）
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->percentCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_percentCheckBox_toggled);
    connect(ui->isoSlider, &QSlider::valueChanged, this,
            &IsosurfaceSegmentationWidget::on_isoSlider_valueChanged);
    connect(ui->isoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &IsosurfaceSegmentationWidget::on_isoSpin_valueChanged);

    connect(ui->showLowCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_showLowCheckBox_toggled);
    connect(ui->showHighCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_showHighCheckBox_toggled);
    connect(ui->showInterfaceCheckBox, &QCheckBox::toggled, this,
            &IsosurfaceSegmentationWidget::on_showInterfaceCheckBox_toggled);

    connect(ui->lowOpacitySlider, &QSlider::valueChanged, this,
            &IsosurfaceSegmentationWidget::on_lowOpacitySlider_valueChanged);
    connect(ui->highOpacitySlider, &QSlider::valueChanged, this,
            &IsosurfaceSegmentationWidget::on_highOpacitySlider_valueChanged);
    connect(ui->ifaceOpacitySlider, &QSlider::valueChanged, this,
            &IsosurfaceSegmentationWidget::on_ifaceOpacitySlider_valueChanged);

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_stepComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &IsosurfaceSegmentationWidget::on_resultDirComboBox_currentIndexChanged);
}

IsosurfaceSegmentationWidget::~IsosurfaceSegmentationWidget()
{
    delete ui;
}

void IsosurfaceSegmentationWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
    refreshResultGroups();
    if (m_dataPath.isEmpty())
    {
        const QString projectDir =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (m_dataPath.isEmpty())
            m_dataPath = PostResultAutoPrepare::firstResultDirectory(projectDir);
    }

    if (ui->stepComboBox->count() == 0)
    {
        // 首次进入：列步号 -> 读取 -> 建管线 -> 绑定色标/阈值/显隐
        loadFileName(m_dataPath);
        buildPipelineIfNeeded();
        updateColoringFromUI();
        updateIsoFromUI();
        updateVisibilityFromUI();
        m_loadedOnce = true;
    }
    else
    {
        // 非首次：把 actor 挂回，并确保与当前 UI 同步
        attachActorsIfNeeded();
        buildPipelineIfNeeded();
        updateColoringFromUI();
        updateIsoFromUI();
        updateVisibilityFromUI();
    }
    renderNow();
}

void IsosurfaceSegmentationWidget::refreshResultGroups()
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

void IsosurfaceSegmentationWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}
void IsosurfaceSegmentationWidget::loadFileName(QString dataPath)
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

void IsosurfaceSegmentationWidget::beforeUnload()
{
    // 只移除，不销毁对象
    detachActorsIfAttached();
}

void IsosurfaceSegmentationWidget::readTecplotLatest()
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

void IsosurfaceSegmentationWidget::readTecplot(const QString& file)
{
    m_grid = PostTecplotUtils::readGrid(file);
}

void IsosurfaceSegmentationWidget::fillScalarCombo()
{
    if (!m_grid)
    {
        ui->scalarComboBox->clear();
        return;
    }

    const QString prev = ui->scalarComboBox->currentText();
    QStringList names;

    // 点数组
    if (auto pd = m_grid->GetPointData())
    {
        for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
            if (const char* n = pd->GetArrayName(i))
                names << QString::fromUtf8(n);
    }
    // 元数组
    if (auto cd = m_grid->GetCellData())
    {
        for (int i = 0; i < cd->GetNumberOfArrays(); ++i)
            if (const char* n = cd->GetArrayName(i))
                names << QString::fromUtf8(n);
    }
    names.removeDuplicates();
    names.sort();

    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();
    ui->scalarComboBox->addItems(names);
    int keep = ui->scalarComboBox->findText(prev);
    ui->scalarComboBox->setCurrentIndex(keep >= 0 ? keep : (ui->scalarComboBox->count() ? 0 : -1));
    ui->scalarComboBox->blockSignals(false);

    setIsoSpinRangeFromScalar();
}

void IsosurfaceSegmentationWidget::buildPipelineIfNeeded()
{
    if (!m_grid)
        return;
    if (m_threshLow)
        return; // 已构建

    // 低值侧
    m_threshLow = vtkSmartPointer<vtkThreshold>::New();
    m_threshLow->SetInputData(m_grid);

    m_mapperLow = vtkSmartPointer<vtkDataSetMapper>::New();
    m_mapperLow->SetInputConnection(m_threshLow->GetOutputPort());

    m_actorLow = vtkSmartPointer<vtkActor>::New();
    m_actorLow->SetMapper(m_mapperLow);
    m_actorLow->GetProperty()->SetOpacity(0.6);

    // 高值侧
    m_threshHigh = vtkSmartPointer<vtkThreshold>::New();
    m_threshHigh->SetInputData(m_grid);

    m_mapperHigh = vtkSmartPointer<vtkDataSetMapper>::New();
    m_mapperHigh->SetInputConnection(m_threshHigh->GetOutputPort());

    m_actorHigh = vtkSmartPointer<vtkActor>::New();
    m_actorHigh->SetMapper(m_mapperHigh);
    m_actorHigh->GetProperty()->SetOpacity(0.6);

    // 等值面（中间界面）
    m_contourIso = vtkSmartPointer<vtkContourFilter>::New();
    m_contourIso->SetInputData(m_grid);

    m_mapperIso = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapperIso->SetInputConnection(m_contourIso->GetOutputPort());

    m_actorIso = vtkSmartPointer<vtkActor>::New();
    m_actorIso->SetMapper(m_mapperIso);
    m_actorIso->GetProperty()->SetOpacity(1.0);
    m_actorIso->GetProperty()->SetColor(1, 1, 1); // 纯白界面

    // 首次构建时：加入场景，并记挂载标志
    if (m_actorLow && !m_attachedLow)
    {
        addActorByService(m_visualService, m_actorLow);
        m_attachedLow = true;
    }
    if (m_actorHigh && !m_attachedHigh)
    {
        addActorByService(m_visualService, m_actorHigh);
        m_attachedHigh = true;
    }
    if (m_actorIso && !m_attachedIso)
    {
        addActorByService(m_visualService, m_actorIso);
        m_attachedIso = true;
    }
}

void IsosurfaceSegmentationWidget::setIsoSpinRangeFromScalar()
{
    if (!m_grid)
        return;
    const QString name = ui->scalarComboBox->currentText();

    // 点/元优先级：先点后元
    auto arr = m_grid->GetPointData()->GetArray(name.toUtf8().data());
    if (!arr)
        arr = m_grid->GetCellData()->GetArray(name.toUtf8().data());
    if (!arr)
        return;

    arr->GetRange(m_scalarRange);

    ui->isoSpin->blockSignals(true);
    ui->isoSpin->setRange(m_scalarRange[0], m_scalarRange[1]);
    ui->isoSpin->setValue((m_scalarRange[0] + m_scalarRange[1]) * 0.5);
    ui->isoSpin->blockSignals(false);
}

double IsosurfaceSegmentationWidget::currentScalarMin() const
{
    return m_scalarRange[0];
}
double IsosurfaceSegmentationWidget::currentScalarMax() const
{
    return m_scalarRange[1];
}

void IsosurfaceSegmentationWidget::updateIsoFromUI()
{
    if (!m_grid || !m_threshLow || !m_threshHigh || !m_contourIso)
        return;

    const QString name = ui->scalarComboBox->currentText();
    // 判定数组是点还是元
    int assoc = vtkDataObject::FIELD_ASSOCIATION_POINTS;
    if (!m_grid->GetPointData()->GetArray(name.toUtf8().data()) &&
        m_grid->GetCellData()->GetArray(name.toUtf8().data()))
    {
        assoc = vtkDataObject::FIELD_ASSOCIATION_CELLS;
    }

    // 百分比/绝对值 -> iso
    double iso = 0.0;
    if (ui->percentCheckBox->isChecked())
    {
        const double t = ui->isoSlider->value() / 100.0;
        iso = currentScalarMin() + t * (currentScalarMax() - currentScalarMin());
        ui->isoSpin->blockSignals(true);
        ui->isoSpin->setValue(iso);
        ui->isoSpin->blockSignals(false);
    }
    else
    {
        iso = ui->isoSpin->value();
        double t = 0.0;
        if (currentScalarMax() > currentScalarMin())
            t = (iso - currentScalarMin()) / (currentScalarMax() - currentScalarMin());
        t = std::max(0.0, std::min(1.0, t));
        ui->isoSlider->blockSignals(true);
        ui->isoSlider->setValue(int(std::round(t * 100)));
        ui->isoSlider->blockSignals(false);
    }

    // —— 阈值分割 ——（用一个很小的 eps 避免两侧重叠）
    const double eps = 1e-6 * std::max(1.0, std::abs(currentScalarMax() - currentScalarMin()));

    // 低值侧：(-inf, iso - eps]
    m_threshLow->SetInputArrayToProcess(0, 0, 0, assoc, name.toUtf8().constData());
#if VTK_VERSION_NUMBER < VTK_VERSION_CHECK(9, 1, 0)
    m_threshLow->ThresholdByUpper(iso - eps);
#else
    m_threshLow->SetThresholdFunction(vtkThreshold::THRESHOLD_UPPER);
    m_threshLow->SetUpperThreshold(iso - eps);
#endif

    // 高值侧：[iso + eps, +inf)
    m_threshHigh->SetInputArrayToProcess(0, 0, 0, assoc, name.toUtf8().constData());
#if VTK_VERSION_NUMBER < VTK_VERSION_CHECK(9, 1, 0)
    m_threshHigh->ThresholdByLower(iso + eps);
#else
    m_threshHigh->SetThresholdFunction(vtkThreshold::THRESHOLD_LOWER);
    m_threshHigh->SetLowerThreshold(iso + eps);
#endif

    m_threshLow->Update();
    m_threshHigh->Update();

    // —— 中间等值面 ——（使用同一数组）
    m_contourIso->SetInputArrayToProcess(0, 0, 0, assoc, name.toUtf8().constData());
    m_contourIso->SetNumberOfContours(1);
    m_contourIso->SetValue(0, iso);
    m_contourIso->Update();
}

void IsosurfaceSegmentationWidget::updateColoringFromUI()
{
    if (!m_grid || !m_mapperLow || !m_mapperHigh || !m_mapperIso)
        return;

    const QString name = ui->scalarComboBox->currentText();

    // 点/元判断 + 取范围
    int assoc = vtkDataObject::FIELD_ASSOCIATION_POINTS;
    auto arr = m_grid->GetPointData()->GetArray(name.toUtf8().data());
    if (!arr)
    {
        arr = m_grid->GetCellData()->GetArray(name.toUtf8().data());
        if (!arr)
            return;
        assoc = vtkDataObject::FIELD_ASSOCIATION_CELLS;
    }

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

    // 低值侧
    m_mapperLow->SetLookupTable(lut);
    if (assoc == vtkDataObject::FIELD_ASSOCIATION_POINTS)
        m_mapperLow->SetScalarModeToUsePointFieldData();
    else
        m_mapperLow->SetScalarModeToUseCellFieldData();
    m_mapperLow->SelectColorArray(name.toUtf8().data());
    m_mapperLow->SetScalarRange(range);

    // 高值侧
    m_mapperHigh->SetLookupTable(lut);
    if (assoc == vtkDataObject::FIELD_ASSOCIATION_POINTS)
        m_mapperHigh->SetScalarModeToUsePointFieldData();
    else
        m_mapperHigh->SetScalarModeToUseCellFieldData();
    m_mapperHigh->SelectColorArray(name.toUtf8().data());
    m_mapperHigh->SetScalarRange(range);

    // 等值面
    m_mapperIso->SetLookupTable(lut);
    m_mapperIso->SetScalarModeToUsePointFieldData(); // 等值面输出为点场
    m_mapperIso->SelectColorArray(name.toUtf8().data());
    m_mapperIso->SetScalarRange(range);

    // 缓存给 Spin/Slider 使用
    m_scalarRange[0] = range[0];
    m_scalarRange[1] = range[1];
}

void IsosurfaceSegmentationWidget::updateVisibilityFromUI()
{
    if (m_actorLow)
        m_actorLow->SetVisibility(ui->showLowCheckBox->isChecked());
    if (m_actorHigh)
        m_actorHigh->SetVisibility(ui->showHighCheckBox->isChecked());
    if (m_actorIso)
        m_actorIso->SetVisibility(ui->showInterfaceCheckBox->isChecked());

    if (m_actorLow)
        m_actorLow->GetProperty()->SetOpacity(ui->lowOpacitySlider->value() / 100.0);
    if (m_actorHigh)
        m_actorHigh->GetProperty()->SetOpacity(ui->highOpacitySlider->value() / 100.0);
    if (m_actorIso)
        m_actorIso->GetProperty()->SetOpacity(ui->ifaceOpacitySlider->value() / 100.0);
}

void IsosurfaceSegmentationWidget::renderNow()
{
    if (auto rw = m_visualService->renderWindow())
        rw->Render();
}

void IsosurfaceSegmentationWidget::attachActorsIfNeeded()
{
    if (m_actorLow && !m_attachedLow)
    {
        addActorByService(m_visualService, m_actorLow);
        m_attachedLow = true;
    }
    if (m_actorHigh && !m_attachedHigh)
    {
        addActorByService(m_visualService, m_actorHigh);
        m_attachedHigh = true;
    }
    if (m_actorIso && !m_attachedIso)
    {
        addActorByService(m_visualService, m_actorIso);
        m_attachedIso = true;
    }
}

void IsosurfaceSegmentationWidget::detachActorsIfAttached()
{
    if (m_actorLow && m_attachedLow)
    {
        removeActorByService(m_visualService, m_actorLow);
        m_attachedLow = false;
    }
    if (m_actorHigh && m_attachedHigh)
    {
        removeActorByService(m_visualService, m_actorHigh);
        m_attachedHigh = false;
    }
    if (m_actorIso && m_attachedIso)
    {
        removeActorByService(m_visualService, m_actorIso);
        m_attachedIso = false;
    }
}

void IsosurfaceSegmentationWidget::on_scalarComboBox_currentIndexChanged(int)
{
    buildPipelineIfNeeded(); // 首次保障已构建（后续无事发生）
    setIsoSpinRangeFromScalar();
    updateColoringFromUI();
    updateIsoFromUI();
    updateVisibilityFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_percentCheckBox_toggled(bool)
{
    updateIsoFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_isoSlider_valueChanged(int)
{
    if (!ui->percentCheckBox->isChecked())
        return;
    updateIsoFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_isoSpin_valueChanged(double)
{
    if (ui->percentCheckBox->isChecked())
        return;
    updateIsoFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_showLowCheckBox_toggled(bool)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_showHighCheckBox_toggled(bool)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_showInterfaceCheckBox_toggled(bool)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_lowOpacitySlider_valueChanged(int)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_highOpacitySlider_valueChanged(int)
{
    updateVisibilityFromUI();
    renderNow();
}
void IsosurfaceSegmentationWidget::on_ifaceOpacitySlider_valueChanged(int)
{
    updateVisibilityFromUI();
    renderNow();
}

void IsosurfaceSegmentationWidget::on_stepComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const QString file = ui->stepComboBox->currentData().toString();
    if (file.isEmpty())
        return;

    // 读取新网格
    readTecplot(file);

    // 重新填充（并尽量保留选择）
    fillScalarCombo();

    // 若已有管线，切换输入；否则首次构建
    if (!m_threshLow || !m_threshHigh || !m_contourIso)
    {
        buildPipelineIfNeeded();
    }
    else
    {
        m_threshLow->SetInputData(m_grid);
        m_threshHigh->SetInputData(m_grid);
        m_contourIso->SetInputData(m_grid);
    }

    // 按当前 UI 刷新色标/阈值/显隐
    updateColoringFromUI();
    updateIsoFromUI();
    updateVisibilityFromUI();

    // 确保 actor 已挂
    attachActorsIfNeeded();

    renderNow();
}
