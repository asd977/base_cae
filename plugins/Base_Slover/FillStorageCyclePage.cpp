#include "FillStorageCyclePage.h"

#include "ui_FillStorageCyclePage.h"

FillStorageCyclePage::FillStorageCyclePage(QWidget* parent)
    : BasePage("FillStorageCycle"), ui(new Ui::FillStorageCyclePage)
{
    ui->setupUi(this);
    ui->plainTextEditBaseVariables->setPlainText(defaultBaseVariablesText());
    ui->plainTextEditUpliftAxis->setPlainText(defaultUpliftAndAxisText());
    ui->plainTextEditRockStress->setPlainText(defaultRockStressText());
    ui->plainTextEditSection->setPlainText(defaultSectionText());
}

FillStorageCyclePage::~FillStorageCyclePage()
{
    delete ui;
}

void FillStorageCyclePage::beforeLoad()
{
}

void FillStorageCyclePage::beforeUnload()
{
}

QString FillStorageCyclePage::sectionText() const
{
    return ui->plainTextEditSection->toPlainText();
}

QString FillStorageCyclePage::baseVariablesText() const
{
    return ui->plainTextEditBaseVariables->toPlainText();
}

QString FillStorageCyclePage::upliftAndAxisText() const
{
    return ui->plainTextEditUpliftAxis->toPlainText();
}

QString FillStorageCyclePage::rockStressText() const
{
    return ui->plainTextEditRockStress->toPlainText();
}

QJsonObject FillStorageCyclePage::saveJson()
{
    QJsonObject obj;
    obj["section_text"] = sectionText();
    obj["base_variables_text"] = baseVariablesText();
    obj["uplift_axis_text"] = upliftAndAxisText();
    obj["rock_stress_text"] = rockStressText();
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    const QString baseVariables = obj.value("base_variables_text").toString();
    if (baseVariables.trimmed().isEmpty())
        ui->plainTextEditBaseVariables->setPlainText(defaultBaseVariablesText());
    else
        ui->plainTextEditBaseVariables->setPlainText(baseVariables);

    const QString upliftAxis = obj.value("uplift_axis_text").toString();
    if (upliftAxis.trimmed().isEmpty())
        ui->plainTextEditUpliftAxis->setPlainText(defaultUpliftAndAxisText());
    else
        ui->plainTextEditUpliftAxis->setPlainText(upliftAxis);

    const QString rockStress = obj.value("rock_stress_text").toString();
    if (rockStress.trimmed().isEmpty())
        ui->plainTextEditRockStress->setPlainText(defaultRockStressText());
    else
        ui->plainTextEditRockStress->setPlainText(rockStress);

    const QString content = obj.value("section_text").toString();
    if (content.trimmed().isEmpty())
        ui->plainTextEditSection->setPlainText(defaultSectionText());
    else
        ui->plainTextEditSection->setPlainText(content);
}

QString FillStorageCyclePage::defaultBaseVariablesText() const
{
    return QString(
        "'F:\\FEMCLASS\\Z03_JUNZ3D\\PREP\\'      1\n"
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
        "'10'      30\n");
}

QString FillStorageCyclePage::defaultUpliftAndAxisText() const
{
    return QString(
        "0.000000  0.000000  0.000000    X1  X2  X3\n"
        "0.000000  0.000000  0.000000    ALFA1  ALFA2  ALFA3\n"
        "1  3  0.000000  0.000000  0.000000  0.000000    IXUPDW XAMID YAMID XBMID "
        "YBMID\n");
}

QString FillStorageCyclePage::defaultRockStressText() const
{
    return QString("1  0    IFROCK  IFISO\n");
}

QString FillStorageCyclePage::defaultSectionText() const
{
    return QString(
        "   1    NCASE\n"
        " ----------------------------------------------\n"
        "    1    ICASE--------------------------------------\n"
        "   0     0     1     IFIIWL_SEEP   IFIIWL_BODY  IFIIWL_FACE\n"
        "   1     0     NUM_WET        IFWET\n"
        "   1     1     1    M1_DRY    M1WET   II\n"
        "   1     0     NUM_DROP       IFDROP\n"
        "  3200.000    3160.000       1  HW_STA    HW_END  II\n"
        "   0     NCHANGE\n"
        "   1  NCIRCLE\n"
        "JPACK_STA JPACK_END\tTT_PACK         HWJ_STA\t      HWJ_END\t TT_FILL "
        "IF_PACK IF_PLIU IF_FILL  IF_FLIU\tCAHEAD CABODY\n"
        "   2     9           0.0000         0.0000        28.0000         "
        "0.0000       1     0     1     0  'NONE'  'NONE'     1\n"
        "'CASE01FILL01'     DIRDYNA\n"
        "   2   NUM_DYNA\n"
        "   3    23     1    MACHAN    MACHAN  II DYNA\n"
        "  10    30     2    MACHAN    MACHAN  II DYNA\n"
        "   0     1     0   IFMODE   IFWAVE     IFSETT\n"
        "  0.020000    0.000000   28.000000    0.000000    DT TSTA TEND "
        "WOMI_1\n"
        "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ\n");
}
