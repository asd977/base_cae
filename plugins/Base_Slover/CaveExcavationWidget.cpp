#include "CaveExcavationWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include "ui_CaveExcavationWidget.h"

CaveExcavationWidget::CaveExcavationWidget(QWidget* parent)
    : BasePage("CaveExcavationAnalysis"), ui(new Ui::CaveExcavationWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#CaveExcavationWidget, QWidget#scrollAreaWidgetContents { background: white; }");
    ui->scrollArea->setStyleSheet("QScrollArea { background: white; border: 0; }");

    connect(ui->btnCompute, &QPushButton::clicked, this, &CaveExcavationWidget::onComputeClicked);
}

CaveExcavationWidget::~CaveExcavationWidget()
{
    delete ui;
}

void CaveExcavationWidget::beforeLoad() {}
void CaveExcavationWidget::beforeUnload() {}

bool CaveExcavationWidget::exportICaveDatFile(const QString& supportCaseSection) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("ICAVE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const QHash<int, QString> fixedValues = {
        {1, QDir::toNativeSeparators(wd.filePath("PREP")) + QDir::separator()},
        {2, QStringLiteral("MAFACTOR.DAT")},
        {4, QStringLiteral("SHUIMX.DAT")},
        {6, QStringLiteral("IIJX_CHEN.DAT")},
        {8, QStringLiteral("IIJX_T01-01.DAT")},
        {9, QStringLiteral("MASET_DCL.DAT")},
        {12, QStringLiteral("IIJX_T11.DAT")},
        {13, QStringLiteral("IISEEP.DAT")},
        {14, QDir::toNativeSeparators(wd.filePath("GEOSTR")) + QDir::separator()},
        {15, QStringLiteral("JDTRACE.DAT")},
        {16, QStringLiteral("KETRACE.DAT")},
        {17, QStringLiteral("TDTRACE.DAT")},
    };

    const QSet<int> quotedFields = {1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int fieldNo = 1; fieldNo <= 25; ++fieldNo)
    {
        QString value;
        if (fixedValues.contains(fieldNo))
            value = fixedValues.value(fieldNo);
        else if (auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(fieldNo)))
            value = edit->text().trimmed();

        if (quotedFields.contains(fieldNo))
            os << "'" << value << "'      " << fieldNo << "\n";
        else
            os << " " << value << "      " << fieldNo << "\n";
    }

    if (!supportCaseSection.trimmed().isEmpty())
        os << supportCaseSection.trimmed() << "\n";
    return true;
}

QJsonObject CaveExcavationWidget::saveJson()
{
    QJsonObject obj;
    for (int i = 1; i <= 25; ++i)
    {
        const QString key = QString("line_%1").arg(i);
        const QStringList fixed = {"1","2","4","6","8","9","12","13","14","15","16","17"};
        if (fixed.contains(QString::number(i)))
            continue;
        auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(i));
        obj[key] = edit ? edit->text().trimmed() : "";
    }
    return obj;
}

void CaveExcavationWidget::loadJson(const QJsonObject& obj)
{
    for (int i = 1; i <= 25; ++i)
    {
        const QStringList fixed = {"1","2","4","6","8","9","12","13","14","15","16","17"};
        if (fixed.contains(QString::number(i)))
            continue;
        auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(i));
        if (!edit)
            continue;
        edit->setText(obj.value(QString("line_%1").arg(i)).toString(edit->text()));
    }
}

void CaveExcavationWidget::onComputeClicked()
{
    emit computeRequested();
}
