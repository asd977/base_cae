#ifndef PROJECTMANAGERWIDGET_H
#define PROJECTMANAGERWIDGET_H

#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>

#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "ProjectInfoWidget.h"

class ProjectManager;
class ProjectManagerWidget : public QWidget {
  Q_OBJECT
 public:
  ProjectManagerWidget(ProjectManager *projectManager);

  void addTreeItem(QTreeWidgetItem *treeItem);
  void setItemWidget(QString uuid, QWidget *widget);
  void setCurrentItem(QTreeWidgetItem *item);
  void expandTreeWidget();
  void setRootItem(QString text);
  void setRootWidget(QWidget *widget);
  void closeProject();
 private slots:
  void onItemPressed(QTreeWidgetItem *item, int column);
  void generateMenu(const QPoint &pos);

 private:
  //    void setupUi();
  void setupTreeWidget();
  void setupSettingWidget();
  void setupConnections();
  void generateItem(QList<QString> text, QTreeWidgetItem *parentItem);

 private:
  QTreeWidget *m_treeWidget;
  QTreeWidgetItem *m_rootItem;
  UiFrameService *m_uiFrameService;
  QWidget *m_settingWidget;
  QMap<QString, QWidget *> m_itemMap;
  QVBoxLayout *m_settingLayout;
  QWidget *m_tmpSettingWidget;
  ProjectManager *m_projectManager;
  QAction *m_newProjectAction;
  QAction *m_openProjectAction;
  QAction *m_saveProjectAction;
  QAction *m_reSaveProjectAction;
  QAction *m_helpAction;

  //    QToolButton *m_newProjectButton;
  //    QToolButton *m_openProjectButton;
  //    QToolButton *m_saveProjectButton;
  //    QToolButton *m_reSaveFileButton;
  //    QToolButton *m_helpButton;
  void tmpInsertWidget();
  void setupMenuButton();
};

#endif  // PROJECTMANAGERWIDGET_H
