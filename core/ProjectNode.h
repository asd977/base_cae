#ifndef PROJECTNODE_H
#define PROJECTNODE_H
#include <QTreeWidget>
#include <QStackedWidget>
#include <qobject.h>
#include "BasePage.h"
#include <qmetatype.h>

class CORE_EXPORT ProjectNode :public QTreeWidgetItem
{
public:
    ProjectNode(const QString& n, const QString& id = QUuid::createUuid().toString(), BasePage* p = nullptr);
    ~ProjectNode() {}
    /**
    * @brief 移除当前节点的子节点
    * @child 子节点指针
    */
    bool removeChild(ProjectNode* child) {return children.removeOne(child);}
    /**
    * @brief 获取所有的子节点
    */
    const QList<ProjectNode*>& getChildren() const { return children; }

    /**
    * @brief 获取节点名称
    */
    QString getName() const { return name; }
    /**
    * @brief 获取uuid
    */
    QString getUuid() const { return uuid; }
    /**
    * @brief 获取节点页面
    */
    BasePage* getPage() const { return page; }
    /**
    * @brief 获取节点页面
    */
    void setPage(BasePage *basePage) { page = basePage; }

    /**
    * @brief 设置节点名称
    */
    void setName(const QString& _name) { name = _name; };
private:
    QString name;
    BasePage* page;
    QList<ProjectNode*> children;
    QString uuid;
};

#endif // PROJECTNODE_H
