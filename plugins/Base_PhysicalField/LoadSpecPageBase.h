#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;
class QVBoxLayout;

class LoadSpecPageBase : public QWidget
{
public:
    LoadSpecPageBase(const QString &title, const QString &description, QWidget *parent = nullptr);

    QString title() const;
    QString description() const;

protected:
    QVBoxLayout *layout() const;
    QTableWidget *createTable(const QStringList &headers);

private:
    QString m_title;
    QString m_description;
    QLabel *m_label = nullptr;
    QVBoxLayout *m_layout = nullptr;
};
