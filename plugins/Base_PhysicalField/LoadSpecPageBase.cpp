#include "LoadSpecPageBase.h"

#include <QLabel>
#include <QVBoxLayout>

LoadSpecPageBase::LoadSpecPageBase(const QString &title, const QString &description, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_description(description)
{
    auto *layout = new QVBoxLayout(this);
    m_label = new QLabel(description, this);
    m_label->setWordWrap(true);
    layout->addWidget(m_label);
    layout->addStretch();
}

QString LoadSpecPageBase::title() const
{
    return m_title;
}

QString LoadSpecPageBase::description() const
{
    return m_description;
}
