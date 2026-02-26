#include "Solver.h"

#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <ServiceRegistry.h>
#include <iostream>
#include <fstream>
#include <PublicConstants.h>
#include <EventManager.h>
using namespace std;

Solver::Solver()
{
    ServiceRegistry::instance().registerService<SolverService>(this);
    initAction();

}


void Solver::newProject()
{
    QWidget *tmp = new QWidget;
    m_root = new ProjectNode("求解器",QUuid::createUuid().toString(),(BasePage*)tmp);
    ProjectManagerService *projectManagerService= ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
}

bool Solver::saveProject()
{
    return true;
}

bool Solver::loadProject()
{
    return true;
}

void Solver::closeProject()
{

}

void Solver::initAction()
{
    auto &em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId, [this, fn](const QVariantMap &)
                           {
                               fn();
                           });
    };

    // --- IO ---
    reg(PublicConstants::kActionPreprocessImportGrid, [this]{ onSeepageAction(); });

}

void Solver::onSeepageAction()
{

}
