#include "MeshGlobalMeshingWidget.h"

#include "NetgenMeshGenerator.h"
#include "ui_MeshGlobalMeshingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>

#include <QCoreApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QUrl>

#include <utility>

namespace
{
constexpr int kElemTypeHex8FromTet = 4;
}

MeshGlobalWorker::MeshGlobalWorker(Params p, QObject* parent)
    : QObject(parent), m_p(std::move(p))
{
}

void MeshGlobalWorker::run()
{
    NetgenMeshingRequest request;
    request.inputGeometryPath = m_p.inputStepPath;
    request.volPath = m_tempVolPath;
    request.vtkPath = m_tempVtkPath;
    request.nasPath = m_tempNasPath;
    request.dimension = m_p.dimension;
    request.elemType = m_p.elemType;
    request.lenMin = m_p.lenMin;
    request.lenMax = m_p.lenMax;
    request.sizeFactor = m_p.sizeFactor;
    request.elementOrder = m_p.elementOrder;
    request.smoothing = m_p.smoothing;
    request.fixShape = m_p.fixShape;
    request.tolerance = m_p.tolerance;
    request.exportVol = m_p.exportVol && m_p.elemType != kElemTypeHex8FromTet;
    request.exportVtk = m_p.exportVtk;
    request.exportNas = m_p.exportNas;

    if (m_p.elemType == kElemTypeHex8FromTet)
        emit log(QStringLiteral("Starting Netgen tetra mesh generation for Hex8 conversion..."));
    else
        emit log(QStringLiteral("Starting Netgen mesh generation..."));

    QString err;
    NetgenMeshingResult result;
    if (!NetgenMeshGenerator::generate(request, &result, &err))
    {
        emit finishedFail(err.isEmpty() ? QStringLiteral("Netgen mesh generation failed") : err);
        return;
    }

    if (m_p.elemType == kElemTypeHex8FromTet)
    {
        emit log(QStringLiteral(
                     "Hex8 mesh generated from Netgen Tet4: points=%1, boundary quads=%2, "
                     "hex=%3, min scaled Jacobian=%4")
                     .arg(result.pointCount)
                     .arg(result.surfaceElementCount)
                     .arg(result.volumeElementCount)
                     .arg(result.minScaledJacobian, 0, 'g', 6));
    }
    else
    {
        emit log(QStringLiteral("Netgen mesh generated: points=%1, surface=%2, volume=%3")
                     .arg(result.pointCount)
                     .arg(result.surfaceElementCount)
                     .arg(result.volumeElementCount));
    }
    emit finishedOK(m_tempVolPath, m_tempVtkPath, m_tempNasPath);
}

MeshGlobalMeshingWidget::MeshGlobalMeshingWidget(QWidget* parent)
    : BasePage("MeshGlobalMeshingWidget"), ui(new Ui::MeshGlobalMeshingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    initCombos();

    connect(ui->cmbDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MeshGlobalMeshingWidget::onDimChanged);
    connect(ui->cmbElemType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MeshGlobalMeshingWidget::onElemTypeChanged);

    onDimChanged(ui->cmbDim->currentIndex());
}

MeshGlobalMeshingWidget::~MeshGlobalMeshingWidget()
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    delete ui;
}

void MeshGlobalMeshingWidget::beforeLoad()
{
}

void MeshGlobalMeshingWidget::beforeUnload()
{
}

void MeshGlobalMeshingWidget::appendLog(const QString& s)
{
    emit logMessage(s);
}

void MeshGlobalMeshingWidget::initCombos()
{
    ui->cmbDim->clear();
    ui->cmbDim->addItem(QStringLiteral("2D Surface Mesh"), 2);
    ui->cmbDim->addItem(QStringLiteral("3D Volume Mesh"), 3);
    ui->cmbDim->setCurrentIndex(1);

    ui->dsMin->setValue(0.5);
    ui->dsMax->setValue(5.0);
    ui->dsFactor->setValue(1.0);
    ui->spOrder->setValue(1);
    ui->spSmooth->setValue(5);
    ui->chkFix->setChecked(true);
    ui->dsTol->setValue(1e-6);
}

void MeshGlobalMeshingWidget::refreshElemCombo(int dim)
{
    ui->cmbElemType->clear();
    if (dim == 2)
    {
        ui->cmbElemType->addItem(QStringLiteral("Triangle"), 1);
        ui->cmbElemType->addItem(QStringLiteral("Quad-dominated"), 2);
        ui->cmbElemType->setCurrentIndex(0);
    }
    else
    {
        ui->cmbElemType->addItem(QStringLiteral("Tet"), 3);
        ui->cmbElemType->addItem(QStringLiteral("Hex8 (Tet split)"), kElemTypeHex8FromTet);
        ui->cmbElemType->setCurrentIndex(0);
    }
}

void MeshGlobalMeshingWidget::refreshAlgoCombo(int dim)
{
    ui->cmbAlgo->clear();
    if (dim == 2)
        ui->cmbAlgo->addItem(QStringLiteral("Netgen Surface"), 1);
    else
        ui->cmbAlgo->addItem(QStringLiteral("Netgen Volume"), 1);
    ui->cmbAlgo->setCurrentIndex(0);
}

void MeshGlobalMeshingWidget::onDimChanged(int)
{
    const int dim = ui->cmbDim->currentData().toInt();
    refreshElemCombo(dim);
    refreshAlgoCombo(dim);
}

void MeshGlobalMeshingWidget::onElemTypeChanged(int)
{
    const bool hexFromTet = ui->cmbElemType->currentData().toInt() == kElemTypeHex8FromTet;
    if (hexFromTet)
        ui->spOrder->setValue(1);
    ui->spOrder->setMaximum(hexFromTet ? 1 : 2);
}

Params MeshGlobalMeshingWidget::collectParams() const
{
    Params p;
    p.dimension = ui->cmbDim->currentData().toInt();
    p.elemType = ui->cmbElemType->currentData().toInt();
    p.lenMin = ui->dsMin->value();
    p.lenMax = ui->dsMax->value();
    p.sizeFactor = ui->dsFactor->value();
    p.elementOrder = ui->spOrder->value();
    p.algorithm = ui->cmbAlgo->currentData().toInt();
    p.smoothing = ui->spSmooth->value();
    p.fixShape = ui->chkFix->isChecked();
    p.tolerance = ui->dsTol->value();
    return p;
}

QString MeshGlobalMeshingWidget::tempDirPath() const
{
    return QCoreApplication::applicationDirPath();
}

QString MeshGlobalMeshingWidget::tempStepPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("geom.step"));
}

QString MeshGlobalMeshingWidget::tempVolPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("mesh.vol"));
}

QString MeshGlobalMeshingWidget::tempVtkPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("mesh.vtk"));
}

QString MeshGlobalMeshingWidget::tempNasPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("mesh.nas"));
}

void MeshGlobalMeshingWidget::onGenerate()
{
    auto* preService = ServiceRegistry::instance().getService<PreProcessingService>();
    if (!preService)
    {
        QMessageBox::warning(this, QStringLiteral("Mesh"), QStringLiteral("PreProcessing service is not available"));
        return;
    }

    preService->generateStepFile();

    const QString filePath =
        QDir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data")))
            .filePath(QStringLiteral("ExportedModel.step"));

    Params p = collectParams();
    PublicVariables::instance().set(PublicConstants::kMeshDimension, p.dimension, "mesh");
    p.inputStepPath = filePath;

    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        const int localRuleCount = meshService->edgeControlRules().size() +
                                   meshService->faceControlRules().size() +
                                   meshService->volumeControlRules().size();
        if (localRuleCount > 0)
        {
            appendLog(QStringLiteral(
                "Netgen backend uses global meshing parameters; existing local entity rules are not mapped."));
        }
    }

    if (p.lenMin > p.lenMax)
    {
        QMessageBox::warning(this, QStringLiteral("Mesh"), QStringLiteral("Minimum size cannot be larger than maximum size"));
        return;
    }

    if (p.elemType == kElemTypeHex8FromTet && p.dimension != 3)
    {
        QMessageBox::warning(this, QStringLiteral("Mesh"), QStringLiteral("Hex8 conversion requires 3D volume meshing"));
        return;
    }

    if (p.inputStepPath.isEmpty() || !QFileInfo::exists(p.inputStepPath))
    {
        QMessageBox::warning(this, QStringLiteral("Mesh"), QStringLiteral("STEP file does not exist"));
        return;
    }

    m_lastGeneratedHex8 = p.elemType == kElemTypeHex8FromTet;

    QDir dir(tempDirPath());
    dir.mkpath(tempDirPath());
    QFile::remove(tempVolPath());
    QFile::remove(tempVtkPath());
    QFile::remove(tempNasPath());

    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    m_thread = new QThread(this);
    auto* worker = new MeshGlobalWorker(p);
    worker->m_tempDirPath = tempDirPath();
    worker->m_tempStepPath = tempStepPath();
    worker->m_tempVolPath = tempVolPath();
    worker->m_tempVtkPath = tempVtkPath();
    worker->m_tempNasPath = tempNasPath();
    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &MeshGlobalWorker::run);
    connect(worker, &MeshGlobalWorker::log, this,
            [this](const QString& s)
            {
                appendLog(s);
            });

    connect(worker, &MeshGlobalWorker::finishedOK, this,
            [this](const QString& vol, const QString& vtk, const QString& nas)
            {
                appendLog(QStringLiteral("Netgen mesh generation succeeded"));
                emit meshGenerated(vol, vtk, nas);

                auto* service =
                    ServiceRegistry::instance().getService<MeshModuleControllerService>();
                if (service)
                {
                    QString err;
                    if (!service->importGeneratedNetgenMesh(vtk, &err))
                    {
                        appendLog(QStringLiteral("Generated mesh import failed: %1").arg(err));
                        QMessageBox::warning(this, QStringLiteral("Mesh"),
                                             QStringLiteral("Mesh generated, but import failed: %1").arg(err));
                    }
                }

                if (m_thread)
                    m_thread->quit();
            });

    connect(worker, &MeshGlobalWorker::finishedFail, this,
            [this](const QString& err)
            {
                appendLog(QStringLiteral("Netgen mesh generation failed: %1").arg(err));
                QMessageBox::critical(this, QStringLiteral("Mesh"), err);
                if (m_thread)
                    m_thread->quit();
            });

    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    m_thread->start();
}

void MeshGlobalMeshingWidget::onExportAs()
{
    const QString filters =
        m_lastGeneratedHex8
            ? QStringLiteral("VTK (*.vtk);;Nastran (*.nas);;All (*.*)")
            : QStringLiteral("Netgen (*.vol);;VTK (*.vtk);;Nastran (*.nas);;All (*.*)");

    QString selectedFilter;
    QString savePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Mesh As"), QString(), filters, &selectedFilter);
    if (savePath.isEmpty())
        return;

    if (QFileInfo(savePath).suffix().isEmpty())
    {
        if (selectedFilter.startsWith(QStringLiteral("Nastran")))
            savePath += QStringLiteral(".nas");
        else if (!m_lastGeneratedHex8 && selectedFilter.startsWith(QStringLiteral("Netgen")))
            savePath += QStringLiteral(".vol");
        else
            savePath += QStringLiteral(".vtk");
    }

    QString src;
    if (savePath.endsWith(QStringLiteral(".vtk"), Qt::CaseInsensitive))
    {
        src = tempVtkPath();
    }
    else if (savePath.endsWith(QStringLiteral(".nas"), Qt::CaseInsensitive))
    {
        src = tempNasPath();
    }
    else if (savePath.endsWith(QStringLiteral(".vol"), Qt::CaseInsensitive))
    {
        if (m_lastGeneratedHex8)
        {
            QMessageBox::warning(
                this, QStringLiteral("Export"),
                QStringLiteral("Hex8 (Tet split) is not stored in Netgen VOL format. "
                               "Please export VTK or Nastran instead."));
            return;
        }
        src = tempVolPath();
    }
    else
    {
        QMessageBox::warning(
            this, QStringLiteral("Export"),
            QStringLiteral("Please use a supported extension: .vtk, .nas%1")
                .arg(m_lastGeneratedHex8 ? QString() : QStringLiteral(" or .vol")));
        return;
    }

    if (!QFileInfo::exists(src))
    {
        QMessageBox::warning(this, QStringLiteral("Export"), QStringLiteral("Temporary mesh file does not exist"));
        return;
    }

    QFile::remove(savePath);
    if (!QFile::copy(src, savePath))
    {
        QMessageBox::warning(this, QStringLiteral("Export"), QStringLiteral("Failed to copy mesh file"));
        return;
    }

    appendLog(QStringLiteral("Saved as: %1").arg(savePath));
}

void MeshGlobalMeshingWidget::onOpenTempDir()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(tempDirPath()));
}

void MeshGlobalMeshingWidget::on_generateButton_clicked()
{
    onGenerate();
}

void MeshGlobalMeshingWidget::showMeshVtk(const QString& vtkPath)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    auto reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
    reader->SetFileName(vtkPath.toLocal8Bit().constData());
    reader->Update();

    auto grid = reader->GetOutput();
    if (!grid || grid->GetNumberOfCells() == 0)
        return;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actorSurf = vtkSmartPointer<vtkActor>::New();
    actorSurf->SetMapper(mapper);
    actorSurf->GetProperty()->SetColor(0.7, 0.7, 0.7);
    actorSurf->GetProperty()->SetOpacity(0.35);
    actorSurf->GetProperty()->SetRepresentationToSurface();

    auto actorWire = vtkSmartPointer<vtkActor>::New();
    actorWire->SetMapper(mapper);
    actorWire->GetProperty()->SetColor(0.35, 0.35, 0.35);
    actorWire->GetProperty()->SetOpacity(1.0);
    actorWire->GetProperty()->SetRepresentationToWireframe();
    actorWire->GetProperty()->SetLineWidth(1.0);
    actorWire->GetProperty()->SetRenderLinesAsTubes(false);

    visual->removeProp("mesh.global.surf", false);
    visual->removeProp("mesh.global.wire", false);
    visual->removeProp("mesh.imported.surface", false);
    visual->removeProp("mesh.imported.line", false);

    visual->addProp("mesh.global.surf", actorSurf, QStringLiteral("{mesh} Netgen surface"));
    visual->addProp("mesh.global.wire", actorWire, QStringLiteral("{mesh} Netgen wire"));
    visual->setGroupVisible("Geometry", false, false);
    visual->resetCamera();
    visual->render();
}
