#include "VectorSettingWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkArrowSource.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkFloatArray.h>
#include <vtkGlyph3D.h>
#include <vtkLookupTable.h>
#include <vtkMaskPoints.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
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
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

VectorSettingWidget::VectorSettingWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::VectorSettingWidget), BasePage(PublicConstants::kPagePostProcessVector)
{
    ui->setupUi(this);
    ui->displayGroupBox->setVisible(false);

    // 在矢量设置顶部新增统一“矢量场”选择框，并在底部增加“按幅值缩放”选项；
    // 选择三分量矢量时自动隐藏冗余的 X/Y/Z 分量下拉框。
    m_vectorCombo = new QComboBox(ui->vectorGroupBox);
    m_scaleByMagCheck = new QCheckBox(QStringLiteral("按幅值缩放箭头"), ui->vectorGroupBox);
    m_vectorCombo->setObjectName(QStringLiteral("vectorFieldComboBox"));
    m_scaleByMagCheck->setObjectName(QStringLiteral("scaleByMagnitudeCheckBox"));
    m_scaleByMagCheck->setChecked(false);
    if (auto* gridLayout = ui->vectorGridLayout)
    {
        auto* vecLabel = new QLabel(QStringLiteral("矢量场"), ui->vectorGroupBox);
        vecLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        const int nextRow = gridLayout->rowCount();
        gridLayout->addWidget(vecLabel, nextRow, 0);
        gridLayout->addWidget(m_vectorCombo, nextRow, 1);
        gridLayout->addWidget(m_scaleByMagCheck, nextRow + 1, 0, 1, 2);
    }

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

    // 0 表示空间均匀自动采样；手动值表示有效点的采样间隔。
    ui->maskSizeSpinBox->setRange(0, 1000000);
    ui->maskSizeSpinBox->setSpecialValueText(QStringLiteral("自动"));
    ui->maskSizeSpinBox->setValue(0);

    const auto rebuildOnSelectionChanged = [this](int)
    {
        if (!m_updatingControls && m_grid)
            rebuildPipeline();
    };
    connect(m_vectorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (m_updatingControls)
                    return;
                const QString vecName = m_vectorCombo->currentData().toString();
                if (!vecName.isEmpty())
                {
                    m_updatingControls = true;
                    const int ix = ui->xComboBox->findData(vecName);
                    const int iy = ui->yComboBox->findData(vecName);
                    int iz = ui->zComboBox->findData(vecName);
                    if (iz < 0) {
                        ui->zComboBox->addItem(ResultDisplayNames::displayName(vecName), vecName);
                        iz = ui->zComboBox->count() - 1;
                    }
                    ui->zComboBox->setEnabled(true);
                    if (ix >= 0) ui->xComboBox->setCurrentIndex(ix);
                    if (iy >= 0) ui->yComboBox->setCurrentIndex(iy);
                    if (iz >= 0) ui->zComboBox->setCurrentIndex(iz);
                    m_updatingControls = false;
                }
                updateComponentRowsVisibility();
                if (m_grid)
                    rebuildPipeline();
            });
    connect(m_scaleByMagCheck, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                m_scaleByMagnitude = checked;
                if (!m_updatingControls && m_grid)
                    rebuildPipeline();
            });
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
        [this](const QVariantMap& data)
        {
            static const QStringList appearanceReasons{
                QStringLiteral("range"), QStringLiteral("color-map"), QStringLiteral("scalar-bar"),
                QStringLiteral("visibility"), QStringLiteral("representation"), QStringLiteral("contour"),
                QStringLiteral("surface-lighting"), QStringLiteral("surface-normals"),
                QStringLiteral("surface-outline"), QStringLiteral("scalar-interpolation"),
                QStringLiteral("threshold"), QStringLiteral("zero-display")};
            m_displayDirty = m_displayDirty || !appearanceReasons.contains(data.value(QStringLiteral("reason")).toString());
            if (!property("postDisplayActive").toBool() || !m_grid)
                return;
            const QSignalBlocker blocker(ui->showScalarBarCheckBox);
            ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            if(m_displayDirty || !m_vectorActor)rebuildPipeline();
            else {updateDisplayAppearance();requestRender();}
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
    return diag * 0.024; // 与空间均匀体素步长匹配，避免箭头过长相互穿插重叠
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
        title = velocityUnit ? QStringLiteral("|V|") : QStringLiteral("|U|");
    else if (key == QStringLiteral("VX") || key == QStringLiteral("V1") ||
             key == QStringLiteral("V") || key == QStringLiteral("VEL") ||
             key == QStringLiteral("VELOCITY") || key == QStringLiteral("VMAG"))
        title = QStringLiteral("|V|");
    else if (key == QStringLiteral("U") || key == QStringLiteral("DISP") ||
             key == QStringLiteral("DISPLACEMENT") || key == QStringLiteral("UMAG"))
        title = QStringLiteral("|U|");
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
    const QSignalBlocker legendBlocker(ui->showScalarBarCheckBox);
    setProperty("postDisplayActive", true);
    clearPostDisplayContext();
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    const QString proj =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    refreshResultGroups();
    if (m_dataPath.isEmpty())
        m_dataPath = PostResultAutoPrepare::firstResultDirectory(proj);
    if (m_grid) {
        postPublishResultFilterSource(m_grid);
        if (m_cachedFilterSignature != postMeshFilterSignature())
            m_displayDirty = true;
    }

    if (ui->stepComboBox->count() == 0)
    {
        // 首次进入：加载文件列表并默认读第一个
        loadFileName(m_dataPath);
    }
    else if (m_vectorActor && m_grid && !m_hasPendingState && !m_displayDirty)
    {
        // 筛选未变化时复用箭头，并恢复本页的色标和显示控制上下文。
        publishPostDisplayContext(QStringLiteral("VectorMag"), m_lastAutoMagMin,
                                  m_lastAutoMagMax, false, false, true);
        if (m_visualService)
        {
            addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")),
                              m_vectorActor, QStringLiteral("矢量"), false);
            updateDisplayAppearance();
            requestRender();
        }
    }
    else
    {
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
    object.insert(QStringLiteral("scaleByMagnitude"), m_scaleByMagnitude);
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
    if (m_scaleByMagCheck)
    {
        const QSignalBlocker blocker(m_scaleByMagCheck);
        m_scaleByMagnitude = state.value(QStringLiteral("scaleByMagnitude"), false).toBool();
        m_scaleByMagCheck->setChecked(m_scaleByMagnitude);
    }
    m_updatingControls = false;
    if (m_vectorCombo) {
        const QSignalBlocker blocker(m_vectorCombo);
        const QString name = ui->xComboBox->currentData().toString();
        const int index = name == ui->yComboBox->currentData().toString()
                && name == ui->zComboBox->currentData().toString()
            ? m_vectorCombo->findData(name) : -1;
        m_vectorCombo->setCurrentIndex(index > 0 ? index : 0);
    }
    updateComponentRowsVisibility();
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
    m_displayDirty = true;
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

    if (info.isDir() || ext == "txt" || ext == "dat" || ext == "tec" ||
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

    // 单元结果插值到节点；已有的节点位移/速度必须保留。
    if (grid->GetCellData()->GetNumberOfArrays() > 0)
    {
        auto convert = vtkSmartPointer<vtkCellDataToPointData>::New();
        convert->SetInputData(grid);
        convert->PassCellDataOn();
        convert->ProcessAllArraysOff();
        for (int i = 0; i < grid->GetCellData()->GetNumberOfArrays(); ++i) {
            const char* name = grid->GetCellData()->GetArrayName(i);
            if (name && !grid->GetPointData()->GetArray(name))
                convert->AddCellDataArray(name);
        }
        convert->Update();
        auto converted = vtkSmartPointer<vtkUnstructuredGrid>::New();
        converted->ShallowCopy(convert->GetOutput());
        if (converted && converted->GetNumberOfPoints() > 0)
            grid = converted;
    }

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

    if (m_vectorCombo)
    {
        const QSignalBlocker vb(m_vectorCombo);
        m_vectorCombo->clear();
        m_vectorCombo->addItem(QStringLiteral("由 X/Y/Z 分量组装"), QString());
        for (const QString& name : names)
        {
            vtkDataArray* arr = arrayByName(m_grid, name);
            if (arr && arr->GetNumberOfComponents() == 3)
                m_vectorCombo->addItem(ResultDisplayNames::displayName(name), name);
        }
    }

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

    // 未识别到任何速度/位移向量或分量时，不把温度、压力等无关标量硬拼为假矢量。
    if (ix < 0 || iy < 0)
    {
        const QSignalBlocker bx(ui->xComboBox);
        const QSignalBlocker by(ui->yComboBox);
        const QSignalBlocker bz(ui->zComboBox);
        ui->xComboBox->setCurrentIndex(-1);
        ui->yComboBox->setCurrentIndex(-1);
        ui->zComboBox->setCurrentIndex(-1);
        ui->zComboBox->setEnabled(true);
        if (m_vectorCombo)
        {
            const QSignalBlocker bv(m_vectorCombo);
            m_vectorCombo->setCurrentIndex(0);
        }
        m_updatingControls = false;
        updateComponentRowsVisibility();
        return;
    }

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
    if (m_vectorCombo)
    {
        const QSignalBlocker bv(m_vectorCombo);
        const QString xName = ui->xComboBox->currentData().toString();
        const int vecIdx = (!xName.isEmpty() && xName == ui->yComboBox->currentData().toString() &&
                            xName == ui->zComboBox->currentData().toString())
                               ? m_vectorCombo->findData(xName)
                               : -1;
        m_vectorCombo->setCurrentIndex(vecIdx > 0 ? vecIdx : 0);
    }
    m_updatingControls = false;
    updateComponentRowsVisibility();
}

void VectorSettingWidget::updateComponentRowsVisibility()
{
    const bool showManual = !m_vectorCombo || m_vectorCombo->currentData().toString().isEmpty();
    ui->xLabel->setVisible(showManual);
    ui->xComboBox->setVisible(showManual);
    ui->yLabel->setVisible(showManual);
    ui->yComboBox->setVisible(showManual);
    ui->zLabel->setVisible(showManual);
    ui->zComboBox->setVisible(showManual);
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

    // 0=空间均匀自动采样；手动值表示“每隔 N 个有效点取 1 个”。
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

    // 移除旧 actor
    if (m_vectorActor)
    {
        if (m_visualService)
            removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")), false);
        m_vectorActor = nullptr;
    }

    // 过滤零矢量（如无滑移壁面节点）并按均匀步长抽样，避免箭头扎堆在壁面或进出口局部。
    auto* vecArr = vtkFloatArray::SafeDownCast(displayGrid->GetPointData()->GetArray("Vector"));
    auto* magArr = vtkFloatArray::SafeDownCast(displayGrid->GetPointData()->GetArray("VectorMag"));
    std::vector<vtkIdType> activeIds;
    const vtkIdType totalPts = displayGrid->GetNumberOfPoints();
    activeIds.reserve(static_cast<size_t>(totalPts));
    if (magArr)
    {
        for (vtkIdType i = 0; i < totalPts; ++i)
        {
            const double m = magArr->GetComponent(i, 0);
            if (std::isfinite(m) && m > 0.0)
                activeIds.push_back(i);
        }
    }

    std::vector<vtkIdType> selectedIds;
    if (ui->maskSizeSpinBox->value() == 0)
    {
        // 自动模式：使用三维空间均匀体素抽样（每个体素只保留最靠近体素中心的一个有效点），
        // 避免四面体加密区扎堆、扫掠区稀疏和箭头严重重叠。
        double b[6];
        displayGrid->GetBounds(b);
        const double diag = std::hypot(std::hypot(b[1] - b[0], b[3] - b[2]), b[5] - b[4]);
        const double h = std::max(diag / 32.0, 1e-12);
        struct VoxelPick
        {
            vtkIdType pid = -1;
            double dist2 = std::numeric_limits<double>::infinity();
        };
        std::unordered_map<std::uint64_t, VoxelPick> voxels;
        voxels.reserve(4096);
        auto packKey = [](std::int64_t ix, std::int64_t iy, std::int64_t iz) -> std::uint64_t
        {
            const std::uint64_t ux = static_cast<std::uint64_t>(ix + (1LL << 20)) & 0x1FFFFFULL;
            const std::uint64_t uy = static_cast<std::uint64_t>(iy + (1LL << 20)) & 0x1FFFFFULL;
            const std::uint64_t uz = static_cast<std::uint64_t>(iz + (1LL << 20)) & 0x1FFFFFULL;
            return (ux << 42) | (uy << 21) | uz;
        };
        for (vtkIdType pid : activeIds)
        {
            double xyz[3];
            displayGrid->GetPoint(pid, xyz);
            const std::int64_t ix = static_cast<std::int64_t>(std::floor((xyz[0] - b[0]) / h));
            const std::int64_t iy = static_cast<std::int64_t>(std::floor((xyz[1] - b[2]) / h));
            const std::int64_t iz = static_cast<std::int64_t>(std::floor((xyz[2] - b[4]) / h));
            const double cx = b[0] + (ix + 0.5) * h;
            const double cy = b[2] + (iy + 0.5) * h;
            const double cz = b[4] + (iz + 0.5) * h;
            const double d2 = (xyz[0] - cx) * (xyz[0] - cx) +
                              (xyz[1] - cy) * (xyz[1] - cy) +
                              (xyz[2] - cz) * (xyz[2] - cz);
            auto& pick = voxels[packKey(ix, iy, iz)];
            if (pick.pid < 0 || d2 < pick.dist2)
            {
                pick.pid = pid;
                pick.dist2 = d2;
            }
        }
        selectedIds.reserve(voxels.size());
        for (const auto& kv : voxels)
            selectedIds.push_back(kv.second.pid);
        m_maskOnRatio = std::max(1, static_cast<int>(activeIds.size() / std::max<size_t>(1, selectedIds.size())));
        ui->maskSizeSpinBox->setToolTip(
            QStringLiteral("空间均匀自动采样，当前约 %1 个箭头").arg(selectedIds.size()));
    }
    else
    {
        const int stride = std::max(1, ui->maskSizeSpinBox->value());
        m_maskOnRatio = stride;
        selectedIds.reserve(activeIds.size() / static_cast<size_t>(stride) + 1);
        for (size_t k = 0; k < activeIds.size(); k += static_cast<size_t>(stride))
            selectedIds.push_back(activeIds[k]);
        ui->maskSizeSpinBox->setToolTip(
            QStringLiteral("当前采样间隔：%1（%2 个箭头）").arg(m_maskOnRatio).arg(selectedIds.size()));
    }

    std::sort(selectedIds.begin(), selectedIds.end());
    auto sampledPts = vtkSmartPointer<vtkPoints>::New();
    sampledPts->SetDataTypeToDouble();
    auto sampledVec = vtkSmartPointer<vtkFloatArray>::New();
    sampledVec->SetName("Vector");
    sampledVec->SetNumberOfComponents(3);
    auto sampledMag = vtkSmartPointer<vtkFloatArray>::New();
    sampledMag->SetName("VectorMag");
    sampledMag->SetNumberOfComponents(1);

    std::vector<double> sampledMags;
    sampledMags.reserve(selectedIds.size());
    for (vtkIdType pid : selectedIds)
    {
        double xyz[3];
        displayGrid->GetPoint(pid, xyz);
        sampledPts->InsertNextPoint(xyz);
        double v[3] = {0.0, 0.0, 0.0};
        if (vecArr) vecArr->GetTuple(pid, v);
        const double m = magArr ? magArr->GetComponent(pid, 0)
                                : std::hypot(std::hypot(v[0], v[1]), v[2]);
        sampledVec->InsertNextTuple3(static_cast<float>(v[0]), static_cast<float>(v[1]),
                                     static_cast<float>(v[2]));
        sampledMag->InsertNextTuple1(static_cast<float>(m));
        sampledMags.push_back(m);
    }

    auto sampledPoly = vtkSmartPointer<vtkPolyData>::New();
    sampledPoly->SetPoints(sampledPts);
    sampledPoly->GetPointData()->SetVectors(sampledVec);
    sampledPoly->GetPointData()->AddArray(sampledMag);
    sampledPoly->GetPointData()->SetActiveScalars("VectorMag");

    // 基于空间均匀采样后的实际箭头分布计算自动色标范围；当极少数角点异常尖峰
    // 远超 95% 分位时，采用 P95 作为默认上限，呈现完整清晰的色彩层次（右侧“数据范围”仍可手动调整）。
    if (!sampledMags.empty())
    {
        std::sort(sampledMags.begin(), sampledMags.end());
        magMin = sampledMags.front();
        const double rawMax = sampledMags.back();
        const double p95 = sampledMags[static_cast<size_t>((sampledMags.size() - 1) * 0.95)];
        magMax = (p95 > 1e-12 && rawMax > p95 * 2.5) ? p95 : rawMax;
    }
    m_lastAutoMagMax = magMax;
    m_lastAutoMagMin = magMin;

    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(QStringLiteral("VectorMag"), magMin, magMax, false, false, true);

    // 箭头源：比例更利落，减少细杆锯齿
    m_arrowSource = vtkSmartPointer<vtkArrowSource>::New();
    m_arrowSource->SetTipResolution(12);
    m_arrowSource->SetShaftResolution(12);
    m_arrowSource->SetTipLength(0.32);
    m_arrowSource->SetTipRadius(0.095 * m_thicknessFactor);
    m_arrowSource->SetShaftRadius(0.028 * m_thicknessFactor);

    // Glyph：向量用于朝向；上色用 VectorMag
    m_glyph = vtkSmartPointer<vtkGlyph3D>::New();
#if VTK_MAJOR_VERSION >= 9
    m_glyph->SetInputData(sampledPoly);
    m_glyph->SetSourceConnection(m_arrowSource->GetOutputPort());
#else
    m_glyph->SetInputData(0, sampledPoly);
    m_glyph->SetSourceConnection(0, m_arrowSource->GetOutputPort());
#endif
    m_glyph->OrientOn();
    m_glyph->SetVectorModeToUseVector();
    if (m_scaleByMagnitude)
        m_glyph->SetScaleModeToScaleByVector();
    else
        m_glyph->SetScaleModeToDataScalingOff();
    m_glyph->SetColorModeToColorByScalar();
    m_glyph->SetInputArrayToProcess(1, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "Vector");
    m_glyph->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "VectorMag");

    {
        const double base = computeAutoBaseScale(displayGrid);
        const double magNorm = (m_scaleByMagnitude && magMax > 1e-12) ? (1.0 / magMax) : 1.0;
        m_glyph->SetScaleFactor(base * m_scaleFactor * magNorm);
    }
    m_glyph->Update();

    // Mapper：显式按 VectorMag 上色
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(m_glyph->GetOutputPort());
    mapper->ScalarVisibilityOn();
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray("VectorMag");
    mapper->SetColorModeToMapScalars();

    // Actor
    m_vectorActor = vtkSmartPointer<vtkActor>::New();
    m_vectorActor->SetMapper(mapper);
    m_vectorActor->GetProperty()->SetRepresentationToSurface();
    updateDisplayAppearance();

    if (m_visualService)
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("vector")),
                          m_vectorActor, QStringLiteral("矢量"), false);

    m_displayDirty = false;
    m_cachedFilterSignature = postMeshFilterSignature();
    requestRender();
}

void VectorSettingWidget::updateDisplayAppearance()
{
    auto* mapper=m_vectorActor?vtkPolyDataMapper::SafeDownCast(m_vectorActor->GetMapper()):nullptr;
    if(!mapper)return;
    // 同步色标 LUT（与 VectorMag 一致）。公共“显示范围”对矢量模同样生效。
    auto nearlyEqual = [](double a, double b)
    {
        const double abs = 1e-15, rel = 1e-12;
        return std::fabs(a - b) <= std::max(abs, rel * std::max(std::fabs(a), std::fabs(b)));
    };
    double displayMin = m_lastAutoMagMin;
    double displayMax = m_lastAutoMagMax;
    resolvePostDisplayRange(QStringLiteral("VectorMag"), m_lastAutoMagMin, m_lastAutoMagMax, displayMin, displayMax);
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
        // Keep this page's colors independent of other result pages' shared legend.
        auto* lut = vtkLookupTable::SafeDownCast(mapper->GetLookupTable());
        if(!lut) {
            auto newLut=vtkSmartPointer<vtkLookupTable>::New();mapper->SetLookupTable(newLut);lut=newLut;
        }
        applyPostFieldLookupTable(lut, QStringLiteral("VectorMag"), displayMin, displayMax);
        scalarBar->SetLookupTable(lut);
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
        const double magNorm = (m_scaleByMagnitude && m_lastAutoMagMax > 1e-12)
                                   ? (1.0 / m_lastAutoMagMax) : 1.0;
        m_glyph->SetScaleFactor(base * m_scaleFactor * magNorm);
        m_glyph->Update();
        requestRender();
    }
}

void VectorSettingWidget::on_thicknessSpinBox_valueChanged(double value)
{
    m_thicknessFactor = qBound(0.25, value, 4.0);
    if (m_arrowSource)
    {
        m_arrowSource->SetTipRadius(0.095 * m_thicknessFactor);
        m_arrowSource->SetShaftRadius(0.028 * m_thicknessFactor);
        m_arrowSource->Modified();
        if (m_glyph)
            m_glyph->Update();
        requestRender();
    }
}
