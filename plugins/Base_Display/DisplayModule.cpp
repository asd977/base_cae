#include "DisplayModule.h"

#include <EventManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QTimer>
#include <QUuid>

#include "SolidMechanicsDisplayPage.h"

DisplayModule::DisplayModule()
{
    registerProjectHooks();
    ensureDisplayNode();
}

void DisplayModule::registerProjectHooks()
{
    auto& em = EventManager::instance();

    em.registerHandler(PublicConstants::kActionNewProject, this,
                       [this](const QVariantMap&)
                       {
                           QTimer::singleShot(0, this, [this]() { ensureDisplayNode(); });
                       });

    em.registerHandler(PublicConstants::kActionOpenProject, this,
                       [this](const QVariantMap&)
                       {
                           QTimer::singleShot(0, this, [this]() { ensureDisplayNode(); });
                       });

    em.registerHandler(PublicConstants::kEventLoadComplete, this,
                       [this](const QVariantMap&)
                       {
                           QTimer::singleShot(0, this, [this]() { ensureDisplayNode(); });
                       });
}

void DisplayModule::ensureDisplayNode()
{
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!pm)
    {
        return;
    }

    if (!m_rootNodeUuid.isEmpty() && pm->findNodeByUuid(m_rootNodeUuid))
    {
        return;
    }

    ProjectNode* node = createDisplayNode();
    m_rootNodeUuid = node->getUuid();
    pm->addTreeItem(node);
}

ProjectNode* DisplayModule::createDisplayNode() const
{
    auto* page = new SolidMechanicsDisplayPage;
    const QString uuid = QUuid::createUuid().toString();
    auto* node = new ProjectNode("固体力学场", uuid, page);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Display");
    node->setData(0, ProjectNode::RoleKind, "SolidMechanicsDisplayRoot");
    return node;
}
