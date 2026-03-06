#include "FillStorageCyclePage.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

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
    auto bind = [this](const QString& key, QLineEdit* edit)
    {
        m_edits.insert(key, edit);
        connect(edit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    };

    bind("IFIIWL_SEEP", ui->editIFIIWL_SEEP);
    bind("IFIIWL_BODY", ui->editIFIIWL_BODY);
    bind("IFIIWL_FACE", ui->editIFIIWL_FACE);
    bind("IFWET", ui->editIFWET);
    bind("IFDROP", ui->editIFDROP);
    bind("NCHANGE", ui->editNCHANGE);
    bind("DIRDYNA", ui->editDIRDYNA);
    bind("NUM_DYNA", ui->editNUM_DYNA);
    bind("M_DYNA_PAIR", ui->editM_DYNA_PAIR);
    bind("DYN_FLAGS", ui->editDYN_FLAGS);
    bind("DT_BLOCK", ui->editDT_BLOCK);
    bind("WAVE_FILES", ui->editWAVE_FILES);

    m_wetTable = ui->tableWet;
    m_dropTable = ui->tableDrop;
    m_circleTable = ui->tableCircle;
    m_toggleCircleColumnsButton = ui->btnToggleCircleColumns;

    m_wetTable->setRowCount(1);
    m_wetTable->setItem(0, 0, new QTableWidgetItem("1"));
    m_wetTable->setItem(0, 1, new QTableWidgetItem("1"));
    m_wetTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_dropTable->setRowCount(1);
    m_dropTable->setItem(0, 0, new QTableWidgetItem("3200.000"));
    m_dropTable->setItem(0, 1, new QTableWidgetItem("3160.000"));
    m_dropTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_circleTable->setRowCount(1);
    const QStringList defaults = {"2", "9", "0.0000", "0.0000", "28.0000", "0.0000", "1", "0",
                                  "1", "0", "'NONE'", "'NONE'"};
    for (int c = 0; c < defaults.size(); ++c)
        m_circleTable->setItem(0, c, new QTableWidgetItem(defaults.at(c)));
    m_circleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_circleTable->horizontalHeader()->setMinimumSectionSize(80);
    m_circleTable->horizontalHeader()->setDefaultSectionSize(110);

    applyCircleTableColumnMode();
    if (m_toggleCircleColumnsButton)
    {
        connect(m_toggleCircleColumnsButton, &QPushButton::clicked, this,
                &FillStorageCyclePage::toggleCircleTableColumnMode);
    }

    connect(m_wetTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_dropTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_circleTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });

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
    const int numWet = m_wetTable ? m_wetTable->rowCount() : 0;
    text += QString("%1 %2     NUM_WET IFWET\n").arg(numWet).arg(valueOf("IFWET"));
    for (int i = 0; i < numWet; ++i)
        text += QString("%1 %2\n").arg(tableItemText(m_wetTable, i, 0, "1"),
                                        tableItemText(m_wetTable, i, 1, "1"));

    const int numDrop = m_dropTable ? m_dropTable->rowCount() : 0;
    text += QString("%1 %2     NUM_DROP IFDROP\n").arg(numDrop).arg(valueOf("IFDROP"));
    for (int i = 0; i < numDrop; ++i)
        text += QString("%1 %2 %3\n")
                    .arg(tableItemText(m_dropTable, i, 0, "0.000"),
                         tableItemText(m_dropTable, i, 1, "0.000"))
                    .arg(i + 1);

    text += QString("%1     NCHANGE\n").arg(valueOf("NCHANGE"));
    const int nCircle = m_circleTable ? m_circleTable->rowCount() : 0;
    text += QString("%1  NCIRCLE\n").arg(nCircle);
    text += "JPACK_STA JPACK_END TT_PACK HWJ_STA HWJ_END TT_FILL IF_PACK IF_PLIU IF_FILL IF_FLIU CAHEAD CABODY\n";
    for (int i = 0; i < nCircle; ++i)
    {
        text += QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13\n")
                    // 第一组：处理前 9 个占位符
                    .arg(tableItemText(m_circleTable, i, 0, "0"),
                         tableItemText(m_circleTable, i, 1, "0"),
                         tableItemText(m_circleTable, i, 2, "0.0000"),
                         tableItemText(m_circleTable, i, 3, "0.0000"),
                         tableItemText(m_circleTable, i, 4, "0.0000"),
                         tableItemText(m_circleTable, i, 5, "0.0000"),
                         tableItemText(m_circleTable, i, 6, "0"),
                         tableItemText(m_circleTable, i, 7, "0"),
                         tableItemText(m_circleTable, i, 8, "0"))
                    // 第二组：处理剩下的 4 个占位符 (%10, %11, %12, %13)
                    .arg(tableItemText(m_circleTable, i, 9, "0"),
                         tableItemText(m_circleTable, i, 10, "'NONE'"),
                         tableItemText(m_circleTable, i, 11, "'NONE'"),
                         QString::number(i + 1)); // 这里的 i+1 建议显式转为 QString
    }

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
    obj["wet_table"] = tableToJson(m_wetTable);
    obj["drop_table"] = tableToJson(m_dropTable);
    obj["circle_table"] = tableToJson(m_circleTable);
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    const QJsonObject values = obj.value("values").toObject();
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it)
        it.value()->setText(values.value(it.key()).toString(it.value()->text()));
    jsonToTable(obj.value("wet_table").toArray(), m_wetTable, 1);
    jsonToTable(obj.value("drop_table").toArray(), m_dropTable, 1);
    jsonToTable(obj.value("circle_table").toArray(), m_circleTable, 1);
    updatePreview();
}

QString FillStorageCyclePage::tableItemText(QTableWidget* table, int row, int col,
                                            const QString& fallback) const
{
    if (!table)
        return fallback;
    auto* item = table->item(row, col);
    const QString t = item ? item->text().trimmed() : QString();
    return t.isEmpty() ? fallback : t;
}

QJsonArray FillStorageCyclePage::tableToJson(QTableWidget* table) const
{
    QJsonArray rows;
    if (!table)
        return rows;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        QJsonArray cols;
        for (int c = 0; c < table->columnCount(); ++c)
            cols.append(tableItemText(table, r, c, QString()));
        rows.append(cols);
    }
    return rows;
}

void FillStorageCyclePage::jsonToTable(const QJsonArray& arr, QTableWidget* table, int minRows)
{
    if (!table)
        return;
    const int rows = qMax(minRows, arr.size());
    table->setRowCount(rows);
    for (int r = 0; r < rows; ++r)
    {
        // Change .value(r) to .at(r)
        const QJsonArray cols = arr.at(r).toArray();

        for (int c = 0; c < table->columnCount(); ++c)
        {
            // Note: QJsonArray also doesn't have .value(c) here!
            // Use .at(c).toString() instead.
            QString val = cols.at(c).toString();

            if (table->item(r, c))
                table->item(r, c)->setText(val);
            else
                table->setItem(r, c, new QTableWidgetItem(val));
        }
    }
}

void FillStorageCyclePage::applyCircleTableColumnMode()
{
    if (!m_circleTable)
        return;

    // 紧凑模式默认隐藏次要列，保留关键时间与水位字段。
    const QList<int> compactHiddenColumns = {6, 7, 8, 9, 10, 11};
    for (int col = 0; col < m_circleTable->columnCount(); ++col)
        m_circleTable->setColumnHidden(col, !m_circleTableShowAllColumns && compactHiddenColumns.contains(col));

    if (m_toggleCircleColumnsButton)
        m_toggleCircleColumnsButton->setText(m_circleTableShowAllColumns ? "显示关键列" : "显示全部列");
}

void FillStorageCyclePage::toggleCircleTableColumnMode()
{
    m_circleTableShowAllColumns = !m_circleTableShowAllColumns;
    applyCircleTableColumnMode();
}
