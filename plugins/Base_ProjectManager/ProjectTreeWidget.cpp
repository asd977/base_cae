#include "ProjectTreeWidget.h"

#include <ContextMenuHub.h>
#include <QHeaderView.h>
#include <services/VisualWidgetService.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QIcon>
#include <QMenu>
#include <QPalette>
#include <QVariant>

#include "ui_ProjectTreeWidget.h"

ProjectTreeWidget::ProjectTreeWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::ProjectTreeWidget)
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
    connect(m_treeWidget, &QTreeWidget::currentItemChanged, this,
            &ProjectTreeWidget::onCurItemChanged);
}

void ProjectTreeWidget::applyTreeTheme()
{
    if (!m_treeWidget)
    {
        return;
    }

    m_treeWidget->setIndentation(20);
    m_treeWidget->setObjectName("projectTreeWidget");
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
    if (!current)
        return;

    // 建议使用 dynamic_cast 确保安全，或者确认 ProjectNode 继承自 QTreeWidgetItem
    auto* node = dynamic_cast<ProjectNode*>(current);
    if (!node)
        return;

    BasePage* page = node->getPage();
    if (!page)
        return;

    // 1. 处理旧页面的卸载（同页重入不触发 unload）
    BasePage* prevPage = nullptr;
    if (previous)
    {
        auto* prevNode = dynamic_cast<ProjectNode*>(previous);
        if (prevNode)
        {
            prevPage = prevNode->getPage();
            if (prevPage && prevPage != page)
            {
                prevPage->beforeUnload();
            }
        }
    }

    // 2. 获取并配置新页面
    QWidget* widget = page->getWidget();

    // 关键修正：确保新页面具有拉伸属性
    if (widget->sizePolicy().verticalPolicy() != QSizePolicy::Expanding)
    {
        widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    if (m_stackWidget->indexOf(widget) == -1)
    {
        m_stackWidget->addWidget(widget);
    }

    const bool switchedToDifferentPage = (prevPage != page);
    if (switchedToDifferentPage)
    {
        page->beforeLoad();
    }
    m_stackWidget->setCurrentWidget(widget);

    ensureSceneVisibleForCurrentNode(current);

    // 3. 强制更新 UI 几何结构
    widget->updateGeometry();
}

QString ProjectTreeWidget::resolveNodeModule(QTreeWidgetItem* node) const
{
    QTreeWidgetItem* cur = node;
    while (cur)
    {
        const QString module = cur->data(0, ProjectNode::RoleModule).toString();
        if (!module.isEmpty())
        {
            return module;
        }
        cur = cur->parent();
    }
    return QString();
}

void ProjectTreeWidget::ensureSceneVisibleForCurrentNode(QTreeWidgetItem* current)
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || !current)
    {
        return;
    }

    const QString module = resolveNodeModule(current);
    if (module == "Geometry")
    {
        // 切回几何节点时，自动恢复几何显示，避免网格预览后“看不到几何”。
        vis->setGroupVisible("Geometry", true, false);

        // 可选聚焦：切几何时弱化网格层，避免遮挡。
        vis->setVisible("mesh.global.surf", false, false);
        vis->setVisible("mesh.global.wire", false, false);
        vis->setVisible("mesh.imported.surface", false, false);
        vis->setVisible("mesh.imported.line", false, false);
        vis->render();
    }
    else if (module == "Mesh")
    {
        const bool hasMeshActors = vis->getProp("mesh.global.surf") != nullptr ||
                                   vis->getProp("mesh.global.wire") != nullptr ||
                                   vis->getProp("mesh.imported.surface") != nullptr ||
                                   vis->getProp("mesh.imported.line") != nullptr;

        if (hasMeshActors)
        {
            // 切到网格节点且存在网格结果时：隐藏几何，仅显示网格结果。
            vis->setGroupVisible("Geometry", false, false);
            vis->setVisible("mesh.global.surf", true, false);
            vis->setVisible("mesh.global.wire", true, false);
            vis->setVisible("mesh.imported.surface", true, false);
            vis->setVisible("mesh.imported.line", true, false);
        }
        else
        {
            // 还没有任何网格结果时，保留几何显示，避免“切到网格页面模型消失”。
            vis->setGroupVisible("Geometry", true, false);
        }
        vis->render();
    }
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
    if (!node)
        return;

    m_treeWidget->setCurrentItem(node);

    QVariantMap ctx;
    ctx["uuid"] = node->data(0, ProjectNode::RoleUuid).toString();
    ctx["module"] = node->data(0, ProjectNode::RoleModule).toString();
    ctx["kind"] = node->data(0, ProjectNode::RoleKind).toString();
    ctx["name"] = node->text(0);

    QMenu menu(this);

    // 你想通用的也可以直接绑本类函数
    ContextMenuHub::addAction(&menu, this, "重命名",
                              [this, uuid = ctx["uuid"].toString()]
                              {
                                  // 直接调用你自己的函数，不走 event
                                  // this->renameNode(uuid);
                              });

    ContextMenuHub::addAction(&menu, this, "删除",
                              [this, uuid = ctx["uuid"].toString()]
                              {
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
    if (!m_rootNode)
        return;
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

void ProjectTreeWidget::addTopLevelItem(QTreeWidgetItem* node)
{
    m_treeWidget->addTopLevelItem(node);
}

void ProjectTreeWidget::blockSignal(bool value)
{
    if (!m_treeWidget)
        return;

    if (value)
    {
        m_treeWidget->blockSignals(true);
    }
    else
    {
        m_treeWidget->blockSignals(false);
    }
}
