#include "StressStrainWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QTextStream>

#include "ui_StressStrainWidget.h"

DamAnalysisWidget::DamAnalysisWidget(QWidget* parent)
    : BasePage("DamAnalysis"), ui(new Ui::StressStrainWidget)
{
    ui->setupUi(this);
    initValueEditors();
    connect(ui->btnCompute, &QPushButton::clicked, this, &DamAnalysisWidget::onComputeClicked);
}

DamAnalysisWidget::~DamAnalysisWidget()
{
    delete ui;
}

void DamAnalysisWidget::beforeLoad()
{
}

void DamAnalysisWidget::beforeUnload()
{
}

void DamAnalysisWidget::initValueEditors()
{
    m_formLayout = new QFormLayout();
    m_formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_baseTable = new QTableWidget(30, 2, this);
    m_baseTable->setHorizontalHeaderLabels({"行号", "值"});
    m_baseTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_baseTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_baseTable->verticalHeader()->setVisible(false);
    for (int i = 0; i < 30; ++i)
    {
        auto* lineItem = new QTableWidgetItem(QString::number(i + 1));
        lineItem->setFlags(lineItem->flags() & ~Qt::ItemIsEditable);
        m_baseTable->setItem(i, 0, lineItem);
        m_baseTable->setItem(i, 1, new QTableWidgetItem(defaultValueByLine(i + 1)));
    }
    m_baseTable->setMinimumHeight(280);

    m_editX = new QLineEdit("0.000000  0.000000  0.000000", this);
    m_editAlfa = new QLineEdit("0.000000  0.000000  0.000000", this);
    m_editAxis = new QLineEdit("1  3  0.000000  0.000000  0.000000  0.000000", this);
    m_editRock = new QLineEdit("1  0", this);

    m_formLayout->addRow("4.3.1 行1~30（土石坝相关文件及变量）", m_baseTable);
    m_formLayout->addRow("4.3.2(1) X1 X2 X3", m_editX);
    m_formLayout->addRow("4.3.2(2) α1 α2 α3", m_editAlfa);
    m_formLayout->addRow("4.3.2(3) IXUPDW XAMID YAMID XBMID YBMID", m_editAxis);
    m_formLayout->addRow("4.3.3 IFROCK IFISO", m_editRock);

    ui->verticalLayout->insertLayout(2, m_formLayout);
}

QString DamAnalysisWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString DamAnalysisWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {
        "F:\\FEMCLASS\\Z03_JUNZ3D\\PREP\\", "FA_DAMUP.DAT", "FA_DAMDW.DAT", "FA_DAMBASE.DAT",
        "FA_DAMMID.DAT", "MA_ROCK.DAT", "MA_DAMUP.DAT", "MA_DAMDW.DAT", "MA_CORE.DAT", "1",
        "MA_PLATE.DAT", "JDTRACE.DAT", "KETRACE.DAT", "TDTRACE.DAT", "0", "0", "0.05", "0.01",
        "2", "4   10", "0.000", "1.962", "1.308", "0.1", "0.2", "2.0", "0.9", "7.0", "0",
        "10"};
    return defaults.value(lineNo - 1);
}

bool DamAnalysisWidget::exportIfillDatFile(int mcase, const QString& cycleSectionText) const
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

    for (int i = 0; i < 30; ++i)
    {
        QString value = m_baseTable->item(i, 1) ? m_baseTable->item(i, 1)->text() : QString();
        if (i == 0)
            value = prepDirAbs;
        os << quoteValue(value) << "      " << (i + 1) << "\n";
    }

    os << m_editX->text().trimmed() << "    X1  X2  X3\n";
    os << m_editAlfa->text().trimmed() << "    ALFA1  ALFA2  ALFA3\n";
    os << m_editAxis->text().trimmed() << "    IXUPDW XAMID YAMID XBMID YBMID\n";
    os << m_editRock->text().trimmed() << "    IFROCK IFISO\n";

    const int safeCaseCount = qMax(1, mcase);
    os << safeCaseCount << "   MCASE\n";
    for (int i = 1; i <= safeCaseCount; ++i)
        os << i << "     " << i << "\n";

    QString dynamicSection = cycleSectionText.trimmed();
    if (dynamicSection.isEmpty())
    {
        dynamicSection =
            "1    NCASE\n"
            "----------------------------------------------\n"
            "1    ICASE--------------------------------------\n"
            "0     0     1     IFIIWL_SEEP   IFIIWL_BODY  IFIIWL_FACE\n"
            "0     0     NUM_WET        IFWET\n"
            "0     0     NUM_DROP       IFDROP\n"
            "0     NCHANGE\n"
            "1  NCIRCLE\n"
            "0 0 0.0000 0.0000 0.0000 0.0000 0 0 0 0 'NONE' 'NONE' 1\n"
            "'CASE01FILL01'     DIRDYNA\n"
            "0   NUM_DYNA\n"
            "0     0     0   IFMODE   IFWAVE     IFSETT\n"
            "0.020000    0.000000    0.000000    0.000000    DT TSTA TEND WOMI_1\n"
            "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ";
    }
    dynamicSection.replace("'CASE01FILL01'", "'" + caseDirAbs + "'");
    os << dynamicSection;
    if (!dynamicSection.endsWith('\n'))
        os << "\n";

    return true;
}

QJsonObject DamAnalysisWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray arr;
    for (int i = 0; i < 30; ++i)
        arr.append(m_baseTable->item(i, 1) ? m_baseTable->item(i, 1)->text() : QString());
    obj["base_values"] = arr;
    obj["x_values"] = m_editX->text();
    obj["alfa_values"] = m_editAlfa->text();
    obj["axis_values"] = m_editAxis->text();
    obj["rock_values"] = m_editRock->text();
    return obj;
}

void DamAnalysisWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int i = 0; i < 30; ++i)
    {
        const QString value = (i < arr.size() ? arr.at(i).toString() : defaultValueByLine(i + 1));
        if (m_baseTable->item(i, 1))
            m_baseTable->item(i, 1)->setText(value);
    }
    m_editX->setText(obj.value("x_values").toString(m_editX->text()));
    m_editAlfa->setText(obj.value("alfa_values").toString(m_editAlfa->text()));
    m_editAxis->setText(obj.value("axis_values").toString(m_editAxis->text()));
    m_editRock->setText(obj.value("rock_values").toString(m_editRock->text()));
}

void DamAnalysisWidget::onComputeClicked()
{
    emit computeRequested();
}
