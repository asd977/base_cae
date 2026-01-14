#pragma once

#include <QWidget>

class QLabel;

class LoadSpecPageBase : public QWidget
{
public:
    LoadSpecPageBase(const QString &title, const QString &description, QWidget *parent = nullptr);

    QString title() const;
    QString description() const;

private:
    QString m_title;
    QString m_description;
    QLabel *m_label = nullptr;
};
