#include "FillStorageCyclePage.h"

#include "ui_FillStorageCyclePage.h"

FillStorageCyclePage::FillStorageCyclePage(QWidget* parent)
    : BasePage("FillStorageCycle"), ui(new Ui::FillStorageCyclePage)
{
    ui->setupUi(this);
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

QJsonObject FillStorageCyclePage::saveJson()
{
    QJsonObject obj;
    obj["section_text"] = sectionText();
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    const QString content = obj.value("section_text").toString();
    if (content.trimmed().isEmpty())
        ui->plainTextEditSection->setPlainText(defaultSectionText());
    else
        ui->plainTextEditSection->setPlainText(content);
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
