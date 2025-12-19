#include "ProjectTreeWidget.h"
#include "ui_ProjectTreeWidget.h"
#include <QIcon>
#include <QPalette>
#include <QVariant>
#include <QAction>
#include <QMenu>

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
	m_layout->setContentsMargins(6, 6, 6, 6);
	m_treeWidget = new QTreeWidget(this);
	m_treeWidget->setColumnCount(1);
	m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
	m_treeWidget->setExpandsOnDoubleClick(true);
	m_treeWidget->setEditTriggers(QAbstractItemView::DoubleClicked);
	m_treeWidget->setHeaderHidden(true);
	m_treeWidget->setIndentation(18);
	m_treeWidget->setAnimated(true);
	m_treeWidget->setAlternatingRowColors(true);
	m_treeWidget->setUniformRowHeights(true);

	m_placeholderLabel = new QLabel(("新建或打开工程以查看模型树"), this);
	m_placeholderLabel->setAlignment(Qt::AlignCenter);
	m_placeholderLabel->setObjectName("projectTreePlaceholder");

	m_stackedLayout = new QStackedLayout();
	m_stackedLayout->addWidget(m_treeWidget);
	m_stackedLayout->addWidget(m_placeholderLabel);
	m_layout->addLayout(m_stackedLayout);

	applyTreeTheme();
    connect(m_treeWidget, &QTreeWidget::currentItemChanged, this, &ProjectTreeWidget::onCurItemChanged);
	updatePlaceholderVisibility();
	//connect(m_treeWidget, &QTreeWidget::itemChanged, this, &ProjectTreeWidget::onItemChanged);
}

void ProjectTreeWidget::applyTreeTheme()
{
	if (!m_treeWidget)
	{
		return;
	}
	QString qss = QString(
		"QTreeWidget {"
		"    border: none;"
		"    font-family: \"Microsoft YaHei\", \"微软雅黑\";"
		"    background: transparent;"
		"}"
		"QTreeView{"
		"    font-size: 14px;"
		"    show-decoration-selected: 1;"
		"    selection-background-color: rgba(56, 140, 230, 45%);"
		"    selection-color: #1b2533;"
		"    color: #1b2533; "
		"    background-color: rgba(255,255,255,0.6);"
		"}"
		"QTreeView::item {"
		"    height: 26px;"
		"    padding: 4px 6px;"
		"    border: 1px solid transparent;"
		"}"
		"QTreeView::item:selected,"
		"QTreeView::branch:selected {"
		"    background: rgba(56, 140, 230, 18%);"
		"    color: #0b1f33;"
		"}"
		"QTreeView::item:hover,"
		"QTreeView::branch:hover {"
		"    background: rgba(79, 160, 255, 12%);"
		"    color: #0b1f33;"
		"}"
		"QTreeView::branch:closed:has-children {"
		"    image: url(:/resources/btn_up.png);"
		"}"
		"QTreeView::branch:open:has-children {"
		"    image: url(:/resources/btn_down.png);"
		"}");
	m_treeWidget->setStyleSheet(qss);
	if (m_placeholderLabel)
	{
		QPalette pal = m_placeholderLabel->palette();
		pal.setColor(QPalette::WindowText, QColor("#60708a"));
		m_placeholderLabel->setPalette(pal);
	}
}

void ProjectTreeWidget::updatePlaceholderVisibility()
{
	if (!m_stackedLayout)
	{
		return;
	}
	const bool hasContent = m_treeWidget && m_treeWidget->topLevelItemCount() > 0;
	m_stackedLayout->setCurrentWidget(hasContent ? static_cast<QWidget*>(m_treeWidget)
		: static_cast<QWidget*>(m_placeholderLabel));
}

void ProjectTreeWidget::buildDefaultProjectTree(const QString& projectName)
{
	m_treeWidget->clear();
	m_namedNodeMap.clear();
	nodeMap.clear();

	m_rootNode = new ProjectNode(projectName);
	m_rootNode->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	m_rootNode->setIcon(0, QIcon(":/resources/icon/reportTemplate.png"));
	m_rootNode->setData(0, Qt::UserRole, QVariant::fromValue(m_rootNode));
	m_treeWidget->addTopLevelItem(m_rootNode);
	m_rootNode->setExpanded(true);

	buildBaseNodes(projectName);
	m_treeWidget->setCurrentItem(m_rootNode);
	updatePlaceholderVisibility();
}

void ProjectTreeWidget::buildBaseNodes(const QString& projectName)
{
	Q_UNUSED(projectName);
	nodeMap.insert(m_rootNode->getName(), m_rootNode);
	const QString iconParameter = ":/resources/menu/Parameter.png";
	const QString iconMaterial = ":/resources/menu/Block.png";
	const QString iconGeometry = ":/resources/icon/geomImport.png";
	const QString iconMesh = ":/resources/icon/mesh.png";
	const QString iconPhysics = ":/resources/icon/bound.png";
	const QString iconBoundary = ":/resources/menu/boundary_face.png";
	const QString iconSolver = ":/resources/icon/report.png";
	const QString iconResult = ":/resources/menu/Nephogram.png";
	const QString iconSurrogate = ":/resources/menu/DataSampling.png";

	ProjectNode* param = createCategory(m_rootNode, ("参数定义"), iconParameter, "parameters");
	ProjectNode* material = createCategory(m_rootNode, ("材料库"), iconMaterial, "materials");
	ProjectNode* geom = createCategory(m_rootNode, ("几何建模"), iconGeometry, "geometry");
	ProjectNode* mesh = createCategory(m_rootNode, ("网格划分"), iconMesh, "mesh");
	ProjectNode* physics = createCategory(m_rootNode, ("物理场"), iconPhysics, "physics");
	ProjectNode* boundary = createCategory(m_rootNode, ("边界条件"), iconBoundary, "boundary");
	ProjectNode* solver = createCategory(m_rootNode, ("求解控制"), iconSolver, "solver");
	ProjectNode* result = createCategory(m_rootNode, ("结果分析"), iconResult, "results");
	ProjectNode* surrogate = createCategory(m_rootNode, ("代理模型"), iconSurrogate, "surrogate");

	createCategory(result, ("云图"), ":/resources/menu/Nephogram.png", "result_cloud");
	createCategory(result, ("矢量图"), ":/resources/menu/Vector.png", "result_vector");
	createCategory(result, ("切片/截面"), ":/resources/menu/Slice.png", "result_section");
	createCategory(result, ("曲线/报告"), ":/resources/menu/Curve.png", "result_curve");

	QList<ProjectNode*> defaults{ param, material, geom, mesh, physics, boundary, solver, result, surrogate };
	for (ProjectNode* node : defaults)
	{
		if (!node)
		{
			continue;
		}
		node->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		node->setData(0, Qt::UserRole, QVariant::fromValue(node));
		m_treeWidget->expandItem(node);
	}
}

ProjectNode* ProjectTreeWidget::createCategory(ProjectNode* parent, const QString& title, const QString& iconPath, const QString& key)
{
	if (!parent)
	{
		return nullptr;
	}
	ProjectNode* node = new ProjectNode(title);
	node->setIcon(0, QIcon(iconPath));
	node->setData(0, Qt::UserRole, QVariant::fromValue(node));
	parent->addChild(node);
	if (!key.isEmpty())
	{
		m_namedNodeMap.insert(key, node);
	}
	nodeMap.insert(title, node);
	return node;
}

ProjectNode* ProjectTreeWidget::addLeaf(ProjectNode* parent, const QString& name, const QString& iconPath, BasePage* page, const QString& key)
{
	if (!parent)
	{
		return nullptr;
	}
	auto* leaf = new ProjectNode(name);
	leaf->setIcon(0, QIcon(iconPath));
	leaf->setFlags(leaf->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	leaf->setPage(page);
	leaf->setData(0, Qt::UserRole, QVariant::fromValue(leaf));
	parent->addChild(leaf);
	if (!key.isEmpty())
	{
		m_namedNodeMap.insert(key, leaf);
	}
	nodeMap.insert(name, leaf);
	m_treeWidget->expandItem(parent);
	updatePlaceholderVisibility();
	return leaf;
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
	updatePlaceholderVisibility();
}

void ProjectTreeWidget::contextMenuEvent(QContextMenuEvent* event)
{
	if (!m_treeWidget)
	{
		return;
	}
	QTreeWidgetItem* item = m_treeWidget->itemAt(m_treeWidget->viewport()->mapFromGlobal(event->globalPos()));
	if (!item)
	{
		return;
	}

	auto* node = dynamic_cast<ProjectNode*>(item);
	QMenu menu(this);
	QAction* renameAction = menu.addAction(tr("重命名"));
	QAction* deleteAction = menu.addAction(tr("删除节点"));

	const bool isEditable = item->flags().testFlag(Qt::ItemIsEditable);
	renameAction->setEnabled(isEditable);
	const bool isLeaf = node && node->getChildren().isEmpty();
	deleteAction->setEnabled(isLeaf);

	connect(renameAction, &QAction::triggered, this, [this, item]() {
		m_treeWidget->setCurrentItem(item);
		m_treeWidget->editItem(item, 0);
	});
	connect(deleteAction, &QAction::triggered, this, [this, item, isLeaf]() {
		if (isLeaf)
		{
			deletePage(item);
		}
	});
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
