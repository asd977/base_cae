#include "Solver.h"

#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"

#include <EventManager.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QMessageBox>
#include <QRegularExpression>
#include <QStringList>

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
    const QString analysisName = QString("渗流分析(%1)").arg(nextSeepageIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), seepagePage);

    connect(seepagePage, &SeepageWidget::computeRequested, this, [this, analysisNode]() {
        onComputeSeepage(analysisNode);
    });

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


void Solver::onComputeSeepage(ProjectNode* analysisNode)
{
    if (!analysisNode) return;

    bool ok = true;
    QStringList failedFiles;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* boundaryPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage());
        if (!boundaryPage) continue;

        const bool oneOk = boundaryPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk) failedFiles << child->getName();
    }

    if (ok)
    {
        QMessageBox::information(nullptr, "计算完成", QString("%1 的边界数据已生成到 data 目录。")
                                                      .arg(analysisNode->getName()));
    }
    else
    {
        QMessageBox::warning(nullptr, "计算失败",
                             QString("以下边界数据生成失败：%1").arg(failedFiles.join("、")));
    }
}

int Solver::nextSeepageIndex() const
{
    if (!m_root) return 1;

    int maxIndex = 0;
    const QRegularExpression re(R"(^渗流分析\((\d+)\)$)");
    for (ProjectNode* node : m_root->getChildren())
    {
        const QRegularExpressionMatch m = re.match(node->getName());
        if (!m.hasMatch()) continue;
        maxIndex = qMax(maxIndex, m.captured(1).toInt());
    }

    return maxIndex + 1;
}
