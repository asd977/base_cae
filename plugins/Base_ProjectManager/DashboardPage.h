#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include "BasePage.h"
#include "ProjectNode.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QScrollArea>
#include <QPushButton>
#include <QAction>
#include <QList>

class DashboardPage : public BasePage
{
    Q_OBJECT
public:
    explicit DashboardPage(ProjectNode* node, QWidget* parent = nullptr);
    ~DashboardPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onChildDoubleClicked(QListWidgetItem* item);

private:
    void refreshDashboard();

    ProjectNode* m_node;
    
    QVBoxLayout* m_mainLayout;
    QLabel* m_titleLabel;
    QScrollArea* m_scrollArea;
    QWidget* m_scrollContainer;
    QVBoxLayout* m_scrollLayout;

    QGroupBox* m_actionsGroup;
    QGridLayout* m_actionsLayout;

    QGroupBox* m_childrenGroup;
    QVBoxLayout* m_childrenLayout;
    QListWidget* m_childrenList;
    QLabel* m_placeholderLabel;

    QList<QAction*> m_ownedActions;
};

#endif // DASHBOARDPAGE_H
