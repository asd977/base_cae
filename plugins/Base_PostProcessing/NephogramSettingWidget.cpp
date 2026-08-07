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

    const auto refreshLegend = [this](int) { refreshCurrentVisualization(); };
    connect(ui->legendTitleFontSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            refreshLegend);
    connect(ui->legendLabelFontSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            refreshLegend);
    connect(ui->legendPrecisionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            refreshLegend);
    connect(ui->legendTickCountSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            refreshLegend);
    connect(ui->legendWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            refreshLegend);
    connect(ui->legendHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            refreshLegend);

    connect(ui->contourEnableCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        ui->contourLevelsSpinBox->setEnabled(enabled);
        ui->contourLabelCheckBox->setEnabled(enabled);
        ui->contourLabelFontSpinBox->setEnabled(enabled && ui->contourLabelCheckBox->isChecked());
        refreshCurrentVisualization();
    });
    connect(ui->contourLevelsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { refreshCurrentVisualization(); });
    connect(ui->contourLabelCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        ui->contourLabelFontSpinBox->setEnabled(checked &&
                                                ui->contourEnableCheckBox->isChecked());
        refreshCurrentVisualization();
    });
    connect(ui->contourLabelFontSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { refreshCurrentVisualization(); });

    updateRangeEditorState();
    ui->contourLevelsSpinBox->setEnabled(ui->contourEnableCheckBox->isChecked());
    ui->contourLabelCheckBox->setEnabled(ui->contourEnableCheckBox->isChecked());
    ui->contourLabelFontSpinBox->setEnabled(ui->contourEnableCheckBox->isChecked() &&
                                            ui->contourLabelCheckBox->isChecked());
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
        addActorByService(m_visualService, m_actor);
        m_actorAttached = true;
    }
}

void NephogramSettingWidget::syncScalarBarFromCurrent()
{
    if (!m_grid || m_currentArrayName.isEmpty() || !m_visualService)
        return;

    bool usePointData = true;
    vtkDataArray* arr = findScalarArray(m_grid, m_currentArrayName, usePointData);
    if (!arr)
        return;

    double dataRange[2];
    arr->GetRange(dataRange);
    if (!isValidScalarRange(dataRange))
    {
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        m_visualService->showScalarBar(false, false);
        return;
    }

    if (ui->autoRangeCheckBox->isChecked())
        setRangeControls(dataRange[0], dataRange[1]);

    double minV = dataRange[0];
    double maxV = dataRange[1];
    displayRange(dataRange[0], dataRange[1], minV, maxV);

    vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor();
    if (!scalarBar)
        return;

    m_visualService->showScalarBar(true, false);
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
    configureMapper(m_mapper, m_currentArrayName, usePointData, minV, maxV, lut);
}

void NephogramSettingWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);

    if (!m_visualService)
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (!m_visualService)
    {
        qWarning() << "Visual service is null in NephogramSettingWidget.";
        return;
    }

    installLegendDragInteraction();

    const QString previousAnalysis = ui->analysisComboBox->currentData().toString();
    const QString previousDir = ui->resultDirComboBox->currentData().toString();
    const QString previousTime = ui->resultTimeComboBox->currentData().toString();
    const QString previousStep = ui->stepComboBox->currentText();

    initResultDirectoryOptions();

    if (!previousAnalysis.isEmpty())
    {
        const int analysisIndex = ui->analysisComboBox->findData(previousAnalysis);
        if (analysisIndex >= 0 && analysisIndex != ui->analysisComboBox->currentIndex())
        {
            ui->analysisComboBox->setCurrentIndex(analysisIndex);
            on_analysisComboBox_currentIndexChanged(analysisIndex);
        }
    }

    if (!previousDir.isEmpty())
    {
        const int dirIndex = ui->resultDirComboBox->findData(previousDir);
        if (dirIndex >= 0)
            ui->resultDirComboBox->setCurrentIndex(dirIndex);
    }

    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);

    if (!previousTime.isNull())
    {
        const int timeIndex = ui->resultTimeComboBox->findData(previousTime);
        if (timeIndex >= 0 && timeIndex != ui->resultTimeComboBox->currentIndex())
        {
            ui->resultTimeComboBox->setCurrentIndex(timeIndex);
            on_resultTimeComboBox_currentIndexChanged(timeIndex);
        }
    }

    if (!previousStep.isEmpty())
    {
        const int previousIndex = ui->stepComboBox->findText(previousStep);
        if (previousIndex >= 0)
            ui->stepComboBox->setCurrentIndex(previousIndex);
    }

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
    if (!m_visualService)
        return;

    clearContourOverlay();

    if (m_actorAttached)
    {
        removeActorByService(m_visualService, m_actor);
        m_actorAttached = false;
    }

    removeLegendDragInteraction();
    m_visualService->showScalarBar(false, false);

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void NephogramSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
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
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);
}

void NephogramSettingWidget::on_resultTimeComboBox_currentIndexChanged(int)
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

void NephogramSettingWidget::on_scalarComboBox_currentIndexChanged(int)
{
    updateNephogram(currentScalarName());
}

void NephogramSettingWidget::readTecplot(const QString& fileName)
{
    QString errorMessage;
    m_grid = PostTecplotUtils::readGrid(fileName, &errorMessage);
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
                scalarNames << QString::fromUtf8(name);
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
                if (!scalarNames.contains(qName))
                    scalarNames << qName;
            }
        }
    }

    const QString previousScalar = currentScalarName();
    ResultDisplayNames::populateCombo(ui->scalarComboBox, scalarNames, previousScalar);
    updateNephogram(currentScalarName());
}

void NephogramSettingWidget::updateNephogram(const QString& selectedScalarName)
{
    if (m_loadingState || !m_grid || selectedScalarName.isEmpty())
        return;

    bool usePointData = true;
    vtkDataArray* scalarArray = findScalarArray(m_grid, selectedScalarName, usePointData);
    if (!scalarArray)
    {
        qWarning() << "Scalar array not found:" << selectedScalarName;
        return;
    }

    m_currentArrayName = selectedScalarName;

    double dataRange[2];
    scalarArray->GetRange(dataRange);
    if (!isValidScalarRange(dataRange))
    {
        qWarning() << "Scalar array has no valid range:" << selectedScalarName;
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }

    if (ui->autoRangeCheckBox->isChecked())
        setRangeControls(dataRange[0], dataRange[1]);

    double minV = dataRange[0];
    double maxV = dataRange[1];
    displayRange(dataRange[0], dataRange[1], minV, maxV);

    syncScalarBarFromCurrent();

    m_mapper->SetInputData(m_grid);
    m_actor->SetMapper(m_mapper);
    m_actor->GetProperty()->SetRepresentationToSurface();
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
    const QString previousDir = ui->resultDirComboBox->currentData().toString();
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
    if (!scalarBar)
        return;

    const double width = ui->legendWidthSpinBox->value() / 100.0;
    const double height = ui->legendHeightSpinBox->value() / 100.0;
    const double defaultX = std::max(0.01, 0.985 - width);
    const double defaultY = std::max(0.01, 0.5 * (1.0 - height));
    const double x = m_hasLegendPosition ? m_legendPositionX : defaultX;
    const double y = m_hasLegendPosition ? m_legendPositionY : defaultY;

    scalarBar->SetOrientationToVertical();
    scalarBar->UnconstrainedFontSizeOn();
    scalarBar->SetWidth(width);
    scalarBar->SetHeight(height);
    scalarBar->SetPosition(qBound(0.0, x, std::max(0.0, 1.0 - width)),
                           qBound(0.0, y, std::max(0.0, 1.0 - height)));
    scalarBar->SetMaximumWidthInPixels(1000);
    scalarBar->SetMaximumHeightInPixels(2000);
    // “刻度数量”表示色标上显示的刻度标签总数，不是数值间隔。
    // 交给 vtkScalarBarActor 在当前上下限之间均匀生成对应数量的刻度。
    scalarBar->UseCustomLabelsOff();
    scalarBar->SetCustomLabels(nullptr);
    scalarBar->SetNumberOfLabels(ui->legendTickCountSpinBox->value());
    const QByteArray labelFormat =
        QStringLiteral("%.%1g").arg(ui->legendPrecisionSpinBox->value()).toLatin1();
    scalarBar->SetLabelFormat(labelFormat.constData());
    scalarBar->DrawBackgroundOff();
    scalarBar->DrawFrameOff();

    vtkTextProperty* titleProperty = scalarBar->GetTitleTextProperty();
    vtkTextProperty* labelProperty = scalarBar->GetLabelTextProperty();
    if (titleProperty)
    {
        titleProperty->SetFontSize(ui->legendTitleFontSpinBox->value());
        titleProperty->SetColor(0.08, 0.08, 0.08);
        titleProperty->SetBold(1);
    }
    if (labelProperty)
    {
        labelProperty->SetFontSize(ui->legendLabelFontSpinBox->value());
        labelProperty->SetColor(0.08, 0.08, 0.08);
        labelProperty->SetBold(0);
    }
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
    if (!lut)
        return;

    if (!isValidScalarRange(minV, maxV))
    {
        minV = 0.0;
        maxV = 1.0;
    }

    if (std::abs(maxV - minV) < 1e-12)
        maxV = minV + 1.0;

    lut->SetNumberOfTableValues(256);
    lut->SetTableRange(minV, maxV);
    lut->SetHueRange(2.0 / 3.0, 0.0);
    lut->SetSaturationRange(1.0, 1.0);
    lut->SetValueRange(1.0, 1.0);
    lut->SetRampToLinear();
    lut->Build();
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
    obj[QStringLiteral("legendTitleFont")] = ui->legendTitleFontSpinBox->value();
    obj[QStringLiteral("legendLabelFont")] = ui->legendLabelFontSpinBox->value();
    obj[QStringLiteral("legendPrecision")] = ui->legendPrecisionSpinBox->value();
    obj[QStringLiteral("legendTickCount")] = ui->legendTickCountSpinBox->value();
    obj[QStringLiteral("legendWidth")] = ui->legendWidthSpinBox->value();
    obj[QStringLiteral("legendHeight")] = ui->legendHeightSpinBox->value();
    if (m_visualService && m_visualService->scalarBarActor())
    {
        const double* position = m_visualService->scalarBarActor()->GetPosition();
        obj[QStringLiteral("legendPositionX")] = position[0];
        obj[QStringLiteral("legendPositionY")] = position[1];
    }
    obj[QStringLiteral("contourEnabled")] = ui->contourEnableCheckBox->isChecked();
    obj[QStringLiteral("contourLevels")] = ui->contourLevelsSpinBox->value();
    obj[QStringLiteral("contourLabels")] = ui->contourLabelCheckBox->isChecked();
    obj[QStringLiteral("contourLabelFont")] = ui->contourLabelFontSpinBox->value();
    return obj;
}

void NephogramSettingWidget::loadJson(const QJsonObject& obj)
{
    QScopedValueRollback<bool> loadingGuard(m_loadingState, true);

    ui->autoRangeCheckBox->setChecked(obj.value(QStringLiteral("autoRange")).toBool(true));
    ui->rangeMinSpinBox->setValue(obj.value(QStringLiteral("rangeMin")).toDouble(0.0));
    ui->rangeMaxSpinBox->setValue(obj.value(QStringLiteral("rangeMax")).toDouble(1.0));
    ui->legendTitleFontSpinBox->setValue(
        obj.value(QStringLiteral("legendTitleFont")).toInt(16));
    ui->legendLabelFontSpinBox->setValue(
        obj.value(QStringLiteral("legendLabelFont")).toInt(13));
    ui->legendPrecisionSpinBox->setValue(
        obj.value(QStringLiteral("legendPrecision")).toInt(4));
    // 旧版本的 legendInterval 表示“数值间隔”，与现在的“刻度数量”语义不同，
    // 无法可靠换算；旧工程统一按默认 7 个刻度显示。
    ui->legendTickCountSpinBox->setValue(
        obj.value(QStringLiteral("legendTickCount")).toInt(7));
    ui->legendWidthSpinBox->setValue(obj.value(QStringLiteral("legendWidth")).toInt(10));
    ui->legendHeightSpinBox->setValue(obj.value(QStringLiteral("legendHeight")).toInt(65));
    m_hasLegendPosition = obj.contains(QStringLiteral("legendPositionX")) &&
                          obj.contains(QStringLiteral("legendPositionY"));
    if (m_hasLegendPosition)
    {
        m_legendPositionX = obj.value(QStringLiteral("legendPositionX")).toDouble();
        m_legendPositionY = obj.value(QStringLiteral("legendPositionY")).toDouble();
    }
    ui->contourEnableCheckBox->setChecked(
        obj.value(QStringLiteral("contourEnabled")).toBool(false));
    ui->contourLevelsSpinBox->setValue(obj.value(QStringLiteral("contourLevels")).toInt(10));
    ui->contourLabelCheckBox->setChecked(
        obj.value(QStringLiteral("contourLabels")).toBool(false));
    ui->contourLabelFontSpinBox->setValue(
        obj.value(QStringLiteral("contourLabelFont")).toInt(12));
    updateRangeEditorState();

    const QString analysisKey = obj.value(QStringLiteral("analysisKey")).toString();
    const QString resultDir = obj.value(QStringLiteral("resultDir")).toString();
    const QString resultTime = obj.value(QStringLiteral("resultTime")).toString();
    const QString step = obj.value(QStringLiteral("step")).toString();
    const QString scalar = obj.value(QStringLiteral("scalar")).toString();
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
    m_loadedFileName = fileName;

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

    if (auto* cd = grid->GetCellData())
    {
        const int n = cd->GetNumberOfArrays();

        for (int i = 0; i < n; ++i)
        {
            const char* name = cd->GetArrayName(i);

            if (name && *name)
            {
                const QString qName = QString::fromUtf8(name);
                if (!scalarNames.contains(qName))
                    scalarNames << qName;
            }
        }
    }

    const QString previousScalar = currentScalarName();
    ResultDisplayNames::populateCombo(ui->scalarComboBox, scalarNames, previousScalar);
    updateNephogram(currentScalarName());
}

void NephogramSettingWidget::clearContourOverlay()
{
    if (!m_visualService)
        return;

    if (auto* ren = m_visualService->renderer())
    {
        ren->RemoveActor(m_contourActor);
        ren->RemoveActor2D(m_contourLabelActor);
    }
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

    if (!ui->contourEnableCheckBox->isChecked())
        return;

    if (scalarName.isEmpty())
        return;

    // 保持逻辑简单：只抽外表面，然后用表面上的点数据生成等值线。
    // 如果结果是 CellData，只在抽出的表面上转 PointData，避免体内部单元参与平均。
    auto surfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfaceFilter->SetInputData(m_grid);
    surfaceFilter->Update();

    vtkPolyData* surface = surfaceFilter->GetOutput();
    if (!surface || surface->GetNumberOfPoints() <= 0 || surface->GetNumberOfCells() <= 0)
        return;

    vtkSmartPointer<vtkPolyData> contourInput = vtkSmartPointer<vtkPolyData>::New();

    if (usePointData)
    {
        contourInput->ShallowCopy(surface);
    }
    else
    {
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

    const QByteArray scalarNameUtf8 = scalarName.toUtf8();
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

    const int levels = std::max(1, ui->contourLevelsSpinBox->value());
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

    vtkPolyData* contourOutput = contour->GetOutput();
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
    contourMapper->SetLookupTable(m_mapper->GetLookupTable());
    contourMapper->SetScalarModeToUsePointFieldData();
    contourMapper->SelectColorArray(scalarNameUtf8.constData());
    contourMapper->SetScalarRange(minV, maxV);
    contourMapper->InterpolateScalarsBeforeMappingOn();
    contourMapper->ScalarVisibilityOn();
    contourMapper->SetResolveCoincidentTopologyToPolygonOffset();
    contourMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -1.0);

    m_contourActor->SetMapper(contourMapper);
    m_contourActor->PickableOff();
    m_contourActor->GetProperty()->LightingOff();
    m_contourActor->GetProperty()->SetOpacity(0.92);
    m_contourActor->GetProperty()->SetLineWidth(1.2);

    ren->AddActor(m_contourActor);

    if (!ui->contourLabelCheckBox->isChecked())
        return;

    vtkDataArray* contourScalars = contourData->GetPointData()
                                       ? contourData->GetPointData()->GetScalars()
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

    QVector<double> addedLevels;
    addedLevels.reserve(maxLabels);

    const vtkIdType cellCount = contourData->GetNumberOfCells();
    for (vtkIdType ci = 0; ci < cellCount; ++ci)
    {
        vtkCell* cell = contourData->GetCell(ci);
        if (!cell)
            continue;

        vtkIdList* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() <= 0)
            continue;

        const vtkIdType midId = ids->GetId(ids->GetNumberOfIds() / 2);
        const double v = contourScalars->GetComponent(midId, 0);

        bool exists = false;
        for (double lv : addedLevels)
        {
            if (std::abs(lv - v) <= tol)
            {
                exists = true;
                break;
            }
        }

        if (exists)
            continue;

        double p[3];
        contourData->GetPoint(midId, p);
        labelPts->InsertNextPoint(p);
        labelVals->InsertNextValue(v);
        addedLevels.push_back(v);

        if (addedLevels.size() >= maxLabels)
            break;
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
        QStringLiteral("%.%1g").arg(ui->legendPrecisionSpinBox->value()).toLatin1();
    labelMapper->SetLabelFormat(contourLabelFormat.constData());
    labelMapper->GetLabelTextProperty()->SetFontSize(ui->contourLabelFontSpinBox->value());
    labelMapper->GetLabelTextProperty()->SetColor(0.08, 0.08, 0.08);
    labelMapper->GetLabelTextProperty()->SetBold(1);
    labelMapper->GetLabelTextProperty()->SetShadow(1);

    m_contourLabelActor->SetMapper(labelMapper);
    ren->AddActor2D(m_contourLabelActor);
}
