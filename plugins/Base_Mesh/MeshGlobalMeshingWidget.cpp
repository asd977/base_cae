#include "MeshGlobalMeshingWidget.h"
#include "ui_MeshGlobalMeshingWidget.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QThread>
#include <vtkUnstructuredGridReader.h>

// ===== 你的几何导出依赖（按你项目实际路径改 include）=====
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>

// #include <GeomManager/GeomManagerInstance.h>   // <<<<<< 按你工程实际改
// #include <GeomManager/GeomShapeObject.h>       // <<<<<< 按你工程实际改

// ===== gmsh =====
#include <gmsh.h>
#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <MeshGridIO.h>

#include <vtkProperty.h>
#include "MeshGlobalMeshingWidget.h"
#include "ui_MeshGlobalMeshingWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include <services/VisualWidgetService.h>
#include <vtkUnstructuredGrid.h>


namespace {
static std::set<int> parseIdSetForParams(const QString& text)
{
    std::set<int> ids;
    const auto parts = text.split(',', QString::SkipEmptyParts);
    for (QString p : parts) {
        p = p.trimmed();
        if (p.contains('-')) {
            const auto lr = p.split('-', QString::SkipEmptyParts);
            if (lr.size() == 2) {
                bool ok1=false, ok2=false;
                int a=lr[0].toInt(&ok1), b=lr[1].toInt(&ok2);
                if (ok1 && ok2) { if (a>b) std::swap(a,b); for (int i=a;i<=b;++i) ids.insert(i); }
            }
        } else { bool ok=false; int v=p.toInt(&ok); if (ok) ids.insert(v); }
    }
    return ids;
}
}

// ---------------- Widget ----------------
MeshGlobalMeshingWidget::MeshGlobalMeshingWidget(QWidget* parent)
    : BasePage("MeshGlobalMeshingWidget")
    , ui(new Ui::MeshGlobalMeshingWidget)
{
    ui->setupUi(this);

    initCombos();


    connect(ui->cmbDim, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MeshGlobalMeshingWidget::onDimChanged);

    connect(ui->cmbElemType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MeshGlobalMeshingWidget::onElemTypeChanged);

    onDimChanged(ui->cmbDim->currentIndex());
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
    ui->cmbDim->addItem("2D（面网格）", 2);
    ui->cmbDim->addItem("3D（体网格）", 3);
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
        ui->cmbElemType->addItem("三角形（Tri）", 1);
        ui->cmbElemType->addItem("四边形（Quad，重组）", 2);
        ui->cmbElemType->setCurrentIndex(0);
    }
    else
    {
        ui->cmbElemType->addItem("四面体（Tet）", 3);
        ui->cmbElemType->addItem("六面体（Hex，重组）", 4);
        ui->cmbElemType->setCurrentIndex(0);
    }
}

void MeshGlobalMeshingWidget::refreshAlgoCombo(int dim)
{
    ui->cmbAlgo->clear();
    if (dim == 2)
    {
        ui->cmbAlgo->addItem("Automatic (2)", 2);
        ui->cmbAlgo->addItem("Delaunay (5)", 5);
        ui->cmbAlgo->addItem("Frontal-Delaunay (6)", 6);
        ui->cmbAlgo->addItem("DelQuad (8)", 8);
        ui->cmbAlgo->setCurrentIndex(0);
    }
    else
    {
        ui->cmbAlgo->addItem("Delaunay (1)", 1);
        ui->cmbAlgo->addItem("Frontal (4)", 4);
        ui->cmbAlgo->addItem("HXT (10)", 10);
        ui->cmbAlgo->setCurrentIndex(0);
    }
}

void MeshGlobalMeshingWidget::onDimChanged(int)
{
    const int dim = ui->cmbDim->currentData().toInt();
    refreshElemCombo(dim);
    refreshAlgoCombo(dim);
}

void MeshGlobalMeshingWidget::onElemTypeChanged(int)
{
    // 这里目前不需要特殊处理，保留扩展点
}

Params MeshGlobalMeshingWidget::collectParams() const
{
    Params p;

    p.dimension    = ui->cmbDim->currentData().toInt();
    p.elemType     = ui->cmbElemType->currentData().toInt();
    p.lenMin       = ui->dsMin->value();
    p.lenMax       = ui->dsMax->value();
    p.sizeFactor   = ui->dsFactor->value();
    p.elementOrder = ui->spOrder->value();
    p.algorithm    = ui->cmbAlgo->currentData().toInt();
    p.smoothing    = ui->spSmooth->value();
    p.fixShape     = ui->chkFix->isChecked();
    p.tolerance    = ui->dsTol->value();

    return p;
}

QString MeshGlobalMeshingWidget::tempDirPath() const
{

    QString fileName = "ExportedModel.step";
    QString currentWorPath = QCoreApplication::applicationDirPath();
    return currentWorPath;
}
QString MeshGlobalMeshingWidget::tempStepPath() const { return tempDirPath() + "/geom.step"; }
QString MeshGlobalMeshingWidget::tempMshPath()  const { return tempDirPath() + "/mesh.msh"; }
QString MeshGlobalMeshingWidget::tempVtkPath()  const { return tempDirPath() + "/mesh.vtk"; }
QString MeshGlobalMeshingWidget::tempNasPath()  const { return tempDirPath() + "/mesh.nas"; }

void MeshGlobalMeshingWidget::onGenerate()
{

    PreProcessingService *service = ServiceRegistry::instance().getService<PreProcessingService>();

    Params p = collectParams();

    if (service) {
        p.occShapeHandle = service->createCompoundShapeHandle();
    }

    if (!p.occShapeHandle) {
        if (!service) {
            QMessageBox::warning(this, "整体剖分", "预处理服务不可用。");
            return;
        }
        service->generateStepFile();
        QString fileName = "ExportedModel.step";
        QString currentWorPath = QCoreApplication::applicationDirPath();
        p.inputStepPath = currentWorPath + "/data/" + fileName;
        appendLog("内存几何不可用，回退到 STEP 文件流程。");
    } else {
        appendLog("直接使用内存几何，不经 STEP 文件中转。");
    }
    auto releaseHandleIfNeeded = [&]()
    {
        if (service && p.occShapeHandle) {
            service->releaseCompoundShapeHandle(p.occShapeHandle);
            p.occShapeHandle = nullptr;
        }
    };

    if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>()) {
        p.edgeRules = meshSvc->edgeControlRules();
        p.faceRules = meshSvc->faceControlRules();
        p.volumeRules = meshSvc->volumeControlRules();
        appendLog(QString("读取局部控制：边%1 面%2 体%3")
                      .arg(p.edgeRules.size())
                      .arg(p.faceRules.size())
                      .arg(p.volumeRules.size()));
    }

    if (service) {
        auto collectCentersAndMeasures = [&](int dim,
                                             const std::set<int>& ids,
                                             QMap<int, QVector3D>& centerDst,
                                             QMap<int, double>& measureDst) {
            for (int id : ids) {
                QVector3D c;
                if (service->entityCenter(dim, id, c)) {
                    centerDst.insert(id, c);
                }
                double m = 0.0;
                if (service->entityMeasure(dim, id, m) && m > 0.0) {
                    measureDst.insert(id, m);
                }
            }
        };

        std::set<int> edgeIds, faceIds, volumeIds;
        for (const auto& r : p.edgeRules) {
            const auto ids = parseIdSetForParams(r.edgeIds);
            edgeIds.insert(ids.begin(), ids.end());
        }
        for (const auto& r : p.faceRules) {
            const auto ids = parseIdSetForParams(r.faceIds);
            faceIds.insert(ids.begin(), ids.end());
        }
        for (const auto& r : p.volumeRules) {
            const auto ids = parseIdSetForParams(r.volumeIds);
            volumeIds.insert(ids.begin(), ids.end());
        }

        collectCentersAndMeasures(1, edgeIds, p.edgeCenters, p.edgeMeasures);
        collectCentersAndMeasures(2, faceIds, p.faceCenters, p.faceMeasures);
        collectCentersAndMeasures(3, volumeIds, p.volumeCenters, p.volumeMeasures);
    }

    if (p.lenMin > p.lenMax)
    {
        QMessageBox::warning(this, "整体剖分", "最小尺寸不能大于最大尺寸。");
        releaseHandleIfNeeded();
        return;
    }

    if (!p.occShapeHandle && (p.inputStepPath.isEmpty() || !QFileInfo::exists(p.inputStepPath)))
    {
        QMessageBox::warning(this, "整体剖分", "STEP 文件不存在。");
        releaseHandleIfNeeded();
        return;
    }


    // 准备 temp 目录
    QDir dir(tempDirPath());
    dir.mkpath(tempDirPath());

    appendLog("准备临时目录：" + tempDirPath());

    // 如果已有线程，先收掉
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    // 启动 worker
    m_thread = new QThread(this);
    auto* worker = new MeshGlobalWorker(p);
    worker->m_tempDirPath  = tempDirPath();
    worker->m_tempStepPath = tempStepPath();
    worker->m_tempMshPath  = tempMshPath();
    worker->m_tempVtkPath  = tempVtkPath();
    worker->m_tempNasPath  = tempNasPath();

    auto releaseShapeHandle = [service, worker]()
    {
        if (!service || !worker || !worker->m_p.occShapeHandle) return;
        service->releaseCompoundShapeHandle(worker->m_p.occShapeHandle);
        worker->m_p.occShapeHandle = nullptr;
    };

    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &MeshGlobalWorker::run);
    connect(worker, &MeshGlobalWorker::log, this, [this](const QString& s){ appendLog(s); });

    connect(worker, &MeshGlobalWorker::finishedOK, this,
            [this, releaseShapeHandle](const QString& msh, const QString& vtk, const QString& nas)
            {
                releaseShapeHandle();
                appendLog("整体剖分成功。");
                emit meshGenerated(msh, vtk, nas);
                QMessageBox::information(this, "整体剖分", "生成成功！");
//                showMeshVtk(tempVtkPath());
                MeshModuleControllerService *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                if (service) {
                    QString err;
                    if (!service->importCurrentGmshMesh(&err)) {
                        appendLog(QString("内存导入失败：%1").arg(err));
                        QMessageBox::warning(this, "整体剖分", QString("网格生成成功，但内存导入失败：%1").arg(err));
                    }
                }

                if (m_thread) { m_thread->quit(); }
            });

    connect(worker, &MeshGlobalWorker::finishedFail, this,
            [this, releaseShapeHandle](const QString& err)
            {
                releaseShapeHandle();
                appendLog("整体剖分失败：" + err);
                QMessageBox::critical(this, "整体剖分", err);
                if (m_thread) { m_thread->quit(); }
            });

    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    m_thread->start();
}

void MeshGlobalMeshingWidget::onExportAs()
{
    const QString savePath = QFileDialog::getSaveFileName(
        this, "另存为网格", QString(),
        "Gmsh (*.msh);;VTK (*.vtk);;Nastran (*.nas);;All (*.*)"
        );
    if (savePath.isEmpty()) return;

    // 这里不再调用 gmsh::write（因为生成在 worker 线程里，UI 线程不碰 gmsh）
    // 直接复制 tempGrid 里的文件即可
    QString src;
    if (savePath.endsWith(".msh", Qt::CaseInsensitive)) src = tempMshPath();
    else if (savePath.endsWith(".vtk", Qt::CaseInsensitive)) src = tempVtkPath();
    else if (savePath.endsWith(".nas", Qt::CaseInsensitive)) src = tempNasPath();
    else src = tempMshPath(); // 默认 msh

    if (!QFileInfo::exists(src))
    {
        QMessageBox::warning(this, "导出", "临时网格文件不存在，请先生成网格。");
        return;
    }

    QFile::remove(savePath);
    if (!QFile::copy(src, savePath))
    {
        QMessageBox::warning(this, "导出", "复制失败。");
        return;
    }

    appendLog("已另存为：" + savePath);
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
    if (!visual) return;

    auto reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
    reader->SetFileName(vtkPath.toLocal8Bit().constData());
    reader->Update();

    auto grid = reader->GetOutput();
    if (!grid || grid->GetNumberOfCells() == 0) return;

    // 共享 mapper（表面 + 线框用同一份数据）
    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    // 1) 表面 actor：灰色 + 半透明
    auto actorSurf = vtkSmartPointer<vtkActor>::New();
    actorSurf->SetMapper(mapper);
    {
        auto* p = actorSurf->GetProperty();
        p->SetColor(0.7, 0.7, 0.7);   // 灰色
        p->SetOpacity(0.35);          // 半透明（0~1）
        p->SetRepresentationToSurface();

        // 灰色半透明更“干净”的常用设置（可选）
        p->SetAmbient(0.35);
        p->SetDiffuse(0.65);
        p->SetSpecular(0.0);
    }

    // 2) 线框 actor：灰色线（叠加）
    auto actorWire = vtkSmartPointer<vtkActor>::New();
    actorWire->SetMapper(mapper);
    {
        auto* p = actorWire->GetProperty();
        p->SetColor(0.35, 0.35, 0.35);   // 深一点的灰，线更清楚
        p->SetOpacity(1.0);
        p->SetRepresentationToWireframe();
        p->SetLineWidth(1.0);            // 需要更粗就 1.5/2.0

        // 避免“虚线感”（不同平台不一定有用）
        p->SetRenderLinesAsTubes(false);
    }

    // 清理旧的显示（你 clearAll 和 removeProp 二选一就行，建议别同时用）
     visual->clearAll();
//    visual->removeProp("mesh.global.surf", true);
//    visual->removeProp("mesh.global.wire", true);

    // 加入显示
    visual->addProp("mesh.global.surf", actorSurf, QStringLiteral("{mesh} 全局网格-表面"));
    visual->addProp("mesh.global.wire", actorWire, QStringLiteral("{mesh} 全局网格-线框"));

    visual->resetCamera();
    visual->render();

    const vtkIdType nPts   = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();

    qDebug()<<QString("网格加载成功：Points=%1, Elements(Cells)=%2")
              .arg((qlonglong)nPts)
              .arg((qlonglong)nCells);

}
