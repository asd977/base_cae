#include "ProjectTreeWidget.h"
#include "ui_ProjectTreeWidget.h"

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
	m_treeWidget->setColumnCount(1);
	m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
	m_treeWidget->setExpandsOnDoubleClick(true);
	m_treeWidget->setEditTriggers(QAbstractItemView::DoubleClicked);
	m_treeWidget->setHeaderHidden(true);
	m_treeWidget->setIndentation(20);
	m_layout->addWidget(m_treeWidget);
	m_layout->setContentsMargins(3, 3, 3, 3);
    connect(m_treeWidget, &QTreeWidget::currentItemChanged, this, &ProjectTreeWidget::onCurItemChanged);
	//connect(m_treeWidget, &QTreeWidget::itemChanged, this, &ProjectTreeWidget::onItemChanged);
//	m_treeWidget->setStyleSheet(QString(
//		"QTreeWidget {border:none;font-family: Microsoft YaHei, 微软雅黑;}"
//		"QTreeView{"
//		"font-size:%2px;"
//		"show-decoration-selected:1;"
//		"selection-background-color:qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,stop:0 rgba(255, 255, 255, 255), stop:0.5 rgba(235, 247, 253, 255), stop:1 rgba(213, 237, 250, 255));"
//		"color:black; "
//		"background-color: rgb(255, 255, 255);	"
//		"}"
//		"  QTreeView::item {"
//		"      height:%1px;"
//		"       border: 1px;"
//		"       color:black;"
//		"      border: transparent;"
//		"  }"
//		"QTreeView::item:selected,"
//		"QTreeView::branch:selected {"
//		"    background: rgb(192,226,252);color: black;"
//		"}"
//		"QTreeView::item:hover,"
//		"QTreeView::branch:hover {"
//		"    background: rgb(226,242,254);color: black;"
//		"}"

//		"QTreeView::branch {"
//		"    color: black; /* 箭头颜色设为黑色 */"
//		"}"
//		"QTreeView::branch:closed:has-children {"
//		" image: url(:/resources/btn_up.png);"
//		"}"

//		"QTreeView::branch:open:has-children {"
//		"image: url(:/resources/btn_down.png);"
//		" }"
//	).arg(30).arg(16));
}

void ProjectTreeWidget::addNodeToTree(QTreeWidgetItem* parentItem, ProjectNode* node, const QString& path)
{
//	QString currentPath = path.isEmpty() ? node->getName() : path + "/" + node->getName();
//	QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
//	item->setText(0, node->getName());
//	item->setData(0, Qt::UserRole, QVariant::fromValue(node));
//	item->setData(0, Qt::UserRole + 1, currentPath);
//	if (node->isLeaf())
//	{
//		item->setFlags(item->flags() | Qt::ItemIsEditable);
//	}
//	for (ProjectNode* child : node->getChildren())
//	{
//		addNodeToTree(item, child, currentPath);
//	}
}

void ProjectTreeWidget::onCurItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    if (current)
    {
        ProjectNode* node = (ProjectNode*)current;
        if (node) {
            BasePage* page = node->getPage();
            if (page) {
                qDebug()<<m_stackWidget->currentIndex();

                if(m_stackWidget->currentIndex() != -1)
                {
                    QWidget *widget = m_stackWidget->currentWidget();
                    BasePage *tmp = (BasePage*)widget;
                    tmp->beforeUnload();
                }

                QWidget* widget = page->getWidget();
                if (m_stackWidget->indexOf(widget) == -1)
                {
                    m_stackWidget->addWidget(widget);
                }
                page->beforeLoad();
                m_stackWidget->setCurrentWidget(widget);
            }
        }
    }
}

void ProjectTreeWidget::onItemChanged(QTreeWidgetItem* item, int column)
{
//	if (item && column == 0)
//	{
//		ProjectNode* node = item->data(0, Qt::UserRole).value<ProjectNode*>();
//		if (node)
//		{
//			QString newName = item->text(0);
//			node->setName(newName);
//			// 更新路径
//			updateItemPath(item, "");
//			// 保存到 XML
//			QFile file(m_projectTreeXmlPath);
//			saveTreeStructure(file);
//		}
//	}
}

void ProjectTreeWidget::updateItemPath(QTreeWidgetItem* item, const QString& parentPath)
{
//	ProjectNode* node = item->data(0, Qt::UserRole).value<ProjectNode*>();
//	if (!node) return;
//	QString currentPath = parentPath.isEmpty() ? node->getName() : parentPath + "/" + node->getName();
//	item->setData(0, Qt::UserRole + 1, currentPath);
//	for (int i = 0; i < item->childCount(); ++i)
//	{
//		updateItemPath(item->child(i), currentPath);
//	}
}

void ProjectTreeWidget::saveTreeStructure(QFile& file)
{
//	QDomDocument doc;
//	QDomProcessingInstruction xmlDeclaration = doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\"");
//	doc.appendChild(xmlDeclaration);
//	QDomElement root = doc.createElement("project");
//	doc.appendChild(root);

//	std::function<void(QDomDocument&, QDomElement&, ProjectNode*)> saveNode = [&](QDomDocument& doc, QDomElement& parent, ProjectNode* node)
//	{
//		if (!node)
//		{
//			return;
//		}
//		QDomElement nodeElem = doc.createElement(node->isLeaf() ? "leaf" : "category");
//		nodeElem.setAttribute("uuid", node->getUuid());
//		nodeElem.setAttribute("name", node->getName());
//		if (node->isLeaf())
//		{
//			nodeElem.setAttribute("pageUuid", node->getPage()->getUuid());
//			nodeElem.setAttribute("pageType", node->getPage()->getPageType());
//			QDomElement state = doc.createElement("state");
//			QVariantMap stateMap = node->getPage()->saveState();
//			for (auto it = stateMap.constBegin(); it != stateMap.constEnd(); ++it)
//			{
//				QDomElement stateItem = doc.createElement(it.key());
//				stateItem.appendChild(doc.createTextNode(it.value().toString()));
//				state.appendChild(stateItem);
//				//qDebug() << "Saving state:" << it.key() << "=" << it.value().toString() << "Raw:" << it.value().toString().toUtf8();
//			}
//			nodeElem.appendChild(state);
//		}
//		for (ProjectNode* child : node->getChildren()) {
//			saveNode(doc, nodeElem, child);
//		}
//		parent.appendChild(nodeElem);
//	};

//	saveNode(doc, root, m_rootNode);

//	if (file.open(QIODevice::WriteOnly | QIODevice::Text))
//	{
//		QTextStream stream(&file);
//		stream << doc.toString();
//		file.close();
//		qDebug() << "Saved XML to:" << file.fileName();
//	}
//	else {
//		qDebug() << "Failed to open file for writing:" << file.fileName();
//	}
}

void ProjectTreeWidget::loadTreeStructure(QFile& file)
{

}

void ProjectTreeWidget::deletePage(QTreeWidgetItem* item)
{
//	if (!item) return;
//	ProjectNode* node = item->data(0, Qt::UserRole).value<ProjectNode*>();
//	if (node && node->isLeaf())
//	{
//		BasePage* page = node->getPage();
//		if (page)
//		{
//			QString uuid = page->getUuid();
//			QWidget* widget = page->getWidget();
//			m_stackWidget->removeWidget(widget);
//			emit deletePageSig(uuid);
//		}
//		// 从父节点移除
//		QTreeWidgetItem* parentItem = item->parent();
//		if (parentItem) {
//			ProjectNode* parentNode = parentItem->data(0, Qt::UserRole).value<ProjectNode*>();
//			if (parentNode) {
//				parentNode->removeChild(node);
//			}
//		}
//		// 删除树节点
//		delete item;
//		// 删除 ProjectNode 和页面
//		delete node;
//		// 保存更新后的树形结构
//		QFile file(m_projectTreeXmlPath);
//		saveTreeStructure(file);
//	}
}

void ProjectTreeWidget::contextMenuEvent(QContextMenuEvent* event)
{
//	QTreeWidgetItem* item = m_treeWidget->itemAt(m_treeWidget->viewport()->mapFromGlobal(event->globalPos()));
//	if (item)
//	{
//		ProjectNode* node = item->data(0, Qt::UserRole).value<ProjectNode*>();
//		if (node && node->isLeaf()) {
//			QMenu menu(this);
//			QAction* deleteAction = menu.addAction("删除节点");
//			connect(deleteAction, &QAction::triggered, this, [=]() {
//				deletePage(item);
//			});
//			menu.exec(event->globalPos());
//		}
//	}
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

void ProjectTreeWidget::clearStackedWidget(QStackedWidget* stackedWidget) 
{
	while (stackedWidget->count() > 0)
	{
		QWidget* widget = stackedWidget->widget(0);
		stackedWidget->removeWidget(widget);
		widget->deleteLater();
	}
}

BasePage* ProjectTreeWidget::createPage(const QString& pageType)
{
	if (pageType == "ConcretePageA")
	{
		return new ConcretePageA();
	}
	else {
		return new ConcretePageA();
	}
}

QTreeWidgetItem* ProjectTreeWidget::findItemByNameIterative(QTreeWidget* tree, const QString& name)
{
	QStack<QTreeWidgetItem*> stack;
	for (int i = 0; i < tree->topLevelItemCount(); ++i) 
	{
		stack.push(tree->topLevelItem(i));
	}

	while (!stack.isEmpty()) 
	{
		QTreeWidgetItem* item = stack.pop();

		if (item->text(0) == name) 
		{
			return item;
		}
		for (int i = 0; i < item->childCount(); ++i)
		{
			stack.push(item->child(i));
		}
	}
    return nullptr;
}

void ProjectTreeWidget::addTopLevelItem(QTreeWidgetItem *node)
{
    m_treeWidget->addTopLevelItem(node);
}
