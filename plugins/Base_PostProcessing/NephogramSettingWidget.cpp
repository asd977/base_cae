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
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkDoubleArray.h>
#include <vtkLabeledDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarBarActor.h>
#include <vtkCommand.h>
#include <vtkCallbackCommand.h>
#include <vtkSelectVisiblePoints.h>
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
    m_legendDragCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_legendDragCallback->SetClientData(this);
    m_legendDragCallback->SetCallback(&NephogramSettingWidget::legendDragCallback);

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
        if (m_grid && !currentScalarName().isEmpty())
        {
            bool usePointData = true;
            if (vtkDataArray* array = findScalarArray(m_grid, currentScalarName(), usePointData))
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
            if (data.value(QStringLiteral("reason")).toString() == QStringLiteral("mesh-filter"))
                invalidateDisplaySurface();
            {
                const QSignalBlocker scalarBarBlocker(ui->showScalarBarCheckBox);
                const QSignalBlocker contourBlocker(ui->showContourCheckBox);
                ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
                ui->showContourCheckBox->setChecked(postContourStyle().enabled);
            }
            refreshCurrentVisualization();
        });

    updateRangeEditorState();
    initResultDirectoryOptions();
}

NephogramSettingWidget::~NephogramSettingWidget()
{
    removeLegendDragInteraction();
    delete ui;
}

void NephogramSettingWidget::ensureActorAttached()
{
    if (!m_actorAttached)
    {
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")),
                          m_actor, QStringLiteral("云图"));
        m_actorAttached = true;
    }
}

void NephogramSettingWidget::syncScalarBarFromCurrent()
{
    if (!m_grid || m_currentArrayName.isEmpty() || !m_visualService)
        return;

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

    double dataRange[2];
    arr->GetRange(dataRange);
    if (isValidScalarRange(dataRange))
        if (property("postDisplayActive").toBool())
            publishPostDisplayContext(m_currentArrayName, dataRange[0], dataRange[1], true, true);
    if (!isValidScalarRange(dataRange))
    {
        clearPostDisplayContext();
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        m_visualService->showScalarBar(false, false);
        clearContourOverlay();
        return;
    }

    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(m_currentArrayName,
                                                              dataRange[0], dataRange[1]);
        const QSignalBlocker autoBlocker(ui->autoRangeCheckBox);
        const QSignalBlocker minBlocker(ui->rangeMinSpinBox);
        const QSignalBlocker maxBlocker(ui->rangeMaxSpinBox);
        ui->autoRangeCheckBox->setChecked(rangeStyle.automatic);
        ui->rangeMinSpinBox->setValue(rangeStyle.initialized ? rangeStyle.minimum : dataRange[0]);
        ui->rangeMaxSpinBox->setValue(rangeStyle.initialized ? rangeStyle.maximum : dataRange[1]);
    }
    double minV = dataRange[0];
    double maxV = dataRange[1];
    resolvePostDisplayRange(m_currentArrayName, dataRange[0], dataRange[1], minV, maxV);

    vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor();
    if (!scalarBar)
        return;

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
    scalarBar->SetTitle(currentScalarDisplayName().toUtf8().constData());
    scalarBar->Modified();

    bool mapperUsePointData = usePointData;
    prepareDisplaySurface(displayGrid, m_currentArrayName, usePointData, mapperUsePointData);
    configureMapper(m_mapper, m_currentArrayName, mapperUsePointData, minV, maxV, lut);
}

void NephogramSettingWidget::beforeLoad()
{
    setProperty("postDisplayActive", true);
    showPostProcessingOnly(m_visualService, false);
    ui->showScalarBarCheckBox->setChecked(postScalarBarStyle().visible);
    ui->showContourCheckBox->setChecked(postContourStyle().enabled);

    if (!m_visualService)
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (!m_visualService)
    {
        qWarning() << "Visual service is null in NephogramSettingWidget.";
        return;
    }

    installLegendDragInteraction();

    // 后处理云图优先启用 FXAA，主要改善等值线和模型轮廓的像素锯齿。
    if (auto* ren = m_visualService->renderer())
        ren->UseFXAAOn();

    initResultDirectoryOptions();
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
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("surface")));
        m_actorAttached = false;
    }

    removeLegendDragInteraction();
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
    QString errorMessage;
    m_grid = PostTecplotUtils::readGrid(fileName, &errorMessage);
    invalidateDisplaySurface();
    m_loadedFileName = fileName;

    if (!m_grid)
    {
        qWarning().noquote() << errorMessage;
        ui->scalarComboBox->clear();
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
            {
                const QString qName = QString::fromUtf8(name);
                if (!ResultDisplayNames::isInternalField(qName))
                    scalarNames << qName;
            }
        }
    }

    if (auto* cd = m_grid->GetCellData())
    {
        const int n = cd->GetNumberOfArrays();

        for (int i = 0; i < n; ++i)
        {
            const char* name = cd->GetArrayName(i);

            if (name && *name)
            {
                const QString qName = QString::fromUtf8(name);
                if (!ResultDisplayNames::isInternalField(qName) && !scalarNames.contains(qName))
                    scalarNames << qName;
            }
        }
    }

    const QString previousScalar = currentScalarName();
    const QString preferredScalar = !PostResultSelectionUtils::sharedResultSelection().fieldName.isEmpty()
                                        ? PostResultSelectionUtils::sharedResultSelection().fieldName
                                        : previousScalar;
    ResultDisplayNames::populateCombo(ui->scalarComboBox, scalarNames, preferredScalar);
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox);
    updateNephogram(currentScalarName());
}

void NephogramSettingWidget::updateNephogram(const QString& selectedScalarName)
{
    if (m_loadingState || !m_grid || selectedScalarName.isEmpty())
        return;

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

    bool mapperUsePointData = usePointData;
    if (!prepareDisplaySurface(displayGrid, selectedScalarName, usePointData, mapperUsePointData))
        return;

    syncScalarBarFromCurrent();

    if (m_displaySurface)
        m_mapper->SetInputData(m_displaySurface);
    else
        m_mapper->SetInputData(displayGrid);
    m_actor->SetMapper(m_mapper);
    auto* surfaceProperty = m_actor->GetProperty();
    surfaceProperty->SetRepresentationToSurface();
    surfaceProperty->EdgeVisibilityOff();
    surfaceProperty->SetInterpolationToPhong();
    surfaceProperty->LightingOn();
    // 保留云图本身的颜色，同时用轻微漫反射增强三维形体感，避免“塑料感”。
    surfaceProperty->SetAmbient(0.48);
    surfaceProperty->SetDiffuse(0.50);
    surfaceProperty->SetSpecular(0.02);
    surfaceProperty->SetSpecularPower(16.0);
    ensureActorAttached();

    updateContourOverlay(selectedScalarName, usePointData, minV, maxV);

    if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
        rw->Render();
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

void NephogramSettingWidget::refreshCurrentVisualization()
{
    if (m_loadingState || !m_grid || currentScalarName().isEmpty())
        return;
    updateNephogram(currentScalarName());
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

void NephogramSettingWidget::installLegendDragInteraction()
{
    if (!m_visualService || !m_legendDragCallback)
        return;

    vtkRenderWindowInteractor* interactor = m_visualService->interactor();
    if (!interactor || m_middlePressObserverId != 0)
        return;

    // 使用较高优先级，并只在鼠标中键命中色标时中止后续事件，
    // 这样拖动图例不会同时触发相机平移，点在其他位置时仍保持原有操作。
    constexpr float priority = 1.0f;
    m_middlePressObserverId = interactor->AddObserver(
        vtkCommand::MiddleButtonPressEvent, m_legendDragCallback, priority);
    m_mouseMoveObserverId = interactor->AddObserver(
        vtkCommand::MouseMoveEvent, m_legendDragCallback, priority);
    m_middleReleaseObserverId = interactor->AddObserver(
        vtkCommand::MiddleButtonReleaseEvent, m_legendDragCallback, priority);
}

void NephogramSettingWidget::removeLegendDragInteraction()
{
    vtkRenderWindowInteractor* interactor = m_visualService ? m_visualService->interactor() : nullptr;
    if (interactor)
    {
        if (m_middlePressObserverId != 0)
            interactor->RemoveObserver(m_middlePressObserverId);
        if (m_mouseMoveObserverId != 0)
            interactor->RemoveObserver(m_mouseMoveObserverId);
        if (m_middleReleaseObserverId != 0)
            interactor->RemoveObserver(m_middleReleaseObserverId);
    }

    m_middlePressObserverId = 0;
    m_mouseMoveObserverId = 0;
    m_middleReleaseObserverId = 0;
    m_draggingLegend = false;
}

void NephogramSettingWidget::legendDragCallback(vtkObject*, unsigned long eventId,
                                                 void* clientData, void*)
{
    auto* self = static_cast<NephogramSettingWidget*>(clientData);
    if (!self || !self->m_legendDragCallback)
        return;

    self->m_legendDragCallback->AbortFlagOff();
    if (self->handleLegendDragEvent(eventId))
        self->m_legendDragCallback->AbortFlagOn();
}

bool NephogramSettingWidget::handleLegendDragEvent(unsigned long eventId)
{
    if (!m_visualService)
        return false;

    vtkRenderWindowInteractor* interactor = m_visualService->interactor();
    vtkRenderWindow* renderWindow = m_visualService->renderWindow();
    vtkRenderer* renderer = m_visualService->renderer();
    vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor();
    if (!interactor || !renderWindow || !renderer || !scalarBar ||
        scalarBar->GetVisibility() == 0)
    {
        m_draggingLegend = false;
        return false;
    }

    int* eventPosition = interactor->GetEventPosition();
    int* windowSize = renderWindow->GetSize();
    double viewport[4];
    renderer->GetViewport(viewport);

    const double viewportWidth = (viewport[2] - viewport[0]) * windowSize[0];
    const double viewportHeight = (viewport[3] - viewport[1]) * windowSize[1];
    if (viewportWidth <= 0.0 || viewportHeight <= 0.0)
        return false;

    const double localX = (eventPosition[0] - viewport[0] * windowSize[0]) / viewportWidth;
    const double localY = (eventPosition[1] - viewport[1] * windowSize[1]) / viewportHeight;
    const double* position = scalarBar->GetPosition();
    const double width = scalarBar->GetWidth();
    const double height = scalarBar->GetHeight();

    if (eventId == vtkCommand::MiddleButtonPressEvent)
    {
        // 留少量容差，标题或刻度文字附近也能顺手拖动。
        constexpr double tolerance = 0.02;
        const bool hit = localX >= position[0] - tolerance &&
                         localX <= position[0] + width + tolerance &&
                         localY >= position[1] - tolerance &&
                         localY <= position[1] + height + tolerance;
        if (!hit)
            return false;

        m_draggingLegend = true;
        m_legendDragOffsetX = localX - position[0];
        m_legendDragOffsetY = localY - position[1];
        return true;
    }

    if (eventId == vtkCommand::MouseMoveEvent)
    {
        if (!m_draggingLegend)
            return false;

        const double x = qBound(0.0, localX - m_legendDragOffsetX,
                                std::max(0.0, 1.0 - width));
        const double y = qBound(0.0, localY - m_legendDragOffsetY,
                                std::max(0.0, 1.0 - height));
        scalarBar->SetPosition(x, y);
        PostScalarBarStyle& style = postScalarBarStyle();
        style.hasCustomPosition = true;
        style.positionX = x;
        style.positionY = y;
        scalarBar->Modified();

        m_legendPositionX = x;
        m_legendPositionY = y;
        m_hasLegendPosition = true;
        renderWindow->Render();
        return true;
    }

    if (eventId == vtkCommand::MiddleButtonReleaseEvent && m_draggingLegend)
    {
        m_draggingLegend = false;
        return true;
    }

    return false;
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

    if (m_displaySurface && m_displaySurfaceUsePointData == mapperUsePointData)
    {
        const QByteArray scalarUtf8 = scalarName.toUtf8();
        vtkDataArray* cachedArray = mapperUsePointData
                                       ? m_displaySurface->GetPointData()->GetArray(scalarUtf8.constData())
                                       : m_displaySurface->GetCellData()->GetArray(scalarUtf8.constData());
        if (cachedArray)
            return true;
    }

    auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(sourceGrid);
    surfaceFilter->Update();

    auto* surface = surfaceFilter->GetOutput();
    if (!surface || surface->GetNumberOfPoints() <= 0 || surface->GetNumberOfCells() <= 0)
        return false;

    // 生成连续点法线。小角度网格面平滑过渡，真正的尖锐几何边仍保留，
    // 比直接绘制三角面片更接近商用 CAE 的表面质感。
    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(surface);
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->ConsistencyOn();
    normals->SplittingOn();
    normals->SetFeatureAngle(55.0);
    normals->Update();

    if (!m_displaySurface)
        m_displaySurface = vtkSmartPointer<vtkPolyData>::New();
    m_displaySurface->ShallowCopy(normals->GetOutput());
    m_displaySurfaceUsePointData = mapperUsePointData;

    const QByteArray scalarUtf8 = scalarName.toUtf8();
    vtkDataArray* displayArray = mapperUsePointData
                                     ? m_displaySurface->GetPointData()->GetArray(scalarUtf8.constData())
                                     : m_displaySurface->GetCellData()->GetArray(scalarUtf8.constData());
    return displayArray != nullptr;
}

void NephogramSettingWidget::invalidateDisplaySurface()
{
    m_displaySurface = nullptr;
    m_displaySurfaceUsePointData = true;
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
    auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());

    auto readerPoly = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    bool isUGrid = true;

    QFileInfo info(fileName);

    if (info.suffix().compare(QStringLiteral("vtu"), Qt::CaseInsensitive) == 0)
    {
        reader->Update();

        if (!reader->GetOutput())
        {
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
    invalidateDisplaySurface();
    m_loadedFileName = fileName;

    QStringList scalarNames;

    if (auto* pd = grid->GetPointData())
    {
        const int n = pd->GetNumberOfArrays();

        for (int i = 0; i < n; ++i)
        {
            const char* name = pd->GetArrayName(i);

            if (name && *name)
            {
                const QString qName = QString::fromUtf8(name);
                if (!ResultDisplayNames::isInternalField(qName))
                    scalarNames << qName;
            }
        }
    }

    if (auto* cd = grid->GetCellData())
    {
        const int n = cd->GetNumberOfArrays();

        for (int i = 0; i < n; ++i)
        {
            const char* name = cd->GetArrayName(i);

            if (name && *name)
            {
                const QString qName = QString::fromUtf8(name);
                if (!ResultDisplayNames::isInternalField(qName) && !scalarNames.contains(qName))
                    scalarNames << qName;
            }
        }
    }

    const QString previousScalar = currentScalarName();
    const QString preferredScalar = !PostResultSelectionUtils::sharedResultSelection().fieldName.isEmpty()
                                        ? PostResultSelectionUtils::sharedResultSelection().fieldName
                                        : previousScalar;
    ResultDisplayNames::populateCombo(ui->scalarComboBox, scalarNames, preferredScalar);
    PostResultSelectionUtils::rememberFieldSelection(ui->scalarComboBox);
    updateNephogram(currentScalarName());
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
    if (!m_visualService || !m_grid || !m_actorAttached)
        return;

    auto* ren = m_visualService->renderer();
    if (!ren)
        return;

    clearContourOverlay();

    if (!postSceneStyle().visible || !postContourStyle().enabled)
        return;

    if (scalarName.isEmpty())
        return;

    // 等值线与云图使用同一张显示表面，避免两套插值造成视觉错位。
    bool mapperUsePointData = usePointData;
    if (!m_displaySurface)
    {
        vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(m_grid);
        prepareDisplaySurface(displayGrid, scalarName, usePointData, mapperUsePointData);
    }
    vtkPolyData* surface = m_displaySurface;
    if (!surface || surface->GetNumberOfPoints() <= 0 || surface->GetNumberOfCells() <= 0)
        return;

    vtkSmartPointer<vtkPolyData> contourInput = vtkSmartPointer<vtkPolyData>::New();
    const QByteArray scalarNameUtf8 = scalarName.toUtf8();

    if (surface->GetPointData() &&
        surface->GetPointData()->GetArray(scalarNameUtf8.constData()))
    {
        contourInput->ShallowCopy(surface);
    }
    else
    {
        // 原始单元显示模式下，等值线仍需要点标量；这里仅为等值线做显示转换。
        auto cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
        cellToPoint->SetInputData(surface);
        cellToPoint->PassCellDataOff();
        cellToPoint->Update();

        if (auto* converted = vtkPolyData::SafeDownCast(cellToPoint->GetOutput()))
            contourInput->ShallowCopy(converted);
    }

    if (!contourInput || contourInput->GetNumberOfPoints() <= 0 ||
        contourInput->GetNumberOfCells() <= 0)
    {
        return;
    }

    auto* pointData = contourInput->GetPointData();
    vtkDataArray* contourArray = pointData ? pointData->GetArray(scalarNameUtf8.constData()) : nullptr;

    if (!contourArray)
    {
        qWarning() << "Contour scalar array not found on surface:" << scalarName;
        return;
    }

    pointData->SetActiveScalars(scalarNameUtf8.constData());

    double surfaceRange[2];
    contourArray->GetRange(surfaceRange);
    if (std::abs(surfaceRange[1] - surfaceRange[0]) < 1e-12 ||
        std::abs(maxV - minV) < 1e-12)
        return;

    auto contour = vtkSmartPointer<vtkContourFilter>::New();
    contour->SetInputData(contourInput);
    contour->SetInputArrayToProcess(0, 0, 0,
                                    vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    scalarNameUtf8.constData());

    const int levels = std::max(1, postContourStyle().levels);
    contour->SetNumberOfContours(levels);

    if (levels == 1)
    {
        contour->SetValue(0, 0.5 * (minV + maxV));
    }
    else
    {
        // 不画 min/max 两端线，减少边界脏线。
        for (int i = 0; i < levels; ++i)
        {
            const double t = double(i + 1) / double(levels + 1);
            contour->SetValue(i, minV + (maxV - minV) * t);
        }
    }

    contour->Update();

    // 把等值线的碎线段连接成长折线。几何位置不做“造假式平滑”，
    // 但连接后的线在抗锯齿和标签布置上会更稳定。
    auto stripper = vtkSmartPointer<vtkStripper>::New();
    stripper->SetInputConnection(contour->GetOutputPort());
    stripper->Update();

    vtkPolyData* contourOutput = stripper->GetOutput();
    if (!contourOutput || contourOutput->GetNumberOfCells() <= 0 ||
        contourOutput->GetNumberOfPoints() <= 0)
    {
        qWarning() << "No contour generated for" << scalarName
                   << "surface range:" << surfaceRange[0] << surfaceRange[1]
                   << "display range:" << minV << maxV;
        return;
    }

    auto contourData = vtkSmartPointer<vtkPolyData>::New();
    contourData->ShallowCopy(contourOutput);

    auto contourMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    contourMapper->SetInputData(contourData);
    // 商软常用深色等值线叠加在连续云图上，而不是让线条继续使用同一 LUT。
    // 这样每条线的轮廓更清楚，也不会在相近颜色区域里“消失”。
    contourMapper->ScalarVisibilityOff();
    contourMapper->SetResolveCoincidentTopologyToPolygonOffset();
    contourMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -1.0);

    m_contourActor->SetMapper(contourMapper);
    m_contourActor->PickableOff();
    auto* contourProperty = m_contourActor->GetProperty();
    contourProperty->LightingOff();
    contourProperty->SetColor(0.10, 0.10, 0.10);
    contourProperty->SetOpacity(0.82);
    contourProperty->SetLineWidth(1.0);
    contourProperty->SetRenderLinesAsTubes(true);

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

    const int maxLabels = std::min(levels, 8);
    const double span = std::max(std::abs(maxV - minV), 1e-12);
    const double tol = span * 1e-5;

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

    const int labelCount = std::min(maxLabels, static_cast<int>(candidates.size()));
    for (int i = 0; i < labelCount; ++i)
    {
        int candidateIndex = i;
        if (candidates.size() > labelCount && labelCount > 1)
        {
            const double t = double(i) / double(labelCount - 1);
            candidateIndex = int(std::lround(t * double(candidates.size() - 1)));
        }

        const auto& candidate = candidates[candidateIndex];
        double p[3];
        contourData->GetPoint(candidate.pointId, p);
        labelPts->InsertNextPoint(p);
        labelVals->InsertNextValue(candidate.value);
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
