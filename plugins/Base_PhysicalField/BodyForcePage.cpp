#include "BodyForcePage.h"
#include "ui_BodyForcePage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include "PickManager.h"

BodyForcePage::BodyForcePage(QWidget* parent)
    : BasePage("BodyForcePage")
{
    auto* ui = new Ui::BodyForcePage;
    ui->setupUi(this);

    m_fbx = ui->spinFbx;
    m_fby = ui->spinFby;
    m_fbz = ui->spinFbz;
    m_ii  = ui->spinII;

    m_chkUniform  = ui->chkUniform;
    m_btnGenerate = ui->btnGenerate;

    // 默认值（可改）
    m_fbx->setValue(0.0);
    m_fby->setValue(0.0);
    m_fbz->setValue(-9.81);   // 比如重力
    m_ii->setValue(1);

    connect(m_btnGenerate, &QPushButton::clicked,
            this, &BodyForcePage::onGenerate);
    connect(m_chkUniform, &QCheckBox::toggled,
            this, &BodyForcePage::onUniformToggled);
}

BodyForcePage::~BodyForcePage() = default;

void BodyForcePage::beforeLoad()
{
    // Part3：体积力通常是“节点集合”（JD），所以用 Node
    PickManager::instance();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void BodyForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

void BodyForcePage::onUniformToggled(bool on)
{
    // 均布体力时，提示用户不必选点（但不强制）
    Q_UNUSED(on);
}

void BodyForcePage::onGenerate()
{
    dumpGeneratedToDebug();
}

void BodyForcePage::dumpGeneratedToDebug()
{
    const double FBX = m_fbx->value();
    const double FBY = m_fby->value();
    const double FBZ = m_fbz->value();
    const int II = m_ii->value();

    const bool isUniform = m_chkUniform->isChecked();

    // 取点集（vtkPid 列表）
    const auto pids = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Node);

    qDebug().noquote() << "========== IIWL.DAT Part3 (Body Force) ==========";

    if (isUniform)
    {
        // 均布体力：NFB=1, JD=0
        const int NFB = 1;
        qDebug().noquote() << QString::number(NFB);
        qDebug().noquote() << QString("JMFB   FBX  FBY  FBZ   II  (II=%1)").arg(II);

        const QString line = QString("%1  %2  %3  %4")
                                 .arg(0) // JD=0
                                 .arg(FBX, 0, 'f', 6)
                                 .arg(FBY, 0, 'f', 6)
                                 .arg(FBZ, 0, 'f', 6);
        qDebug().noquote() << line;

        qDebug().noquote() << "  [uniform] JD=0 means uniform body acceleration (m/s^2).";
        qDebug().noquote() << "===============================================";
        return;
    }

    // 非均布：必须选点
    if (pids.isEmpty())
    {
        QMessageBox::information(this, tr("提示"),
                                 tr("请先在视图中点选体力作用的节点（或勾选“均布体力”）。"));
        return;
    }

    const int NFB = pids.size();
    qDebug().noquote() << QString::number(NFB);
    qDebug().noquote() << QString("JMFB   FBX  FBY  FBZ   II  (II=%1)").arg(II);

    // 这里 JD 需要“节点号”。你 PickManager 现在返回的是 vtkPid。
    // 如果你网格里有 node_id 数组（你 PickManager 里也用过 "node_id"），我们就优先打印 node_id；
    // 否则先打印 vtkPid（后续你再统一成 node_id）。
    // 为了不引入 MeshService，这里只用 PickManager 已经缓存的 node_id 映射会更理想，
    // 但你当前 PickManager.cpp 里 node_id 的访问是 private，所以这里先按“vtkPid打印”，并额外打印提示。

    for (auto pid : pids)
    {
        // 先用 vtkPid 作为 JD（你后面如果要求严格的 node_id，我再帮你改成读 node_id 数组）
        const int JD = int(pid);

        const QString line = QString("%1  %2  %3  %4")
                                 .arg(JD)
                                 .arg(FBX, 0, 'f', 6)
                                 .arg(FBY, 0, 'f', 6)
                                 .arg(FBZ, 0, 'f', 6);
        qDebug().noquote() << line;
    }

    qDebug().noquote() << "  [note] JD is currently vtkPid. If you need real node_id, we should read PointData['node_id'].";
    qDebug().noquote() << "===============================================";
}
