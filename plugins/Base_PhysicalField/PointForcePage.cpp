#include "PointForcePage.h"
#include "ui_PointForcePage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <ServiceRegistry.h>

#include "PickManager.h"

#include <services/MeshModuleControllerService.h>

PointForcePage::PointForcePage(QWidget* parent)
    : BasePage("PointForcePage")
{
    auto* ui = new Ui::PointForcePage;
    ui->setupUi(this);

    m_x   = ui->spinX;
    m_y   = ui->spinY;
    m_z   = ui->spinZ;
    m_fcx = ui->spinFcx;
    m_fcy = ui->spinFcy;
    m_fcz = ui->spinFcz;
    m_ii  = ui->spinII;

    m_btnGenerate = ui->btnGenerate;

    // 默认值（可改）
    m_x->setValue(0.0);
    m_y->setValue(0.0);
    m_z->setValue(0.0);
    m_fcx->setValue(0.0);
    m_fcy->setValue(0.0);
    m_fcz->setValue(0.0);
    m_ii->setValue(1);

    connect(m_btnGenerate, &QPushButton::clicked,
            this, &PointForcePage::onGenerate);
}

PointForcePage::~PointForcePage() = default;

void PointForcePage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void PointForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}


void PointForcePage::onGenerate()
{
    dumpGeneratedToDebug();
}

void PointForcePage::dumpGeneratedToDebug()
{
    // 1) 取“点集”（vtkPid）
    const auto pids = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Node);
    if (pids.isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("请先在视图中点选一些网格点（集中力作用点）。"));
        return;
    }

    // 2) 获取网格
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
    if (!grid)
    {
        QMessageBox::warning(this, tr("错误"), tr("当前没有有效网格（vtkUnstructuredGrid）。"));
        return;
    }

    // 3) 读取集中力分量
    const double FCX = m_fcx->value();
    const double FCY = m_fcy->value();
    const double FCZ = m_fcz->value();
    const int II     = m_ii->value();

    // 4) 建立 pid -> 归属单元(KE) 的映射（用 vtkCellId 先代替 KE）
    //    若一个点属于多个单元：这里取“第一次遇到的单元”（稳定、快）
    QHash<vtkIdType, vtkIdType> pid2cell;
    pid2cell.reserve(pids.size() * 2);

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell) continue;

        vtkIdList* ids = cell->GetPointIds();
        if (!ids) continue;

        const vtkIdType np = ids->GetNumberOfIds();
        for (vtkIdType i = 0; i < np; ++i)
        {
            const vtkIdType pid = ids->GetId(i);
            if (!pid2cell.contains(pid))
                pid2cell.insert(pid, cid);
        }
    }

    // 5) 输出
    const int NFC = pids.size();

    qDebug().noquote() << "========== IIWL.DAT Part4 (Point Force) ==========";
    qDebug().noquote() << QString::number(NFC);
    qDebug().noquote() << "JMFC  X  Y  Z  FCX  FCY  FCZ  II";

    for (auto pid : pids)
    {
        if (pid < 0 || pid >= grid->GetNumberOfPoints())
            continue;

        double p[3] = {0,0,0};
        grid->GetPoint(pid, p);

        // KE：用该点归属的任意一个单元（当前用 vtk cellId 代替）
        const vtkIdType cid = pid2cell.value(pid, -1);
        const int KE = (cid >= 0) ? int(cid) : -1;

        // 一行：KE x y z fcx fcy fcz ii
        const QString line = QString("%1  %2  %3  %4  %5  %6  %7  %8")
                                 .arg(KE)
                                 .arg(p[0], 0, 'f', 6)
                                 .arg(p[1], 0, 'f', 6)
                                 .arg(p[2], 0, 'f', 6)
                                 .arg(FCX, 0, 'f', 6)
                                 .arg(FCY, 0, 'f', 6)
                                 .arg(FCZ, 0, 'f', 6)
                                 .arg(II);

        qDebug().noquote() << line;
    }

    qDebug().noquote()
        << "  [note] KE is currently vtk cellId. If you have CellData['elem_id'] (or solver element id mapping), replace KE with that.";
    qDebug().noquote() << "==================================================";
}
