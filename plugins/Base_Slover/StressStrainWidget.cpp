#include "StressStrainWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "ui_StressStrainWidget.h"

StressStrainWidget::StressStrainWidget(QWidget* parent)
    : BasePage("StressStrainAnalysis"), ui(new Ui::StressStrainWidget)
{
    ui->setupUi(this);
    connect(ui->btnCompute, &QPushButton::clicked, this, &StressStrainWidget::onComputeClicked);
}

StressStrainWidget::~StressStrainWidget()
{
    delete ui;
}

void StressStrainWidget::beforeLoad()
{
}

void StressStrainWidget::beforeUnload()
{
}

bool StressStrainWidget::exportIfillDatFile(const QString& baseVariablesText,
                                            const QString& upliftAndAxisText,
                                            const QString& rockStressText,
                                            const QString& cycleSectionText) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString projectDirAbs = QDir(projectDir).absolutePath();

    QDir wd(projectDirAbs);
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    const QString prepDirAbs = ensureTrailingSep(wd.filePath("PREP"));
    const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01FILL01"));

    QFile file(wd.filePath("IFILL.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setRealNumberNotation(QTextStream::FixedNotation);
    os.setRealNumberPrecision(6);

    QString baseSection = baseVariablesText.trimmed();
    if (baseSection.isEmpty())
    {
        baseSection =
            "'" + prepDirAbs + "'      1\n"
            "'FA_DAMUP.DAT'      2\n"
            "'FA_DAMDW.DAT'      3\n"
            "'FA_DAMBASE.DAT'      4\n"
            "'FA_DAMMID.DAT'      5\n"
            "'MA_ROCK.DAT'      6\n"
            "'MA_DAMUP.DAT'      7\n"
            "'MA_DAMDW.DAT'      8\n"
            "'MA_CORE.DAT'      9\n"
            "'4'      10\n"
            "'MA_PLATE.DAT'      11\n"
            "'JDTRACE.DAT'      12\n"
            "'KETRACE.DAT'      13\n"
            "'TDTRACE.DAT'      14\n"
            "'0'      15\n"
            "'0'      16\n"
            "'0.05'      17\n"
            "'0.01'      18\n"
            "'2'      19\n"
            "'4   10'      20\n"
            "'0.000'      21\n"
            "'1.962'      22\n"
            "'1.308'      23\n"
            "'0.1'      24\n"
            "'0.2'      25\n"
            "'2.0'      26\n"
            "'0.9'      27\n"
            "'7.0'      28\n"
            "'0'      29\n"
            "'10'      30";
    }
    baseSection.replace("F:\\FEMCLASS\\Z03_JUNZ3D\\PREP\\", prepDirAbs);
    os << baseSection;
    if (!baseSection.endsWith('\n'))
        os << "\n";

    QString upliftSection = upliftAndAxisText.trimmed();
    if (upliftSection.isEmpty())
    {
        upliftSection =
            "0.000000  0.000000  0.000000    X1  X2  X3\n"
            "0.000000  0.000000  0.000000    ALFA1  ALFA2  ALFA3\n"
            "1  3  0.000000  0.000000  0.000000  0.000000    IXUPDW XAMID YAMID XBMID "
            "YBMID";
    }
    os << upliftSection;
    if (!upliftSection.endsWith('\n'))
        os << "\n";

    QString rockSection = rockStressText.trimmed();
    if (rockSection.isEmpty())
        rockSection = "1  0    IFROCK  IFISO";
    os << rockSection;
    if (!rockSection.endsWith('\n'))
        os << "\n";

    const int mcase = detectCaseCount(cycleSectionText);
    os << QString("%1   MCASE\n").arg(mcase);
    for (int i = 1; i <= mcase; ++i)
        os << QString("%1     %1\n").arg(i);

    QString dynamicSection = cycleSectionText;
    if (dynamicSection.trimmed().isEmpty())
    {
        dynamicSection =
            "   1    NCASE\n"
            " ----------------------------------------------\n"
            "    1    ICASE--------------------------------------\n"
            "   0     0     1     IFIIWL_SEEP   IFIIWL_BODY  IFIIWL_FACE\n"
            "   0     0     NUM_WET        IFWET\n"
            "   0     0     NUM_DROP       IFDROP\n"
            "   0     NCHANGE\n"
            "   1  NCIRCLE\n"
            "JPACK_STA JPACK_END\tTT_PACK         HWJ_STA\t      HWJ_END\t TT_FILL "
            "IF_PACK IF_PLIU IF_FILL  IF_FLIU\tCAHEAD CABODY\n"
            "   0     0           0.0000         0.0000         0.0000         "
            "0.0000       0     0     0     0  'NONE'  'NONE'     1\n"
            "'CASE01FILL01'     DIRDYNA\n"
            "   0   NUM_DYNA\n"
            "   0     0     0   IFMODE   IFWAVE     IFSETT\n"
            "  0.020000    0.000000    0.000000    0.000000    DT TSTA TEND "
            "WOMI_1\n"
            "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ\n";
    }

    dynamicSection.replace("'CASE01FILL01'", "'" + caseDirAbs + "'");
    os << dynamicSection;
    if (!dynamicSection.endsWith('\n'))
        os << "\n";

    return true;
}

int StressStrainWidget::detectCaseCount(const QString& cycleSectionText) const
{
    const QRegularExpression caseRegex(QStringLiteral("(?m)^\\s*\\d+\\s+ICASE"));
    const int count = cycleSectionText.count(caseRegex);
    return count > 0 ? count : 1;
}

QJsonObject StressStrainWidget::saveJson()
{
    return QJsonObject();
}

void StressStrainWidget::loadJson(const QJsonObject&)
{
}

void StressStrainWidget::onComputeClicked()
{
    emit computeRequested();
}
