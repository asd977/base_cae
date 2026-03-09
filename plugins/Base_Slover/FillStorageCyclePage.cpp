#include "FillStorageCyclePage.h"

#include <QComboBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QTableWidget>
#include <QWidget>

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
    auto bindLine = [this](const QString& key, QLineEdit* edit)
    {
        m_editors.insert(key, edit);
        connect(edit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    };
    auto bindCombo = [this](const QString& key, QComboBox* combo)
    {
        m_editors.insert(key, combo);
        connect(combo, &QComboBox::currentTextChanged, this, [this]() { updatePreview(); });
    };

    bindCombo("IFIIWL_SEEP", ui->comboIFIIWL_SEEP);
    bindCombo("IFIIWL_BODY", ui->comboIFIIWL_BODY);
    bindCombo("IFIIWL_FACE", ui->comboIFIIWL_FACE);
    bindCombo("IFWET", ui->comboIFWET);
    bindCombo("IFDROP", ui->comboIFDROP);
    bindLine("DIRDYNA", ui->editDIRDYNA);
    bindCombo("IFMODE", ui->comboIFMODE);
    bindCombo("IFWAVE", ui->comboIFWAVE);
    bindLine("DT_BLOCK", ui->editDT_BLOCK);
    bindLine("WAVE_FILES", ui->editWAVE_FILES);

    ui->comboIFIIWL_SEEP->setItemData(0, "0");
    ui->comboIFIIWL_SEEP->setItemData(1, "1");
    ui->comboIFIIWL_SEEP->setItemData(2, "2");
    ui->comboIFIIWL_BODY->setItemData(0, "0");
    ui->comboIFIIWL_BODY->setItemData(1, "1");
    ui->comboIFIIWL_BODY->setItemData(2, "2");
    ui->comboIFIIWL_FACE->setItemData(0, "0");
    ui->comboIFIIWL_FACE->setItemData(1, "1");
    ui->comboIFWET->setItemData(0, "0");
    ui->comboIFWET->setItemData(1, "1");
    ui->comboIFDROP->setItemData(0, "0");
    ui->comboIFDROP->setItemData(1, "1");
    ui->comboIFMODE->setItemData(0, "0");
    ui->comboIFMODE->setItemData(1, "1");
    ui->comboIFWAVE->setItemData(0, "0");
    ui->comboIFWAVE->setItemData(1, "1");

    m_wetTable = ui->tableWet;
    m_dropTable = ui->tableDrop;
    m_nChangeTable = ui->tableNChange;
    m_dynaMapTable = ui->tableDynaMap;
    m_circleTable = ui->tableCircle;

    m_wetTable->setRowCount(1);
    m_wetTable->setItem(0, 0, new QTableWidgetItem("1"));
    m_wetTable->setItem(0, 1, new QTableWidgetItem("1"));
    m_wetTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_dropTable->setRowCount(1);
    m_dropTable->setItem(0, 0, new QTableWidgetItem("3200.000"));
    m_dropTable->setItem(0, 1, new QTableWidgetItem("3160.000"));
    m_dropTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_nChangeTable->setRowCount(1);
    m_nChangeTable->setItem(0, 0, new QTableWidgetItem("1"));
    m_nChangeTable->setItem(0, 1, new QTableWidgetItem("1"));
    m_nChangeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_dynaMapTable->setRowCount(1);
    m_dynaMapTable->setItem(0, 0, new QTableWidgetItem("1"));
    m_dynaMapTable->setItem(0, 1, new QTableWidgetItem("1"));
    m_dynaMapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_circleTable->setRowCount(1);
    const QStringList defaults = {"2", "9", "0.0000", "0.0000", "28.0000", "0.0000", "1", "0",
                                  "1", "0", "'NONE'", "'NONE'"};
    for (int c = 0; c < defaults.size(); ++c)
        m_circleTable->setItem(0, c, new QTableWidgetItem(defaults.at(c)));
    m_circleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    connect(m_wetTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_dropTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_nChangeTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_dynaMapTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_circleTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });

    ui->plainTextEditSection->setReadOnly(true);
}

QString FillStorageCyclePage::valueOf(const QString& key) const
{
    auto it = m_editors.constFind(key);
    if (it == m_editors.constEnd())
        return QString();
    if (auto* combo = qobject_cast<QComboBox*>(it.value()))
        return combo->currentData().toString().trimmed();
    if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
        return edit->text().trimmed();
    return QString();
}

void FillStorageCyclePage::setValueOf(const QString& key, const QString& value)
{
    auto it = m_editors.find(key);
    if (it == m_editors.end())
        return;
    if (auto* combo = qobject_cast<QComboBox*>(it.value()))
    {
        const int idx = combo->findData(value.trimmed());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        return;
    }
    if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
        edit->setText(value);
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
    const bool wetEnabled = valueOf("IFWET") == "1";
    const int numWet = wetEnabled && m_wetTable ? m_wetTable->rowCount() : 0;
    text += QString("%1 %2     NUM_WET IFWET\n").arg(numWet).arg(valueOf("IFWET"));
    for (int i = 0; i < numWet; ++i)
        text += QString("%1 %2\n").arg(tableItemText(m_wetTable, i, 0, "1"),
                                        tableItemText(m_wetTable, i, 1, "1"));

    const bool dropEnabled = valueOf("IFDROP") == "1";
    const int numDrop = dropEnabled && m_dropTable ? m_dropTable->rowCount() : 0;
    const QString ifDropOut = dropEnabled ? QString::number(numDrop) : QStringLiteral("0");
    text += QString("%1 %2     NUM_DROP IFDROP\n").arg(numDrop).arg(ifDropOut);
    for (int i = 0; i < numDrop; ++i)
        text += QString("%1 %2 %3\n")
                    .arg(tableItemText(m_dropTable, i, 0, "0.000"),
                         tableItemText(m_dropTable, i, 1, "0.000"))
                    .arg(i + 1);

    const int nChange = m_nChangeTable ? m_nChangeTable->rowCount() : 0;
    text += QString("%1     NCHANGE\n").arg(nChange);
    for (int i = 0; i < nChange; ++i)
        text += QString("%1 %2 %3\n")
                    .arg(tableItemText(m_nChangeTable, i, 0, "1"),
                         tableItemText(m_nChangeTable, i, 1, "1"))
                    .arg(i + 1);
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
    const int numDyna = m_dynaMapTable ? m_dynaMapTable->rowCount() : 0;
    text += QString("%1   NUM_DYNA\n").arg(numDyna);
    for (int i = 0; i < numDyna; ++i)
        text += QString("%1 %2 %3\n")
                    .arg(tableItemText(m_dynaMapTable, i, 0, "1"),
                         tableItemText(m_dynaMapTable, i, 1, "1"))
                    .arg(i + 1);

    text += QString("%1 %2   IFMODE IFWAVE\n").arg(valueOf("IFMODE"), valueOf("IFWAVE"));
    text += QString("%1    DT TSTA TEND WOMI_1\n").arg(valueOf("DT_BLOCK"));
    text += QString("%1   AX AY AZ\n").arg(valueOf("WAVE_FILES"));
    return text;
}

QJsonObject FillStorageCyclePage::saveJson()
{
    QJsonObject obj;
    QJsonObject values;
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it)
    {
        if (auto* combo = qobject_cast<QComboBox*>(it.value()))
            values[it.key()] = combo->currentData().toString();
        else if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
            values[it.key()] = edit->text();
    }
    obj["values"] = values;
    obj["wet_table"] = tableToJson(m_wetTable);
    obj["drop_table"] = tableToJson(m_dropTable);
    obj["nchange_table"] = tableToJson(m_nChangeTable);
    obj["dyna_map_table"] = tableToJson(m_dynaMapTable);
    obj["circle_table"] = tableToJson(m_circleTable);
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    const QJsonObject values = obj.value("values").toObject();
    for (auto it = m_editors.begin(); it != m_editors.end(); ++it)
    {
        QString current;
        if (auto* combo = qobject_cast<QComboBox*>(it.value()))
            current = combo->currentData().toString();
        else if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
            current = edit->text();
        setValueOf(it.key(), values.value(it.key()).toString(current));
    }
    jsonToTable(obj.value("wet_table").toArray(), m_wetTable, 1);
    jsonToTable(obj.value("drop_table").toArray(), m_dropTable, 1);
    jsonToTable(obj.value("nchange_table").toArray(), m_nChangeTable, 1);
    jsonToTable(obj.value("dyna_map_table").toArray(), m_dynaMapTable, 1);
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
        const QJsonArray cols = r < arr.size() ? arr.at(r).toArray() : QJsonArray();

        for (int c = 0; c < table->columnCount(); ++c)
        {
            // Note: QJsonArray also doesn't have .value(c) here!
            // Use .at(c).toString() instead.
            QString val = c < cols.size() ? cols.at(c).toString() : QString();

            if (table->item(r, c))
                table->item(r, c)->setText(val);
            else
                table->setItem(r, c, new QTableWidgetItem(val));
        }
    }
}
