#include "GeoStressPointPage.h"
#include "ui_GeoStressPointPage.h"

#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>

// VTK
#include <vtkUnstructuredGrid.h>

GeoStressPointPage::GeoStressPointPage(QWidget* parent)
    : BasePage("GeoStressPointPage")
{
    auto* ui = new Ui::GeoStressPointPage;
    ui->setupUi(this);

    m_sx  = ui->spinSx;
    m_sy  = ui->spinSy;
    m_sz  = ui->spinSz;
    m_sxy = ui->spinSxy;
    m_sxz = ui->spinSxz;
    m_syz = ui->spinSyz;

    m_btnGenerate = ui->btnGenerate;

    connect(m_btnGenerate, &QPushButton::clicked,
            this, &GeoStressPointPage::onGenerate);

    // 你也可以改默认值
    m_sx->setValue(0.0);
    m_sy->setValue(0.0);
    m_sz->setValue(0.0);
    m_sxy->setValue(0.0);
    m_sxz->setValue(0.0);
    m_syz->setValue(0.0);
}

GeoStressPointPage::~GeoStressPointPage() = default;

void GeoStressPointPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    //测点：节点拾取
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void GeoStressPointPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

void GeoStressPointPage::onGenerate()
{
    dumpGeneratedToDebug();
}

void GeoStressPointPage::dumpGeneratedToDebug()
{
    // 1) 取点集（vtkPid）
    const auto pids = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Node);
    if (pids.isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("请先在视图中点选一些测点（节点）。"));
        return;
    }

    // 2) 获取网格，拿点坐标
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
    if (!grid)
    {
        QMessageBox::warning(this, tr("错误"), tr("当前没有有效网格（vtkUnstructuredGrid）。"));
        return;
    }

    // 3) 读取统一应力 6 分量
    const double Sx  = m_sx->value();
    const double Sy  = m_sy->value();
    const double Sz  = m_sz->value();
    const double Sxy = m_sxy->value();
    const double Sxz = m_sxz->value();
    const double Syz = m_syz->value();

    // 4) 先按 3D：NDIM=3, NCOM=6
    const int NDIM = 3;
    const int NCOM = 6;
    const int NUM_GEO = pids.size();

    qDebug().noquote() << "========== IIWL.DAT Part1 (3D) ==========";
    qDebug().noquote() << QString("%1  %2  %3").arg(NUM_GEO).arg(NDIM).arg(NCOM);
    qDebug().noquote() << "XP       YP       ZP       SGX     SGY     SGZ     SXY     SXZ     SYZ     II";

    // 5) 对每个点输出一行：x y z + 6分量 + II
    //    注意：你给的老格式是 2D(x,y,Sx,Sy,Txy)，这里我们按你要求的 3D 扩展输出
    for (auto pid : pids)
    {
        if (pid < 0 || pid >= grid->GetNumberOfPoints())
            continue;

        double p[3] = {0,0,0};
        grid->GetPoint(pid, p);

        // II 这里先固定 1
        const int II = 1;

        const QString line = QString("%1  %2  %3  %4  %5  %6  %7  %8  %9  %10")
                                 .arg(p[0], 0, 'f', 6)
                                 .arg(p[1], 0, 'f', 6)
                                 .arg(p[2], 0, 'f', 6)
                                 .arg(Sx , 0, 'f', 6)
                                 .arg(Sy , 0, 'f', 6)
                                 .arg(Sz , 0, 'f', 6)
                                 .arg(Sxy, 0, 'f', 6)
                                 .arg(Sxz, 0, 'f', 6)
                                 .arg(Syz, 0, 'f', 6)
                                 .arg(II);

        qDebug().noquote() << line;
    }

    qDebug().noquote() << "=========================================";
}
