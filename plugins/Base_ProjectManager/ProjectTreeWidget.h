#ifndef PROJECTTREEWIDGET_H
#define PROJECTTREEWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <qtreewidget.h>
#include "ProjectNode.h"
#include "qfile.h"
#include <QDomDocument>
#include <QMenu>
#include <QContextMenuEvent>
#include "ConcretePageA.h"
#include <QTextStream>
#include <qdebug.h>
#include "QStack"
#include <services/PreProcessingService.h>
#include <ServiceRegistry.h>

/**
* @brief 项目模型树类
* @author tongle
* @date 2025.07.31
*/
namespace Ui {
class ProjectTreeWidget;
}

class ProjectTreeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectTreeWidget(QWidget *parent = nullptr);
    ~ProjectTreeWidget();
	QTreeWidget * getTreeWidget() const { return m_treeWidget; };
	void setStackedWidget(QStackedWidget* stackWidget) { m_stackWidget = stackWidget; };
	/**
	* @brief 给树添加节点
	* @param parentItem 父节点指针
	* @param node 项目节点类
	* @param path 保存路径
	*/
	void addNodeToTree(QTreeWidgetItem* parentItem, ProjectNode* node, const QString& path);
	/**
	* @brief 根据页面类型动态创建basepage的子类指针界面
	* @param pageType 页面类型
	* @note 页面工厂函数
	*/
	BasePage* createPage(const QString& pageType);
	/**
	* @brief 保存当前树结构
	* @param file xml的名称
	*/
	void saveTreeStructure(QFile& file);
	/**
	* @brief 加载当前树结构
	* @param file xml的名称
	*/
	void loadTreeStructure(QFile& file);
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
	/**
	* @brief 删除stackedwidget里面的所有内容
	* @param stackedWidget stackedwidget指针
	*/
	void clearStackedWidget(QStackedWidget* stackedWidget);
	/**
	* @brief 获取项目xml文件的路径
	*/
	QString getProjectTreeXmlPath()const {return m_projectTreeXmlPath;};
	/**
	* @brief 获取根节点下面的所有节点(主要用于打开项目使用)
	*/
	QMap<QString, ProjectNode*> getProjectRootNodeMap() { return nodeMap; };
	/**
	* @brief 设置项目xml文件的路径
	* @param xmlPath xml路径
	*/
	void setProjectTreeXmlPath(const QString& xmlPath){ m_projectTreeXmlPath = xmlPath; };
	/**
	* @brief 通过名称找寻QTreeWidgetItem
	* @param tree 树指针
	* @param name 名称
	*/
	QTreeWidgetItem* findItemByNameIterative(QTreeWidget* tree, const QString& name);
	/**
	* @brief 改变节点名称
	* @param item 节点
	* @param name 名称
	*/
	void changeNodeName(QTreeWidgetItem* item, const QString & name);


    void addTopLevelItem(QTreeWidgetItem* node);
signals:
	void deletePageSig(const QString &uuid);
public:
	ProjectNode* m_rootNode; //项目根节点指针
	QTreeWidget* m_treeWidget; // 项目树指针
	QStackedWidget* m_stackWidget; // 堆栈窗口窗口指针
private:
	Ui::ProjectTreeWidget *ui;
	QHBoxLayout * m_layout;
	QString m_projectTreeXmlPath = "./project.xml";// 项目树结构保存地址
	QMap<QString, ProjectNode*> nodeMap;// 节点的map
protected:
	/**
	* @brief 右键节点弹出的删除事件
	*/
	void contextMenuEvent(QContextMenuEvent* event) override;
private:
	/**
	* @brief 页面初始化
	*/
	void init();
	/**
	* @brief 更新节点路径
	*/
	void updateItemPath(QTreeWidgetItem* item, const QString& parentPath);
public slots:
	/**
	* @brief 双击节点修改名称的槽函数
	*/
	void onItemChanged(QTreeWidgetItem* item, int column);
	/**
	* @brief 树节点变化展示不同界面的槽函数
	*/
	void onCurItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
};

#endif // PROJECTTREEWIDGET_H
