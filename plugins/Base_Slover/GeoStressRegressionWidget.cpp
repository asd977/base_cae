#include "GeoStressRegressionWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTextStream>

#include "ui_GeoStressRegressionWidget.h"

namespace
{
const QString kRoofFileName = "FAROOF.DAT";
const QString kForceFileName = "FTLOAD.DAT";

QString ensureTrailingSep(const QString& path)
{
    QString p = QDir::toNativeSeparators(path);
    if (!p.endsWith(QDir::separator()))
        p += QDir::separator();
    return p;
}
}

GeoStressRegressionWidget::GeoStressRegressionWidget(QWidget* parent)
    : BasePage("GeoStressRegressionAnalysis"), ui(new Ui::GeoStressRegressionWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    ui->reservedEdit->setText("0");

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

bool GeoStressRegressionWidget::exportIGEOSDatFile(const ExportPayload& payload) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("IGEOS.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const QString prepDir = ensureTrailingSep(wd.filePath("PREP"));

    os << "'" << prepDir << "'       1\n";
    os << "'" << kRoofFileName << "'       2\n";
    os << "'" << payload.sideFaceFiles.value(0, "FA_SIDE1.DAT") << "'       3\n";
    os << "'" << payload.sideFaceFiles.value(1, "FA_SIDE2.DAT") << "'       4\n";
    os << "'" << payload.sideFaceFiles.value(2, "FA_SIDE3.DAT") << "'       5\n";
    os << "'" << payload.sideFaceFiles.value(3, "FA_SIDE4.DAT") << "'       6\n";
    os << "'" << ui->factorEdit->text().trimmed() << "'       7\n";
    os << "'" << QString::number(ui->calcModeCombo->currentIndex()) << "'       8\n";
    os << "'" << ui->reservedEdit->text().trimmed() << "'       9\n";
    os << "'" << (payload.forceFile.isEmpty() ? kForceFileName : payload.forceFile) << "'      10\n";
    os << "'" << QString::number(payload.forceEnabled) << "'      11\n";
    os << "'" << ui->lambdaEdit->text().trimmed() << "'      12\n";

    os << "   1     NCHANGE\n";
    os << "   1     1     1    MACHAN    MACHAN  II\n";
    os << QString("%1    NUM_GEO\n").arg(payload.measureRows.size());
    const int ndim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (ndim == 2)
    {
        os << "XP          YP          SGX         SGY         TXY         II\n";
        for (const auto& row : payload.measureRows)
        {
            os << row.x << "   " << row.y << "   " << row.sgx << "   " << row.sgy << "   "
               << row.txy << "   " << row.ii << "\n";
        }
    }
    else
    {
        os << "XP          YP          ZP          SGX         SGY         SGZ         TXY         TYZ         TZX         II\n";
        for (const auto& row : payload.measureRows)
        {
            os << row.x << "   " << row.y << "   " << row.z << "   " << row.sgx << "   "
               << row.sgy << "   " << row.sgz << "   " << row.txy << "   " << row.tyz
               << "   " << row.tzx << "   " << row.ii << "\n";
        }
    }

    os << QString("%1    NFA_GEO\n").arg(payload.faceRows.size());
    os << "IDIR       HEIGHT      GAMA          PGROUT  II\n";
    for (const auto& row : payload.faceRows)
    {
        os << row.idir << "   " << row.height << "   " << row.gama << "   " << row.pgrout << "   " << row.ii << "\n";
    }

    return true;
}

QJsonObject GeoStressRegressionWidget::saveJson()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    QJsonObject obj;
    obj["prep_dir"] = ensureTrailingSep(QDir(wd.filePath("PREP")).absolutePath());
    obj["roof"] = kRoofFileName;
    obj["factor"] = ui->factorEdit->text().trimmed();
    obj["calc_mode"] = ui->calcModeCombo->currentIndex();
    obj["reserved"] = ui->reservedEdit->text().trimmed();
    obj["force_enabled"] = ui->forceEnabledCombo->currentIndex();
    obj["lambda"] = ui->lambdaEdit->text().trimmed();

    return obj;
}

void GeoStressRegressionWidget::loadJson(const QJsonObject& obj)
{
    ui->factorEdit->setText(obj.value("factor").toString(ui->factorEdit->text()));
    ui->calcModeCombo->setCurrentIndex(obj.value("calc_mode").toInt(ui->calcModeCombo->currentIndex()));
    ui->reservedEdit->setText(obj.value("reserved").toString(ui->reservedEdit->text()));
    ui->forceEnabledCombo->setCurrentIndex(
        obj.value("force_enabled").toInt(ui->forceEnabledCombo->currentIndex()));
    ui->lambdaEdit->setText(obj.value("lambda").toString(ui->lambdaEdit->text()));

    // 兼容旧工程：measure_rows/face_rows 由子节点替代，因此不再在主页面恢复。
}

void GeoStressRegressionWidget::onComputeClicked()
{
    emit computeRequested();
}
