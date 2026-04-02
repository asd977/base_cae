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
    ui->prepDirEdit->setReadOnly(true);
    ui->roofEdit->setReadOnly(true);
    ui->forceFileEdit->setReadOnly(true);
    ui->reservedEdit->setText("0");

    ui->measureGroup->hide();
    ui->faceAttrGroup->hide();

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
    os << "'" << payload.sideFaceFiles.value(0, ui->leftEdit->text().trimmed()) << "'       3\n";
    os << "'" << payload.sideFaceFiles.value(1, ui->rightEdit->text().trimmed()) << "'       4\n";
    os << "'" << payload.sideFaceFiles.value(2, ui->frontEdit->text().trimmed()) << "'       5\n";
    os << "'" << payload.sideFaceFiles.value(3, ui->backEdit->text().trimmed()) << "'       6\n";
    os << "'" << ui->factorEdit->text().trimmed() << "'       7\n";
    os << "'" << QString::number(ui->calcModeCombo->currentIndex()) << "'       8\n";
    os << "'" << ui->reservedEdit->text().trimmed() << "'       9\n";
    os << "'" << (payload.forceFile.isEmpty() ? ui->forceFileEdit->text().trimmed() : payload.forceFile) << "'      10\n";
    os << "'" << QString::number(payload.forceEnabled) << "'      11\n";
    os << "'" << ui->lambdaEdit->text().trimmed() << "'      12\n";

    os << "   1     NCHANGE\n";
    os << "   1     1     1    MACHAN    MACHAN  II\n";
    os << QString("%1    NUM_GEO\n").arg(payload.measureRows.size());
    os << "X           Y           SGX         SGY         TXY         II\n";
    for (const auto& row : payload.measureRows)
    {
        os << row.x << "   " << row.y << "   " << row.sgx << "   " << row.sgy << "   " << row.txy << "   " << row.ii << "\n";
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
    QJsonObject obj;
    obj["prep_dir"] = ui->prepDirEdit->text().trimmed();
    obj["roof"] = ui->roofEdit->text().trimmed();
    obj["fa_left"] = ui->leftEdit->text().trimmed();
    obj["fa_right"] = ui->rightEdit->text().trimmed();
    obj["fa_front"] = ui->frontEdit->text().trimmed();
    obj["fa_back"] = ui->backEdit->text().trimmed();
    obj["factor"] = ui->factorEdit->text().trimmed();
    obj["calc_mode"] = ui->calcModeCombo->currentIndex();
    obj["reserved"] = ui->reservedEdit->text().trimmed();
    obj["force_file"] = ui->forceFileEdit->text().trimmed();
    obj["force_enabled"] = ui->forceEnabledCombo->currentIndex();
    obj["lambda"] = ui->lambdaEdit->text().trimmed();

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
    ui->calcModeCombo->setCurrentIndex(obj.value("calc_mode").toInt(ui->calcModeCombo->currentIndex()));
    ui->reservedEdit->setText(obj.value("reserved").toString(ui->reservedEdit->text()));
    ui->forceFileEdit->setText(obj.value("force_file").toString(ui->forceFileEdit->text()));
    ui->forceEnabledCombo->setCurrentIndex(
        obj.value("force_enabled").toInt(ui->forceEnabledCombo->currentIndex()));
    ui->lambdaEdit->setText(obj.value("lambda").toString(ui->lambdaEdit->text()));

    // 兼容旧工程：measure_rows/face_rows 由子节点替代，因此不再在主页面恢复。
}

void GeoStressRegressionWidget::onComputeClicked()
{
    emit computeRequested();
}
