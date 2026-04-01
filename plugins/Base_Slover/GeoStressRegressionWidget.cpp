#include "GeoStressRegressionWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTextStream>

#include "ui_GeoStressRegressionWidget.h"

GeoStressRegressionWidget::GeoStressRegressionWidget(QWidget* parent)
    : BasePage("GeoStressRegressionAnalysis"), ui(new Ui::GeoStressRegressionWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    ui->prepDirEdit->setText(ensureTrailingSep(wd.filePath("PREP")));
    connect(ui->btnCompute, &QPushButton::clicked, this, &GeoStressRegressionWidget::onComputeClicked);
}

GeoStressRegressionWidget::~GeoStressRegressionWidget()
{
    delete ui;
}

void GeoStressRegressionWidget::beforeLoad()
{
}

void GeoStressRegressionWidget::beforeUnload()
{
}

bool GeoStressRegressionWidget::exportIGEOSDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    QFile file(wd.filePath("IGEOS.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const QString prepDir = ensureTrailingSep(wd.filePath("PREP"));

    os << "'" << prepDir << "'       1\n";
    os << "'" << ui->roofEdit->text().trimmed() << "'       2\n";
    os << "'" << ui->leftEdit->text().trimmed() << "'       3\n";
    os << "'" << ui->rightEdit->text().trimmed() << "'       4\n";
    os << "'" << ui->frontEdit->text().trimmed() << "'       5\n";
    os << "'" << ui->backEdit->text().trimmed() << "'       6\n";
    os << "'" << ui->factorEdit->text().trimmed() << "'       7\n";
    os << "'0'       8\n";
    os << "'0'       9\n";
    os << "'FTLOAD.DAT'      10\n";
    os << "'0'      11\n";
    os << "'   0.00000'      12\n";

    os << "   1     NCHANGE\n";
    os << "   1     1     1    MACHAN    MACHAN  II\n";
    os << "   0    NUM_GEO\n";
    os << "IDIR       HEIGHT      GAMA          PGROUT\n";
    os << " 0.000000E+00   0.000000E+00     0.000000E+00   0.000000E+00   0.000000E+00       1\n";

    os << "   4    NFA_GEO\n";
    os << " FNAME        IDIR       HEIGHT      GAMA          PGROUT          II\n";
    os << "'" << ui->leftEdit->text().trimmed() << "'     1    " << ui->heightEdit->text().trimmed()
       << "    " << ui->gamaEdit->text().trimmed() << "   0.000000E+00     1\n";
    os << "'" << ui->rightEdit->text().trimmed() << "'     1    " << ui->heightEdit->text().trimmed()
       << "    " << ui->gamaEdit->text().trimmed() << "   0.000000E+00     2\n";
    os << "'" << ui->frontEdit->text().trimmed() << "'     2    " << ui->heightEdit->text().trimmed()
       << "    " << ui->gamaEdit->text().trimmed() << "   0.000000E+00     3\n";
    os << "'" << ui->backEdit->text().trimmed() << "'     2    " << ui->heightEdit->text().trimmed()
       << "    " << ui->gamaEdit->text().trimmed() << "   0.000000E+00     4\n";

    return true;
}

QJsonObject GeoStressRegressionWidget::saveJson()
{
    QJsonObject obj;
    obj["prep_dir"] = ui->prepDirEdit->text().trimmed();
    obj["roof"] = ui->roofEdit->text().trimmed();
    obj["fa_left"] = ui->leftEdit->text().trimmed();
    obj["fa_right"] = ui->rightEdit->text().trimmed();
    obj["fa_front"] = ui->frontEdit->text().trimmed();
    obj["fa_back"] = ui->backEdit->text().trimmed();
    obj["factor"] = ui->factorEdit->text().trimmed();
    obj["height"] = ui->heightEdit->text().trimmed();
    obj["gama"] = ui->gamaEdit->text().trimmed();
    return obj;
}

void GeoStressRegressionWidget::loadJson(const QJsonObject& obj)
{
    ui->prepDirEdit->setText(obj.value("prep_dir").toString(ui->prepDirEdit->text()));
    ui->roofEdit->setText(obj.value("roof").toString(ui->roofEdit->text()));
    ui->leftEdit->setText(obj.value("fa_left").toString(ui->leftEdit->text()));
    ui->rightEdit->setText(obj.value("fa_right").toString(ui->rightEdit->text()));
    ui->frontEdit->setText(obj.value("fa_front").toString(ui->frontEdit->text()));
    ui->backEdit->setText(obj.value("fa_back").toString(ui->backEdit->text()));
    ui->factorEdit->setText(obj.value("factor").toString(ui->factorEdit->text()));
    ui->heightEdit->setText(obj.value("height").toString(ui->heightEdit->text()));
    ui->gamaEdit->setText(obj.value("gama").toString(ui->gamaEdit->text()));
}

void GeoStressRegressionWidget::onComputeClicked()
{
    emit computeRequested();
}
