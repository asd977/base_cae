#include "DisplayManager.h"

#include <EventManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QUuid>

#include "AcousticBoundaryWidget.h"
#include "AcousticInputWidget.h"
#include "AcousticOutputWidget.h"
#include "DisplayRootWidget.h"
#include "FatigueInputWidget.h"
#include "FatigueOutputWidget.h"
#include "OptimizationInputWidget.h"
#include "OptimizationOutputWidget.h"
#include "OtherSettingsWidget.h"
#include "SolidBoundaryWidget.h"
#include "SolidInputWidget.h"
#include "SolidOutputWidget.h"
#include "SolverFrequencyWidget.h"
#include "SolverTimeWidget.h"

DisplayManager::DisplayManager()
{
    auto& em = EventManager::instance();
    em.registerHandler(PublicConstants::kActionNewProject, this,
                       [this](const QVariantMap&)
                       {
                           clearTree();
                           initializeTree();
                       });
    em.registerHandler(PublicConstants::kActionOpenProject, this,
                       [this](const QVariantMap&)
                       {
                           clearTree();
                           initializeTree();
                       });
    em.registerHandler(PublicConstants::kEventLoadComplete, this,
                       [this](const QVariantMap&)
                       {
                           initializeTree();
                       });
}

void DisplayManager::initializeTree()
{
    if (m_root)
        return;

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!pm)
        return;

    m_root = new ProjectNode(QString::fromUtf8("Base_Display"), QUuid::createUuid().toString(),
                             new DisplayRootWidget());
    setupNodeRole(m_root, "DisplayRoot");
    pm->addTreeItem(m_root);

    auto addNode = [this](ProjectNode* parent, const QString& title, const QString& kind, BasePage* page)
    {
        auto* node = new ProjectNode(title, QUuid::createUuid().toString(), page);
        setupNodeRole(node, kind);
        parent->addChild(node);
        return node;
    };

    auto* acoustic = addNode(m_root, QString::fromUtf8("1 声学场"), "Acoustic", new AcousticInputWidget());
    addNode(acoustic, QString::fromUtf8("1.1 输入"), "AcousticInput", new AcousticInputWidget());
    addNode(acoustic, QString::fromUtf8("1.1 边界条件"), "AcousticBoundary", new AcousticBoundaryWidget());
    addNode(acoustic, QString::fromUtf8("1.2 输出"), "AcousticOutput", new AcousticOutputWidget());

    auto* solid = addNode(m_root, QString::fromUtf8("2 固体力学场"), "Solid", new SolidInputWidget());
    addNode(solid, QString::fromUtf8("2.1 输入"), "SolidInput", new SolidInputWidget());
    addNode(solid, QString::fromUtf8("2.2 边界条件"), "SolidBoundary", new SolidBoundaryWidget());
    addNode(solid, QString::fromUtf8("2.3 输出"), "SolidOutput", new SolidOutputWidget());

    auto* solver = addNode(m_root, QString::fromUtf8("3 求解器定义"), "Solver", new SolverFrequencyWidget());
    addNode(solver, QString::fromUtf8("3.1 频域"), "FrequencyDomain", new SolverFrequencyWidget());
    addNode(solver, QString::fromUtf8("3.2 时域"), "TimeDomain", new SolverTimeWidget());

    auto* optimization =
        addNode(m_root, QString::fromUtf8("4 优化模块"), "Optimization", new OptimizationInputWidget());
    addNode(optimization, QString::fromUtf8("4.1 输入"), "OptimizationInput", new OptimizationInputWidget());
    addNode(optimization, QString::fromUtf8("4.2 输出"), "OptimizationOutput",
            new OptimizationOutputWidget());

    auto* fatigue = addNode(m_root, QString::fromUtf8("5 疲劳模块"), "Fatigue", new FatigueInputWidget());
    addNode(fatigue, QString::fromUtf8("5.1 输入"), "FatigueInput", new FatigueInputWidget());
    addNode(fatigue, QString::fromUtf8("5.2 输出"), "FatigueOutput", new FatigueOutputWidget());

    addNode(m_root, QString::fromUtf8("其它"), "Others", new OtherSettingsWidget());
}

void DisplayManager::clearTree()
{
    if (!m_root)
        return;
    while (m_root->childCount() > 0)
        delete m_root->takeChild(0);
    delete m_root;
    m_root = nullptr;
}

void DisplayManager::setupNodeRole(ProjectNode* node, const QString& kind) const
{
    if (!node)
        return;
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "Display");
    node->setData(0, ProjectNode::RoleKind, kind);
}
