#include "GeoStressRegressionWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QStringConverter>
#include <QTextStream>

#include "ui_GeoStressRegressionWidget.h"

#include "SolverUiLayoutHelper.h"
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
    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);

    ui->reservedEdit->setText("0");
    ui->reservedEdit->hide();
    ui->label8->hide();


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

int GeoStressRegressionWidget::calculationModeCode() const
{
    return ui->calcModeCombo->currentIndex();
}

bool GeoStressRegressionWidget::exportIGEOSDatFile(const ExportPayload& payload) const
{
    if (!payload.roofFileReady)
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(wd.filePath("IGEOS.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setEncoding(QStringConverter::System);
    const QString inputPath = ensureTrailingSep(inputDir.absolutePath());
    const int ndim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (ndim == 2);

    QFile imidFile(wd.filePath("IMID_GEOS.DAT"));
    if (!imidFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream imidOs(&imidFile);
    imidOs.setEncoding(QStringConverter::System);
    imidOs << (is2D ? "  2\n" : "  3\n");
    imidOs << (is2D ? "  2\n" : "  3\n");
    imidOs << (is2D ? "  0\n" : "  1\n");
    imidOs << (is2D ? "   2.00000\n" : "   0.00000\n");
    imidOs << "'" << inputPath << "'\n";

    os << "'" << inputPath << "'       1\n";
    os << "'" << kRoofFileName << "'       2\n";
    os << "'" << payload.sideFaceFiles.value(0, "FA_SIDE1.DAT") << "'       3\n";
    os << "'" << payload.sideFaceFiles.value(1, "FA_SIDE2.DAT") << "'       4\n";
    os << "'" << payload.sideFaceFiles.value(2, "FA_SIDE3.DAT") << "'       5\n";
    os << "'" << payload.sideFaceFiles.value(3, "FA_SIDE4.DAT") << "'       6\n";
    os << "'" << ui->factorEdit->text().trimmed() << "'       7\n";
    os << "'" << QString::number(ui->calcModeCombo->currentIndex()) << "'       8\n";
    os << "'" << ui->reservedEdit->text().trimmed() << "'       9\n";
    os << "'" << (payload.forceFile.isEmpty() ? kForceFileName : payload.forceFile) << "'      10\n";

    // IGEOS 第 11 项是是否读取 FTLOAD.DAT，与模型维度无关。
    // 计算方式 0 明确表示“仅计算自重应力”，此时无条件禁止读取 FTLOAD.DAT。
    // 只有非自重模式，并且外力页面存在非零外力及有效作用边/面时才启用。
    const int calcMode = ui->calcModeCombo->currentIndex();
    const int forceEnabled =
        (calcMode != 0 && payload.forceEnabled != 0) ? 1 : 0;
    os << "'" << QString::number(forceEnabled) << "'      11\n";
    os << "'" << ui->lambdaEdit->text().trimmed() << "'      12\n";

    os << "   0     NCHANGE\n";
    os << QString("%1    NUM_GEO\n").arg(payload.measureRows.size());
    if (is2D)
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
    QJsonObject obj;
    obj["roof"] = kRoofFileName;
    obj["factor"] = ui->factorEdit->text().trimmed();
    obj["calc_mode"] = ui->calcModeCombo->currentIndex();
    obj["reserved"] = ui->reservedEdit->text().trimmed();
    obj["lambda"] = ui->lambdaEdit->text().trimmed();

    return obj;
}

void GeoStressRegressionWidget::loadJson(const QJsonObject& obj)
{
    ui->factorEdit->setText(obj.value("factor").toString(ui->factorEdit->text()));
    ui->calcModeCombo->setCurrentIndex(obj.value("calc_mode").toInt(ui->calcModeCombo->currentIndex()));
    ui->reservedEdit->setText(obj.value("reserved").toString(ui->reservedEdit->text()));
    ui->lambdaEdit->setText(obj.value("lambda").toString(ui->lambdaEdit->text()));

    // 兼容旧工程：measure_rows/face_rows 由子节点替代，因此不再在主页面恢复。
}

void GeoStressRegressionWidget::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    emit computeRequested();
}

void GeoStressRegressionWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止计算")
                                      : QStringLiteral("数值法计算"));
}
