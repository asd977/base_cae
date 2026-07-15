#include "NephogramSettingWidget.h"

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
#include <vtkScalarBarActor.h>
#include <vtkSelectVisiblePoints.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "PostResultAutoPrepare.h"
#include "PostVisualUtils.h"
#include "ui_NephogramSettingWidget.h"

namespace
{
QString uniqueDisplayName(const QString& baseName, const QMap<QString, QString>& existing)
{
    if (!existing.contains(baseName))
        return baseName;

    int index = 2;
    while (existing.contains(QStringLiteral("%1 (%2)").arg(baseName).arg(index)))
        ++index;

    return QStringLiteral("%1 (%2)").arg(baseName).arg(index);
}
} // namespace

NephogramSettingWidget::NephogramSettingWidget(VisualWidgetService* postProcessingWidget,
                                               QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessNephogram), ui(new Ui::NephogramSettingWidget)
{
    ui->setupUi(this);
    m_visualService = postProcessingWidget;

    m_actor = vtkSmartPointer<vtkActor>::New();
    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_contourActor = vtkSmartPointer<vtkActor>::New();
    m_contourLabelActor = vtkSmartPointer<vtkActor2D>::New();

    connect(ui->stepComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_stepComboBox_currentIndexChanged);
    connect(ui->scalarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_scalarComboBox_currentIndexChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NephogramSettingWidget::on_resultDirComboBox_currentIndexChanged);


    connect(ui->contourEnableCheckBox, &QCheckBox::toggled, this, [this](bool) {
        updateNephogram(ui->scalarComboBox->currentText());
    });
    connect(ui->contourLevelsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        updateNephogram(ui->scalarComboBox->currentText());
    });
    connect(ui->contourLabelCheckBox, &QCheckBox::toggled, this, [this](bool) {
        updateNephogram(ui->scalarComboBox->currentText());
    });

    initResultDirectoryOptions();
}

NephogramSettingWidget::~NephogramSettingWidget()
{
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
    if (!m_grid || m_currentArrayName.isEmpty())
        return;

    bool usePointData = true;
    vtkDataArray* arr = findScalarArray(m_grid, m_currentArrayName, usePointData);
    if (!arr)
        return;

    double range[2];
    arr->GetRange(range);

    vtkScalarBarActor* scalarBar = m_visualService ? m_visualService->scalarBarActor() : nullptr;
    if (!scalarBar)
        return;

    if (m_visualService)
        m_visualService->showScalarBar(true, false);

    scalarBar->SetWidth(0.08);
    scalarBar->SetHeight(0.5);
    scalarBar->SetPosition(0.90, 0.25);
    scalarBar->GetTitleTextProperty()->SetColor(0.0, 0.0, 0.0);
    scalarBar->GetLabelTextProperty()->SetColor(0.0, 0.0, 0.0);

    if (auto* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable()))
    {
        updateLookupTableRange(lut, range[0], range[1]);
        scalarBar->SetTitle(m_currentArrayName.toUtf8().constData());
        configureMapper(m_mapper, m_currentArrayName, usePointData, range[0], range[1], lut);
    }
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

    const QString previousDir = ui->resultDirComboBox->currentData().toString();
    const QString previousStep = ui->stepComboBox->currentText();

    initResultDirectoryOptions();

    if (!previousDir.isEmpty())
    {
        const int dirIndex = ui->resultDirComboBox->findData(previousDir);
        if (dirIndex >= 0)
            ui->resultDirComboBox->setCurrentIndex(dirIndex);
    }

    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);

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

    m_visualService->showScalarBar(false, false);

    if (auto* rw = m_visualService->renderWindow())
        rw->Render();
}

void NephogramSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const QString stepText = ui->stepComboBox->itemText(index);
    const QString filePath = m_stepFileMap.value(stepText);

    if (filePath.isEmpty())
    {
        qWarning() << "No data file mapped for step:" << stepText;
        return;
    }

    if (QFileInfo(filePath).suffix().compare(QStringLiteral("vtu"), Qt::CaseInsensitive) == 0)
    {
        readVtu(filePath);
        return;
    }

    readTecplot(filePath);
}

void NephogramSettingWidget::loadFileName(const QString& dataPath)
{
    ui->stepComboBox->blockSignals(true);
    ui->stepComboBox->clear();
    m_stepFileMap.clear();

    QDir directory(dataPath);

    if (!directory.exists())
    {
        qWarning() << "Directory not found:" << dataPath;
        ui->stepComboBox->blockSignals(false);
        return;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    const QVector<PostResultAutoPrepare::ResultFile> preparedFiles =
        PostResultAutoPrepare::resultFiles(projectDir, dataPath, true);
    if (!preparedFiles.isEmpty())
    {
        for (const PostResultAutoPrepare::ResultFile& file : preparedFiles)
        {
            const QString stepName = uniqueDisplayName(
                file.displayName.trimmed().isEmpty() ? QStringLiteral("结果数据")
                                                     : file.displayName.trimmed(),
                m_stepFileMap);
            m_stepFileMap.insert(stepName, file.filePath);
            ui->stepComboBox->addItem(stepName);
            ui->stepComboBox->setItemData(ui->stepComboBox->count() - 1,
                                          QStringLiteral("求解完成后自动生成"),
                                          Qt::ToolTipRole);
        }

        ui->stepComboBox->blockSignals(false);
        ui->stepComboBox->setCurrentIndex(0);
        on_stepComboBox_currentIndexChanged(ui->stepComboBox->currentIndex());
        return;
    }

    ui->stepComboBox->blockSignals(false);
    ui->scalarComboBox->clear();
    qWarning() << "No prepared post-processing results found in" << dataPath;
}

void NephogramSettingWidget::on_resultDirComboBox_currentIndexChanged(int)
{
    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);
}

void NephogramSettingWidget::on_scalarComboBox_currentIndexChanged(int)
{
    updateNephogram(ui->scalarComboBox->currentText());
}

void NephogramSettingWidget::readTecplot(const QString& fileName)
{
    vtkSmartPointer<vtkTecplotReader> reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());
    reader->Update();

    vtkMultiBlockDataSet* multi = reader->GetOutput();

    if (!multi || multi->GetNumberOfBlocks() <= 0)
    {
        qWarning() << "Tecplot contains no readable blocks:" << fileName;
        return;
    }

    vtkSmartPointer<vtkAppendFilter> appender = vtkSmartPointer<vtkAppendFilter>::New();

    int addedBlocks = 0;
    for (int i = 0; i < multi->GetNumberOfBlocks(); ++i)
    {
        if (auto* block = vtkUnstructuredGrid::SafeDownCast(multi->GetBlock(i)))
        {
            appender->AddInputData(block);
            ++addedBlocks;
        }
    }

    if (addedBlocks == 0)
    {
        qWarning() << "Tecplot has no unstructured grid blocks:" << fileName;
        return;
    }

    appender->Update();
    m_grid = appender->GetOutput();
    m_loadedFileName = fileName;

    if (!m_grid || m_grid->GetNumberOfPoints() == 0)
    {
        qWarning() << "Failed to combine blocks for" << fileName;
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
    if (m_loadingState)
        return;

    if (!m_grid)
    {
        qWarning() << "No grid to visualize.";
        return;
    }

    bool usePointData = true;
    vtkDataArray* scalarArray = findScalarArray(m_grid, selectedScalarName, usePointData);

    if (!scalarArray)
    {
        qWarning() << "Scalar array not found:" << selectedScalarName;
        return;
    }

    m_currentArrayName = selectedScalarName;
    syncScalarBarFromCurrent();

    m_mapper->SetInputData(m_grid);
    m_actor->SetMapper(m_mapper);
    m_actor->GetProperty()->SetRepresentationToSurface();

    ensureActorAttached();

    double contourRange[2];
    scalarArray->GetRange(contourRange);

    updateContourOverlay(selectedScalarName, usePointData, contourRange[0], contourRange[1]);

    if (auto* rw = m_visualService ? m_visualService->renderWindow() : nullptr)
        rw->Render();
}

void NephogramSettingWidget::initResultDirectoryOptions()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    ui->resultDirComboBox->blockSignals(true);
    ui->resultDirComboBox->clear();

    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);
    const QDir project(projectDir);
    for (const PostResultAutoPrepare::ResultGroup& group : groups)
    {
        const QString relPath = QDir::fromNativeSeparators(
            project.relativeFilePath(group.sourcePath));
        ui->resultDirComboBox->addItem(group.displayName, relPath);

        const int index = ui->resultDirComboBox->count() - 1;
        ui->resultDirComboBox->setItemData(
            index, QStringLiteral("自动识别的计算结果"), Qt::ToolTipRole);
    }

    if (ui->resultDirComboBox->count() > 0)
        ui->resultDirComboBox->setCurrentIndex(0);

    ui->resultDirComboBox->blockSignals(false);
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
    obj["scalar"] = m_currentArrayName;
    obj["resultDir"] = ui->resultDirComboBox->currentData().toString();
    obj["step"] = ui->stepComboBox->currentText();
    return obj;
}

void NephogramSettingWidget::loadJson(const QJsonObject& obj)
{
    QScopedValueRollback<bool> loadingGuard(m_loadingState, true);

    const QString resultDir = obj.value(QStringLiteral("resultDir")).toString();
    const QString step = obj.value(QStringLiteral("step")).toString();
    const QString scalar = obj.value(QStringLiteral("scalar")).toString();

    initResultDirectoryOptions();

    if (!resultDir.isEmpty())
    {
        const int dirIndex = ui->resultDirComboBox->findData(resultDir);
        if (dirIndex >= 0)
            ui->resultDirComboBox->setCurrentIndex(dirIndex);
    }

    m_dataPath = selectedResultDirectoryPath();
    loadFileName(m_dataPath);

    if (!step.isEmpty())
    {
        const int stepIndex = ui->stepComboBox->findText(step);
        if (stepIndex >= 0)
            ui->stepComboBox->setCurrentIndex(stepIndex);
    }

    if (!scalar.isEmpty())
    {
        const int scalarIndex = ui->scalarComboBox->findText(scalar);
        if (scalarIndex >= 0)
            ui->scalarComboBox->setCurrentIndex(scalarIndex);
        m_currentArrayName = scalar;
    }
    else
    {
        m_currentArrayName = ui->scalarComboBox->currentText();
    }
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

    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();
    ui->scalarComboBox->addItems(scalarNames);

    if (ui->scalarComboBox->count() > 0)
        ui->scalarComboBox->setCurrentIndex(0);

    ui->scalarComboBox->blockSignals(false);

    updateNephogram(ui->scalarComboBox->currentText());
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

    // 用表面数据自己的范围，不用整体体数据范围。
    // 否则某些层值可能根本不落在当前表面范围内，结果就是一条线也没有。
    double range[2];
    contourArray->GetRange(range);

    if (std::abs(range[1] - range[0]) < 1e-12)
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
        contour->SetValue(0, 0.5 * (range[0] + range[1]));
    }
    else
    {
        // 不画 min/max 两端线，减少边界脏线。
        for (int i = 0; i < levels; ++i)
        {
            const double t = double(i + 1) / double(levels + 1);
            contour->SetValue(i, range[0] + (range[1] - range[0]) * t);
        }
    }

    contour->Update();

    vtkPolyData* contourOutput = contour->GetOutput();
    if (!contourOutput || contourOutput->GetNumberOfCells() <= 0 ||
        contourOutput->GetNumberOfPoints() <= 0)
    {
        qWarning() << "No contour generated for" << scalarName
                   << "surface range:" << range[0] << range[1]
                   << "input range:" << minV << maxV;
        return;
    }

    auto contourData = vtkSmartPointer<vtkPolyData>::New();
    contourData->ShallowCopy(contourOutput);

    auto contourMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    contourMapper->SetInputData(contourData);
    contourMapper->ScalarVisibilityOff();
    contourMapper->SetResolveCoincidentTopologyToPolygonOffset();
    contourMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -1.0);

    m_contourActor->SetMapper(contourMapper);
    m_contourActor->PickableOff();
    m_contourActor->GetProperty()->LightingOff();
    m_contourActor->GetProperty()->SetColor(0.08, 0.08, 0.08);
    m_contourActor->GetProperty()->SetOpacity(0.85);
    m_contourActor->GetProperty()->SetLineWidth(1.0);

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
    const double span = std::max(std::abs(range[1] - range[0]), 1e-12);
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
    labelMapper->SetLabelFormat("%.3g");
    labelMapper->GetLabelTextProperty()->SetFontSize(11);
    labelMapper->GetLabelTextProperty()->SetColor(0.08, 0.08, 0.08);

    m_contourLabelActor->SetMapper(labelMapper);
    ren->AddActor2D(m_contourLabelActor);
}
