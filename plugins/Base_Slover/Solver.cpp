#include "Solver.h"

#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"

#include <EventManager.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

Solver::Solver()
{
    ServiceRegistry::instance().registerService<SolverService>(this);
    initAction();
}

void Solver::newProject()
{
    if (m_root) return;

    m_root = new ProjectNode("求解器", QUuid::createUuid().toString(), nullptr);
    ProjectManagerService* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
}

bool Solver::saveProject()
{
    if (!m_root) return true;

    for (ProjectNode* analysisNode : m_root->getChildren())
    {
        for (ProjectNode* child : analysisNode->getChildren())
        {
            auto* boundaryPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage());
            if (boundaryPage)
            {
                boundaryPage->exportDatFile();
            }
        }
    }

    return true;
}

bool Solver::loadProject()
{
    return true;
}

void Solver::closeProject()
{
    m_root = nullptr;
}

void Solver::initAction()
{
    auto& em = EventManager::instance();

    em.registerHandler(PublicConstants::kActionSeepageKey, [this](const QVariantMap&)
                       {
                           onSeepageAction();
                       });
}

void Solver::onSeepageAction()
{
    if (!m_root) return;

    auto* seepagePage = new SeepageWidget();
    auto* analysisNode = new ProjectNode("渗流分析", QUuid::createUuid().toString(), seepagePage);

    auto addBoundary = [&](const QString& name, const QString& pageType, const QString& fileName)
    {
        auto* page = new SeepageBoundaryPage(pageType, name, fileName);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        analysisNode->addChild(node);
    };

    addBoundary("上游迎水面", "SeepageBoundaryUpstreamWater", "JD_UPSEEP.DAT");
    addBoundary("下游迎水面", "SeepageBoundaryDownstreamWater", "JD_DWSEEP.DAT");
    addBoundary("上游给定流量面", "SeepageBoundaryUpstreamFlux", "FA_UPSEEP.DAT");
    addBoundary("下游给定流量面", "SeepageBoundaryDownstreamFlux", "FA_DWSEEP.DAT");

    m_root->addChild(analysisNode);
}
