#ifndef OUTPUTWIDGET_H
#define OUTPUTWIDGET_H

#include <QWidget>
#include <QVector>

class QShowEvent;

enum class LogLevel {
    Info,
    Warning,
    Error,
    Debug
};

struct LogEntry {
    QString timestamp;
    LogLevel level;
    QString message;
};

namespace Ui {
class OutputWidget;
}

class OutputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OutputWidget(QWidget *parent = nullptr);
    ~OutputWidget();

    void outputMessage(const QString &str);
    void showEvent(QShowEvent *event);

private slots:
    void onFilterChanged(int index);
    void onClearClicked();

private:
    void trimOverflow();
    void refreshView();
    QString formatEntry(const LogEntry &entry) const;
    LogEntry parseEntry(const QString &rawText) const;
    bool shouldDisplay(LogLevel level) const;

    Ui::OutputWidget *ui;
    int MAX_LINES;
    QVector<LogEntry> m_entries;
};

#endif // OUTPUTWIDGET_H
