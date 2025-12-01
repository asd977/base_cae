#include "RibbonButtonGroup.h"
#include "ui_RibbonButtonGroup.h"

#include <QToolButton>

RibbonButtonGroup::RibbonButtonGroup(QWidget *parent)
  : QWidget(parent)
  , ui(new Ui::RibbonButtonGroup)
  , m_title("")
{
  ui->setupUi(this);
  QString styleSheetText = QString(
      "QToolButton {"
      "    border: none;"
      "    padding: 5px;"
      "}"
      "QToolButton:hover {"
      "    background-color: #D3D3D3;"
      "}"
  );
  setStyleSheet(styleSheetText);
}

RibbonButtonGroup::~RibbonButtonGroup()
{
  delete ui;
}

void RibbonButtonGroup::setTitle(const QString &title)
{
  m_title = title;
//  ui->label->setText(m_title);
}

QString RibbonButtonGroup::title() const
{
  return m_title;
}

int RibbonButtonGroup::buttonCount() const
{
  return ui->horizontalLayout->count();
}

void RibbonButtonGroup::addButton(QToolButton *button)
{
  button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  button->setMinimumSize(48, 48);
  button->setAutoRaise(true);
  button->setIconSize(QSize(32,32));
  button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

  ui->horizontalLayout->addWidget(button);
}

void RibbonButtonGroup::removeButton(QToolButton *button)
{
  /// \todo What happens if button is not part of the layout?
  ui->horizontalLayout->removeWidget(button);
}
