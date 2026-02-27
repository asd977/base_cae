#include "SeepageWidget.h"
#include "ui_SeepageWidget.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <PublicConstants.h>
#include <PublicVariables.h>

SeepageWidget::SeepageWidget(QWidget *parent) :
    BasePage("SeepageAnalysis"),
    ui(new Ui::SeepageWidget)
{
    ui->setupUi(this);
    connect(ui->btnCompute, &QPushButton::clicked, this, &SeepageWidget::onComputeClicked);
}

SeepageWidget::~SeepageWidget()
{
    delete ui;
}

void SeepageWidget::beforeLoad() {}

void SeepageWidget::beforeUnload() {}

double SeepageWidget::upstreamWaterLevel() const
{
    return ui->spinUpstreamWaterLevel->value();
}

double SeepageWidget::downstreamWaterLevel() const
{
    return ui->spinDownstreamWaterLevel->value();
}

double SeepageWidget::upstreamFlux() const
{
    return ui->spinUpstreamFlux->value();
}

double SeepageWidget::downstreamFlux() const
{
    return ui->spinDownstreamFlux->value();
}


bool SeepageWidget::exportIseepDatFile() const
{
    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString projectDirAbs = QDir(projectDir).absolutePath();

    QDir wd(projectDirAbs);
    if (!wd.exists("PREP")) wd.mkpath("PREP");
    if (!wd.exists("SEEPCASE01")) wd.mkpath("SEEPCASE01");

    auto ensureTrailingSep = [](const QString& path) {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator())) p += QDir::separator();
        return p;
    };

    const QString prepDirAbs = ensureTrailingSep(wd.filePath("PREP"));
    const QString seepCaseDirAbs = ensureTrailingSep(wd.filePath("SEEPCASE01"));

    QFile file(wd.filePath("ISEEP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream os(&file);
    os.setRealNumberNotation(QTextStream::FixedNotation);
    os.setRealNumberPrecision(6);

    os << "'" << prepDirAbs << "'\n";
    os << "'JD_UPSEEP.DAT'\n";
    os << "'JD_DWSEEP.DAT'\n";
    os << "'2'\n";
    os << "'2'\n";
    os << "'0'\n";
    os << "'JDFREE.DAT'\n";
    os << "'FA_UPSEEP.DAT'\n";
    os << "'FA_DWSEEP.DAT'\n";
    os << "'0'\n";
    os << "'JDJING.DAT'\n";
    os << "'JDTRACE.DAT'\n";
    os << "'KETRACE.DAT'\n";
    os << "'TDTRACE.DAT'\n";
    os << "'------------'\n";
    os << "'------------'\n";
    os << "'------------'\n";
    os << "'------------'\n";
    os << "'1'\n";
    os << "'------------'\n";

    os << "0     NCHANGE\n";
    os << "1     MCASE\n";
    os << "1 1   JISUAN CASE  II\n";
    os << "1     NCASE\n";
    os << "'------------'\n";
    os << "1     ICASE\n";
    os << "0     NCHANGE_CASE\n";
    os << "100 0 ITER  ITAG_FREE\n";
    os << upstreamWaterLevel() << " "
       << downstreamWaterLevel() << " "
       << upstreamFlux() << " "
       << downstreamFlux() << "    HWUP   HWDW\n";
    os << "'" << seepCaseDirAbs << "'     DIRDYNA\n";
    os << "60.000000 0.000000 3600.000000    DT TSTA TEND\n";
    os << "'HWDROP_UP.DAT'  'HWDROP_DW.DAT'   HWDROP_UP  HWDROP_DW\n";

    file.close();
    return exportInouseDatFile()
           && exportMaMaskDatFile()
           && exportMaRockDatFile()
           && exportKetraceDatFile()
           && exportJdtraceDatFile();
}

bool SeepageWidget::exportInouseDatFile() const
{
    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

    QFile file(wd.filePath("PREP/INOUSE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream os(&file);
    os << "           0    NUM OF NO USE NODE\n";
    os << "           0    NUM OF NO USE ELEMENT\n";
    os << "       0    杆单元悬浮节点数\n";
    os << "           0           2    NEBAR  NDIM\n";
    return true;
}

bool SeepageWidget::exportMaMaskDatFile() const
{
    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

    QFile file(wd.filePath("PREP/MA_MASK.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream os(&file);
    os << "   1   NUM\n";
    os << "  11     1\n";
    return true;
}

bool SeepageWidget::exportMaRockDatFile() const
{
    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

    QFile file(wd.filePath("PREP/MA_ROCK.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream os(&file);
    os << "           1  NUM_ROCK\n";
    os << "  11\n";
    return true;
}

bool SeepageWidget::exportKetraceDatFile() const
{
    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

    QFile file(wd.filePath("PREP/KETRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream os(&file);
    os << "           1   NUM\n";
    os << "        1        1\n";
    return true;
}

bool SeepageWidget::exportJdtraceDatFile() const
{
    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

    QFile file(wd.filePath("PREP/JDTRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream os(&file);
    os << "           2   NUM\n";
    os << "       951        1\n";
    os << "       943        2\n";
    return true;
}


QJsonObject SeepageWidget::saveJson()
{
    QJsonObject obj;
    obj["upstream_water_level"] = ui->spinUpstreamWaterLevel->value();
    obj["downstream_water_level"] = ui->spinDownstreamWaterLevel->value();
    obj["upstream_flux"] = ui->spinUpstreamFlux->value();
    obj["downstream_flux"] = ui->spinDownstreamFlux->value();
    return obj;
}

void SeepageWidget::loadJson(const QJsonObject& obj)
{
    ui->spinUpstreamWaterLevel->setValue(obj.value("upstream_water_level").toDouble(ui->spinUpstreamWaterLevel->value()));
    ui->spinDownstreamWaterLevel->setValue(obj.value("downstream_water_level").toDouble(ui->spinDownstreamWaterLevel->value()));
    ui->spinUpstreamFlux->setValue(obj.value("upstream_flux").toDouble(ui->spinUpstreamFlux->value()));
    ui->spinDownstreamFlux->setValue(obj.value("downstream_flux").toDouble(ui->spinDownstreamFlux->value()));
}

void SeepageWidget::onComputeClicked()
{
    emit computeRequested();
}
