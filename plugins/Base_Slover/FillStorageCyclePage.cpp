#include "FillStorageCyclePage.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QRegExp>

#include "ui_FillStorageCyclePage.h"

FillStorageCyclePage::FillStorageCyclePage(QWidget* parent)
    : BasePage("FillStorageCycle"), ui(new Ui::FillStorageCyclePage)
{
    ui->setupUi(this);
    initValueEditors();
    updatePreview();
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

void FillStorageCyclePage::initValueEditors()
{
    m_formLayout = new QFormLayout();
    auto add = [this](const QString& label, const QString& key, const QString& value)
    {
        auto* edit = new QLineEdit(value, this);
        connect(edit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
        m_formLayout->addRow(label, edit);
        m_edits.insert(key, edit);
    };

    add("IFIIWL_SEEP", "IFIIWL_SEEP", "0");
    add("IFIIWL_BODY", "IFIIWL_BODY", "0");
    add("IFIIWL_FACE", "IFIIWL_FACE", "1");
    add("NUM_WET", "NUM_WET", "0");
    add("IFWET", "IFWET", "0");
    add("M1_DRY M1_WET", "M1_WET_PAIR", "1 1");
    add("NUM_DROP", "NUM_DROP", "0");
    add("IFDROP", "IFDROP", "0");
    add("HW_DROP_STA HW_DROP_END", "HW_DROP", "3200.000 3160.000");
    add("NCHANGE", "NCHANGE", "0");
    add("NCIRCLE", "NCIRCLE", "1");
    add("JPACK_STA JPACK_END", "JPACK", "2 9");
    add("TT_PACK", "TT_PACK", "0.0000");
    add("HWJ_STA HWJ_END", "HWJ", "0.0000 28.0000");
    add("TT_FILL", "TT_FILL", "0.0000");
    add("IF_PACK IF_PLIU IF_FILL IF_FLIU", "IF_FLAGS", "1 0 1 0");
    add("CAHEAD CABODY", "CA_FILES", "'NONE' 'NONE'");
    add("DIRDYNA", "DIRDYNA", "'CASE01FILL01'");
    add("NUM_DYNA", "NUM_DYNA", "0");
    add("M1_STA M1_DYNA", "M_DYNA_PAIR", "3 23");
    add("IFMODE IFWAVE IFSETT", "DYN_FLAGS", "0 1 0");
    add("DT TSTA TEND WOMI_1", "DT_BLOCK", "0.020000 0.000000 28.000000 0.000000");
    add("AX AY AZ", "WAVE_FILES", "'WAVE-AX.DAT' 'WAVE-AY.DAT' 'WAVE-AZ.DAT'");

    ui->verticalLayout->insertLayout(2, m_formLayout);
    ui->plainTextEditSection->setReadOnly(true);
}

QString FillStorageCyclePage::valueOf(const QString& key) const
{
    auto it = m_edits.constFind(key);
    return it == m_edits.constEnd() ? QString() : it.value()->text().trimmed();
}

void FillStorageCyclePage::setValueOf(const QString& key, const QString& value)
{
    auto it = m_edits.find(key);
    if (it != m_edits.end())
        it.value()->setText(value);
}

void FillStorageCyclePage::updatePreview()
{
    ui->plainTextEditSection->setPlainText(sectionText());
}

QString FillStorageCyclePage::sectionText() const
{
    return sectionText(1);
}

QString FillStorageCyclePage::sectionText(int caseIndex) const
{
    QString text;
    text += "1    NCASE\n";
    text += " ----------------------------------------------\n";
    text += QString("%1    ICASE--------------------------------------\n").arg(caseIndex);
    text += QString("%1 %2 %3     IFIIWL_SEEP IFIIWL_BODY IFIIWL_FACE\n")
                .arg(valueOf("IFIIWL_SEEP"), valueOf("IFIIWL_BODY"), valueOf("IFIIWL_FACE"));
    text += QString("%1 %2     NUM_WET IFWET\n").arg(valueOf("NUM_WET"), valueOf("IFWET"));
    if (valueOf("NUM_WET").toInt() > 0)
        text += valueOf("M1_WET_PAIR") + "    M1_DRY M1_WET\n";

    text += QString("%1 %2     NUM_DROP IFDROP\n").arg(valueOf("NUM_DROP"), valueOf("IFDROP"));
    if (valueOf("NUM_DROP").toInt() > 0)
        text += valueOf("HW_DROP") + "    HW_DROP_STA HW_DROP_END\n";

    text += QString("%1     NCHANGE\n").arg(valueOf("NCHANGE"));
    text += QString("%1  NCIRCLE\n").arg(valueOf("NCIRCLE"));
    text += "JPACK_STA JPACK_END TT_PACK HWJ_STA HWJ_END TT_FILL IF_PACK IF_PLIU IF_FILL IF_FLIU CAHEAD CABODY\n";

    const QStringList jpack = valueOf("JPACK").split(QRegExp("\\s+"), Qt::SkipEmptyParts);
    const QStringList hwj = valueOf("HWJ").split(QRegExp("\\s+"), Qt::SkipEmptyParts);
    const QStringList flags = valueOf("IF_FLAGS").split(QRegExp("\\s+"), Qt::SkipEmptyParts);
    const QString jpackSta = jpack.value(0, "0");
    const QString jpackEnd = jpack.value(1, "0");
    const QString hwjSta = hwj.value(0, "0.0000");
    const QString hwjEnd = hwj.value(1, "0.0000");
    const QString ifPack = flags.value(0, "0");
    const QString ifPliu = flags.value(1, "0");
    const QString ifFill = flags.value(2, "0");
    const QString ifFliu = flags.value(3, "0");
    const QStringList cafiles = valueOf("CA_FILES").split(QRegExp("\\s+"), Qt::SkipEmptyParts);
    text += QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 1\n")
                .arg(jpackSta, jpackEnd, valueOf("TT_PACK"), hwjSta, hwjEnd, valueOf("TT_FILL"),
                     ifPack, ifPliu, ifFill, ifFliu, cafiles.value(0, "'NONE'"),
                     cafiles.value(1, "'NONE'"));

    text += QString("%1     DIRDYNA\n").arg(valueOf("DIRDYNA"));
    text += QString("%1   NUM_DYNA\n").arg(valueOf("NUM_DYNA"));
    if (valueOf("NUM_DYNA").toInt() > 0)
        text += valueOf("M_DYNA_PAIR") + "  1    MACHAN MACHAN II DYNA\n";

    text += QString("%1   IFMODE IFWAVE IFSETT\n").arg(valueOf("DYN_FLAGS"));
    text += QString("%1    DT TSTA TEND WOMI_1\n").arg(valueOf("DT_BLOCK"));
    text += QString("%1   AX AY AZ\n").arg(valueOf("WAVE_FILES"));
    return text;
}

QJsonObject FillStorageCyclePage::saveJson()
{
    QJsonObject obj;
    QJsonObject values;
    for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it)
        values[it.key()] = it.value()->text();
    obj["values"] = values;
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    const QJsonObject values = obj.value("values").toObject();
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it)
        it.value()->setText(values.value(it.key()).toString(it.value()->text()));
    updatePreview();
}
