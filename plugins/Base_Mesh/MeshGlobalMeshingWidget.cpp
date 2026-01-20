#include "MeshGlobalMeshingWidget.h"
#include "ui_MeshGlobalMeshingWidget.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QThread>

// ===== 你的几何导出依赖（按你项目实际路径改 include）=====
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>

// #include <GeomManager/GeomManagerInstance.h>   // <<<<<< 按你工程实际改
// #include <GeomManager/GeomShapeObject.h>       // <<<<<< 按你工程实际改

// ===== gmsh =====
#include <gmsh.h>



#include "MeshGlobalMeshingWidget.h"
#include "ui_MeshGlobalMeshingWidget.h"
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
    const QString base = QCoreApplication::applicationDirPath();
    return base + "./tmp";
}
QString MeshGlobalMeshingWidget::tempStepPath() const { return tempDirPath() + "/geom.step"; }
QString MeshGlobalMeshingWidget::tempMshPath()  const { return tempDirPath() + "/mesh.msh"; }
QString MeshGlobalMeshingWidget::tempVtkPath()  const { return tempDirPath() + "/mesh.vtk"; }
QString MeshGlobalMeshingWidget::tempNasPath()  const { return tempDirPath() + "/mesh.nas"; }

void MeshGlobalMeshingWidget::onGenerate()
{
    const Params p = collectParams();

    if (p.lenMin > p.lenMax)
    {
        QMessageBox::warning(this, "整体剖分", "最小尺寸不能大于最大尺寸。");
        return;
    }

    if (p.inputStepPath.isEmpty() || !QFileInfo::exists(p.inputStepPath))
    {
        QMessageBox::warning(this, "整体剖分", "STEP 文件不存在。");
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

    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &MeshGlobalWorker::run);
    connect(worker, &MeshGlobalWorker::log, this, [this](const QString& s){ appendLog(s); });

    connect(worker, &MeshGlobalWorker::finishedOK, this,
            [this](const QString& msh, const QString& vtk, const QString& nas)
            {
                appendLog("整体剖分成功。");
                emit meshGenerated(msh, vtk, nas);
                QMessageBox::information(this, "整体剖分", "生成成功！");
                if (m_thread) { m_thread->quit(); }
            });

    connect(worker, &MeshGlobalWorker::finishedFail, this,
            [this](const QString& err)
            {
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

