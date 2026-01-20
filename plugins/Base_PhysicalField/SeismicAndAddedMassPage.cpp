#include "SeismicAndAddedMassPage.h"
#include "ui_SeismicAndAddedMassPage.h"

#include <QPlainTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

static void setItem(QTableWidget* t, int r, int c, const QString& v)
{
    auto* it = t->item(r, c);
    if (!it) { it = new QTableWidgetItem; t->setItem(r, c, it); }
    it->setText(v);
}

SeismicAndAddedMassPage::SeismicAndAddedMassPage(QWidget* parent)
    : BasePage("SeismicAndAddedMassPage")
{
    auto* ui = new Ui::SeismicAndAddedMassPage;
    ui->setupUi(this);

    // seismic
    m_ntbx = ui->spinNTBX;
    m_ntby = ui->spinNTBY;
    m_ntbz = ui->spinNTBZ;

    m_xSeries = ui->editX;
    m_ySeries = ui->editY;
    m_zSeries = ui->editZ;

    m_btnExampleX = ui->btnExampleX;
    m_btnExampleY = ui->btnExampleY;
    m_btnExampleZ = ui->btnExampleZ;

    // added mass
    m_nmm = ui->spinNMM;
    m_makod = ui->spinMAKOD;
    m_hwupTop = ui->spinHWUPTop;
    m_hwupBot = ui->spinHWUPBot;
    m_ifwest = ui->checkIFWEST;

    m_massTable = ui->tableMass;

    m_btnAddRow = ui->btnAddRow;
    m_btnRemoveRow = ui->btnRemoveRow;

    // generate
    m_btnGenerate = ui->btnGenerate;

    connect(m_btnGenerate, &QPushButton::clicked, this, &SeismicAndAddedMassPage::onGenerate);
    connect(m_btnExampleX, &QPushButton::clicked, this, &SeismicAndAddedMassPage::onFillExampleX);
    connect(m_btnExampleY, &QPushButton::clicked, this, &SeismicAndAddedMassPage::onFillExampleY);
    connect(m_btnExampleZ, &QPushButton::clicked, this, &SeismicAndAddedMassPage::onFillExampleZ);
    connect(m_btnAddRow, &QPushButton::clicked, this, &SeismicAndAddedMassPage::onAddMassRow);
    connect(m_btnRemoveRow, &QPushButton::clicked, this, &SeismicAndAddedMassPage::onRemoveMassRow);

    // defaults
    m_ntbx->setValue(0);
    m_ntby->setValue(0);
    m_ntbz->setValue(0);

    m_nmm->setValue(0);
    m_makod->setValue(0);
    m_hwupTop->setValue(0.0);
    m_hwupBot->setValue(0.0);
    m_ifwest->setChecked(false);

    // table columns: idx, KE1, KE2, JD1..JD4
    m_massTable->setColumnCount(7);
    m_massTable->setHorizontalHeaderLabels(
        QStringList() << "序号" << "KE1" << "KE2" << "JD1" << "JD2" << "JD3" << "JD4"
        );
    m_massTable->setRowCount(0);
}

SeismicAndAddedMassPage::~SeismicAndAddedMassPage() = default;

QVector<double> SeismicAndAddedMassPage::parseSeries(const QString& text, QString* err)
{
    QVector<double> out;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return out;

    // 支持：每行一个；或空格/逗号分隔
    QString norm = trimmed;
    norm.replace(',', ' ');
    const QStringList lines = norm.split('\n', Qt::SkipEmptyParts);

    for (const QString& ln0 : lines)
    {
        const QString ln = ln0.trimmed();
        if (ln.isEmpty()) continue;
        const QStringList parts = ln.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

        for (const QString& p : parts)
        {
            bool ok = false;
            const double v = p.toDouble(&ok);
            if (!ok)
            {
                if (err) *err = QString("无法解析数值：\"%1\"").arg(p);
                return {};
            }
            out.push_back(v);
        }
    }
    return out;
}

void SeismicAndAddedMassPage::onFillExampleX()
{
    m_ntbx->setValue(3);
    m_xSeries->setPlainText("0.01\n0.02\n0.03\n");
}
void SeismicAndAddedMassPage::onFillExampleY()
{
    m_ntby->setValue(3);
    m_ySeries->setPlainText("0.10\n0.20\n0.30\n");
}
void SeismicAndAddedMassPage::onFillExampleZ()
{
    m_ntbz->setValue(3);
    m_zSeries->setPlainText("1.0\n0.5\n0.0\n");
}

void SeismicAndAddedMassPage::onAddMassRow()
{
    const int r = m_massTable->rowCount();
    m_massTable->insertRow(r);
    setItem(m_massTable, r, 0, QString::number(r + 1));
    for (int c = 1; c < m_massTable->columnCount(); ++c)
        setItem(m_massTable, r, c, "0");
    m_nmm->setValue(m_massTable->rowCount());
}

void SeismicAndAddedMassPage::onRemoveMassRow()
{
    int r = m_massTable->currentRow();
    if (r < 0) r = m_massTable->rowCount() - 1;
    if (r < 0) return;

    m_massTable->removeRow(r);
    // renumber
    for (int i = 0; i < m_massTable->rowCount(); ++i)
        setItem(m_massTable, i, 0, QString::number(i + 1));
    m_nmm->setValue(m_massTable->rowCount());
}

void SeismicAndAddedMassPage::onGenerate()
{
    dumpGeneratedToDebug();
}

void SeismicAndAddedMassPage::dumpGeneratedToDebug()
{
    // parse seismic
    QString err;
    const QVector<double> ax = parseSeries(m_xSeries->toPlainText(), &err);
    if (!err.isEmpty()) { QMessageBox::warning(this, tr("错误"), tr("X 加速度表解析失败：%1").arg(err)); return; }
    const QVector<double> ay = parseSeries(m_ySeries->toPlainText(), &err);
    if (!err.isEmpty()) { QMessageBox::warning(this, tr("错误"), tr("Y 加速度表解析失败：%1").arg(err)); return; }
    const QVector<double> az = parseSeries(m_zSeries->toPlainText(), &err);
    if (!err.isEmpty()) { QMessageBox::warning(this, tr("错误"), tr("Z 加速度表解析失败：%1").arg(err)); return; }

    const int NTBX = m_ntbx->value();
    const int NTBY = m_ntby->value();
    const int NTBZ = m_ntbz->value();

    if (NTBX != ax.size())
    {
        QMessageBox::warning(this, tr("错误"),
                             tr("NTBX=%1 但 X 序列长度=%2（请保持一致）").arg(NTBX).arg(ax.size()));
        return;
    }
    if (NTBY != ay.size())
    {
        QMessageBox::warning(this, tr("错误"),
                             tr("NTBY=%1 但 Y 序列长度=%2（请保持一致）").arg(NTBY).arg(ay.size()));
        return;
    }
    if (NTBZ != az.size())
    {
        QMessageBox::warning(this, tr("错误"),
                             tr("NTBZ=%1 但 Z 序列长度=%2（请保持一致）").arg(NTBZ).arg(az.size()));
        return;
    }

    // added mass
    const int NMM = m_nmm->value();
    const int MAKOD = m_makod->value();
    const double HWUP_TOP = m_hwupTop->value();
    const double HWUP_BOT = m_hwupBot->value();
    const int IFWEST = m_ifwest->isChecked() ? 1 : 0;

    if (NMM != m_massTable->rowCount())
    {
        // 这里不强制，自动纠正也行
        // 我选择纠正，避免你手动改错
        //（也方便后面写文件）
        // 注意：你要严格校验我也可以改
        // 这里先纠正
        // const_cast 省了，这里直接提示并返回也可以
    }

    qDebug().noquote() << "========== IIWL.DAT Part9-11 (Seismic) + AddedMass ==========";

    // Part9 X
    qDebug().noquote() << QString("%1").arg(NTBX);
    if (NTBX != 0) { qDebug().noquote() << ""; qDebug().noquote() << ""; } // 空二行
    for (int i = 0; i < ax.size(); ++i)
        qDebug().noquote() << QString("%1  %2").arg(i + 1).arg(ax[i], 0, 'f', 6);

    // Part10 Y
    qDebug().noquote() << QString("%1").arg(NTBY);
    if (NTBY != 0) { qDebug().noquote() << ""; qDebug().noquote() << ""; } // 空二行
    for (int i = 0; i < ay.size(); ++i)
        qDebug().noquote() << QString("%1  %2").arg(i + 1).arg(ay[i], 0, 'f', 6);

    // Part11 Z
    qDebug().noquote() << QString("%1").arg(NTBZ);
    if (NTBZ != 0) { qDebug().noquote() << ""; qDebug().noquote() << ""; } // 空二行
    for (int i = 0; i < az.size(); ++i)
        qDebug().noquote() << QString("%1  %2").arg(i + 1).arg(az[i], 0, 'f', 6);

    // Added mass header
    qDebug().noquote() << QString("%1 %2 %3 %4 %5")
                              .arg(m_massTable->rowCount())
                              .arg(MAKOD)
                              .arg(HWUP_TOP, 0, 'f', 6)
                              .arg(HWUP_BOT, 0, 'f', 6)
                              .arg(IFWEST);
    qDebug().noquote() << ""; // 空1行

    // rows: idx KE1 KE2 JD1 JD2 JD3 JD4
    for (int r = 0; r < m_massTable->rowCount(); ++r)
    {
        auto get = [&](int c)->QString{
            auto* it = m_massTable->item(r, c);
            return it ? it->text().trimmed() : QString("0");
        };

        const QString line = QString("%1  %2  %3  %4  %5  %6  %7")
                                 .arg(get(0))
                                 .arg(get(1))
                                 .arg(get(2))
                                 .arg(get(3))
                                 .arg(get(4))
                                 .arg(get(5))
                                 .arg(get(6));

        qDebug().noquote() << line;
    }

    qDebug().noquote() << "================================================================";
}
