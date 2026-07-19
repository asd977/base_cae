#include "CaveBlockStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleValidator>
#include <QFile>
#include <QIntValidator>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QTextStream>
#include <QVector3D>
#include <cmath>

#include "PickManager.h"
#include "CaveBlockPlanePage.h"
#include "ui_CaveBlockStabilityWidget.h"
#include <vtkCellData.h>
#include <vtkCellSizeFilter.h>
#include <vtkDataArray.h>
#include <vtkNew.h>
#include <vtkUnstructuredGrid.h>

#include "SolverUiLayoutHelper.h"
CaveBlockStabilityWidget::CaveBlockStabilityWidget(QWidget* parent)
    : BasePage("CaveBlockStabilityAnalysis"), ui(new Ui::CaveBlockStabilityWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);
    ui->comboIfDir->addItem("使用起点和终点定义方向", "0");
    ui->comboIfDir->addItem("使用指定面定义方向", "1");

    auto updateDirectionEditors = [this]()
    {
        const bool usePoints = ui->comboIfDir->currentData().toString() == QStringLiteral("0");
        for (QLineEdit* edit : {ui->axEdit, ui->ayEdit, ui->azEdit,
                                ui->bxEdit, ui->byEdit, ui->bzEdit})
            edit->setEnabled(usePoints);
        ui->im1Edit->setEnabled(!usePoints);
        ui->im2Edit->setEnabled(!usePoints);
    };
    connect(ui->comboIfDir, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [updateDirectionEditors](int) { updateDirectionEditors(); });
    updateDirectionEditors();

    setAttribute(Qt::WA_StyledBackground, true);
    ui->icutFileEdit->setText(QStringLiteral("ICUT.DAT"));
    ui->safeFileEdit->setText(QStringLiteral("SAFE_CAVEBLK.DAT"));
    ui->icutFileEdit->hide();
    ui->safeFileEdit->hide();
    ui->labelIcut->hide();
    ui->labelSafe->hide();
    // ui->fileHiddenWidget->hide();
    ui->computeBtn->setText(QStringLiteral("计算"));

    auto* numberValidator = new QDoubleValidator(-1.0e12, 1.0e12, 8, this);
    numberValidator->setNotation(QDoubleValidator::ScientificNotation);
    for (QLineEdit* edit : {ui->weightEdit, ui->xMinEdit, ui->xMaxEdit,
                            ui->yMinEdit, ui->yMaxEdit, ui->zMinEdit, ui->zMaxEdit,
                            ui->fxEdit, ui->fyEdit, ui->fzEdit,
                            ui->axEdit, ui->ayEdit, ui->azEdit,
                            ui->bxEdit, ui->byEdit, ui->bzEdit})
        edit->setValidator(numberValidator);
    auto* faceValidator = new QIntValidator(1, 100000000, this);
    ui->im1Edit->setValidator(faceValidator);
    ui->im2Edit->setValidator(faceValidator);

    connect(ui->computeBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (ui->computeBtn->property("computing").toBool())
                {
                    emit stopRequested();
                    return;
                }

                QString error;
                if (!validateInputs(&error))
                {
                    QMessageBox::warning(this, QStringLiteral("设置不完整"), error);
                    return;
                }
                emit computeRequested();
            });
}

CaveBlockStabilityWidget::~CaveBlockStabilityWidget()
{
    delete ui;
}


void CaveBlockStabilityWidget::setComputing(bool computing)
{
    ui->computeBtn->setProperty("computing", computing);
    ui->computeBtn->setText(computing ? QStringLiteral("停止") : QStringLiteral("计算"));
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
    obj["if_dir"] = ui->comboIfDir->currentData().toString();
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
    {
        const int index = ui->comboIfDir->findData(obj.value("if_dir").toString("0"));
        ui->comboIfDir->setCurrentIndex(index >= 0 ? index : 0);
    }
    ui->axEdit->setText(obj.value("ax").toString(ui->axEdit->text()));
    ui->ayEdit->setText(obj.value("ay").toString(ui->ayEdit->text()));
    ui->azEdit->setText(obj.value("az").toString(ui->azEdit->text()));
    ui->bxEdit->setText(obj.value("bx").toString(ui->bxEdit->text()));
    ui->byEdit->setText(obj.value("by").toString(ui->byEdit->text()));
    ui->bzEdit->setText(obj.value("bz").toString(ui->bzEdit->text()));
    ui->im1Edit->setText(obj.value("im1").toString(ui->im1Edit->text()));
    ui->im2Edit->setText(obj.value("im2").toString(ui->im2Edit->text()));
}

bool CaveBlockStabilityWidget::exportDatFiles(QVector<CaveBlockPlanePage*>& planePages)
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
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    icut.setCodec("UTF-8");
#endif
    icut << QString("%1 1\n").arg(planePages.size());
    icut << "X1 Y1 Z1 X2 Y2 Z2 X3 Y3 Z3 II\n";
    for (int i = 0; i < planePages.size(); ++i)
        icut << planePages[i]->icutLine(i + 1) << "\n";

    QFile paraFile(sliderDir.filePath("PARA.DAT"));
    if (!paraFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream para(&paraFile);
 #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    para.setCodec("UTF-8");
#endif
    para << QString("%1\n").arg(planePages.size());
    para << "FP C(KPA) RT(KPA) II\n";
    for (int i = 0; i < planePages.size(); ++i)
        para << planePages[i]->paraLine(i + 1) << "\n";

    QFile mesg(sliderDir.filePath("MESG227.DAT"));
    if (!mesg.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&mesg);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    os.setCodec("UTF-8");
#endif
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
    os << quoteValue(ui->comboIfDir->currentData().toString()) << "\n";
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
    const QStringList lines = {
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",

        "                              ",
        "                              ",

        "MESG227.DAT",
        QDir::toNativeSeparators(sliderDir.absolutePath() + QDir::separator()),

        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------"
    };

    for (const QString& line : lines)
    {
        os << quoteValue(line) << "\n";
    }

    QFile icutPlaneFile(sliderDir.filePath("ICUTPLANE.DAT"));
    if (!icutPlaneFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream ps(&icutPlaneFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    ps.setCodec("UTF-8");
#endif
    const int ndim = 3;
    ps << QString("%1 %2   NCUT NDIM\n").arg(planePages.size(), 6).arg(ndim, 6);
    ps << "X1 Y1 Z1 n1 n2 n3 II\n";
    QVector<double> areas;
    areas.reserve(planePages.size());
    QVector<QVector3D> normals;
    normals.reserve(planePages.size());
    for (int i = 0; i < planePages.size(); ++i)
    {
        const QVector3D p = planePages[i]->p1Vector();
        const QVector3D n = planePages[i]->normalVector();
        normals.push_back(n);
        ps << QString("%1 %2 %3 %4 %5 %6 %7\n")
                  .arg(double(p.x()), 12, 'f', 6)
                  .arg(double(p.y()), 12, 'f', 6)
                  .arg(double(p.z()), 12, 'f', 6)
                  .arg(double(n.x()), 12, 'f', 6)
                  .arg(double(n.y()), 12, 'f', 6)
                  .arg(double(n.z()), 12, 'f', 6)
                  .arg(i + 1, 6);
        areas.push_back(planePages[i]->pickedFaceArea());
    }

    const double gamma = valueOrDefault(ui->weightEdit, "23.52").toDouble();
    auto computeSolidVolume = []() -> double
    {
        PickManager::instance().updateStructure();
        vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
        if (!grid)
            return 0.0;

        vtkNew<vtkCellSizeFilter> sizeFilter;
        sizeFilter->SetInputData(grid);
        sizeFilter->ComputeVertexCountOff();
        sizeFilter->ComputeLengthOff();
        sizeFilter->ComputeAreaOff();
        sizeFilter->ComputeVolumeOn();
        sizeFilter->Update();

        vtkUnstructuredGrid* outGrid = vtkUnstructuredGrid::SafeDownCast(sizeFilter->GetOutput());
        if (!outGrid || !outGrid->GetCellData())
            return 0.0;
        vtkDataArray* volumes = outGrid->GetCellData()->GetArray("Volume");
        if (!volumes)
            return 0.0;
        double vol = 0.0;
        for (vtkIdType i = 0; i < volumes->GetNumberOfTuples(); ++i)
            vol += volumes->GetComponent(i, 0);
        return vol;
    };
    const double volume = computeSolidVolume();
    const double weight = gamma * volume;
    ps << "VOL(M3) GAMA(KN/M3) WEIGHT(KN) IVERTICAL\n";
    ps << QString("%1 %2 %3 %4\n").arg(volume, 12, 'E', 6).arg(gamma, 10, 'f', 4).arg(
              weight, 12, 'E', 6).arg(3, 6);
    const int iVertical = 3;
    QVector3D gravityVector(0.0f, 0.0f, 0.0f);
    gravityVector[iVertical - 1] = float(-weight);
    ps << "GRAVITY_VECTOR\n";
    ps << QString("%1 %2 %3\n")
              .arg(double(gravityVector.x()), 12, 'E', 6)
              .arg(double(gravityVector.y()), 12, 'E', 6)
              .arg(double(gravityVector.z()), 12, 'E', 6);
    ps << "FACE_NORMAL_PRESSURE(KN)\n";
    for (int i = 0; i < planePages.size(); ++i)
    {
        const double normalPressure = QVector3D::dotProduct(normals[i], gravityVector);
        ps << QString("%1 %2 ICUT\n").arg(normalPressure, 12, 'E', 6).arg(i + 1, 6);
    }
    ps << "FACE_AREA(M2)\n";
    for (int i = 0; i < areas.size(); ++i)
        ps << QString("%1 %2 ICUT\n").arg(areas[i], 12, 'E', 6).arg(i + 1, 6);

    return true;
}

bool CaveBlockStabilityWidget::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    auto value = [](const QLineEdit* edit, bool* ok = nullptr)
    {
        bool parsed = false;
        const double result = edit->text().trimmed().toDouble(&parsed);
        if (ok)
            *ok = parsed;
        return result;
    };

    bool ok = false;
    if (value(ui->weightEdit, &ok) <= 0.0 || !ok)
        return fail(QStringLiteral("块体容重必须为正数。"));

    for (const auto& range : {qMakePair(ui->xMinEdit, ui->xMaxEdit),
                              qMakePair(ui->yMinEdit, ui->yMaxEdit),
                              qMakePair(ui->zMinEdit, ui->zMaxEdit)})
    {
        bool minOk = false;
        bool maxOk = false;
        const double minValue = value(range.first, &minOk);
        const double maxValue = value(range.second, &maxOk);
        if (!minOk || !maxOk || maxValue <= minValue)
            return fail(QStringLiteral("每个方向的计算范围最大值必须大于最小值。"));
    }

    if (ui->comboIfDir->currentData().toString() == QStringLiteral("0"))
    {
        const QVector3D a(float(value(ui->axEdit)), float(value(ui->ayEdit)), float(value(ui->azEdit)));
        const QVector3D b(float(value(ui->bxEdit)), float(value(ui->byEdit)), float(value(ui->bzEdit)));
        if ((b - a).lengthSquared() <= 1.0e-12f)
            return fail(QStringLiteral("滑移方向的起点和终点不能相同。"));
    }
    else
    {
        bool firstOk = false;
        bool secondOk = false;
        const int first = ui->im1Edit->text().trimmed().toInt(&firstOk);
        const int second = ui->im2Edit->text().trimmed().toInt(&secondOk);
        if (!firstOk || !secondOk || first <= 0 || second <= 0 || first == second)
            return fail(QStringLiteral("定义滑移方向的两个面编号必须为不同的正整数。"));
    }
    return true;
}

QString CaveBlockStabilityWidget::quoteValue(QString value)
{
    return "'" + value.trimmed() + "'";
}

QString CaveBlockStabilityWidget::valueOrDefault(QLineEdit* edit, QString def)
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}
