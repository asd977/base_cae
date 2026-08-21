#include "NephogramSettingWidget.h"
#include "PostComboBoxPerformance.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCamera.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkContourFilter.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkGeometryFilter.h>
#include <vtkExtractCells.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkDoubleArray.h>
#include <vtkLabeledDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkStripper.h>
#include <vtkTecplotReader.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <QByteArray>
#include <QCheckBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "ResultDisplayNames.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ui_NephogramSettingWidget.h"

namespace
{
bool isValidScalarRange(double minV, double maxV)
{
    return std::isfinite(minV) && std::isfinite(maxV) && minV <= maxV;
}

bool isValidScalarRange(const double range[2])
{
    return range && isValidScalarRange(range[0], range[1]);
}

QString normalizedResultFilePath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool sameResultFile(const QString& left, const QString& right)
{
    if (left.trimmed().isEmpty() || right.trimmed().isEmpty())
        return false;
    return normalizedResultFilePath(left).compare(
               normalizedResultFilePath(right), Qt::CaseInsensitive) == 0;
}

bool scalarArrayMatchesSurface(vtkPolyData* surface, const QString& scalarName,
                               bool usePointData)
{
    if (!surface || scalarName.isEmpty())
        return false;

    const QByteArray name = scalarName.toUtf8();
    vtkDataArray* array = usePointData
                              ? (surface->GetPointData()
                                     ? surface->GetPointData()->GetArray(name.constData())
                                     : nullptr)
                              : (surface->GetCellData()
                                     ? surface->GetCellData()->GetArray(name.constData())
                                     : nullptr);
    if (!array)
        return false;

    const vtkIdType expected = usePointData ? surface->GetNumberOfPoints()
                                             : surface->GetNumberOfCells();
    return expected > 0 && array->GetNumberOfTuples() == expected;
}




} // namespace

NephogramSettingWidget::NephogramSettingWidget(VisualWidgetService* postProcessingWidget,
                                               QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessNephogram), ui(new Ui::NephogramSettingWidget)
{
    ui->setupUi(this);
    // 色标、等值线和显示范围统一由“结果显示”窗口控制。
    ui->displayGroupBox->setVisible(false);

    configurePostResultCombo(ui->analysisComboBox);
    configurePostResultCombo(ui->resultDirComboBox);
    configurePostResultCombo(ui->resultTimeComboBox);
    configurePostResultCombo(ui->stepComboBox);
    m_visualService = postProcessingWidget;

    m_actor = vtkSmartPointer<vtkActor>::New();
    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_contourActor = vtkSmartPointer<vtkActor>::New();
    m_contourLabelActor = vtkSmartPointer<vtkActor2D>::New();

    ui->rangeMinSpinBox->setRange(-1.0e100, 1.0e100);
    ui->rangeMaxSpinBox->setRange(-1.0e100, 1.0e100);

    ui->rangeMinSpinBox->setDecimals(8);
    ui->rangeMaxSpinBox->setDecimals(8);

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_stepComboBox_currentIndexChanged);
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->analysisComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_analysisComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_resultDirComboBox_currentIndexChanged);
    connect(ui->resultTimeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_resultTimeComboBox_currentIndexChanged);

    connect(ui->autoRangeCheckBox, &QCheckBox::toggled, this, [this](bool) {
        updateRangeEditorState();
        refreshCurrentVisualization();
    });
    connect(ui->rangeMinSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double value) {
                if (m_updatingRangeControls || ui->autoRangeCheckBox->isChecked())
                    return;
                if (value >= ui->rangeMaxSpinBox->value())
                {
                    const double delta = std::max(std::abs(value) * 1.0e-6, 1.0e-9);
                    const QSignalBlocker blocker(ui->rangeMaxSpinBox);
                    ui->rangeMaxSpinBox->setValue(value + delta);
                }
                refreshCurrentVisualization();
            });
    connect(ui->rangeMaxSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double value) {
                if (m_updatingRangeControls || ui->autoRangeCheckBox->isChecked())
                    return;
                if (value <= ui->rangeMinSpinBox->value())
                {
                    const double delta = std::max(std::abs(value) * 1.0e-6, 1.0e-9);
                    const QSignalBlocker blocker(ui->rangeMinSpinBox);
                    ui->rangeMinSpinBox->setValue(value - delta);
                }
                refreshCurrentVisualization();
            });

    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    connect(ui->showScalarBarCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        postScalarBarStyle().visible = checked;
        if (m_visualService)
        {
            m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
            m_visualService->render();
        }
    });
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);
    connect(ui->showContourCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        postContourStyle().enabled = checked;
        updateNephogram(currentScalarName());
    });
    connect(ui->displaySettingsButton, &QPushButton::clicked, this, [this]() {
        double dataMin = 0.0;
        double dataMax = 1.0;
        if (!currentScalarName().isEmpty() && m_grid)
        {
            bool usePointData = true;
            vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
            vtkDataArray* array = findScalarArray(displayGrid, currentScalarName(), usePointData);
            if (array)
            {
                double range[2]{0.0, 1.0};
                array->GetRange(range);
                dataMin = range[0];
                dataMax = range[1];
            }
        }
        if (editPostDisplaySettings(this, m_visualService, true, true,
                                    currentScalarName(), dataMin, dataMax))
        {
            ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
            ui->showContourCheckBox->setChecked(postContourStyle().enabled);
            refreshCurrentVisualization();
        }
    });

    EventManager::instance().registerHandler(
        postDisplaySettingsChangedEvent(), this,
        [this](const QVariantMap& data)
        {
            if (!property("postDisplayActive").toBool())
                return;
            const QString reason = data.value(QStringLiteral("reason")).toString();
            if (reason == QStringLiteral("mesh-filter") ||
                reason == QStringLiteral("domain-filter"))
            {
                invalidateDisplaySurface();
                // 后处理只响应结果自身的 MATYPE/LAYER；这里仅重新隔离场景，
                // 不再读取 Mesh 当前的 KETYPE/拓扑筛选状态。
                if (m_visualService)
                    showPostProcessingOnly(m_visualService, false);
            }
            {
                const QSignalBlocker scalarBarBlocker(ui->showScalarBarCheckBox);
                const QSignalBlocker contourBlocker(ui->showContourCheckBox);
                ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
                ui->showContourCheckBox->setChecked(postContourStyle().enabled);
            }
            refreshCurrentVisualization();
        });

    // 全局透明度改变时，普通不透明云图与透明域界面显示之间切换。
    // 不修改结果网格，也不参与材料/分层筛选。
    EventManager::instance().registerHandler(
        QStringLiteral("visualization.opacity.changed"), this,
        [this](const QVariantMap&)
        {
            if (!property("postDisplayActive").toBool())
                return;

            // 不透明首帧不需要域界面，避免工程首次进入后处理时遍历全部体单元。
            // 第一次真正进入半透明状态时只同步准备一次；后续拖动只切换 Mapper
            // 输入和修改 Actor 透明度，不再重跑结果筛选、色标和等值线管线。
            // 以当前云图 Actor 的实际透明度为准，而不是只看全局滑块值。
            // 这样 Actor 自身基础透明度不为 1.0 时也不会选错 Mapper 输入。
            const double opacity = m_actor && m_actor->GetProperty()
                                       ? m_actor->GetProperty()->GetOpacity()
                                       : 1.0;
            const bool needsDomainSurface =
                opacity < 0.999 && !postMeshFilterReport().customFilter;
            if (needsDomainSurface)
                ensureTransparentDomainSurface();

            const bool surfacePrepared =
                m_transparentDomainSurface &&
                m_transparentDomainSurface->GetNumberOfCells() > 0;
            const bool wantsDomainSurface =
                opacity < 0.999 && !postMeshFilterReport().customFilter &&
                surfacePrepared;
            if (wantsDomainSurface == m_usingTransparentDomainSurface)
                return;

            vtkDataSet* mapperInput = wantsDomainSurface
                                          ? static_cast<vtkDataSet*>(
                                                m_transparentDomainSurface.GetPointer())
                                          : static_cast<vtkDataSet*>(m_grid.GetPointer());
            if (!m_mapper || !mapperInput)
                return;

            m_mapper->SetInputData(mapperInput);
            m_mapper->Modified();
            m_usingTransparentDomainSurface = wantsDomainSurface;

            // 发送透明度事件的 VisualizationWidget 会统一 Render；这里不单独绘制。
        });

    updateRangeEditorState();
    // 工程恢复阶段只创建页面，不在构造函数里扫描结果目录。
    // 首次真正进入页面或恢复该页 JSON 状态时再初始化结果选项。
}

NephogramSettingWidget::~NephogramSettingWidget()
{
    delete ui;
}

void NephogramSettingWidget::ensureActorAttached()
{
    if (!m_visualService || !m_actor)
    {
        m_actorAttached = false;
        return;
    }

    const QString actorId = postActorId(getUuid(), QStringLiteral("surface"));
    if (m_visualService->getProp(actorId) == m_actor.GetPointer())
    {
        m_actorAttached = true;
        return;
    }

    // 挂载只更新注册表和透明渲染状态；当前调用链会在 Mapper/等值线全部就绪后
    // 统一 Render，避免透明模型在一次页面切换中连续绘制两帧。
    m_actorAttached = addActorByService(m_visualService, actorId, m_actor,
                                        QStringLiteral("云图"), false);
}

bool NephogramSettingWidget::ensureTransparentDomainSurface()
{
    if (m_transparentDomainSurface &&
        m_transparentDomainSurface->GetNumberOfCells() > 0)
        return true;
    if (!m_grid || m_grid->GetNumberOfCells() <= 0)
        return false;

    QElapsedTimer domainTimer;
    domainTimer.start();
    vtkSmartPointer<vtkPolyData> surface =
        postBuildDomainAwareDisplaySurface(m_grid, m_visualService);
    if (!surface || surface->GetNumberOfCells() <= 0)
        return false;

    m_transparentDomainSurface = surface;
    const qint64 domainMs = domainTimer.elapsed();
    if (domainMs >= 100)
        qInfo() << "[PostPerf] transparent domain surface prepare"
                << domainMs << "ms"
                << "volumeCells=" << m_grid->GetNumberOfCells()
                << "surfaceCells=" << surface->GetNumberOfCells();
    return true;
}

void NephogramSettingWidget::syncScalarBarFromCurrent()
{
    if (m_currentArrayName.isEmpty() || !m_visualService || !m_grid)
        return;

    // 普通云图直接使用完整/筛选后的 vtkUnstructuredGrid。
    // 材料或分层被隐藏后，vtkDataSetMapper 会自然显示剩余体单元的新外边界，
    // 不需要 domains.vtp，也不需要每次先把体网格转换成 PolyData。
    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        if (m_mapper)
        {
            m_mapper->SetInputData(displayGrid);
            m_mapper->ScalarVisibilityOff();
        }
        m_visualService->showScalarBar(false, false);
        clearContourOverlay();
        return;
    }

    bool usePointData = true;
    vtkDataArray* arr = findScalarArray(displayGrid, m_currentArrayName, usePointData);
    if (!arr || arr->GetNumberOfTuples() <= 0)
    {
        clearPostDisplayContext();
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        m_visualService->showScalarBar(false, false);
        clearContourOverlay();
        return;
    }

    double dataRange[2]{0.0, 1.0};
    arr->GetRange(dataRange);
    if (!syncScalarBarFromResolved(displayGrid, arr, usePointData,
                                   dataRange[0], dataRange[1]))
        return;

    m_mapper->SetInputData(displayGrid);
    m_actor->SetMapper(m_mapper);
    double minV = dataRange[0];
    double maxV = dataRange[1];
    resolvePostDisplayRange(m_currentArrayName, dataRange[0], dataRange[1], minV, maxV);
    updateContourOverlay(m_currentArrayName, usePointData, minV, maxV);
}

bool NephogramSettingWidget::syncScalarBarFromResolved(vtkUnstructuredGrid* displayGrid,
                                                        vtkDataArray* arr,
                                                        bool usePointData,
                                                        double dataMin,
                                                        double dataMax)
{
    if (!arr || !m_visualService || m_currentArrayName.isEmpty() ||
        !isValidScalarRange(dataMin, dataMax))
    {
        clearPostDisplayContext();
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        clearContourOverlay();
        return false;
    }

    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(m_currentArrayName, dataMin, dataMax, true, true);

    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(m_currentArrayName,
                                                              dataMin, dataMax);
        const QSignalBlocker autoBlocker(ui->autoRangeCheckBox);
        const QSignalBlocker minBlocker(ui->rangeMinSpinBox);
        const QSignalBlocker maxBlocker(ui->rangeMaxSpinBox);
        ui->autoRangeCheckBox->setChecked(rangeStyle.automatic);
        ui->rangeMinSpinBox->setValue(rangeStyle.initialized ? rangeStyle.minimum : dataMin);
        ui->rangeMaxSpinBox->setValue(rangeStyle.initialized ? rangeStyle.maximum : dataMax);
    }

    double minV = dataMin;
    double maxV = dataMax;
    resolvePostDisplayRange(m_currentArrayName, dataMin, dataMax, minV, maxV);

    vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor();
    if (!scalarBar)
        return false;

    postScalarBarStyle().visible = ui->showScalarBarCheckBox->isChecked();
    m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    applyLegendAppearance(scalarBar, minV, maxV);

    vtkLookupTable* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
    if (!lut)
    {
        auto newLut = vtkSmartPointer<vtkLookupTable>::New();
        scalarBar->SetLookupTable(newLut);
        lut = newLut;
    }

    updateLookupTableRange(lut, minV, maxV);
    const QString legendTitle = resolvedPostScalarBarTitle(currentScalarDisplayName());
    scalarBar->SetTitle(legendTitle.toUtf8().constData());
    scalarBar->Modified();

    // Mapper 的输入由 updateNephogram 统一切到域感知显示面；
    // 这里仅更新 LUT/标量模式，不再决定几何输入。
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        m_mapper->ScalarVisibilityOff();
        m_visualService->showScalarBar(false, false);
        return false;
    }
    configureMapper(m_mapper, m_currentArrayName, usePointData, minV, maxV, lut);
    return true;
}

void NephogramSettingWidget::beforeLoad()
{
    setProperty("postDisplayActive", true);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);

    if (!m_visualService)
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (!m_visualService)
    {
        qWarning() << "Visual service is null in NephogramSettingWidget.";
        return;
    }

    // 首次进入后处理时必须先拿到可视化服务，再隐藏几何/网格 Actor。
    // 否则原网格 Surface 和云图会共面叠加，出现灰色块、闪烁或局部遮挡。
    showPostProcessingOnly(m_visualService, false);

    // 色标拖动由通用“结果显示”控制器统一安装，不再由云图页面独占。

    // 后处理云图优先启用 FXAA，主要改善等值线和模型轮廓的像素锯齿。
    if (auto* ren = m_visualService->renderer())
        ren->UseFXAAOn();

    // 在重新解析/刷新云图之前先挂载 Actor，使 VisualizationWidget 先按当前
    // 全局透明度设置实际 Opacity。这样离开页面期间改过透明度，回来时也能
    // 立即选择正确的体网格或透明域界面 Mapper，不依赖已经错过的事件。
    ensureActorAttached();

    if (m_resultOptionsPreparedByLoadJson)
    {
        // applyDeferredLoadJson() 刚刚已经完成了一次结果目录初始化，
        // beforeLoad() 不再紧接着重复扫描同一目录。
        m_resultOptionsPreparedByLoadJson = false;
    }
    else
    {
        initResultDirectoryOptions();
    }
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);

    if (ui->stepComboBox->currentIndex() == -1)
        m_initialized = false;

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

    // loadFileName() 在有有效结果时已经通过 updateNephogram() 完成 Mapper、
    // 色标和一次 Render。页面重入时不再重复渲染同一帧。
    if (m_grid && !currentScalarName().isEmpty())
    {
        ensureActorAttached();
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
    setPostViewActive(false);
    setProperty("postDisplayActive", false);
    clearPostDisplayContext();
    if (!m_visualService)
        return;

    clearContourOverlay();

    if (m_actorAttached)
    {
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("surface")), false);
        m_actorAttached = false;
    }

    m_visualService->showScalarBar(false, false);

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void NephogramSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    if (index < 0)
        return;

    const QString filePath = ui->stepComboBox->itemData(index).toString();
    if (filePath.isEmpty())
    {
        qWarning() << "No data file mapped for result selection.";
        return;
    }

    // 同一个结果文件在页面重入、目录下拉重新填充时不要重复读取。
    // m_grid 和 m_displaySurface 会跨 beforeUnload()/beforeLoad() 保留。
    if (m_grid && sameResultFile(filePath, m_loadedFileName))
    {
        if (!m_loadingState && !currentScalarName().isEmpty())
            updateNephogram(currentScalarName());
        return;
    }

    if (QFileInfo(filePath).suffix().compare(QStringLiteral("vtu"),
                                              Qt::CaseInsensitive) == 0)
    {
        readVtu(filePath);
        return;
    }
    readTecplot(filePath);
}

void NephogramSettingWidget::loadFileName(const QString& dataPath)
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
    {
        ui->scalarComboBox->clear();
        qWarning() << "No prepared post-processing results found in" << dataPath;
    }
}


void NephogramSettingWidget::on_analysisComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousDir = ui->resultDirComboBox->currentData().toString();
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, projectDir,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        previousDir);
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);
}

void NephogramSettingWidget::on_resultDirComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);
}

void NephogramSettingWidget::on_resultTimeComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
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

void NephogramSettingWidget::on_scalarComboBox_currentIndexChanged(int)
{
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox);
    PostResultSelectionUtils::rememberBasicSelection(ui->analysisComboBox, ui->resultDirComboBox,
                                                     ui->resultTimeComboBox, ui->stepComboBox);
    updateNephogram(currentScalarName());
}

void NephogramSettingWidget::readTecplot(const QString& fileName)
{
    QElapsedTimer loadTimer;
    loadTimer.start();
    invalidateDisplaySurface();
    m_transparentDomainSurface = nullptr;
    m_usingTransparentDomainSurface = false;
    m_grid = nullptr;
    m_loadedFileName = fileName;

    // Tecplot/VTU 统一读取成标准 vtkUnstructuredGrid。新内部结果优先是二进制 VTU；
    // 旧 Tecplot 仍由 PostTecplotUtils 兼容。普通云图不再读取任何 Surface 旁路缓存。
    QString gridError;
    m_grid = PostTecplotUtils::readGrid(fileName, &gridError);
    const qint64 gridReadMs = loadTimer.elapsed();
    if (gridReadMs >= 100)
        qInfo() << "[PostPerf] result grid read" << gridReadMs << "ms" << fileName;
    if (!gridError.isEmpty())
        qWarning().noquote() << gridError;
    if (!m_grid)
    {
        ui->scalarComboBox->clear();
        return;
    }

    QStringList scalarNames;
    auto collectArrays = [&](vtkPointData* pointData, vtkCellData* cellData)
    {
        if (pointData)
        {
            const int n = pointData->GetNumberOfArrays();
            scalarNames.reserve(scalarNames.size() + n);
            for (int i = 0; i < n; ++i)
            {
                const char* name = pointData->GetArrayName(i);
                if (name && *name)
                {
                    const QString qName = QString::fromUtf8(name);
                    if (!ResultDisplayNames::isInternalField(qName) && !scalarNames.contains(qName))
                        scalarNames << qName;
                }
            }
        }
        if (cellData)
        {
            const int n = cellData->GetNumberOfArrays();
            scalarNames.reserve(scalarNames.size() + n);
            for (int i = 0; i < n; ++i)
            {
                const char* name = cellData->GetArrayName(i);
                if (name && *name)
                {
                    const QString qName = QString::fromUtf8(name);
                    if (!ResultDisplayNames::isInternalField(qName) && !scalarNames.contains(qName))
                        scalarNames << qName;
                }
            }
        }
    };
    collectArrays(m_grid->GetPointData(), m_grid->GetCellData());

    const QString previousScalar = currentScalarName();
    const QString preferredScalar = !PostResultSelectionUtils::sharedResultSelection().fieldName.isEmpty()
                                        ? PostResultSelectionUtils::sharedResultSelection().fieldName
                                        : previousScalar;
    ResultDisplayNames::populateCombo(ui->scalarComboBox, scalarNames, preferredScalar);
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox);
    updateNephogram(currentScalarName());
}

void NephogramSettingWidget::updateNephogram(const QString& selectedScalarName,
                                             bool renderNow)
{
    if (m_loadingState || selectedScalarName.isEmpty() || m_loadedFileName.isEmpty())
        return;

    if (!m_grid)
    {
        QString gridError;
        QElapsedTimer loadTimer;
        loadTimer.start();
        m_grid = PostTecplotUtils::readGrid(m_loadedFileName, &gridError);
        const qint64 loadMs = loadTimer.elapsed();
        if (loadMs >= 100)
            qInfo() << "[PostPerf] result grid read" << loadMs << "ms" << m_loadedFileName;
        if (!gridError.isEmpty())
            qWarning().noquote() << gridError;
    }

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        invalidateDisplaySurface();
        clearContourOverlay();
        if (m_mapper)
        {
            m_mapper->SetInputData(displayGrid);
            m_mapper->ScalarVisibilityOff();
        }
        if (m_visualService)
        {
            m_visualService->showScalarBar(false, false);
            m_visualService->render();
        }
        return;
    }

    bool usePointData = true;
    vtkDataArray* scalarArray = findScalarArray(displayGrid, selectedScalarName, usePointData);
    if (!scalarArray || scalarArray->GetNumberOfTuples() <= 0)
    {
        qWarning() << "Scalar array not found:" << selectedScalarName;
        clearPostDisplayContext();
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        clearContourOverlay();
        return;
    }

    m_currentArrayName = selectedScalarName;

    double dataRange[2];
    scalarArray->GetRange(dataRange);
    if (!isValidScalarRange(dataRange))
    {
        qWarning() << "Scalar array has no valid range:" << selectedScalarName;
        clearPostDisplayContext();
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }

    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(selectedScalarName, dataRange[0], dataRange[1], true, true);

    PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(selectedScalarName,
                                                          dataRange[0], dataRange[1]);
    {
        const QSignalBlocker autoBlocker(ui->autoRangeCheckBox);
        const QSignalBlocker minBlocker(ui->rangeMinSpinBox);
        const QSignalBlocker maxBlocker(ui->rangeMaxSpinBox);
        ui->autoRangeCheckBox->setChecked(rangeStyle.automatic);
        ui->rangeMinSpinBox->setValue(rangeStyle.initialized ? rangeStyle.minimum : dataRange[0]);
        ui->rangeMaxSpinBox->setValue(rangeStyle.initialized ? rangeStyle.maximum : dataRange[1]);
    }
    double minV = dataRange[0];
    double maxV = dataRange[1];
    resolvePostDisplayRange(selectedScalarName, dataRange[0], dataRange[1], minV, maxV);

    if (!syncScalarBarFromResolved(displayGrid, scalarArray, usePointData,
                                   dataRange[0], dataRange[1]))
        return;

    // 普通云图直接显示筛选后的完整 vtkUnstructuredGrid。
    // 这是 VTK/商软的常规路径，也避免把退化单元的内部三角化面误当成结果面。
    // 只有“未做材料/分层筛选 + 当前为半透明”时，才额外使用域界面 Surface，
    // 以满足透明状态能够看到内部材料/分层云图的需求。
    bool mapperUsePointData = usePointData;
    vtkDataSet* mapperInput = displayGrid;
    const bool customDomainFilter = postMeshFilterReport().customFilter;

    // 新 Actor 挂入注册表时，可视化服务才会乘上当前全局透明度。
    // 这里不渲染，只为后续按“实际有效透明度”选择正确 Mapper 输入。
    ensureActorAttached();
    const bool translucent = m_actor && m_actor->GetProperty() &&
                             m_actor->GetProperty()->GetOpacity() < 0.999;
    m_usingTransparentDomainSurface = false;

    if (translucent && !customDomainFilter)
    {
        ensureTransparentDomainSurface();
        if (m_transparentDomainSurface &&
            m_transparentDomainSurface->GetNumberOfCells() > 0 &&
            scalarArrayMatchesSurface(m_transparentDomainSurface,
                                      selectedScalarName, mapperUsePointData))
        {
            mapperInput = m_transparentDomainSurface;
            m_usingTransparentDomainSurface = true;
        }
    }

    // 材料/分层筛选时始终直接显示筛选后的 vtkUnstructuredGrid；
    // 不会因为透明模式去展开内部域界面。等值线 Surface 使用独立缓存。

    m_mapper->SetInputData(mapperInput);
    configureMapper(m_mapper, selectedScalarName, mapperUsePointData,
                    minV, maxV,
                    vtkLookupTable::SafeDownCast(
                        m_visualService->scalarBarActor()->GetLookupTable()));
    m_actor->SetMapper(m_mapper);
    auto* surfaceProperty = m_actor->GetProperty();
    applyPostActorRepresentation(m_actor);
    surfaceProperty->SetInterpolationToFlat();
    surfaceProperty->LightingOff();
    surfaceProperty->SetAmbient(1.0);
    surfaceProperty->SetDiffuse(0.0);
    surfaceProperty->SetSpecular(0.0);
    ensureActorAttached();

    // 等值线明确需要一张 PolyData 表面时才在这里按需提取并缓存。
    updateContourOverlay(selectedScalarName, usePointData, minV, maxV);

    if (m_initialized && renderNow)
    {
        if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
        {
            QElapsedTimer renderTimer;
            renderTimer.start();
            rw->Render();
            const qint64 renderMs = renderTimer.elapsed();
            if (renderMs >= 100)
                qInfo() << "[PostPerf] cloud render" << renderMs << "ms";
        }
    }
}

void NephogramSettingWidget::initResultDirectoryOptions()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);
    QString previousAnalysis = ui->analysisComboBox->currentData().toString();
    QString previousDir = ui->resultDirComboBox->currentData().toString();
    const auto& sharedSelection = PostResultSelectionUtils::sharedResultSelection();
    if (previousAnalysis.isEmpty())
        previousAnalysis = sharedSelection.analysisKey;
    if (previousDir.isEmpty())
        previousDir = sharedSelection.resultDir;
    if (previousAnalysis.isEmpty() && !previousDir.isEmpty())
        previousAnalysis =
            PostResultSelectionUtils::analysisKeyForSource(groups, previousDir);

    PostResultSelectionUtils::populateAnalysisCombo(
        ui->analysisComboBox, groups, previousAnalysis);
    PostResultSelectionUtils::populateStageCombo(
        ui->resultDirComboBox, groups,
        PostResultSelectionUtils::selectedAnalysisKey(ui->analysisComboBox),
        previousDir);
}


QString NephogramSettingWidget::selectedResultDirectoryPath() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    const QString dirName = ui->resultDirComboBox->currentData().toString();

    if (dirName.isEmpty())
        return projectDir;

    if (QDir::isAbsolutePath(dirName))
        return QDir(dirName).absolutePath();
    return QDir(projectDir).absoluteFilePath(dirName);
}

QString NephogramSettingWidget::currentScalarName() const
{
    return ResultDisplayNames::internalName(ui->scalarComboBox);
}

QString NephogramSettingWidget::currentScalarDisplayName() const
{
    return ResultDisplayNames::scalarBarName(currentScalarName());
}

void NephogramSettingWidget::refreshCurrentVisualization(bool renderNow)
{
    if (m_loadingState || currentScalarName().isEmpty() || m_loadedFileName.isEmpty())
        return;
    updateNephogram(currentScalarName(), renderNow);
}

void NephogramSettingWidget::updateRangeEditorState()
{
    const bool custom = !ui->autoRangeCheckBox->isChecked();
    ui->rangeMinSpinBox->setEnabled(custom);
    ui->rangeMaxSpinBox->setEnabled(custom);
    ui->rangeMinLabel->setEnabled(custom);
    ui->rangeMaxLabel->setEnabled(custom);
}

void NephogramSettingWidget::setRangeControls(double minV, double maxV)
{
    if (!std::isfinite(minV) || !std::isfinite(maxV))
        return;
    if (std::abs(maxV - minV) < 1e-12)
        maxV = minV + 1.0;

    QScopedValueRollback<bool> guard(m_updatingRangeControls, true);
    const QSignalBlocker minBlocker(ui->rangeMinSpinBox);
    const QSignalBlocker maxBlocker(ui->rangeMaxSpinBox);
    ui->rangeMinSpinBox->setValue(minV);
    ui->rangeMaxSpinBox->setValue(maxV);
}

bool NephogramSettingWidget::displayRange(double dataMin, double dataMax, double& minV,
                                           double& maxV) const
{
    if (!isValidScalarRange(dataMin, dataMax))
    {
        minV = 0.0;
        maxV = 1.0;
        return false;
    }

    minV = ui->autoRangeCheckBox->isChecked() ? dataMin : ui->rangeMinSpinBox->value();
    maxV = ui->autoRangeCheckBox->isChecked() ? dataMax : ui->rangeMaxSpinBox->value();
    if (!std::isfinite(minV) || !std::isfinite(maxV) || minV >= maxV)
    {
        minV = dataMin;
        maxV = dataMax;
        if (std::abs(maxV - minV) < 1e-12)
            maxV = minV + 1.0;
        return false;
    }
    return true;
}

void NephogramSettingWidget::applyLegendAppearance(vtkScalarBarActor* scalarBar, double minV,
                                                    double maxV) const
{
    Q_UNUSED(minV);
    Q_UNUSED(maxV);
    if (!scalarBar)
        return;

    applyStandardPostScalarBar(scalarBar);
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
        arr = pointData->GetArray(scalarName.toUtf8().constData());

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
    applyStandardPostLookupTable(lut, minV, maxV);
}

bool NephogramSettingWidget::prepareDisplaySurface(vtkUnstructuredGrid* sourceGrid,
                                                    const QString& scalarName,
                                                    bool sourceUsePointData,
                                                    bool& mapperUsePointData)
{
    mapperUsePointData = sourceUsePointData;
    if (!sourceGrid || scalarName.isEmpty())
        return false;

    mapperUsePointData = sourceUsePointData;

    // Surface 缓存必须同时绑定当前网格筛选状态。即使筛选是在云图页面之外修改的，
    // 再次进入页面也不能误用旧 Surface。
    const QString filterSignature =
        postSceneStyle().followMeshFilter
            ? postMeshFilterSignature()
            : QStringLiteral("__post_all_cells__");

    if (m_displaySurface && m_displaySurfaceFilterSignature == filterSignature)
    {
        // 不能只判断“数组存在”。多块/旧 Tecplot 路径中曾出现数组存在但 tuple
        // 数量不足的情况，Mapper 会把缺失 tuple 当成 NaN，表现为大片灰色。
        if (scalarArrayMatchesSurface(m_displaySurface, scalarName, mapperUsePointData))
            return true;

        invalidateDisplaySurface();
    }

    // 这里只服务于等值线。等值线应该来自“当前筛选结果的可见边界”，
    // 不需要把所有 MATYPE/LAYER 内部界面展开。使用 VTK 官方 SurfaceFilter
    // 可避免筛选后出现大量内部薄片/碎三角。
    QElapsedTimer surfaceTimer;
    surfaceTimer.start();
    auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(sourceGrid);
    surfaceFilter->PassThroughPointIdsOn();
    surfaceFilter->PassThroughCellIdsOn();
    surfaceFilter->Update();

    vtkSmartPointer<vtkPolyData> surface = vtkSmartPointer<vtkPolyData>::New();
    if (surfaceFilter->GetOutput())
        surface->ShallowCopy(surfaceFilter->GetOutput());

    const qint64 extractMs = surfaceTimer.elapsed();
    if (extractMs >= 100)
        qInfo() << "[PostPerf] contour surface extraction" << extractMs << "ms"
                << "volumeCells=" << sourceGrid->GetNumberOfCells()
                << "surfaceCells=" << (surface ? surface->GetNumberOfCells() : 0);

    if (!surface || surface->GetNumberOfPoints() <= 0 || surface->GetNumberOfCells() <= 0)
        return false;

    if (!m_displaySurface)
        m_displaySurface = vtkSmartPointer<vtkPolyData>::New();
    m_displaySurface->ShallowCopy(surface);
    m_displaySurfaceUsePointData = mapperUsePointData;
    m_displaySurfaceFilterSignature = filterSignature;

    if (!scalarArrayMatchesSurface(m_displaySurface, scalarName, mapperUsePointData))
    {
        // 不保留半有效 Surface，避免后续切换字段继续命中坏缓存。
        invalidateDisplaySurface();
        return false;
    }
    return true;
}

void NephogramSettingWidget::invalidateDisplaySurface()
{
    m_displaySurface = nullptr;
    m_displaySurfaceUsePointData = true;
    m_displaySurfaceFilterSignature.clear();
    invalidateContourCache();
}

void NephogramSettingWidget::invalidateContourCache()
{
    m_contourDataCache = nullptr;
    m_contourCacheScalarName.clear();
    m_contourCacheSurfaceMTime = 0;
    m_contourCacheLevels = 0;
    m_contourCacheMin = 0.0;
    m_contourCacheMax = 0.0;
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
    {
        mapper->SetScalarModeToUsePointFieldData();
        // 节点结果继续在单元表面平滑插值，保持云图视觉连续。
        mapper->InterpolateScalarsBeforeMappingOn();
    }
    else
    {
        mapper->SetScalarModeToUseCellFieldData();
        // 单元结果本身就是分片常值，不做额外的标量预插值。
        mapper->InterpolateScalarsBeforeMappingOff();
    }

    // 输入均为已经构造完成的数据对象，避免后续 Render 反复更新上游管线。
    mapper->StaticOn();
}


QJsonObject NephogramSettingWidget::saveJson()
{
    QJsonObject obj;
    obj[QStringLiteral("scalar")] = currentScalarName();
    obj[QStringLiteral("analysisKey")] = ui->analysisComboBox->currentData().toString();
    obj[QStringLiteral("resultDir")] = ui->resultDirComboBox->currentData().toString();
    obj[QStringLiteral("resultTime")] = ui->resultTimeComboBox->currentData().toString();
    obj[QStringLiteral("step")] = ui->stepComboBox->currentText();
    obj[QStringLiteral("autoRange")] = ui->autoRangeCheckBox->isChecked();
    obj[QStringLiteral("rangeMin")] = ui->rangeMinSpinBox->value();
    obj[QStringLiteral("rangeMax")] = ui->rangeMaxSpinBox->value();
    const PostScalarBarStyle& barStyle = postScalarBarStyle();
    const PostContourStyle& contourStyle = postContourStyle();
    obj[QStringLiteral("showScalarBar")] = ui->showScalarBarCheckBox->isChecked();
    obj[QStringLiteral("legendTitleFont")] = barStyle.titleFont;
    obj[QStringLiteral("legendLabelFont")] = barStyle.labelFont;
    obj[QStringLiteral("legendPrecision")] = barStyle.precision;
    obj[QStringLiteral("legendTickCount")] = barStyle.tickCount;
    obj[QStringLiteral("legendWidth")] = barStyle.widthPercent;
    obj[QStringLiteral("legendHeight")] = barStyle.heightPercent;
    if (m_visualService && m_visualService->scalarBarActor())
    {
        const double* position = m_visualService->scalarBarActor()->GetPosition();
        obj[QStringLiteral("legendPositionX")] = position[0];
        obj[QStringLiteral("legendPositionY")] = position[1];
    }
    obj[QStringLiteral("contourEnabled")] = contourStyle.enabled;
    obj[QStringLiteral("contourLevels")] = contourStyle.levels;
    obj[QStringLiteral("contourLabels")] = contourStyle.labels;
    obj[QStringLiteral("contourLabelFont")] = contourStyle.labelFont;
    return obj;
}

void NephogramSettingWidget::loadJson(const QJsonObject& obj)
{
    QScopedValueRollback<bool> loadingGuard(m_loadingState, true);
    const bool useLegacyDisplaySettings = !postDisplaySettingsAuthoritative();

    if (useLegacyDisplaySettings)
    {
        ui->autoRangeCheckBox->setChecked(obj.value(QStringLiteral("autoRange")).toBool(true));
        ui->rangeMinSpinBox->setValue(obj.value(QStringLiteral("rangeMin")).toDouble(0.0));
        ui->rangeMaxSpinBox->setValue(obj.value(QStringLiteral("rangeMax")).toDouble(1.0));
        PostScalarBarStyle& barStyle = postScalarBarStyle();
        PostContourStyle& contourStyle = postContourStyle();
        barStyle.visible = obj.value(QStringLiteral("showScalarBar")).toBool(barStyle.visible);
        barStyle.titleFont = obj.value(QStringLiteral("legendTitleFont")).toInt(barStyle.titleFont);
        barStyle.labelFont = obj.value(QStringLiteral("legendLabelFont")).toInt(barStyle.labelFont);
        barStyle.precision = obj.value(QStringLiteral("legendPrecision")).toInt(barStyle.precision);
        barStyle.tickCount = obj.value(QStringLiteral("legendTickCount")).toInt(barStyle.tickCount);
        barStyle.widthPercent = obj.value(QStringLiteral("legendWidth")).toInt(barStyle.widthPercent);
        barStyle.heightPercent = obj.value(QStringLiteral("legendHeight")).toInt(barStyle.heightPercent);
        m_hasLegendPosition = obj.contains(QStringLiteral("legendPositionX")) &&
                              obj.contains(QStringLiteral("legendPositionY"));
        if (m_hasLegendPosition)
        {
            m_legendPositionX = obj.value(QStringLiteral("legendPositionX")).toDouble();
            m_legendPositionY = obj.value(QStringLiteral("legendPositionY")).toDouble();
            barStyle.hasCustomPosition = true;
            barStyle.positionX = m_legendPositionX;
            barStyle.positionY = m_legendPositionY;
        }
        contourStyle.enabled = obj.value(QStringLiteral("contourEnabled")).toBool(contourStyle.enabled);
        contourStyle.levels = obj.value(QStringLiteral("contourLevels")).toInt(contourStyle.levels);
        contourStyle.labels = obj.value(QStringLiteral("contourLabels")).toBool(contourStyle.labels);
        contourStyle.labelFont = obj.value(QStringLiteral("contourLabelFont")).toInt(contourStyle.labelFont);
        postDisplaySettingsAuthoritative() = true;
    }
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);
    updateRangeEditorState();

    const QString analysisKey = obj.value(QStringLiteral("analysisKey")).toString();
    const QString resultDir = obj.value(QStringLiteral("resultDir")).toString();
    const QString resultTime = obj.value(QStringLiteral("resultTime")).toString();
    const QString step = obj.value(QStringLiteral("step")).toString();
    const QString scalar = obj.value(QStringLiteral("scalar")).toString();
    if (useLegacyDisplaySettings && !scalar.isEmpty() &&
        obj.contains(QStringLiteral("autoRange")))
    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(scalar);
        rangeStyle.automatic = obj.value(QStringLiteral("autoRange")).toBool(true);
        rangeStyle.minimum = obj.value(QStringLiteral("rangeMin")).toDouble(0.0);
        rangeStyle.maximum = obj.value(QStringLiteral("rangeMax")).toDouble(1.0);
        rangeStyle.initialized = rangeStyle.maximum > rangeStyle.minimum;
    }
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString resolvedResultDir =
        PostResultSelectionUtils::absoluteSourcePath(projectDir, resultDir);

    initResultDirectoryOptions();
    m_resultOptionsPreparedByLoadJson = true;
    QString resolvedAnalysisKey = analysisKey;
    if (resolvedAnalysisKey.isEmpty() && !resolvedResultDir.isEmpty())
        resolvedAnalysisKey =
            PostResultSelectionUtils::analysisKeyForSource(projectDir, resolvedResultDir);
    if (!resolvedAnalysisKey.isEmpty())
    {
        const int analysisIndex = ui->analysisComboBox->findData(resolvedAnalysisKey);
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
        const int dirIndex = PostResultSelectionUtils::sourceComboIndex(
            ui->resultDirComboBox, projectDir, resolvedResultDir);
        if (dirIndex >= 0)
        {
            const QSignalBlocker blocker(ui->resultDirComboBox);
            ui->resultDirComboBox->setCurrentIndex(dirIndex);
        }
    }

    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);

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

    if (!step.isEmpty())
    {
        const int stepIndex = ui->stepComboBox->findText(step);
        if (stepIndex >= 0)
            ui->stepComboBox->setCurrentIndex(stepIndex);
    }

    if (!scalar.isEmpty())
    {
        const int scalarIndex = ResultDisplayNames::findInternalName(ui->scalarComboBox, scalar);
        if (scalarIndex >= 0)
            ui->scalarComboBox->setCurrentIndex(scalarIndex);
    }
    m_currentArrayName = currentScalarName();
}

void NephogramSettingWidget::readVtu(const QString& fileName)
{
    // VTU/Tecplot 都走 PostTecplotUtils 的统一入口：统一内存缓存、中文路径兼容、
    // TOPOLOGY_TYPE 和旧退化单元修复，避免不同页面对同一结果读出不同的数据。
    readTecplot(fileName);
}

void NephogramSettingWidget::clearContourOverlay()
{
    if (!m_visualService)
        return;

    removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("contour")), false);
    removePostPropByService(m_visualService,
                            postActorId(getUuid(), QStringLiteral("contour-labels")),
                            m_contourLabelActor, false);
}

void NephogramSettingWidget::updateContourOverlay(const QString& scalarName,
                                                  bool usePointData,
                                                  double minV,
                                                  double maxV)
{
    if (!m_visualService || !m_actorAttached)
        return;

    clearContourOverlay();

    if (!postSceneStyle().visible || !postContourStyle().enabled)
        return;

    if (scalarName.isEmpty())
        return;

    // 普通云图不提面；只有用户打开等值线时才按当前筛选结果生成一张
    // PolyData 表面，并复用 prepareDisplaySurface() 自身的签名缓存。
    bool mapperUsePointData = usePointData;
    if (m_grid)
    {
        vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
        prepareDisplaySurface(displayGrid, scalarName, usePointData, mapperUsePointData);
    }
    vtkPolyData* surface = m_displaySurface;
    if (!surface || surface->GetNumberOfPoints() <= 0 || surface->GetNumberOfCells() <= 0)
        return;

    const QByteArray scalarNameUtf8 = scalarName.toUtf8();
    const int levels = std::max(1, postContourStyle().levels);
    const vtkMTimeType surfaceMTime = surface->GetMTime();
    const double span = std::max(std::abs(maxV - minV), 1.0);
    const double cacheTol = span * 1.0e-12;

    const bool contourCacheHit =
        m_contourDataCache &&
        m_contourCacheSurfaceMTime == surfaceMTime &&
        m_contourCacheScalarName == scalarName &&
        m_contourCacheLevels == levels &&
        std::abs(m_contourCacheMin - minV) <= cacheTol &&
        std::abs(m_contourCacheMax - maxV) <= cacheTol;

    if (!contourCacheHit)
    {
        vtkSmartPointer<vtkPolyData> contourInput = vtkSmartPointer<vtkPolyData>::New();

        if (surface->GetPointData() &&
            surface->GetPointData()->GetArray(scalarNameUtf8.constData()))
        {
            contourInput->ShallowCopy(surface);
        }
        else
        {
            // CellData 的等值线需要点标量。这个转换只在缓存失效时执行，
            // 不再因为色标刷新/页面重入反复转换整张表面。
            auto cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
            cellToPoint->SetInputData(surface);
            // ParaView/VTK 的标准做法是先把 CellData 转成 PointData 再 Contour。
            // 这里只转换当前标量，避免为了等值线把整套应力/材料数组都重新插值一遍。
            cellToPoint->ProcessAllArraysOff();
            cellToPoint->AddCellDataArray(scalarNameUtf8.constData());
            cellToPoint->PassCellDataOff();
            cellToPoint->Update();

            if (auto* converted = vtkPolyData::SafeDownCast(cellToPoint->GetOutput()))
                contourInput->ShallowCopy(converted);
        }

        if (!contourInput || contourInput->GetNumberOfPoints() <= 0 ||
            contourInput->GetNumberOfCells() <= 0)
        {
            invalidateContourCache();
            return;
        }

        auto* pointData = contourInput->GetPointData();
        vtkDataArray* contourArray = pointData
                                         ? pointData->GetArray(scalarNameUtf8.constData())
                                         : nullptr;
        if (!contourArray)
        {
            qWarning() << "Contour scalar array not found on surface:" << scalarName;
            invalidateContourCache();
            return;
        }

        pointData->SetActiveScalars(scalarNameUtf8.constData());

        double surfaceRange[2];
        contourArray->GetRange(surfaceRange);
        if (std::abs(surfaceRange[1] - surfaceRange[0]) < 1e-12 ||
            std::abs(maxV - minV) < 1e-12)
        {
            invalidateContourCache();
            return;
        }

        // 保持 VTK 官方常规管线：直接在显示 Surface 上 Contour。
        // 不再提前强制三角化；强制三角化会把 Quad 的插值人为固定到某条对角线，
        // 容易在鞍点附近出现小三角、尖折和不自然的短线。
        auto contour = vtkSmartPointer<vtkContourFilter>::New();
        contour->SetInputData(contourInput);
        contour->SetInputArrayToProcess(0, 0, 0,
                                        vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                        scalarNameUtf8.constData());
        // 等值线只是叠加线，不需要法线/梯度；官方文档也明确指出这两项计算较重。
        contour->ComputeNormalsOff();
        contour->ComputeGradientsOff();
        contour->ComputeScalarsOn();
        // 同一张表面一次通常要生成多条等值线，ScalarTree 可减少重复单元搜索。
        contour->UseScalarTreeOn();
        contour->SetNumberOfContours(levels);

        if (levels == 1)
        {
            contour->SetValue(0, 0.5 * (minV + maxV));
        }
        else
        {
            // 不画 min/max 两端线，减少模型边界上的脏线。
            for (int i = 0; i < levels; ++i)
            {
                const double t = double(i + 1) / double(levels + 1);
                contour->SetValue(i, minV + (maxV - minV) * t);
            }
        }

        contour->Update();

        // VTK 官方示例也是 ContourFilter -> Stripper。
        // 这里只连接拓扑上连续的短线段，不做 Clean/平滑，不改动等值线几何。
        auto stripper = vtkSmartPointer<vtkStripper>::New();
        stripper->SetInputConnection(contour->GetOutputPort());
        stripper->JoinContiguousSegmentsOn();
        stripper->Update();

        vtkPolyData* contourOutput = stripper->GetOutput();
        if (!contourOutput || contourOutput->GetNumberOfCells() <= 0 ||
            contourOutput->GetNumberOfPoints() <= 0)
        {
            invalidateContourCache();
            return;
        }

        if (!m_contourDataCache)
            m_contourDataCache = vtkSmartPointer<vtkPolyData>::New();
        m_contourDataCache->ShallowCopy(contourOutput);
        m_contourCacheSurfaceMTime = surfaceMTime;
        m_contourCacheScalarName = scalarName;
        m_contourCacheLevels = levels;
        m_contourCacheMin = minV;
        m_contourCacheMax = maxV;
    }

    vtkPolyData* contourData = m_contourDataCache;
    if (!contourData || contourData->GetNumberOfCells() <= 0 ||
        contourData->GetNumberOfPoints() <= 0)
        return;

    auto contourMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    contourMapper->SetInputData(contourData);
    // 商软常用深色等值线叠加在连续云图上，而不是让线条继续使用同一 LUT。
    // 这样每条线的轮廓更清楚，也不会在相近颜色区域里“消失”。
    contourMapper->ScalarVisibilityOff();
    // 不再依赖 vtkMapper 的全局 coincident-topology 状态。
    // 该开关是全局的，容易与几何/网格/其他后处理 Actor 的设置相互影响。
    contourMapper->StaticOn();

    m_contourActor->SetMapper(contourMapper);
    m_contourActor->PickableOff();

    auto* contourProperty = m_contourActor->GetProperty();
    contourProperty->LightingOff();
    contourProperty->SetColor(0.10, 0.10, 0.10);
    contourProperty->SetOpacity(1.0);
    contourProperty->SetLineWidth(1.0);
    // 等值线只负责正常绘制；与云图面的共面关系由后处理视图级策略统一处理。

    addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("contour")),
                      m_contourActor, QStringLiteral("等值线"), false);

    if (!postContourStyle().labels)
        return;

    vtkDataArray* contourScalars = contourData->GetPointData()
                                       ? contourData->GetPointData()->GetArray(scalarNameUtf8.constData())
                                       : nullptr;
    if (!contourScalars)
        return;

    auto labelPoly = vtkSmartPointer<vtkPolyData>::New();
    auto labelPts = vtkSmartPointer<vtkPoints>::New();
    auto labelVals = vtkSmartPointer<vtkDoubleArray>::New();
    labelVals->SetName("ContourLabels");

    // 每一个实际生成的等值级都保留一个数值标签，不再把 10/20/30 级
    // 二次抽样压缩成最多 8 个标签。
    const double labelSpan = std::max(std::abs(maxV - minV), 1e-12);
    const double tol = labelSpan * 1e-5;

    struct LabelCandidate
    {
        double value{0.0};
        vtkIdType pointId{-1};
        double length{0.0};
    };

    QVector<LabelCandidate> candidates;
    candidates.reserve(levels);

    const vtkIdType cellCount = contourData->GetNumberOfCells();
    for (vtkIdType ci = 0; ci < cellCount; ++ci)
    {
        vtkCell* cell = contourData->GetCell(ci);
        if (!cell)
            continue;

        vtkIdList* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() < 2)
            continue;

        const vtkIdType midId = ids->GetId(ids->GetNumberOfIds() / 2);
        const double v = contourScalars->GetComponent(midId, 0);

        double lineLength = 0.0;
        double p0[3];
        contourData->GetPoint(ids->GetId(0), p0);
        for (vtkIdType pi = 1; pi < ids->GetNumberOfIds(); ++pi)
        {
            double p1[3];
            contourData->GetPoint(ids->GetId(pi), p1);
            const double dx = p1[0] - p0[0];
            const double dy = p1[1] - p0[1];
            const double dz = p1[2] - p0[2];
            lineLength += std::sqrt(dx * dx + dy * dy + dz * dz);
            p0[0] = p1[0];
            p0[1] = p1[1];
            p0[2] = p1[2];
        }

        int existing = -1;
        for (int i = 0; i < candidates.size(); ++i)
        {
            if (std::abs(candidates[i].value - v) <= tol)
            {
                existing = i;
                break;
            }
        }

        if (existing < 0)
        {
            candidates.push_back({v, midId, lineLength});
        }
        else if (lineLength > candidates[existing].length)
        {
            // 同一等值级只在最长的一条连续线附近标一个值，减少标签堆叠。
            candidates[existing] = {v, midId, lineLength};
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const LabelCandidate& a, const LabelCandidate& b) {
                  return a.value < b.value;
              });

    for (const auto& candidate : candidates)
    {
        double p[3];
        contourData->GetPoint(candidate.pointId, p);
        labelPts->InsertNextPoint(p);
        labelVals->InsertNextValue(candidate.value);
    }

    if (labelPts->GetNumberOfPoints() <= 0)
        return;

    labelPoly->SetPoints(labelPts);
    labelPoly->GetPointData()->SetScalars(labelVals);

    // 标签数据直接使用全部等值级。标签数量本身与等值级数量一致（UI 最大 30），
    // 不再额外做“可见点采样”，避免同一组等值线因为视角不同丢掉部分数值。
    auto labelMapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
    labelMapper->SetInputData(labelPoly);
    labelMapper->SetLabelModeToLabelScalars();
    const QByteArray contourLabelFormat =
        QStringLiteral("%.%1g").arg(postScalarBarStyle().precision).toLatin1();
    labelMapper->SetLabelFormat(contourLabelFormat.constData());
    labelMapper->GetLabelTextProperty()->SetFontSize(postContourStyle().labelFont);
    labelMapper->GetLabelTextProperty()->SetColor(0.08, 0.08, 0.08);
    labelMapper->GetLabelTextProperty()->SetBold(0);
    labelMapper->GetLabelTextProperty()->SetShadow(0);
    labelMapper->GetLabelTextProperty()->SetBackgroundColor(1.0, 1.0, 1.0);
    labelMapper->GetLabelTextProperty()->SetBackgroundOpacity(0.72);
    labelMapper->GetLabelTextProperty()->SetJustificationToCentered();
    labelMapper->GetLabelTextProperty()->SetVerticalJustificationToCentered();

    m_contourLabelActor->SetMapper(labelMapper);
    addPostPropByService(m_visualService,
                         postActorId(getUuid(), QStringLiteral("contour-labels")),
                         m_contourLabelActor, QStringLiteral("等值线数值"), false);
}
