#include "ProjectManagerWidget.h"

#include <ServiceRegistry.h>
#include <qheaderview.h>
#include <services/UiFrameService.h>

#include <QAction>
#include <QDebug>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include "ProjectManager.h"
ProjectManagerWidget::ProjectManagerWidget(ProjectManager *projectManager)
    : m_projectManager(projectManager) {
  m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
  qDebug() << "m_uiFrameService " << m_uiFrameService;
  setupMenuButton();
  setupTreeWidget();
  setupSettingWidget();
  setupConnections();
  tmpInsertWidget();
}

void ProjectManagerWidget::setupMenuButton() {
  //-----------添加到文件的
  QPushButton *button = new QPushButton(("文件"));

  QMenu *menu = new QMenu(button);

  // 创建 QActions，并添加到 QMenu
  m_newProjectAction = new QAction(("新建"), menu);
  m_openProjectAction = new QAction(("打开"), menu);
  m_saveProjectAction = new QAction("保存", menu);
  m_reSaveProjectAction = new QAction("另存为", menu);
  m_helpAction = new QAction(("帮助"), menu);
  menu->addAction(m_newProjectAction);
  menu->addAction(m_openProjectAction);
  menu->addAction(m_saveProjectAction);
  menu->addAction(m_reSaveProjectAction);
  menu->addAction(m_helpAction);

  button->setMenu(menu);
  button->setFixedWidth(65);
  m_uiFrameService->addFileButton(button);
  //-----------添加到文件的

  //    connect(m_newProjectAction, &QAction::triggered, m_projectManager,
  //    &ProjectManager::newProject); connect(m_openProjectAction,
  //    &QAction::triggered, m_projectManager, &ProjectManager::openProject);
  //    connect(m_saveProjectAction, &QAction::triggered, m_projectManager,
  //    &ProjectManager::saveProject);

  // 添加到开始 新建 打开 保存
  QToolButton *newProjectButton = new QToolButton;
  newProjectButton->setText(("新建"));
  newProjectButton->setIcon(QIcon(":/resources/menu/new"));
  m_uiFrameService->addMenuButton(("开始"), "project", newProjectButton);
  connect(newProjectButton, &QToolButton::clicked, m_projectManager,
          &ProjectManager::newProject);

  QToolButton *openProjectButton = new QToolButton;
  openProjectButton->setText(("打开"));
  openProjectButton->setIcon(QIcon(":/resources/menu/open"));
  m_uiFrameService->addMenuButton(("开始"), "project", openProjectButton);
  connect(openProjectButton, &QToolButton::clicked, m_projectManager,
          &ProjectManager::openProject);

  QToolButton *storeProjectButton = new QToolButton;
  storeProjectButton->setText(("保存"));
  storeProjectButton->setIcon(QIcon(":/resources/menu/save"));
  m_uiFrameService->addMenuButton(("开始"), "project", storeProjectButton);
  connect(storeProjectButton, &QToolButton::clicked, m_projectManager,
          &ProjectManager::saveProject);
}

void ProjectManagerWidget::setupTreeWidget() {
  // 树形列表窗口
  QWidget *treeWidget = new QWidget;
  m_treeWidget = new QTreeWidget();
  m_treeWidget->setIndentation(20);

  QHBoxLayout *mylayout = new QHBoxLayout(treeWidget);
  mylayout->addWidget(m_treeWidget);
  mylayout->setContentsMargins(3, 3, 3, 3);

  m_treeWidget->setColumnCount(1);
  m_treeWidget->header()->setVisible(false);
  m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  m_treeWidget->setExpandsOnDoubleClick(true);
  m_treeWidget->setEditTriggers(QAbstractItemView::EditKeyPressed);
  m_treeWidget->setHeaderLabel("Setting");

  QAction *toggleAction =
      m_uiFrameService->addWidget(treeWidget, ("模型开发器"));

  QToolButton *toolButton =
      new QToolButton(this);  // 'this' should be the parent widget
  toolButton->setText(("模型开发器"));
  toolButton->setIcon(QIcon(":/resources/menu/model.png"));
  m_uiFrameService->addMenuButton(("视图"), "1", toolButton);
  connect(toolButton, &QToolButton::clicked, toggleAction, &QAction::trigger);

  QList<QString> tmp;
}

void ProjectManagerWidget::generateItem(QList<QString> text,
                                        QTreeWidgetItem *parentItem) {
  for (QString name : text) {
    QTreeWidgetItem *childItem = new QTreeWidgetItem(parentItem);
    childItem->setText(0, name);
  }
}

void ProjectManagerWidget::setRootItem(QString text) {
  m_rootItem = new QTreeWidgetItem();
  m_rootItem->setText(0, text);
  // 添加树节点到树形列表
  m_treeWidget->addTopLevelItem(m_rootItem);
  m_treeWidget->expandItem(m_rootItem);
}

void ProjectManagerWidget::setRootWidget(QWidget *widget) {
  QString uuid = QUuid::createUuid().toString();
  m_rootItem->setData(0, Qt::UserRole, uuid);
  setItemWidget(uuid, widget);
}

void ProjectManagerWidget::setupConnections() {
  connect(m_treeWidget, &QTreeWidget::itemClicked, this,
          &ProjectManagerWidget::onItemPressed);
  connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this,
          &ProjectManagerWidget::generateMenu);
  connect(m_newProjectAction, &QAction::triggered, m_projectManager,
          &ProjectManager::newProject);
  connect(m_openProjectAction, &QAction::triggered, m_projectManager,
          &ProjectManager::openProject);
  connect(m_saveProjectAction, &QAction::triggered, m_projectManager,
          &ProjectManager::saveProject);
  connect(m_reSaveProjectAction, &QAction::triggered, m_projectManager,
          &ProjectManager::reSaveProject);
}

void ProjectManagerWidget::generateMenu(const QPoint &pos) {
  QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
  if (item) {
    qDebug() << item->text(0);
  }
}

void ProjectManagerWidget::onItemPressed(QTreeWidgetItem *item, int column) {
  QString uuid;
  QVariant retrievedData = item->data(0, Qt::UserRole);
  if (retrievedData.isValid()) {
    uuid = retrievedData.toString();
  } else {
    return;
  }
  qDebug() << "press " << uuid;
  if (m_itemMap[uuid]) {
    if (m_tmpSettingWidget != nullptr) {
      m_tmpSettingWidget->setParent(nullptr);
      m_settingLayout->removeWidget(m_tmpSettingWidget);
    }
    m_tmpSettingWidget = m_itemMap[uuid];
    m_settingLayout->addWidget(m_tmpSettingWidget);
    m_tmpSettingWidget->update();
  }
}

void ProjectManagerWidget::setupSettingWidget() {
  m_settingWidget = new QWidget;

  m_settingLayout = new QVBoxLayout(m_settingWidget);
  m_tmpSettingWidget = new QWidget();
  m_settingLayout->addWidget(m_tmpSettingWidget);
  m_settingLayout->setSpacing(0);
  m_settingLayout->setContentsMargins(0, 0, 0, 0);
  QAction *toggleAction =
      m_uiFrameService->addWidget(m_settingWidget, ("设置"));

  QToolButton *toolButton =
      new QToolButton(this);  // 'this' should be the parent widget
  toolButton->setText(("设置"));
  toolButton->setIcon(QIcon(":/resources/menu/settings.png"));
  m_uiFrameService->addMenuButton(("视图"), "1", toolButton);
  connect(toolButton, &QToolButton::clicked, toggleAction, &QAction::trigger);
}

void ProjectManagerWidget::addTreeItem(QTreeWidgetItem *treeItem) {
  m_rootItem->addChild(treeItem);
}

void ProjectManagerWidget::tmpInsertWidget() {}

void ProjectManagerWidget::setItemWidget(QString uuid, QWidget *widget) {
  m_itemMap[uuid] = widget;
}

void ProjectManagerWidget::setCurrentItem(QTreeWidgetItem *item) {
  m_treeWidget->setCurrentItem(item);
  emit m_treeWidget->itemClicked(item, 0);
}

void ProjectManagerWidget::expandTreeWidget() { m_treeWidget->expandAll(); }

void ProjectManagerWidget::closeProject() {
  // 清空
  if (m_tmpSettingWidget != nullptr)
    m_settingLayout->removeWidget(m_tmpSettingWidget);

  for (QMap<QString, QWidget *>::const_iterator it = m_itemMap.constBegin();
       it != m_itemMap.constEnd(); ++it) {
    // 释放 QWidget 对象的内存
    delete it.value();
  }

  m_tmpSettingWidget = nullptr;
  // 清空 QMap
  m_itemMap.clear();
  if (m_rootItem) {
    delete m_rootItem;
    m_rootItem = nullptr;
  }
}
