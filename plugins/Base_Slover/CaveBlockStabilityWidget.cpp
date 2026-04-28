#include "CaveBlockStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTextStream>
#include <QVBoxLayout>

#include "CaveBlockPlanePage.h"

CaveBlockStabilityWidget::CaveBlockStabilityWidget(QWidget* parent)
    : BasePage("CaveBlockStabilityAnalysis")
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel("洞室块体稳定性分析（生成 ICUT.DAT / PARA.DAT / MESG227.DAT）", this));

    auto* fileForm = new QFormLayout();
    m_icutFileEdit = new QLineEdit("ICUT.DAT", this);
    m_safeFileEdit = new QLineEdit("SAFE_CAVEBLK.DAT", this);
    m_weightEdit = new QLineEdit("23.52", this);
    fileForm->addRow("块体切割面文件", m_icutFileEdit);
    fileForm->addRow("块体稳定性文件", m_safeFileEdit);
    fileForm->addRow("块体容重(kN/m3)", m_weightEdit);
    root->addLayout(fileForm);

    auto* xyzGrid = new QGridLayout();
    xyzGrid->addWidget(new QLabel("", this), 0, 0);
    xyzGrid->addWidget(new QLabel("MIN", this), 0, 1);
    xyzGrid->addWidget(new QLabel("MAX", this), 0, 2);
    m_xMinEdit = new QLineEdit("-1.0E6", this);
    m_xMaxEdit = new QLineEdit("1.0E6", this);
    m_yMinEdit = new QLineEdit("-1.0E6", this);
    m_yMaxEdit = new QLineEdit("1.0E6", this);
    m_zMinEdit = new QLineEdit("-1.0E6", this);
    m_zMaxEdit = new QLineEdit("1.0E6", this);
    xyzGrid->addWidget(new QLabel("x方向", this), 1, 0);
    xyzGrid->addWidget(m_xMinEdit, 1, 1);
    xyzGrid->addWidget(m_xMaxEdit, 1, 2);
    xyzGrid->addWidget(new QLabel("y方向", this), 2, 0);
    xyzGrid->addWidget(m_yMinEdit, 2, 1);
    xyzGrid->addWidget(m_yMaxEdit, 2, 2);
    xyzGrid->addWidget(new QLabel("z方向", this), 3, 0);
    xyzGrid->addWidget(m_zMinEdit, 3, 1);
    xyzGrid->addWidget(m_zMaxEdit, 3, 2);
    root->addLayout(xyzGrid);

    auto* loadGrid = new QGridLayout();
    m_fxEdit = new QLineEdit("-50.0", this);
    m_fyEdit = new QLineEdit("0.0", this);
    m_fzEdit = new QLineEdit("0.0", this);
    loadGrid->addWidget(new QLabel("支护力 FX", this), 0, 0);
    loadGrid->addWidget(m_fxEdit, 0, 1);
    loadGrid->addWidget(new QLabel("FY", this), 0, 2);
    loadGrid->addWidget(m_fyEdit, 0, 3);
    loadGrid->addWidget(new QLabel("FZ", this), 0, 4);
    loadGrid->addWidget(m_fzEdit, 0, 5);
    root->addLayout(loadGrid);

    auto* dirGrid = new QGridLayout();
    m_ifDirEdit = new QLineEdit("0", this);
    m_axEdit = new QLineEdit("0.0", this);
    m_ayEdit = new QLineEdit("0.0", this);
    m_azEdit = new QLineEdit("0.0", this);
    m_bxEdit = new QLineEdit("-1.0", this);
    m_byEdit = new QLineEdit("-1.0", this);
    m_bzEdit = new QLineEdit("-1.0", this);
    m_im1Edit = new QLineEdit("1", this);
    m_im2Edit = new QLineEdit("2", this);
    dirGrid->addWidget(new QLabel("是否指定滑动方向", this), 0, 0);
    dirGrid->addWidget(m_ifDirEdit, 0, 1);
    dirGrid->addWidget(new QLabel("点A(X Y Z)", this), 1, 0);
    dirGrid->addWidget(m_axEdit, 1, 1);
    dirGrid->addWidget(m_ayEdit, 1, 2);
    dirGrid->addWidget(m_azEdit, 1, 3);
    dirGrid->addWidget(new QLabel("点B(X Y Z)", this), 2, 0);
    dirGrid->addWidget(m_bxEdit, 2, 1);
    dirGrid->addWidget(m_byEdit, 2, 2);
    dirGrid->addWidget(m_bzEdit, 2, 3);
    dirGrid->addWidget(new QLabel("切割面1", this), 3, 0);
    dirGrid->addWidget(m_im1Edit, 3, 1);
    dirGrid->addWidget(new QLabel("切割面2", this), 3, 2);
    dirGrid->addWidget(m_im2Edit, 3, 3);
    root->addLayout(dirGrid);

    root->addStretch();
}

QJsonObject CaveBlockStabilityWidget::saveJson()
{
    QJsonObject obj;
    obj["icut_file"] = m_icutFileEdit->text().trimmed();
    obj["safe_file"] = m_safeFileEdit->text().trimmed();
    obj["weight"] = m_weightEdit->text().trimmed();
    obj["x_min"] = m_xMinEdit->text().trimmed();
    obj["x_max"] = m_xMaxEdit->text().trimmed();
    obj["y_min"] = m_yMinEdit->text().trimmed();
    obj["y_max"] = m_yMaxEdit->text().trimmed();
    obj["z_min"] = m_zMinEdit->text().trimmed();
    obj["z_max"] = m_zMaxEdit->text().trimmed();
    obj["fx"] = m_fxEdit->text().trimmed();
    obj["fy"] = m_fyEdit->text().trimmed();
    obj["fz"] = m_fzEdit->text().trimmed();
    obj["if_dir"] = m_ifDirEdit->text().trimmed();
    obj["ax"] = m_axEdit->text().trimmed();
    obj["ay"] = m_ayEdit->text().trimmed();
    obj["az"] = m_azEdit->text().trimmed();
    obj["bx"] = m_bxEdit->text().trimmed();
    obj["by"] = m_byEdit->text().trimmed();
    obj["bz"] = m_bzEdit->text().trimmed();
    obj["im1"] = m_im1Edit->text().trimmed();
    obj["im2"] = m_im2Edit->text().trimmed();
    return obj;
}

void CaveBlockStabilityWidget::loadJson(const QJsonObject& obj)
{
    m_icutFileEdit->setText(obj.value("icut_file").toString(m_icutFileEdit->text()));
    m_safeFileEdit->setText(obj.value("safe_file").toString(m_safeFileEdit->text()));
    m_weightEdit->setText(obj.value("weight").toString(m_weightEdit->text()));
    m_xMinEdit->setText(obj.value("x_min").toString(m_xMinEdit->text()));
    m_xMaxEdit->setText(obj.value("x_max").toString(m_xMaxEdit->text()));
    m_yMinEdit->setText(obj.value("y_min").toString(m_yMinEdit->text()));
    m_yMaxEdit->setText(obj.value("y_max").toString(m_yMaxEdit->text()));
    m_zMinEdit->setText(obj.value("z_min").toString(m_zMinEdit->text()));
    m_zMaxEdit->setText(obj.value("z_max").toString(m_zMaxEdit->text()));
    m_fxEdit->setText(obj.value("fx").toString(m_fxEdit->text()));
    m_fyEdit->setText(obj.value("fy").toString(m_fyEdit->text()));
    m_fzEdit->setText(obj.value("fz").toString(m_fzEdit->text()));
    m_ifDirEdit->setText(obj.value("if_dir").toString(m_ifDirEdit->text()));
    m_axEdit->setText(obj.value("ax").toString(m_axEdit->text()));
    m_ayEdit->setText(obj.value("ay").toString(m_ayEdit->text()));
    m_azEdit->setText(obj.value("az").toString(m_azEdit->text()));
    m_bxEdit->setText(obj.value("bx").toString(m_bxEdit->text()));
    m_byEdit->setText(obj.value("by").toString(m_byEdit->text()));
    m_bzEdit->setText(obj.value("bz").toString(m_bzEdit->text()));
    m_im1Edit->setText(obj.value("im1").toString(m_im1Edit->text()));
    m_im2Edit->setText(obj.value("im2").toString(m_im2Edit->text()));
}

bool CaveBlockStabilityWidget::exportDatFiles(const QVector<CaveBlockPlanePage*>& planePages) const
{
    if (planePages.isEmpty())
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());

    QFile icutFile(wd.filePath(valueOrDefault(m_icutFileEdit, "ICUT.DAT")));
    if (!icutFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream icut(&icutFile);
    icut << QString("%1 1\n").arg(planePages.size());
    icut << "X1 Y1 Z1 X2 Y2 Z2 X3 Y3 Z3 II\n";
    for (int i = 0; i < planePages.size(); ++i)
        icut << planePages[i]->icutLine(i + 1) << "\n";

    QFile paraFile(wd.filePath("PARA.DAT"));
    if (!paraFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream para(&paraFile);
    para << QString("%1\n").arg(planePages.size());
    para << "剪摩fp 剪摩C(kPa) 抗拉强度Rt(kPa) II\n";
    for (int i = 0; i < planePages.size(); ++i)
        para << planePages[i]->paraLine(i + 1) << "\n";

    QFile mesg(wd.filePath("MESG227.DAT"));
    if (!mesg.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&mesg);
    os << quoteValue("---") << "\n";
    os << quoteValue(valueOrDefault(m_icutFileEdit, "ICUT.DAT")) << "\n";
    os << quoteValue(valueOrDefault(m_safeFileEdit, "SAFE_CAVEBLK.DAT")) << "\n";
    os << quoteValue(valueOrDefault(m_weightEdit, "23.52")) << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(m_xMinEdit, "-1.0E6"))
                         .arg(valueOrDefault(m_xMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(m_yMinEdit, "-1.0E6"))
                         .arg(valueOrDefault(m_yMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(m_zMinEdit, "-1.0E6"))
                         .arg(valueOrDefault(m_zMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(valueOrDefault(m_fxEdit, "-50.0"))
                         .arg(valueOrDefault(m_fyEdit, "0.0"))
                         .arg(valueOrDefault(m_fzEdit, "0.0")))
       << "\n";
    os << quoteValue(valueOrDefault(m_ifDirEdit, "0")) << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(valueOrDefault(m_axEdit, "0.0"))
                         .arg(valueOrDefault(m_ayEdit, "0.0"))
                         .arg(valueOrDefault(m_azEdit, "0.0")))
       << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(valueOrDefault(m_bxEdit, "-1.0"))
                         .arg(valueOrDefault(m_byEdit, "-1.0"))
                         .arg(valueOrDefault(m_bzEdit, "-1.0")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(m_im1Edit, "1"))
                         .arg(valueOrDefault(m_im2Edit, "2")))
       << "\n";
    os << quoteValue("---") << "\n";
    os << quoteValue("---") << "\n";
    os << quoteValue("---") << "\n";

    return true;
}

QString CaveBlockStabilityWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString CaveBlockStabilityWidget::valueOrDefault(QLineEdit* edit, const QString& def) const
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}
