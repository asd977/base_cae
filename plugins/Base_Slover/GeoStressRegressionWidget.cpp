#include "GeoStressRegressionWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableWidget>
#include <QTableWidgetItem>
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

    ui->measureTable->setColumnCount(6);
    ui->measureTable->setHorizontalHeaderLabels(
        QStringList() << "X"
                      << "Y"
                      << "SGX"
                      << "SGY"
                      << "TXY"
                      << "序号");
    ui->measureTable->setRowCount(1);
    ui->measureTable->setItem(0, 0, new QTableWidgetItem("0"));
    ui->measureTable->setItem(0, 1, new QTableWidgetItem("0"));
    ui->measureTable->setItem(0, 2, new QTableWidgetItem("0"));
    ui->measureTable->setItem(0, 3, new QTableWidgetItem("0"));
    ui->measureTable->setItem(0, 4, new QTableWidgetItem("0"));
    ui->measureTable->setItem(0, 5, new QTableWidgetItem("1"));

    ui->faceAttrTable->setColumnCount(5);
    ui->faceAttrTable->setHorizontalHeaderLabels(
        QStringList() << "剖切方向(IDIR 1-x/2-y/3-z)"
                      << "侧压高程(m)"
                      << "测压容重(kg/m3)"
                      << "附加矩形常压(Pa)"
                      << "序号");
    ui->faceAttrTable->setRowCount(4);
    for (int row = 0; row < 4; ++row)
    {
        const QString idir = (row < 2) ? "1" : "2";
        ui->faceAttrTable->setItem(row, 0, new QTableWidgetItem(idir));
        ui->faceAttrTable->setItem(row, 1, new QTableWidgetItem("2000.0000"));
        ui->faceAttrTable->setItem(row, 2, new QTableWidgetItem("2650.0000"));
        ui->faceAttrTable->setItem(row, 3, new QTableWidgetItem("0"));
        ui->faceAttrTable->setItem(row, 4, new QTableWidgetItem(QString::number(row + 1)));
    }

    connect(ui->addMeasureBtn, &QPushButton::clicked, this,
            [this]()
            {
                const int row = ui->measureTable->rowCount();
                ui->measureTable->insertRow(row);
                for (int col = 0; col < 5; ++col)
                    ui->measureTable->setItem(row, col, new QTableWidgetItem("0"));
                ui->measureTable->setItem(row, 5, new QTableWidgetItem(QString::number(row + 1)));
            });
    connect(ui->removeMeasureBtn, &QPushButton::clicked, this,
            [this]()
            {
                const int row = ui->measureTable->currentRow();
                if (row >= 0)
                    ui->measureTable->removeRow(row);
            });
    connect(ui->addFaceBtn, &QPushButton::clicked, this,
            [this]()
            {
                const int row = ui->faceAttrTable->rowCount();
                ui->faceAttrTable->insertRow(row);
                ui->faceAttrTable->setItem(row, 0, new QTableWidgetItem("1"));
                ui->faceAttrTable->setItem(row, 1, new QTableWidgetItem("2000.0000"));
                ui->faceAttrTable->setItem(row, 2, new QTableWidgetItem("2650.0000"));
                ui->faceAttrTable->setItem(row, 3, new QTableWidgetItem("0"));
                ui->faceAttrTable->setItem(row, 4, new QTableWidgetItem(QString::number(row + 1)));
            });
    connect(ui->removeFaceBtn, &QPushButton::clicked, this,
            [this]()
            {
                const int row = ui->faceAttrTable->currentRow();
                if (row >= 0)
                    ui->faceAttrTable->removeRow(row);
            });

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
    os << "'" << QString::number(ui->calcModeCombo->currentIndex()) << "'       8\n";
    os << "'" << ui->reservedEdit->text().trimmed() << "'       9\n";
    os << "'" << ui->forceFileEdit->text().trimmed() << "'      10\n";
    os << "'" << QString::number(ui->forceEnabledCombo->currentIndex()) << "'      11\n";
    os << "'" << ui->lambdaEdit->text().trimmed() << "'      12\n";

    os << "   1     NCHANGE\n";
    os << "   1     1     1    MACHAN    MACHAN  II\n";
    os << QString("%1    NUM_GEO\n").arg(ui->measureTable->rowCount());
    os << "X           Y           SGX         SGY         TXY         II\n";
    for (int row = 0; row < ui->measureTable->rowCount(); ++row)
    {
        const QString x = ui->measureTable->item(row, 0) ? ui->measureTable->item(row, 0)->text().trimmed() : "0";
        const QString y = ui->measureTable->item(row, 1) ? ui->measureTable->item(row, 1)->text().trimmed() : "0";
        const QString sgx = ui->measureTable->item(row, 2) ? ui->measureTable->item(row, 2)->text().trimmed() : "0";
        const QString sgy = ui->measureTable->item(row, 3) ? ui->measureTable->item(row, 3)->text().trimmed() : "0";
        const QString txy = ui->measureTable->item(row, 4) ? ui->measureTable->item(row, 4)->text().trimmed() : "0";
        const QString ii =
            ui->measureTable->item(row, 5) ? ui->measureTable->item(row, 5)->text().trimmed() : QString::number(row + 1);
        os << x << "   " << y << "   " << sgx << "   " << sgy << "   " << txy << "   " << ii << "\n";
    }

    os << QString("%1    NFA_GEO\n").arg(ui->faceAttrTable->rowCount());
    os << "IDIR       HEIGHT      GAMA          PGROUT  II\n";
    for (int row = 0; row < ui->faceAttrTable->rowCount(); ++row)
    {
        const QString idir =
            ui->faceAttrTable->item(row, 0) ? ui->faceAttrTable->item(row, 0)->text().trimmed() : "1";
        const QString height =
            ui->faceAttrTable->item(row, 1) ? ui->faceAttrTable->item(row, 1)->text().trimmed() : "0";
        const QString gama =
            ui->faceAttrTable->item(row, 2) ? ui->faceAttrTable->item(row, 2)->text().trimmed() : "0";
        const QString pgrout =
            ui->faceAttrTable->item(row, 3) ? ui->faceAttrTable->item(row, 3)->text().trimmed() : "0";
        const QString ii =
            ui->faceAttrTable->item(row, 4) ? ui->faceAttrTable->item(row, 4)->text().trimmed() : QString::number(row + 1);
        os << idir << "   " << height << "   " << gama << "   " << pgrout << "   " << ii << "\n";
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

    QJsonArray measureRows;
    for (int row = 0; row < ui->measureTable->rowCount(); ++row)
    {
        QJsonObject rowObj;
        rowObj["x"] = ui->measureTable->item(row, 0) ? ui->measureTable->item(row, 0)->text().trimmed() : "0";
        rowObj["y"] = ui->measureTable->item(row, 1) ? ui->measureTable->item(row, 1)->text().trimmed() : "0";
        rowObj["sgx"] = ui->measureTable->item(row, 2) ? ui->measureTable->item(row, 2)->text().trimmed() : "0";
        rowObj["sgy"] = ui->measureTable->item(row, 3) ? ui->measureTable->item(row, 3)->text().trimmed() : "0";
        rowObj["txy"] = ui->measureTable->item(row, 4) ? ui->measureTable->item(row, 4)->text().trimmed() : "0";
        rowObj["ii"] =
            ui->measureTable->item(row, 5) ? ui->measureTable->item(row, 5)->text().trimmed() : QString::number(row + 1);
        measureRows.append(rowObj);
    }
    obj["measure_rows"] = measureRows;

    QJsonArray faceRows;
    for (int row = 0; row < ui->faceAttrTable->rowCount(); ++row)
    {
        QJsonObject rowObj;
        rowObj["idir"] = ui->faceAttrTable->item(row, 0) ? ui->faceAttrTable->item(row, 0)->text().trimmed() : "1";
        rowObj["height"] = ui->faceAttrTable->item(row, 1) ? ui->faceAttrTable->item(row, 1)->text().trimmed() : "0";
        rowObj["gama"] = ui->faceAttrTable->item(row, 2) ? ui->faceAttrTable->item(row, 2)->text().trimmed() : "0";
        rowObj["pgrout"] = ui->faceAttrTable->item(row, 3) ? ui->faceAttrTable->item(row, 3)->text().trimmed() : "0";
        rowObj["ii"] =
            ui->faceAttrTable->item(row, 4) ? ui->faceAttrTable->item(row, 4)->text().trimmed() : QString::number(row + 1);
        faceRows.append(rowObj);
    }
    obj["face_rows"] = faceRows;

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

    if (obj.contains("measure_rows") && obj.value("measure_rows").isArray())
    {
        const QJsonArray rows = obj.value("measure_rows").toArray();
        ui->measureTable->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row)
        {
            const QJsonObject rowObj = rows.at(row).toObject();
            ui->measureTable->setItem(row, 0, new QTableWidgetItem(rowObj.value("x").toString("0")));
            ui->measureTable->setItem(row, 1, new QTableWidgetItem(rowObj.value("y").toString("0")));
            ui->measureTable->setItem(row, 2, new QTableWidgetItem(rowObj.value("sgx").toString("0")));
            ui->measureTable->setItem(row, 3, new QTableWidgetItem(rowObj.value("sgy").toString("0")));
            ui->measureTable->setItem(row, 4, new QTableWidgetItem(rowObj.value("txy").toString("0")));
            ui->measureTable->setItem(
                row, 5, new QTableWidgetItem(rowObj.value("ii").toString(QString::number(row + 1))));
        }
    }

    if (obj.contains("face_rows") && obj.value("face_rows").isArray())
    {
        const QJsonArray rows = obj.value("face_rows").toArray();
        ui->faceAttrTable->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row)
        {
            const QJsonObject rowObj = rows.at(row).toObject();
            ui->faceAttrTable->setItem(row, 0, new QTableWidgetItem(rowObj.value("idir").toString("1")));
            ui->faceAttrTable->setItem(row, 1, new QTableWidgetItem(rowObj.value("height").toString("0")));
            ui->faceAttrTable->setItem(row, 2, new QTableWidgetItem(rowObj.value("gama").toString("0")));
            ui->faceAttrTable->setItem(row, 3, new QTableWidgetItem(rowObj.value("pgrout").toString("0")));
            ui->faceAttrTable->setItem(
                row, 4, new QTableWidgetItem(rowObj.value("ii").toString(QString::number(row + 1))));
        }
    }
}

void GeoStressRegressionWidget::onComputeClicked()
{
    emit computeRequested();
}
