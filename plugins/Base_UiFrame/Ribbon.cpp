#include "Ribbon.h"

#include "RibbonTabContent.h"

#include <QApplication>
#include <QStyleOption>
#include <QPainter>
#include <QPushButton>

Ribbon::Ribbon(QWidget *parent)
  : QTabWidget(parent)
{
//  setAutoFillBackground(true);

  // Set stylesheet
  QString styleSheetText = QString(


    "QTabBar::tab {"
             " font-size: 17px;"
    "  padding: 0px;"
              "width:70px;"
              "height:30px;"

    "  margin: 0px;"
    "}"
    ""
    "QTabBar::tab::!selected {"
    "  background-color:rgb(234,237,238);"
    "}"
    ""
    "QTabBar::tab:selected {"
    "  background-color: rgb(255, 255, 255);"
    "}"
    ""
    "QTabBar::tab:hover"
    "{"
    "  background-color: rgb(205, 232, 255);"
    "}"
    ""
    "QTabBar::tab:selected:hover {"

    "}"
    );

  setStyleSheet(styleSheetText);


}

void Ribbon::addFileButton(QPushButton *button)
{
//    this->setTabEnabled(0, false);
//    this->tabBar()->setTabButton(0, QTabBar::RightSide, button);
}

void Ribbon::addTab(const QString &tabName)
{
  // Note: superclass QTabWidget also has a function addTab()
  RibbonTabContent *ribbonTabContent = new RibbonTabContent;
  QTabWidget::addTab(ribbonTabContent, tabName);
}

void Ribbon::addTab(const QIcon &tabIcon, const QString &tabName)
{
  // Note: superclass QTabWidget also has a function addTab()
  RibbonTabContent *ribbonTabContent = new RibbonTabContent;
  QTabWidget::addTab(ribbonTabContent, tabIcon, tabName);
}

void Ribbon::removeTab(const QString &tabName)
{
  // Find ribbon tab
  for (int i = 0; i < count(); i++)
  {
    if (tabText(i).toLower() == tabName.toLower())
    {
      // Remove tab
      QWidget *tab = QTabWidget::widget(i);
      QTabWidget::removeTab(i);
      delete tab;
      break;
    }
  }
}

void Ribbon::addGroup(const QString &tabName, const QString &groupName)
{
  // Find ribbon tab
  QWidget *tab = nullptr;
  for (int i = 0; i < count(); i++)
  {
    if (tabText(i).toLower() == tabName.toLower())
    {
      tab = QTabWidget::widget(i);
      break;
    }
  }

  if (tab != nullptr)
  {
    // Tab found
    // Add ribbon group
    RibbonTabContent *ribbonTabContent = static_cast<RibbonTabContent*>(tab);
    ribbonTabContent->addGroup(groupName);
  }
  else
  {
    // Tab not found
    // Create tab
    addTab(tabName);

    // Add ribbon group
    addGroup(tabName, groupName);
  }
}

void Ribbon::addButton(const QString &tabName, const QString &groupName, QToolButton *button)
{
  // Find ribbon tab
  QWidget *tab = nullptr;
  for (int i = 0; i < count(); i++)
  {
    if (tabText(i).toLower() == tabName.toLower())
    {
      tab = QTabWidget::widget(i);
      break;
    }
  }

  if (tab != nullptr)
  {
    // Tab found
    // Add ribbon button
    RibbonTabContent *ribbonTabContent = static_cast<RibbonTabContent*>(tab);
    ribbonTabContent->addButton(groupName, button);
  }
  else
  {
    // Tab not found.
    // Create tab
    addTab(tabName);

    // Add ribbon button
    addButton(tabName, groupName, button);
  }
}

void Ribbon::removeButton(const QString &tabName, const QString &groupName, QToolButton *button)
{
  // Find ribbon tab
  QWidget *tab = nullptr;
  for (int i = 0; i < count(); i++)
  {
    if (tabText(i).toLower() == tabName.toLower())
    {
      tab = QTabWidget::widget(i);
      break;
    }
  }

  if (tab != nullptr)
  {
    // Tab found
    // Remove ribbon button
    RibbonTabContent *ribbonTabContent = static_cast<RibbonTabContent*>(tab);
    ribbonTabContent->removeButton(groupName, button);

    if (ribbonTabContent->groupCount() == 0)
    {
      removeTab(tabName);
    }
  }
}
