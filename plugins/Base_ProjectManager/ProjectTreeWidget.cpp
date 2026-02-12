#include "ProjectTreeWidget.h"
#include "ui_ProjectTreeWidget.h"
#include <QIcon>
#include <QPalette>
#include <QVariant>
#include <QAction>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <ContextMenuHub.h>
#include <QHeaderView.h>

ProjectTreeWidget::ProjectTreeWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ProjectTreeWidget)
{
    ui->setupUi(this);
	init();
}

ProjectTreeWidget::~ProjectTreeWidget()
{
    delete ui;
}

void ProjectTreeWidget::init()
{
	m_layout = new QHBoxLayout(this);
	m_treeWidget = new QTreeWidget(this);


    m_layout->addWidget(m_treeWidget);
    m_layout->setContentsMargins(0, 0, 0, 0);

	applyTreeTheme();
    connect(m_treeWidget, &QTreeWidget::currentItemChanged, this, &ProjectTreeWidget::onCurItemChanged);
}

void ProjectTreeWidget::applyTreeTheme()
{
    if (!m_treeWidget)
    {
        return;
    }

    // 将树形样式和菜单样式组合在一起
    // %1 为行高 (20px)，%2 为字体大小 (12px)
    m_treeWidget->setIndentation(20);
    m_treeWidget->setStyleSheet(QString(
                                    "QTreeWidget {"
                                    "    border: none;"
                                    "    font-family: Microsoft YaHei, 微软雅黑;"
                                    "}"
                                    "QTreeView {"
                                    "    font-size: %2px;"
                                    "    show-decoration-selected: 1;"
                                    "    selection-background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,"
                                    "        stop:0 rgba(255, 255, 255, 255), stop:0.5 rgba(235, 247, 253, 255), stop:1 rgba(213, 237, 250, 255));"
                                    "    color: black;"
                                    "    background-color: rgb(255, 255, 255);"
                                    "}"
                                    "QTreeView::item {"
                                    "    height: %1px;"
                                    "    border: 1px solid transparent;"
                                    "    color: black;"
                                    "    background-color: rgb(255, 255, 255);"
                                    "}"
                                    "QTreeView::item:selected, QTreeView::branch:selected {"
                                    "    background-color: rgb(192, 226, 252) !important;"
                                    "    color: black;"
                                    "}"
                                    "QTreeView::item:hover, QTreeView::branch:hover {"
                                    "    background-color: rgb(226, 242, 254) !important;"
                                    "    color: black;"
                                    "}"
                                    "QTreeView::branch {"
                                    "    color: black;"
                                    "}"
                                    "QTreeView::branch:closed:has-children {"
                                    "    image: url(:/resources/btn_up.png);"
                                    "}"
                                    "QTreeView::branch:open:has-children {"
                                    "    image: url(:/resources/btn_down.png);"
                                    "}"
                                    ).arg(30).arg(16));
    m_treeWidget->setColumnCount(1);
    m_treeWidget->header()->setVisible(false);
//    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setExpandsOnDoubleClick(true);
    m_treeWidget->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_treeWidget->setHeaderLabel("Setting");
}
void ProjectTreeWidget::buildDefaultProjectTree(const QString& projectName)
{
        m_treeWidget->clear();
        nodeMap.clear();

        m_rootNode = new ProjectNode(projectName);
        m_rootNode->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_rootNode->setData(0, Qt::UserRole, QVariant::fromValue(m_rootNode));
        m_treeWidget->addTopLevelItem(m_rootNode);
        m_rootNode->setExpanded(true);
        m_treeWidget->setCurrentItem(m_rootNode);
}

void ProjectTreeWidget::onCurItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    if (!current) return;

    // 建议使用 dynamic_cast 确保安全，或者确认 ProjectNode 继承自 QTreeWidgetItem
    auto *node = dynamic_cast<ProjectNode*>(current);
    if (!node) return;

    BasePage* page = node->getPage();
    if (!page) return;

    // 1. 处理旧页面的卸载
    if (previous) {
        auto *prevNode = dynamic_cast<ProjectNode*>(previous);
        if (prevNode) {
            if (BasePage *prevPage = prevNode->getPage()) {
                prevPage->beforeUnload();
            }
        }
    }

    // 2. 获取并配置新页面
    QWidget* widget = page->getWidget();

    // 关键修正：确保新页面具有拉伸属性
    if (widget->sizePolicy().verticalPolicy() != QSizePolicy::Expanding) {
        widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    if (m_stackWidget->indexOf(widget) == -1) {
        m_stackWidget->addWidget(widget);
    }

    page->beforeLoad();
    m_stackWidget->setCurrentWidget(widget);

    // 3. 强制更新 UI 几何结构
    widget->updateGeometry();
}


void ProjectTreeWidget::deletePage(QTreeWidgetItem* item)
{
	if (!item || !m_treeWidget)
	{
		return;
	}
	auto* node = dynamic_cast<ProjectNode*>(item);
	if (!node || !node->getChildren().isEmpty())
	{
		return;
	}

	if (BasePage* page = node->getPage())
	{
		const QString uuid = page->getUuid();
		QWidget* widget = page->getWidget();
		m_stackWidget->removeWidget(widget);
		emit deletePageSig(uuid);
	}

	if (QTreeWidgetItem* parentItem = item->parent())
	{
		if (auto* parentNode = dynamic_cast<ProjectNode*>(parentItem))
		{
			parentNode->removeChild(node);
		}
	}

	delete node;
}

void ProjectTreeWidget::contextMenuEvent(QContextMenuEvent* event)
{
    auto* node = dynamic_cast<ProjectNode*>(m_treeWidget->itemAt(event->pos()));
    if (!node) return;

    m_treeWidget->setCurrentItem(node);

    QVariantMap ctx;
    ctx["uuid"]   = node->data(0, ProjectNode::RoleUuid).toString();
    ctx["module"] = node->data(0, ProjectNode::RoleModule).toString();
    ctx["kind"]   = node->data(0, ProjectNode::RoleKind).toString();
    ctx["name"]   = node->text(0);

    QMenu menu(this);

    // 你想通用的也可以直接绑本类函数
    ContextMenuHub::addAction(&menu, this, "重命名", [this, uuid=ctx["uuid"].toString()]{
        // 直接调用你自己的函数，不走 event
        // this->renameNode(uuid);
    });

    ContextMenuHub::addAction(&menu, this, "删除", [this, uuid=ctx["uuid"].toString()]{
        // this->deleteNode(uuid);
    });

    ContextMenuHub::addSeparator(&menu);
    static const QString kScope_ProjectTreeNode = "project_tree.node";
    // 模块扩展：同步往菜单里塞 action
    ContextMenuHub::instance().buildMenu(kScope_ProjectTreeNode, &menu, ctx);

    if (!menu.isEmpty())
        menu.exec(event->globalPos());
}

void ProjectTreeWidget::deleteProjectNode(ProjectNode* node)
{
	if (!m_rootNode) return;
	// 页面析构的时候调用此函数
	for (ProjectNode* child : node->getChildren())
	{
		deleteProjectNode(child);
	}
	if (node->getPage())
	{
		delete node->getPage();
	}
	delete node;
}

void ProjectTreeWidget::addTopLevelItem(QTreeWidgetItem *node)
{
    m_treeWidget->addTopLevelItem(node);
}

void ProjectTreeWidget::blockSignal(bool value)
{
    if (!m_treeWidget) return;

    if (value)
    {

        m_treeWidget->blockSignals(true);
    }
    else
    {
        m_treeWidget->blockSignals(false);


    }
}
