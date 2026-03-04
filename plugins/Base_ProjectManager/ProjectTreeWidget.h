#ifndef PROJECTTREEWIDGET_H
#define PROJECTTREEWIDGET_H

#include <ServiceRegistry.h>
#include <qdebug.h>
#include <qtreewidget.h>
#include <services/PreProcessingService.h>
#include <services/VisualWidgetService.h>

#include <QContextMenuEvent>
#include <QDomDocument>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStackedLayout>
#include <QTextStream>
#include <QWidget>

#include "ProjectNode.h"
#include "QStack"
#include "qfile.h"

/**
 * @brief 项目模型树类
 * @author tongle
 * @date 2025.07.31
 */
namespace Ui {
class ProjectTreeWidget;
}

class ProjectTreeWidget : public QWidget {
  Q_OBJECT

 public:
  explicit ProjectTreeWidget(QWidget* parent = nullptr);
  ~ProjectTreeWidget();
  QTreeWidget* getTreeWidget() const { return m_treeWidget; };
  void setStackedWidget(QStackedWidget* stackWidget) {
    m_stackWidget = stackWidget;
  };
  /**
   * @brief 构建默认的工程模型树骨架
   * @param projectName 工程名称
   */
  void buildDefaultProjectTree(const QString& projectName);

  /**
   * @brief 删除当前节点和节点对应的页面
   * @param item 当前节点的指针
   */
  void deletePage(QTreeWidgetItem* item);
  /**
   * @brief 删除项目节点指针
   * @param ProjectNode 项目节点指针
   * @note 递归删除 删除子节点父节点以及自己
   */
  void deleteProjectNode(ProjectNode* node);

  void addTopLevelItem(QTreeWidgetItem* node);

  void blockSignal(bool value);
 signals:
  void deletePageSig(const QString& uuid);

 public:
  ProjectNode* m_rootNode;        // 项目根节点指针
  QTreeWidget* m_treeWidget;      // 项目树指针
  QStackedWidget* m_stackWidget;  // 堆栈窗口窗口指针
 private:
  Ui::ProjectTreeWidget* ui;
  QHBoxLayout* m_layout;
  QString m_projectTreeXmlPath = "./project.xml";  // 项目树结构保存地址
  QMap<QString, ProjectNode*> nodeMap;             // 节点的map
 protected:
  /**
   * @brief 右键节点弹出的删除事件
   */
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void ensureSceneVisibleForCurrentNode(QTreeWidgetItem* current);
  QString resolveNodeModule(QTreeWidgetItem* node) const;

 private:
  /**
   * @brief 页面初始化
   */
  void init();
  /**
   * @brief 初始化树样式
   */
  void applyTreeTheme();
 public slots:
  /**
   * @brief 树节点变化展示不同界面的槽函数
   */
  void onCurItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
};

#endif  // PROJECTTREEWIDGET_H
