#include "CaveBlockStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTextStream>

#include "CaveBlockPlanePage.h"
#include "ui_CaveBlockStabilityWidget.h"

CaveBlockStabilityWidget::CaveBlockStabilityWidget(QWidget* parent)
    : BasePage("CaveBlockStabilityAnalysis"), ui(new Ui::CaveBlockStabilityWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    connect(ui->computeBtn, &QPushButton::clicked, this,
            [this]()
            {
                emit computeRequested();
            });
}

CaveBlockStabilityWidget::~CaveBlockStabilityWidget()
{
    delete ui;
}

void CaveBlockStabilityWidget::beforeLoad()
{
}

void CaveBlockStabilityWidget::beforeUnload()
{
}

QJsonObject CaveBlockStabilityWidget::saveJson()
{
    QJsonObject obj;
    obj["icut_file"] = ui->icutFileEdit->text().trimmed();
    obj["safe_file"] = ui->safeFileEdit->text().trimmed();
    obj["weight"] = ui->weightEdit->text().trimmed();
    obj["x_min"] = ui->xMinEdit->text().trimmed();
    obj["x_max"] = ui->xMaxEdit->text().trimmed();
    obj["y_min"] = ui->yMinEdit->text().trimmed();
    obj["y_max"] = ui->yMaxEdit->text().trimmed();
    obj["z_min"] = ui->zMinEdit->text().trimmed();
    obj["z_max"] = ui->zMaxEdit->text().trimmed();
    obj["fx"] = ui->fxEdit->text().trimmed();
    obj["fy"] = ui->fyEdit->text().trimmed();
    obj["fz"] = ui->fzEdit->text().trimmed();
    obj["if_dir"] = ui->ifDirEdit->text().trimmed();
    obj["ax"] = ui->axEdit->text().trimmed();
    obj["ay"] = ui->ayEdit->text().trimmed();
    obj["az"] = ui->azEdit->text().trimmed();
    obj["bx"] = ui->bxEdit->text().trimmed();
    obj["by"] = ui->byEdit->text().trimmed();
    obj["bz"] = ui->bzEdit->text().trimmed();
    obj["im1"] = ui->im1Edit->text().trimmed();
    obj["im2"] = ui->im2Edit->text().trimmed();
    return obj;
}

void CaveBlockStabilityWidget::loadJson(const QJsonObject& obj)
{
    ui->icutFileEdit->setText(obj.value("icut_file").toString(ui->icutFileEdit->text()));
    ui->safeFileEdit->setText(obj.value("safe_file").toString(ui->safeFileEdit->text()));
    ui->weightEdit->setText(obj.value("weight").toString(ui->weightEdit->text()));
    ui->xMinEdit->setText(obj.value("x_min").toString(ui->xMinEdit->text()));
    ui->xMaxEdit->setText(obj.value("x_max").toString(ui->xMaxEdit->text()));
    ui->yMinEdit->setText(obj.value("y_min").toString(ui->yMinEdit->text()));
    ui->yMaxEdit->setText(obj.value("y_max").toString(ui->yMaxEdit->text()));
    ui->zMinEdit->setText(obj.value("z_min").toString(ui->zMinEdit->text()));
    ui->zMaxEdit->setText(obj.value("z_max").toString(ui->zMaxEdit->text()));
    ui->fxEdit->setText(obj.value("fx").toString(ui->fxEdit->text()));
    ui->fyEdit->setText(obj.value("fy").toString(ui->fyEdit->text()));
    ui->fzEdit->setText(obj.value("fz").toString(ui->fzEdit->text()));
    ui->ifDirEdit->setText(obj.value("if_dir").toString(ui->ifDirEdit->text()));
    ui->axEdit->setText(obj.value("ax").toString(ui->axEdit->text()));
    ui->ayEdit->setText(obj.value("ay").toString(ui->ayEdit->text()));
    ui->azEdit->setText(obj.value("az").toString(ui->azEdit->text()));
    ui->bxEdit->setText(obj.value("bx").toString(ui->bxEdit->text()));
    ui->byEdit->setText(obj.value("by").toString(ui->byEdit->text()));
    ui->bzEdit->setText(obj.value("bz").toString(ui->bzEdit->text()));
    ui->im1Edit->setText(obj.value("im1").toString(ui->im1Edit->text()));
    ui->im2Edit->setText(obj.value("im2").toString(ui->im2Edit->text()));
}

bool CaveBlockStabilityWidget::exportDatFiles(const QVector<CaveBlockPlanePage*>& planePages) const
{
    if (planePages.isEmpty())
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());

    if (!wd.exists("SliderCase") && !wd.mkpath("SliderCase"))
        return false;

    QDir sliderDir(wd.filePath("SliderCase"));

    QFile icutFile(sliderDir.filePath(valueOrDefault(ui->icutFileEdit, "ICUT.DAT")));
    if (!icutFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream icut(&icutFile);
    icut << QString("%1 1\n").arg(planePages.size());
    icut << "X1 Y1 Z1 X2 Y2 Z2 X3 Y3 Z3 II\n";
    for (int i = 0; i < planePages.size(); ++i)
        icut << planePages[i]->icutLine(i + 1) << "\n";

    QFile paraFile(sliderDir.filePath("PARA.DAT"));
    if (!paraFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream para(&paraFile);
    para << QString("%1\n").arg(planePages.size());
    para << "剪摩fp 剪摩C(kPa) 抗拉强度Rt(kPa) II\n";
    for (int i = 0; i < planePages.size(); ++i)
        para << planePages[i]->paraLine(i + 1) << "\n";

    QFile mesg(sliderDir.filePath("MESG227.DAT"));
    if (!mesg.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&mesg);
    os << quoteValue("---") << "\n";
    os << quoteValue(valueOrDefault(ui->icutFileEdit, "ICUT.DAT")) << "\n";
    os << quoteValue(valueOrDefault(ui->safeFileEdit, "SAFE_CAVEBLK.DAT")) << "\n";
    os << quoteValue(valueOrDefault(ui->weightEdit, "23.52")) << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(ui->xMinEdit, "-1.0E6"))
                         .arg(valueOrDefault(ui->xMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(ui->yMinEdit, "-1.0E6"))
                         .arg(valueOrDefault(ui->yMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(ui->zMinEdit, "-1.0E6"))
                         .arg(valueOrDefault(ui->zMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(valueOrDefault(ui->fxEdit, "-50.0"))
                         .arg(valueOrDefault(ui->fyEdit, "0.0"))
                         .arg(valueOrDefault(ui->fzEdit, "0.0")))
       << "\n";
    os << quoteValue(valueOrDefault(ui->ifDirEdit, "0")) << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(valueOrDefault(ui->axEdit, "0.0"))
                         .arg(valueOrDefault(ui->ayEdit, "0.0"))
                         .arg(valueOrDefault(ui->azEdit, "0.0")))
       << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(valueOrDefault(ui->bxEdit, "-1.0"))
                         .arg(valueOrDefault(ui->byEdit, "-1.0"))
                         .arg(valueOrDefault(ui->bzEdit, "-1.0")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(ui->im1Edit, "1"))
                         .arg(valueOrDefault(ui->im2Edit, "2")))
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
