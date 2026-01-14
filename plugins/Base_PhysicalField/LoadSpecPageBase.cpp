#include "LoadSpecPageBase.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

LoadSpecPageBase::LoadSpecPageBase(const QString &title, const QString &description, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_description(description)
{
    m_layout = new QVBoxLayout(this);
    m_label = new QLabel(description, this);
    m_label->setWordWrap(true);
    m_layout->addWidget(m_label);
    m_layout->addStretch();
}

QString LoadSpecPageBase::title() const
{
    return m_title;
}

QString LoadSpecPageBase::description() const
{
    return m_description;
}

QVBoxLayout *LoadSpecPageBase::layout() const
{
    return m_layout;
}

QTableWidget *LoadSpecPageBase::createTable(const QStringList &headers)
{
    auto *table = new QTableWidget(this);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    return table;
}
