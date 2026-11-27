#include "ProjectTreeWidget.h"
#include "DashboardPage.h"
#include <SettingsLayout.h>

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <QHeaderView.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QRegularExpression>
#include <QVariant>

#include "ui_ProjectTreeWidget.h"

namespace
{
QString normalizedPageTitle(QWidget* widget, QTreeWidgetItem* item)
{
    QString title;
    if (widget)
    {
        if (auto* header = widget->findChild<QLabel*>(QStringLiteral("pageHeaderLabel")))
            title = header->text().trimmed();
    }

    if (title.isEmpty() && item)
        title = item->text(0).trimmed();

    // Tree items may carry an instance suffix, e.g. 位移约束(3). The dock
    // describes the task type, while the selected tree row keeps the instance.
    title.remove(QRegularExpression(QStringLiteral("\\(\\d+\\)$")));
    return title.trimmed();
}
}

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
    connect(m_treeWidget, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int)
            {
                auto* node = dynamic_cast<ProjectNode*>(item);
                BasePage* page = node ? node->getPage() : nullptr;
                QWidget* widget = page ? page->getWidget() : nullptr;
                if (!widget || !widget->property("projectTreeDockPanel").toBool())
                    return;
                if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
                    uiFrame->setWidgetVisible(widget, true);
            });
}

void ProjectTreeWidget::applyTreeTheme()
{
    if (!m_treeWidget)
    {
        return;
    }

    m_treeWidget->setIndentation(18);
    m_treeWidget->setObjectName("projectTreeWidget");
    m_treeWidget->setColumnCount(1);
    m_treeWidget->header()->setVisible(false);
    //    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setExpandsOnDoubleClick(true);
    m_treeWidget->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_treeWidget->setAnimated(false);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setIconSize(QSize(16, 16));
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setItemsExpandable(true);
    m_treeWidget->setFocusPolicy(Qt::NoFocus);
    m_treeWidget->setHeaderLabel("Setting");
}
void ProjectTreeWidget::buildDefaultProjectTree(const QString& projectName)
{
    m_activeSettingsPage = nullptr;
    m_treeWidget->clear();
    nodeMap.clear();

    m_rootNode = new ProjectNode(projectName);
    m_rootNode->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    QFont rootFont = m_rootNode->font(0);
    rootFont.setWeight(QFont::DemiBold);
    m_rootNode->setFont(0, rootFont);
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
    {
        page = new DashboardPage(node, this);
        node->setPage(page);
    }

    QWidget* widget = page->getWidget();
    if (widget && widget->property("projectTreeDockPanel").toBool())
    {
        page->applyDeferredLoadJson();
        page->beforeLoad();

        // Dock 型控制页是当前场景的辅助工具，不等同于切换到它所属的模块。
        // 例如在后处理云图中打开“网格显示控制”，应该只修改筛选条件，
        // 不能因为节点属于 Mesh 就把网格 Actor 强制显示出来覆盖云图。
        QWidget* activeWidget = m_activeSettingsPage ? m_activeSettingsPage->getWidget() : nullptr;
        const bool preserveActivePostScene =
            widget->property("preserveSceneOnActivation").toBool() &&
            activeWidget && activeWidget->property("postDisplayActive").toBool();
        if (!preserveActivePostScene)
            ensureSceneVisibleForCurrentNode(current);
        return;
    }

    // 1. 处理旧页面的卸载（同页重入不触发 unload）
    Q_UNUSED(previous);
    BasePage* prevPage = m_activeSettingsPage.data();
    if (prevPage && prevPage != page)
        prevPage->beforeUnload();

    // 2. 获取并配置新页面
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
        page->applyDeferredLoadJson();
        page->beforeLoad();
        CaeSettingsLayout::prepare(widget);
    }
    m_activeSettingsPage = page;
    m_stackWidget->setCurrentWidget(widget);

    const QString pageTitle = normalizedPageTitle(widget, current);
    if (!pageTitle.isEmpty())
        m_stackWidget->setWindowTitle(pageTitle);

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
    auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>();
    if(!visual||!current)return;
    const QString module=resolveNodeModule(current);
    auto* node=dynamic_cast<ProjectNode*>(current);
    if(node&&node->getPage()&&node->getPage()->getWidget()
       &&node->getPage()->getWidget()->property("mesh.ownsScene").toBool())
    {
        visual->setGroupVisible(QStringLiteral("post"),false,false);
        visual->render();
        return;
    }
    const QString pageType=node&&node->getPage()?node->getPage()->getPageType():QString();
    const bool section=pageType.startsWith(QStringLiteral("SlopeLimitEquilibrium"));
    const bool meshAvailable=visual->getProp("mesh.imported.surface")||visual->getProp("mesh.imported.line")
        ||visual->getProp("mesh.global.surf")||visual->getProp("mesh.global.wire");
    const bool post=module==QLatin1String("PostProcessing");
    const bool meshVisible=meshAvailable&&module!=QLatin1String("Geometry")&&!post&&!section;
    const bool geometryVisible=!post&&!section&&(module==QLatin1String("Geometry")||!meshAvailable);
    // Module visibility is a mask. Never overwrite each object's saved choice.
    visual->setGroupVisible(QStringLiteral("Geometry"),geometryVisible,false);
    visual->setGroupVisible(QStringLiteral("GeometryPreview"),geometryVisible,false);
    visual->setGroupVisible(QStringLiteral("网格"),meshVisible,false);
    visual->setGroupVisible(QStringLiteral("Mesh"),meshVisible,false);
    visual->setGroupVisible(QStringLiteral("post"),post,false);
    EventManager::instance().sendEvent(QStringLiteral("scene.mesh.visibility.changed"),
        {{QStringLiteral("visible"),meshVisible}});
    visual->render();
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
        if (m_activeSettingsPage == page)
            m_activeSettingsPage = nullptr;
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

    static const QString kScope_ProjectTreeNode = "project_tree.node";
    // 模块扩展：同步往菜单里塞 action
    ContextMenuHub::instance().buildMenu(kScope_ProjectTreeNode, &menu, ctx);

    // 工程根节点由 ProjectManager 自己管理，不交给各业务模块注册菜单。
    // 这里只提供“显示名称重命名”；工程目录和 .xml 清单文件路径保持不变。
    QAction* renameProjectAction = nullptr;
    if (node == m_rootNode)
    {
        if (!menu.isEmpty())
            menu.addSeparator();
        renameProjectAction = menu.addAction(QStringLiteral("重命名"));
    }

    if (menu.isEmpty())
        return;

    QAction* chosen = menu.exec(event->globalPos());
    // Closing a context menu also returns nullptr.  For non-root nodes the
    // project rename action is nullptr as well, so comparing the two pointers
    // directly used to treat a dismissed menu as "Rename".
    if (!renameProjectAction || chosen != renameProjectAction)
        return;

    bool accepted = false;
    QString newName = QInputDialog::getText(
        this, QStringLiteral("重命名工程"), QStringLiteral("工程名称"),
        QLineEdit::Normal, node->text(0), &accepted).trimmed();
    if (!accepted)
        return;
    if (newName.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("重命名工程"),
                             QStringLiteral("工程名称不能为空。"));
        return;
    }
    if (newName == node->text(0).trimmed())
        return;

    emit projectRenameRequested(newName);
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
        BasePage* page = node->getPage();
        if (m_activeSettingsPage == page)
            m_activeSettingsPage = nullptr;
        QWidget* pageWidget = page->getWidget();
        if (pageWidget && pageWidget->property("projectTreeDockPanel").toBool())
            node->setPage(nullptr);
        else
            delete page;
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
