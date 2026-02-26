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
    QDir wd(projectDir);
    if (!wd.exists("PREP")) wd.mkpath("PREP");

    QFile file(wd.filePath("PREP/ISEEP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream os(&file);
    os.setRealNumberNotation(QTextStream::FixedNotation);
    os.setRealNumberPrecision(6);

    os << "'./PREP/'\n";
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

    os << "0\n";
    os << "1\n";
    os << "1 1\n";
    os << "1\n";
    os << "1\n";
    os << "0\n";
    os << "100 0\n";
    os << upstreamWaterLevel() << " "
       << downstreamWaterLevel() << " "
       << upstreamFlux() << " "
       << downstreamFlux() << "\n";
    os << "'./SEEPCASE01/'\n";
    os << "60.000000 0.000000 3600.000000\n";
    os << "'HWDROP_UP.DAT' 'HWDROP_DW.DAT'\n";

    file.close();
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
