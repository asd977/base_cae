#include "DashboardPage.h"
#include <ContextMenuHub.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <QMenu>
#include <QTreeWidget>

DashboardPage::DashboardPage(ProjectNode* node, QWidget* parent)
    : BasePage("DashboardPage"), m_node(node)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("DashboardPage { background-color: white; }");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Standard title header matching the other pages
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("pageHeaderLabel");
    m_titleLabel->setText(m_node->text(0) + " - 管理面板");
    m_mainLayout->addWidget(m_titleLabel);

    // Scroll area for content
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("QScrollArea { background-color: white; border: none; }");
    m_mainLayout->addWidget(m_scrollArea);

    m_scrollContainer = new QWidget(m_scrollArea);
    m_scrollContainer->setStyleSheet("background-color: white;");
    m_scrollArea->setWidget(m_scrollContainer);

    m_scrollLayout = new QVBoxLayout(m_scrollContainer);
    m_scrollLayout->setContentsMargins(9, 9, 9, 9);
    m_scrollLayout->setSpacing(9);

    // Group 1: Quick Actions (Standard QGroupBox)
    m_actionsGroup = new QGroupBox("快速操作", m_scrollContainer);
    m_actionsLayout = new QGridLayout(m_actionsGroup);
    m_actionsLayout->setContentsMargins(9, 9, 9, 9);
    m_actionsLayout->setSpacing(6);
    m_scrollLayout->addWidget(m_actionsGroup);

    // Group 2: Child Items (Standard QGroupBox)
    m_childrenGroup = new QGroupBox("子项列表", m_scrollContainer);
    m_childrenLayout = new QVBoxLayout(m_childrenGroup);
    m_childrenLayout->setContentsMargins(9, 9, 9, 9);
    m_childrenLayout->setSpacing(6);

    m_placeholderLabel = new QLabel("暂无子节点，请使用上方按钮或右键菜单创建。", m_childrenGroup);
    m_childrenLayout->addWidget(m_placeholderLabel);

    m_childrenList = new QListWidget(m_childrenGroup);
    m_childrenLayout->addWidget(m_childrenList);
    m_scrollLayout->addWidget(m_childrenGroup);

    m_scrollLayout->addStretch();

    connect(m_childrenList, &QListWidget::itemDoubleClicked, this, &DashboardPage::onChildDoubleClicked);
}

DashboardPage::~DashboardPage()
{
    for (auto* act : m_ownedActions) {
        delete act;
    }
}

void DashboardPage::beforeLoad()
{
    refreshDashboard();
}

void DashboardPage::beforeUnload()
{
}

void DashboardPage::onChildDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    auto* childNode = item->data(Qt::UserRole).value<ProjectNode*>();
    if (childNode && m_node->treeWidget()) {
        m_node->treeWidget()->setCurrentItem(childNode);
    }
}

void DashboardPage::refreshDashboard()
{
    // 1. Clear old actions
    for (auto* act : m_ownedActions) {
        delete act;
    }
    m_ownedActions.clear();

    QLayoutItem* item;
    while ((item = m_actionsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // Update title
    m_titleLabel->setText(m_node->text(0) + " - 管理面板");

    // Check if it's the absolute root project node
    bool isProjectRoot = (m_node->parent() == nullptr);

    int row = 0;
    int col = 0;
    const int maxCols = 2;

    if (isProjectRoot) {
        // Root project node gets manual project actions
        struct ActionInfo {
            QString text;
            QString event;
            QString iconPath;
        };
        QList<ActionInfo> manualActions = {
            {"新建工程", PublicConstants::kActionNewProject, ":/resources/menu/minimal/app-new.svg"},
            {"打开工程", PublicConstants::kActionOpenProject, ":/resources/menu/minimal/app-open.svg"},
            {"保存工程", PublicConstants::kActionSaveProject, ":/resources/menu/minimal/app-save.svg"},
            {"退出系统", PublicConstants::kEventQuitApp, ":/resources/menu/minimal/app-exit.svg"}
        };

        for (const auto& info : manualActions) {
            QPushButton* btn = new QPushButton(info.text, m_scrollContainer);
            btn->setIcon(QIcon(info.iconPath));
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [info]() {
                EventManager::instance().sendEvent(info.event);
            });
            m_actionsLayout->addWidget(btn, row, col);
            col++;
            if (col >= maxCols) {
                col = 0;
                row++;
            }
        }
        m_actionsGroup->show();
    } else {
        // Query actions from ContextMenuHub
        QMenu* dummyMenu = new QMenu(this);
        QVariantMap ctx;
        ctx["uuid"] = m_node->data(0, ProjectNode::RoleUuid).toString();
        ctx["module"] = m_node->data(0, ProjectNode::RoleModule).toString();
        ctx["kind"] = m_node->data(0, ProjectNode::RoleKind).toString();
        ctx["name"] = m_node->text(0);

        ContextMenuHub::instance().buildMenu("project_tree.node", dummyMenu, ctx);

        for (auto* act : dummyMenu->actions()) {
            if (act->isSeparator() || act->text().isEmpty()) continue;

            act->setParent(this);
            m_ownedActions.append(act);

            QPushButton* btn = new QPushButton(act->text(), m_scrollContainer);
            if (!act->icon().isNull()) {
                btn->setIcon(act->icon());
            }
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, act, &QAction::trigger);
            m_actionsLayout->addWidget(btn, row, col);

            col++;
            if (col >= maxCols) {
                col = 0;
                row++;
            }
        }
        delete dummyMenu;

        if (m_ownedActions.isEmpty()) {
            m_actionsGroup->hide();
        } else {
            m_actionsGroup->show();
        }
    }

    // 2. Refresh Child List
    m_childrenList->clear();
    const int childCount = m_node->childCount();
    if (childCount == 0) {
        m_placeholderLabel->show();
        m_childrenList->hide();
    } else {
        m_placeholderLabel->hide();
        m_childrenList->show();
        for (int i = 0; i < childCount; ++i) {
            auto* childNode = dynamic_cast<ProjectNode*>(m_node->child(i));
            if (!childNode) continue;

            QListWidgetItem* listItem = new QListWidgetItem(m_childrenList);
            listItem->setText(childNode->text(0));
            
            QIcon icon = childNode->icon(0);
            if (icon.isNull()) {
                icon = QIcon(":/resources/icon/geom-unused.svg");
            }
            listItem->setIcon(icon);
            listItem->setData(Qt::UserRole, QVariant::fromValue(childNode));
            m_childrenList->addItem(listItem);
        }
    }
}
