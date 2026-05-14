#ifndef RESULTRULECONFIGDIALOG_H
#define RESULTRULECONFIGDIALOG_H

#include <QDialog>

#include "ResultRuleManager.h"

class QTableWidget;

class ResultRuleConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ResultRuleConfigDialog(QWidget* parent = nullptr);

private:
    void buildUi();
    void loadFromManager();
    void saveToManager();

    void addDirectoryRule();
    void removeDirectoryRule();

    void addFileRule();
    void removeFileRule();

    QVector<ResultDirectoryRule> collectDirectoryRules() const;
    QVector<ResultFileRule> collectFileRules() const;

private:
    QTableWidget* m_dirTable{nullptr};
    QTableWidget* m_fileTable{nullptr};
};

#endif // RESULTRULECONFIGDIALOG_H
