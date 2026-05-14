#include "OutputWidget.h"

#include <QDateTime>
#include <QScrollBar>
#include <QTextCursor>

#include "ui_OutputWidget.h"

OutputWidget::OutputWidget(QWidget* parent) : QWidget(parent), ui(new Ui::OutputWidget)
{
    ui->setupUi(this);
    ui->textEdit->setReadOnly(true);
    ui->textEdit->setUndoRedoEnabled(false);
    ui->levelFilterComboBox->setCurrentIndex(0);
    MAX_LINES = 300;

    connect(ui->levelFilterComboBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(onFilterChanged(int)));
    connect(ui->clearPushButton, SIGNAL(clicked()), this, SLOT(onClearClicked()));
}

void OutputWidget::outputMessage(const QString& str)
{
    m_entries.push_back(parseEntry(str));
    trimOverflow();
    refreshView();
}

void OutputWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ui->textEdit->moveCursor(QTextCursor::End);
}

void OutputWidget::onFilterChanged(int)
{
    refreshView();
}

void OutputWidget::onClearClicked()
{
    m_entries.clear();
    ui->textEdit->clear();
}

void OutputWidget::trimOverflow()
{
    while (m_entries.size() > MAX_LINES)
    {
        m_entries.pop_front();
    }
}

void OutputWidget::refreshView()
{
    const bool isAtBottom =
        ui->textEdit->verticalScrollBar()->value() == ui->textEdit->verticalScrollBar()->maximum();

    ui->textEdit->clear();
    for (int i = 0; i < m_entries.size(); ++i)
    {
        if (!shouldDisplay(m_entries[i].level))
        {
            continue;
        }
        ui->textEdit->append(formatEntry(m_entries[i]));
    }

    if (isAtBottom)
    {
        ui->textEdit->moveCursor(QTextCursor::End);
    }
}

QString OutputWidget::formatEntry(const LogEntry& entry) const
{
    QString color = "#2e7d32";
    QString levelText = entry.category.isEmpty() ? "通用" : entry.category;
    if (entry.level == LogLevel::Warning)
    {
        color = "#ed6c02";
        levelText = "WARN";
    }
    else if (entry.level == LogLevel::Error)
    {
        color = "#d32f2f";
        levelText = "ERROR";
    }


    return QString("<span style='color:#9e9e9e;'>[%1]</span> "
                   "<span style='color:%2; font-weight:600;'>[%3]</span> %4")
        .arg(entry.timestamp)
        .arg(color)
        .arg(levelText)
        .arg(entry.message.toHtmlEscaped());
}

LogEntry OutputWidget::parseEntry(const QString& rawText) const
{
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime().toString("MM-dd HH:mm");
    entry.level = LogLevel::Info;
    entry.message = rawText;
    entry.category = detectCategory(rawText);

    if (rawText.startsWith("[ERROR]", Qt::CaseInsensitive) || rawText.startsWith("[错误]"))
    {
        entry.level = LogLevel::Error;
        entry.message = rawText.mid(rawText.indexOf(']') + 1).trimmed();
    }
    else if (rawText.startsWith("[WARN]", Qt::CaseInsensitive) ||
             rawText.startsWith("[WARNING]", Qt::CaseInsensitive) || rawText.startsWith("[警告]"))
    {
        entry.level = LogLevel::Warning;
        entry.message = rawText.mid(rawText.indexOf(']') + 1).trimmed();
    }
    else if (rawText.startsWith("[INFO]", Qt::CaseInsensitive) || rawText.startsWith("[信息]"))
    {
        entry.level = LogLevel::Info;
        entry.message = rawText.mid(rawText.indexOf(']') + 1).trimmed();
    }

    entry.category = detectCategory(entry.message);
    return entry;
}

QString OutputWidget::detectCategory(const QString& message) const
{
    if (message.contains("几何") || message.contains("前处理")) return "几何";
    if (message.contains("网格")) return "网格";
    if (message.contains("求解")) return "求解";
    if (message.contains("材料")) return "材料";
    if (message.contains("后处理") || message.contains("云图") || message.contains("等值")) return "后处理";
    if (message.contains("项目")) return "项目";
    return "通用";
}

bool OutputWidget::shouldDisplay(LogLevel level) const
{
    const int filterIndex = ui->levelFilterComboBox->currentIndex();
    if (filterIndex == 0)
    {
        return true;
    }
    if (filterIndex == 1)
    {
        return level == LogLevel::Info;
    }
    if (filterIndex == 2)
    {
        return level == LogLevel::Warning;
    }
    if (filterIndex == 3)
    {
        return level == LogLevel::Error;
    }
    if (filterIndex == 4)
    {
        return level == LogLevel::Debug;
    }
    return true;
}

OutputWidget::~OutputWidget()
{
    delete ui;
}
