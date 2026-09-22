#include "VectorSettingWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkArrowSource.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkFloatArray.h>
#include <vtkGlyph3D.h>
#include <vtkLookupTable.h>
#include <vtkMaskPoints.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalBlocker>

#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "PostTecplotUtils.h"
#include "ResultDisplayNames.h"
#include "PostVisualUtils.h"
#include "ui_VectorSettingWidget.h"

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <limits>

VectorSettingWidget::VectorSettingWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::VectorSettingWidget), BasePage(PublicConstants::kPagePostProcessVector)
{
    ui->setupUi(this);
    ui->displayGroupBox->setVisible(false);

    configurePostResultCombo(ui->analysisComboBox);
    configurePostResultCombo(ui->resultDirComboBox);
    configurePostResultCombo(ui->resultTimeComboBox);
    configurePostResultCombo(ui->stepComboBox);

    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    connect(ui->showScalarBarCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        postScalarBarStyle().visible = checked;
        if (m_visualService)
        {
            m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
            m_visualService->render();
        }
    });
    connect(ui->displaySettingsButton, &QPushButton::clicked, this, [this]() {
        if (editPostDisplaySettings(this, m_visualService, false, false))
        {
            ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            if (m_visualService)
                m_visualService->render();
        }
    });

    // 矢量参数统一采用直接更新；采样、分量变化后无需再点“确定”。
    connect(ui->scaleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VectorSettingWidget::on_scaleSpinBox_valueChanged);
    connect(ui->thicknessSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VectorSettingWidget::on_thicknessSpinBox_valueChanged);
    connect(ui->analysisComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VectorSettingWidget::on_analysisComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VectorSettingWidget::on_resultDirComboBox_currentIndexChanged);
    connect(ui->resultTimeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VectorSettingWidget::on_resultTimeComboBox_currentIndexChanged);

    // 0 表示自动采样；自动模式把箭头数量控制在约 2000 个，兼顾可读性与性能。
    ui->maskSizeSpinBox->setRange(0, 1000000);
    ui->maskSizeSpinBox->setSpecialValueText(QStringLiteral("自动"));
    ui->maskSizeSpinBox->setValue(0);

    const auto rebuildOnSelectionChanged = [this](int)
    {
        if (!m_updatingControls && m_grid)
            rebuildPipeline();
    };
    connect(ui->xComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            rebuildOnSelectionChanged);
    connect(ui->yComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            rebuildOnSelectionChanged);
    connect(ui->zComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            rebuildOnSelectionChanged);
    connect(ui->maskSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int)
            {
                if (!m_updatingControls && m_grid)
                    rebuildPipeline();
            });

    // 长度和粗细使用可直接输入的数值控件，避免滑块无法看出当前倍率。
    m_scaleFactor = ui->scaleSpinBox->value();
    m_thicknessFactor = ui->thicknessSpinBox->value();

    EventManager::instance().registerHandler(
        postDisplaySettingsChangedEvent(), this,
        [this](const QVariantMap&)
        {
            if (!property("postDisplayActive").toBool() || !m_grid)
                return;
            const QSignalBlocker blocker(ui->showScalarBarCheckBox);
            ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            rebuildPipeline();
        });

    m_visualService = postProcessingWidget;
}
VectorSettingWidget::~VectorSettingWidget()
{
    delete ui;
}

double VectorSettingWidget::computeAutoBaseScale(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return 1.0;
    double b[6];
    grid->GetBounds(b);
    const double dx = b[1] - b[0];
    const double dy = b[3] - b[2];
    const double dz = b[5] - b[4];
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (diag <= 0.0 || !std::isfinite(diag))
        return 1.0;
    return diag * 0.06; // 默认箭头基准长度为模型包围盒对角线的 6%
}

int VectorSettingWidget::computeAutoMaskRatio(vtkUnstructuredGrid* grid) const
{
    if (!grid)
        return 1;
    constexpr vtkIdType targetGlyphCount = 2000;
    const vtkIdType pointCount = grid->GetNumberOfPoints();
    if (pointCount <= targetGlyphCount)
        return 1;
    return std::max(1, static_cast<int>(std::ceil(pointCount / double(targetGlyphCount))));
}

QString VectorSettingWidget::vectorLegendTitle() const
{
    const QString xName = ui->xComboBox->currentData().toString();
    const QString key = ResultDisplayNames::keyOf(xName);
    const QString unit = ResultDisplayNames::unitOf(xName);

    QString title;
    const bool velocityUnit = unit.contains(QLatin1Char('/')) &&
                              unit.contains(QLatin1Char('s'), Qt::CaseInsensitive);
    if (key == QStringLiteral("UX") || key == QStringLiteral("U1") ||
        key == QStringLiteral("DX"))
        title = velocityUnit ? QStringLiteral("|v|") : QStringLiteral("|u|");
    else if (key == QStringLiteral("VX") || key == QStringLiteral("V1") ||
             key == QStringLiteral("V") || key == QStringLiteral("VEL") ||
             key == QStringLiteral("VELOCITY") || key == QStringLiteral("VMAG"))
        title = QStringLiteral("|v|");
    else if (key == QStringLiteral("U") || key == QStringLiteral("DISP") ||
             key == QStringLiteral("DISPLACEMENT") || key == QStringLiteral("UMAG"))
        title = QStringLiteral("|u|");
    else if (xName == ui->yComboBox->currentData().toString() &&
             (ui->zComboBox->currentData().toString().isEmpty() ||
              xName == ui->zComboBox->currentData().toString()))
        title = ResultDisplayNames::scalarBarName(xName);
    else
        title = QStringLiteral("矢量模");

    if (!unit.isEmpty() && !title.contains(unit))
        title += QStringLiteral(" (%1)").arg(unit);
    return title;
}

void VectorSettingWidget::beforeLoad()
{
    setProperty("postDisplayActive", true);
    clearPostDisplayContext();
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    const QString proj =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    refreshResultGroups();
    if (m_dataPath.isEmpty())
        m_dataPath = PostResultAutoPrepare::firstResultDirectory(proj);

    if (ui->stepComboBox->count() == 0)
    {
        // 首次进入：加载文件列表并默认读第一个
        loadFileName(m_dataPath);
    }
    else
    {
        // 再次进入时也按当前筛选和显示设置重建，避免恢复旧筛选下的箭头。
        rebuildPipeline();
    }
    m_loadedOnce = true;
    applyPendingState();
}

QJsonObject VectorSettingWidget::saveJson()
{
    QJsonObject object;
    object.insert(QStringLiteral("analysisKey"), ui->analysisComboBox->currentData().toString());
    object.insert(QStringLiteral("resultDir"), ui->resultDirComboBox->currentData().toString());
    object.insert(QStringLiteral("resultTime"), ui->resultTimeComboBox->currentData().toString());
    object.insert(QStringLiteral("stepFile"), ui->stepComboBox->currentData().toString());
    object.insert(QStringLiteral("xField"), ui->xComboBox->currentData().toString());
    object.insert(QStringLiteral("yField"), ui->yComboBox->currentData().toString());
    object.insert(QStringLiteral("zField"), ui->zComboBox->currentData().toString());
    object.insert(QStringLiteral("maskRatio"), ui->maskSizeSpinBox->value());
    object.insert(QStringLiteral("scale"), ui->scaleSpinBox->value());
    object.insert(QStringLiteral("thickness"), ui->thicknessSpinBox->value());
    return object;
}

void VectorSettingWidget::loadJson(const QJsonObject& object)
{
    m_pendingState.clear();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingState.insert(it.key(), it.value().toVariant());
    m_hasPendingState = true;
    if (m_loadedOnce)
        applyPendingState();
}

void VectorSettingWidget::applyPendingState()
{
    if (!m_hasPendingState)
        return;

    const QVariantMap state = m_pendingState;
    m_pendingState.clear();
    m_hasPendingState = false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString resultDir = state.value(QStringLiteral("resultDir")).toString();
    const QString resolvedResultDir =
        PostResultSelectionUtils::absoluteSourcePath(projectDir, resultDir);
    QString analysisKey = state.value(QStringLiteral("analysisKey")).toString();
    if (analysisKey.isEmpty() && !resolvedResultDir.isEmpty())
        analysisKey =
            PostResultSelectionUtils::analysisKeyForSource(projectDir, resolvedResultDir);

    if (!analysisKey.isEmpty())
    {
        const int analysisIndex = ui->analysisComboBox->findData(analysisKey);
        if (analysisIndex >= 0)
        {
            const QSignalBlocker blocker(ui->analysisComboBox);
            ui->analysisComboBox->setCurrentIndex(analysisIndex);
        }
    }
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, projectDir,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        resolvedResultDir);
    if (!resolvedResultDir.isEmpty())
    {
        const int index = PostResultSelectionUtils::sourceComboIndex(
            ui->resultDirComboBox, projectDir, resolvedResultDir);
        if (index >= 0)
        {
            const QSignalBlocker blocker(ui->resultDirComboBox);
            ui->resultDirComboBox->setCurrentIndex(index);
        }
    }
    m_dataPath = ui->resultDirComboBox->currentData().toString();
    loadFileName(m_dataPath);

    const QString resultTime = state.value(QStringLiteral("resultTime")).toString();
    if (!resultTime.isNull())
    {
        const int timeIndex = ui->resultTimeComboBox->findData(resultTime);
        if (timeIndex >= 0)
        {
            {
                const QSignalBlocker blocker(ui->resultTimeComboBox);
                ui->resultTimeComboBox->setCurrentIndex(timeIndex);
            }
            on_resultTimeComboBox_currentIndexChanged(timeIndex);
        }
    }

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

    m_updatingControls = true;
    auto restoreCombo = [](QComboBox* combo, const QString& name)
    {
        if (!combo || name.isEmpty())
            return;
        const int index = combo->findData(name);
        if (index >= 0)
        {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(index);
        }
    };
    restoreCombo(ui->xComboBox, state.value(QStringLiteral("xField")).toString());
    restoreCombo(ui->yComboBox, state.value(QStringLiteral("yField")).toString());
    restoreCombo(ui->zComboBox, state.value(QStringLiteral("zField")).toString());
    {
        const QSignalBlocker blocker(ui->maskSizeSpinBox);
        ui->maskSizeSpinBox->setValue(
            qBound(0, state.contains(QStringLiteral("maskRatio"))
                          ? state.value(QStringLiteral("maskRatio")).toInt()
                          : 0,
                   1000000));
    }
    {
        double scale = state.value(QStringLiteral("scale"), 1.0).toDouble();
        // 兼容旧工程中滑块按 25～400 保存的整数值。
        if (scale > 10.0)
            scale /= 100.0;
        const QSignalBlocker blocker(ui->scaleSpinBox);
        ui->scaleSpinBox->setValue(qBound(0.1, scale, 10.0));
        m_scaleFactor = ui->scaleSpinBox->value();
    }
    {
        const QSignalBlocker blocker(ui->thicknessSpinBox);
        ui->thicknessSpinBox->setValue(
            qBound(0.25, state.value(QStringLiteral("thickness"), 1.0).toDouble(), 4.0));
        m_thicknessFactor = ui->thicknessSpinBox->value();
    }
    m_updatingControls = false;
    rebuildPipeline();
}

void VectorSettingWidget::refreshResultGroups()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);
    QString previousAnalysis = ui->analysisComboBox->currentData().toString();
    const QString previousPath = m_dataPath;
    if (previousAnalysis.isEmpty() && !previousPath.isEmpty())
        previousAnalysis =
            PostResultSelectionUtils::analysisKeyForSource(groups, previousPath);

    PostResultSelectionUtils::populateAnalysisCombo(
        ui->analysisComboBox, groups, previousAnalysis);
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, groups,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        previousPath);

    m_dataPath = ui->resultDirComboBox->currentData().toString();
}

void VectorSettingWidget::on_analysisComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousPath = m_dataPath;
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, projectDir,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        previousPath);
    m_dataPath = ui->resultDirComboBox->currentData().toString();
    loadFileName(m_dataPath);
}

void VectorSettingWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void VectorSettingWidget::on_resultTimeComboBox_currentIndexChanged(int)
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousName = ui->stepComboBox->currentText();
    PostResultSelectionUtils::populateResultCombo(
        ui->stepComboBox, projectDir, m_dataPath,
        PostResultSelectionUtils::selectedFrameKey(ui->resultTimeComboBox),
        QString(), true);

    const int sameName = ui->stepComboBox->findText(previousName);
    if (sameName >= 0)
        ui->stepComboBox->setCurrentIndex(sameName);
    if (ui->stepComboBox->count() > 0)
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());
}

void VectorSettingWidget::beforeUnload()
{
    setPostViewActive(false);
    setProperty("postDisplayActive", false);
    clearPostDisplayContext();
    // 页面被切换：仅隐藏当前向量箭头，保留对象以便回来后快速恢复。
    if (m_visualService && m_vectorActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")), false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    requestRender();
    // 不要置空 m_vectorActor/m_glyph/m_grid —— 保持状态以用于恢复
}

void VectorSettingWidget::loadFileName(QString dataPath)
{
    QDir directory(dataPath);
    if (!directory.exists())
    {
        ui->resultTimeComboBox->clear();
        ui->stepComboBox->clear();
        qWarning() << "Directory not found:" << dataPath;
        return;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousTime = ui->resultTimeComboBox->currentData().toString();
    const QString previousFile = ui->stepComboBox->currentData().toString();

    PostResultSelectionUtils::populateTimeCombo(
        ui->resultTimeComboBox, projectDir, dataPath, previousTime, true);
    PostResultSelectionUtils::populateResultCombo(
        ui->stepComboBox, projectDir, dataPath,
        PostResultSelectionUtils::selectedFrameKey(ui->resultTimeComboBox),
        previousFile, true);

    if (ui->stepComboBox->count() > 0)
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());
    else
        qWarning() << "No configured result files found in" << dataPath;
}

void VectorSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;

    const QString filePath = ui->stepComboBox->currentData().toString();
    if (filePath.isEmpty())
    {
        qWarning() << "[Vector] No data file mapped for selection.";
        return;
    }
    setFileName(filePath);

    // 切文件时移除旧箭头，并直接按当前设置生成新结果。
    if (m_visualService && m_vectorActor)
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")), false);

    if (ensureDataLoaded())
        rebuildPipeline();
}

void VectorSettingWidget::setFileName(const QString& file)
{
    if (m_fileName == file)
        return;
    m_fileName = file;
    m_grid = nullptr; // 强制下次重建
}

bool VectorSettingWidget::ensureDataLoaded()
{
    if (m_grid)
        return true;
    if (m_fileName.isEmpty())
        return false;

    QFileInfo info(m_fileName);
    QString ext = info.suffix().toLower();

    vtkSmartPointer<vtkUnstructuredGrid> grid;

    if (ext == "txt" || ext == "dat" || ext == "tec" ||
        ext == "vtu" || ext == "vtm")
    {
        grid = PostTecplotUtils::readGrid(m_fileName);
    }
    else
    {
        qWarning() << "[Vector] Unsupported file:" << m_fileName;
        return false;
    }

    if (!grid || grid->GetNumberOfPoints() == 0)
        return false;

    m_grid = grid;
    populateComponentCombos();
    return true;
}

void VectorSettingWidget::populateComponentCombos()
{
    if (!m_grid)
        return;

    m_updatingControls = true;
    QStringList names;
    vtkPointData* pd = m_grid->GetPointData();
    const int nArr = pd->GetNumberOfArrays();
    for (int i = 0; i < nArr; ++i)
    {
        vtkDataArray* arr = pd->GetArray(i);
        if (!arr || !arr->GetName())
            continue;
        const QString arrayName = QString::fromUtf8(arr->GetName());
        if (!ResultDisplayNames::isInternalField(arrayName) &&
            (arr->GetNumberOfComponents() == 1 || arr->GetNumberOfComponents() == 3))
            names << arrayName;
    }
    names.removeDuplicates();
    names.sort();

    auto setCombo = [&](QComboBox* combo)
    {
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (const QString& name : names)
            combo->addItem(ResultDisplayNames::displayName(name), name);
    };
    setCombo(ui->xComboBox);
    setCombo(ui->yComboBox);
    setCombo(ui->zComboBox);

    auto findKey = [&](QComboBox* combo, const QStringList& keys) -> int
    {
        for (const QString& wanted : keys)
        {
            for (int i = 0; i < combo->count(); ++i)
            {
                if (ResultDisplayNames::keyOf(combo->itemData(i).toString()) == wanted)
                    return i;
            }
        }
        return -1;
    };
    auto selectIndex = [](QComboBox* combo, int index)
    {
        if (combo && index >= 0 && index < combo->count())
            combo->setCurrentIndex(index);
    };

    // 优先匹配完整的位移分量组，其次速度分量组，避免把 UX/VY/SZ 混在一起。
    int ix = findKey(ui->xComboBox, {QStringLiteral("UX"), QStringLiteral("U1"), QStringLiteral("DX")});
    int iy = findKey(ui->yComboBox, {QStringLiteral("UY"), QStringLiteral("U2"), QStringLiteral("DY")});
    int iz = findKey(ui->zComboBox, {QStringLiteral("UZ"), QStringLiteral("U3"), QStringLiteral("DZ")});
    if (ix < 0 || iy < 0)
    {
        ix = findKey(ui->xComboBox, {QStringLiteral("VX"), QStringLiteral("V1")});
        iy = findKey(ui->yComboBox, {QStringLiteral("VY"), QStringLiteral("V2")});
        iz = findKey(ui->zComboBox, {QStringLiteral("VZ"), QStringLiteral("V3")});
    }

    // 若结果直接提供三分量数组，则三个选择框指向同一数组。
    if (ix < 0 || iy < 0)
    {
        int packedIndex = -1;
        for (int i = 0; i < ui->xComboBox->count(); ++i)
        {
            vtkDataArray* arr = arrayByName(m_grid, ui->xComboBox->itemData(i).toString());
            if (arr && arr->GetNumberOfComponents() == 3)
            {
                packedIndex = i;
                break;
            }
        }
        if (packedIndex >= 0)
        {
            ix = iy = iz = packedIndex;
        }
    }

    if (ix < 0 && ui->xComboBox->count() > 0)
        ix = 0;
    if (iy < 0 && ui->yComboBox->count() > 1)
        iy = 1;
    else if (iy < 0)
        iy = ix;

    selectIndex(ui->xComboBox, ix);
    selectIndex(ui->yComboBox, iy);

    if (iz >= 0)
    {
        ui->zComboBox->setEnabled(true);
        selectIndex(ui->zComboBox, iz);
    }
    else
    {
        const QSignalBlocker blocker(ui->zComboBox);
        ui->zComboBox->clear();
        ui->zComboBox->addItem(QStringLiteral("二维结果，Z 分量为零"), QString());
        ui->zComboBox->setEnabled(false);
    }
    m_updatingControls = false;
}

vtkDataArray* VectorSettingWidget::arrayByName(vtkUnstructuredGrid* grid, const QString& name)
{
    if (!grid || name.isEmpty())
        return nullptr;
    return grid->GetPointData()->GetArray(name.toUtf8().constData());
}

bool VectorSettingWidget::buildVectorArraysFromSelections(vtkUnstructuredGrid* grid, double& magMin,
                                                          double& magMax)
{
    if (!grid)
        return false;

    const QString xName = ui->xComboBox->currentData().toString();
    const QString yName = ui->yComboBox->currentData().toString();
    const QString zName = ui->zComboBox->currentData().toString();

    vtkDataArray* arrX = arrayByName(grid, xName);
    vtkDataArray* arrY = arrayByName(grid, yName);
    vtkDataArray* arrZ = arrayByName(grid, zName);

    if (!arrX || !arrY)
    {
        qWarning() << "[Vector] 所选分量数组不存在。";
        return false;
    }

    const vtkIdType nPts = grid->GetNumberOfPoints();
    if (arrX->GetNumberOfTuples() < nPts || arrY->GetNumberOfTuples() < nPts ||
        (arrZ && arrZ->GetNumberOfTuples() < nPts))
    {
        qWarning() << "[Vector] 分量数组长度与点数不一致。";
        return false;
    }

    const bool packed3 = arrZ && xName == yName && xName == zName &&
                         arrX->GetNumberOfComponents() == 3;

    auto vec = vtkSmartPointer<vtkFloatArray>::New();
    vec->SetName("Vector");
    vec->SetNumberOfComponents(3);
    vec->SetNumberOfTuples(nPts);

    auto mag = vtkSmartPointer<vtkFloatArray>::New();
    mag->SetName("VectorMag");
    mag->SetNumberOfComponents(1);
    mag->SetNumberOfTuples(nPts);

    magMin = std::numeric_limits<double>::infinity();
    magMax = -std::numeric_limits<double>::infinity();

    if (packed3)
    {
        double t[3];
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            arrX->GetTuple(i, t);
            if (!std::isfinite(t[0]) || !std::isfinite(t[1]) || !std::isfinite(t[2]))
                t[0] = t[1] = t[2] = 0.0;
            vec->SetTuple3(i, static_cast<float>(t[0]), static_cast<float>(t[1]),
                           static_cast<float>(t[2]));
            const double m = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            mag->SetTuple1(i, static_cast<float>(m));
            if (m < magMin)
                magMin = m;
            if (m > magMax)
                magMax = m;
        }
    }
    else
    {
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            double x = arrX->GetComponent(i, 0);
            double y = arrY->GetComponent(i, 0);
            double z = arrZ ? arrZ->GetComponent(i, 0) : 0.0;
            if (!std::isfinite(x))
                x = 0.0;
            if (!std::isfinite(y))
                y = 0.0;
            if (!std::isfinite(z))
                z = 0.0;
            vec->SetTuple3(i, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            const double m = std::sqrt(x * x + y * y + z * z);
            mag->SetTuple1(i, static_cast<float>(m));
            if (m < magMin)
                magMin = m;
            if (m > magMax)
                magMax = m;
        }
    }

    auto* pd = grid->GetPointData();
    pd->SetVectors(vec);               // 朝向/长度基于 Vector
    pd->AddArray(mag);                 // 上色使用 VectorMag
    pd->SetActiveScalars("VectorMag"); // 不是必须，但有助于调试/检查

    return true;
}

void VectorSettingWidget::rebuildPipeline()
{
    if (!ensureDataLoaded())
        return;

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0 ||
        displayGrid->GetNumberOfPoints() <= 0)
    {
        clearPostDisplayContext();
        if (m_vectorActor && m_visualService)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")), false);
        m_vectorActor = nullptr;
        if (m_visualService)
        {
            m_visualService->showScalarBar(false, false);
            requestRender();
        }
        return;
    }

    // 0=自动：目标约 2000 个箭头；手动值仍表示“每隔 N 个点取 1 个”。
    m_maskOnRatio = ui->maskSizeSpinBox->value() == 0
                        ? computeAutoMaskRatio(displayGrid)
                        : ui->maskSizeSpinBox->value();
    m_maskOnRatio = std::max(1, m_maskOnRatio);
    ui->maskSizeSpinBox->setToolTip(
        ui->maskSizeSpinBox->value() == 0
            ? QStringLiteral("自动采样，当前间隔：%1").arg(m_maskOnRatio)
            : QStringLiteral("当前采样间隔：%1").arg(m_maskOnRatio));

    // 生成 Vector / VectorMag，并得到其范围
    double magMin = 0.0, magMax = 1.0;
    if (!buildVectorArraysFromSelections(displayGrid, magMin, magMax))
    {
        clearPostDisplayContext();
        if (m_vectorActor && m_visualService)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")), false);
        m_vectorActor = nullptr;
        if (m_visualService)
        {
            m_visualService->showScalarBar(false, false);
            requestRender();
        }
        return;
    }

    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(QStringLiteral("VectorMag"), magMin, magMax, false, false, true);

    auto nearlyEqual = [](double a, double b)
    {
        const double abs = 1e-15, rel = 1e-12;
        return std::fabs(a - b) <= std::max(abs, rel * std::max(std::fabs(a), std::fabs(b)));
    };

    // 移除旧 actor
    if (m_vectorActor)
    {
        if (m_visualService)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")), false);
        m_vectorActor = nullptr;
    }

    // 抽样
    auto mask = vtkSmartPointer<vtkMaskPoints>::New();
    mask->SetInputData(displayGrid);
    mask->SetOnRatio(m_maskOnRatio); // 每隔 N 个取 1 个
    mask->RandomModeOff();
    mask->Update();

    // 箭头源
    m_arrowSource = vtkSmartPointer<vtkArrowSource>::New();
    m_arrowSource->SetTipLength(0.35);
    m_arrowSource->SetTipRadius(0.08 * m_thicknessFactor);
    m_arrowSource->SetShaftRadius(0.02 * m_thicknessFactor);

    // Glyph：向量用于朝向；上色用 VectorMag
    m_glyph = vtkSmartPointer<vtkGlyph3D>::New();
#if VTK_MAJOR_VERSION >= 9
    m_glyph->SetInputConnection(mask->GetOutputPort());
    m_glyph->SetSourceConnection(m_arrowSource->GetOutputPort());
#else
    m_glyph->SetInputConnection(0, mask->GetOutputPort());
    m_glyph->SetSourceConnection(0, m_arrowSource->GetOutputPort());
#endif
    m_glyph->OrientOn();
    m_glyph->SetVectorModeToUseVector();
    m_glyph->SetScaleModeToDataScalingOff(); // 等长
    // 指定向量与标量数组
    m_glyph->SetInputArrayToProcess(1, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    "Vector"); // vectors
    m_glyph->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    "VectorMag"); // scalars

    {
        const double base = computeAutoBaseScale(displayGrid);
        m_glyph->SetScaleFactor(base * m_scaleFactor); // 自适应尺度 × 滑条倍率
    }
    m_glyph->Update();

    // Mapper：显式按 VectorMag 上色
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(m_glyph->GetOutputPort());
    mapper->ScalarVisibilityOn();
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray("VectorMag");
    mapper->SetColorModeToMapScalars();

    // 同步色标 LUT（与 VectorMag 一致）。公共“显示范围”对矢量模同样生效。
    double displayMin = magMin;
    double displayMax = magMax;
    resolvePostDisplayRange(QStringLiteral("VectorMag"), magMin, magMax, displayMin, displayMax);
    if (!(displayMax > displayMin) || nearlyEqual(displayMax, displayMin))
    {
        const double center = std::isfinite(displayMin) ? displayMin : 0.0;
        const double delta = std::max(std::abs(center) * 1.0e-6, 1.0e-9);
        displayMin = center - delta;
        displayMax = center + delta;
    }

    if (vtkSmartPointer<vtkScalarBarActor> scalarBar =
            m_visualService ? m_visualService->scalarBarActor() : nullptr)
    {
        vtkLookupTable* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
        if (!lut)
        {
            auto l = vtkSmartPointer<vtkLookupTable>::New();
            l->Build();
            lut = l;
            scalarBar->SetLookupTable(lut);
        }
        applyPostFieldLookupTable(lut, QStringLiteral("VectorMag"), displayMin, displayMax);
        mapper->SetLookupTable(lut);
        mapper->UseLookupTableScalarRangeOn();

        applyStandardPostScalarBar(scalarBar);
        const QString legendTitle = resolvedPostScalarBarTitle(vectorLegendTitle());
        scalarBar->SetTitle(legendTitle.toUtf8().constData());
        scalarBar->Modified();
        m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    }
    else
    {
        mapper->SetScalarRange(displayMin, displayMax);
    }

    // Actor
    m_vectorActor = vtkSmartPointer<vtkActor>::New();
    m_vectorActor->SetMapper(mapper);
    m_vectorActor->GetProperty()->SetRepresentationToSurface();

    if (m_visualService)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")),
                          m_vectorActor, QStringLiteral("矢量"), false);

    requestRender();
}

void VectorSettingWidget::requestRender() const
{
    if (!m_visualService)
        return;
    if (auto* renderWindow = m_visualService->renderWindow())
        renderWindow->Render();
}

void VectorSettingWidget::on_scaleSpinBox_valueChanged(double value)
{
    m_scaleFactor = qBound(0.1, value, 10.0);
    if (m_glyph)
    {
        vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
        const double base = computeAutoBaseScale(displayGrid);
        m_glyph->SetScaleFactor(base * m_scaleFactor);
        m_glyph->Update();
        requestRender();
    }
}

void VectorSettingWidget::on_thicknessSpinBox_valueChanged(double value)
{
    m_thicknessFactor = qBound(0.25, value, 4.0);
    if (m_arrowSource)
    {
        m_arrowSource->SetTipRadius(0.08 * m_thicknessFactor);
        m_arrowSource->SetShaftRadius(0.02 * m_thicknessFactor);
        m_arrowSource->Modified();
        if (m_glyph)
            m_glyph->Update();
        requestRender();
    }
}
