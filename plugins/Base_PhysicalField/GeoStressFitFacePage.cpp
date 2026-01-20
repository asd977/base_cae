#include "GeoStressFitFacePage.h"
#include "ui_GeoStressFitFacePage.h"

#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include "PickManager.h"

GeoStressFitFacePage::GeoStressFitFacePage(QWidget* parent)
    : BasePage("GeoStressFitFacePage")
{
    auto* ui = new Ui::GeoStressFitFacePage;
    ui->setupUi(this);

    m_fname  = ui->editFname;
    m_idir   = ui->comboIdir;
    m_height = ui->spinHeight;
    m_rou    = ui->spinRou;
    m_pgrout = ui->spinPgrout;
    m_ii     = ui->spinII;
    m_btnGenerate = ui->btnGenerate;

    // 默认值（你按需要改）
    m_fname->setText("LEFT.DAT");
    m_idir->setCurrentIndex(0);     // 1
    m_height->setValue(0.0);
    m_rou->setValue(2700.0);
    m_pgrout->setValue(0.0);
    m_ii->setValue(1);

    connect(m_btnGenerate, &QPushButton::clicked,
            this, &GeoStressFitFacePage::onGenerate);
}

GeoStressFitFacePage::~GeoStressFitFacePage() = default;

void GeoStressFitFacePage::beforeLoad()
{
    // Part2：选“面”
    PickManager::instance();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
}

void GeoStressFitFacePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

void GeoStressFitFacePage::onGenerate()
{
    dumpGeneratedToDebug();
}

void GeoStressFitFacePage::dumpGeneratedToDebug()
{
    // 取 faceKey 集合（PickManager 里 Face 用的是 faceKey）
    const auto faceKeys = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face);
    if (faceKeys.isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("请先在视图中点选一些边界面。"));
        return;
    }

    const int NFA_GEO = faceKeys.size();

    // 参数（统一应用）
    QString fname = m_fname->text().trimmed();
    if (fname.isEmpty()) fname = "LEFT.DAT";

    const int IDIR = m_idir->currentData().toInt();  // 1/2/3
    const double HEIGHT = m_height->value();
    const double ROU    = m_rou->value();
    const double PGROUT = m_pgrout->value();
    const int II = m_ii->value();

    qDebug().noquote() << "========== IIWL.DAT Part2 ==========";
    qDebug().noquote() << QString("%1").arg(NFA_GEO);
    qDebug().noquote() << "FNAME   IDIR  HEIGHT  GAMA  PGROUT   II";

    // 每个面输出一行：目前按“同一套参数”，逐行输出（与格式一致）
    // 注：面几何信息（faceKey/cellId/faceLocalId）当前不写进 IIWL.DAT 的这部分，
    //     这部分只记录“该边界面对应的文件名与梯形荷载参数”。
    for (int i = 0; i < faceKeys.size(); ++i)
    {
        Q_UNUSED(faceKeys[i]);

        const QString line = QString("\"%1\"  %2  %3  %4  %5  %6")
                                 .arg(fname)
                                 .arg(IDIR)
                                 .arg(HEIGHT, 0, 'f', 6)
                                 .arg(ROU,    0, 'f', 6)
                                 .arg(PGROUT, 0, 'f', 6)
                                 .arg(II);

        qDebug().noquote() << line;
    }

    qDebug().noquote() << "====================================";
}
