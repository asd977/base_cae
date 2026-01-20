#include "TemperatureLoadPage.h"
#include "ui_TemperatureLoadPage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include "PickManager.h"

TemperatureLoadPage::TemperatureLoadPage(QWidget* parent)
    : BasePage("TemperatureLoadPage")
{
    auto* ui = new Ui::TemperatureLoadPage;
    ui->setupUi(this);

    m_alfa = ui->spinAlfa;
    m_dt   = ui->spinDT;
    m_ii   = ui->spinII;

    m_btnGenerate = ui->btnGenerate;

    // 默认值
    m_alfa->setValue(0.0);
    m_dt->setValue(0.0);
    m_ii->setValue(1);

    connect(m_btnGenerate, &QPushButton::clicked,
            this, &TemperatureLoadPage::onGenerate);
}

TemperatureLoadPage::~TemperatureLoadPage() = default;

void TemperatureLoadPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    // Part7：按文档“J1..Jn 节点号” => 选点
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void TemperatureLoadPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

void TemperatureLoadPage::onGenerate()
{
    dumpGeneratedToDebug();
}

void TemperatureLoadPage::dumpGeneratedToDebug()
{
    const auto pids = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Node);
    if (pids.isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("请先在视图中点选变温作用的节点（J1..Jn）。"));
        return;
    }

    const int NFW = pids.size();
    const double ALFA = m_alfa->value();
    const double DT   = m_dt->value();
    const int II      = m_ii->value();

    qDebug().noquote() << "========== IIWL.DAT Part7 (Temperature Load) ==========";
    qDebug().noquote() << QString("%1").arg(NFW);
    qDebug().noquote() << "JMFW  ALFA  DT  II  (说明行)";
    qDebug().noquote() << QString("  [info] uniform: ALFA=%1, DT=%2, II=%3")
                              .arg(ALFA, 0, 'f', 6)
                              .arg(DT,   0, 'f', 6)
                              .arg(II);

    // 这里按“每行输出节点 + 参数”的形式（更可解析）
    // JMFW：这里直接用 J（节点号）作为 JMFW
    for (auto pid : pids)
    {
        const int J = int(pid); // 先用 vtkPid 代替；你要 node_id 我再给你改成读数组
        const QString line = QString("%1  %2  %3  %4")
                                 .arg(J)
                                 .arg(ALFA, 0, 'f', 6)
                                 .arg(DT,   0, 'f', 6)
                                 .arg(II);
        qDebug().noquote() << line;
    }

    qDebug().noquote() << "  [note] J is currently vtk pointId. If you need solver node id, output PointData['node_id'] mapping.";
    qDebug().noquote() << "========================================================";
}
