#include "Solver.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhysicalFieldService.h>
#include <services/ProjectManagerService.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QProcess>
#include <algorithm>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkStaticCellLocator.h>
#include <vtkUnstructuredGrid.h>

#include "FillStorageCyclePage.h"
#include "CaveExcavationBodyPage.h"
#include "CaveBlockPlanePage.h"
#include "CaveBlockStabilityWidget.h"
#include "CaveExcavationWidget.h"
#include "CaveSupportComputationPage.h"
#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"
#include "StressBoundaryPage.h"
#include "DamAnalysisWidget.h"
#include "GeoStressRegressionWidget.h"
#include "GeoStressMeasurePointPage.h"
#include "GeoStressOuterForcePage.h"
#include "GeoStressSideFacePage.h"
#include "SlopeStabilityWidget.h"
#include "SolverSettingWidget.h"

Solver::Solver()
{
    ServiceRegistry::instance().registerService<SolverService>(this);
    initAction();
    initTreeMenu();
}

void Solver::newProject()
{
    if (m_root)
        return;
    SolverSettingWidget *tmp = new SolverSettingWidget;

    m_root = new ProjectNode("计算分析", QUuid::createUuid().toString(), tmp);
    setupNodeRole(m_root, "SolverRoot");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
}

bool Solver::saveProject()
{
    if (!m_root)
        return true;

    QJsonObject root;
    root["module"] = "Solver";
    root["root_title"] = "计算分析";

    QJsonArray analysisItems;
    for (ProjectNode* analysisNode : m_root->getChildren())
    {
        if (!analysisNode)
            continue;

        QJsonObject analysisObj;
        analysisObj["uuid"] = analysisNode->getUuid();
        analysisObj["title"] = analysisNode->text(0);

        if (auto* analysisPage = analysisNode->getPage())
        {
            analysisObj["type"] = analysisPage->getPageType();
            analysisObj["state"] = analysisPage->saveJson();
        }

        QJsonArray boundaryItems;
        for (ProjectNode* child : analysisNode->getChildren())
        {
            if (!child || !child->getPage())
                continue;

            if (auto* boundaryPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage()))
            {
                boundaryPage->exportDatFile();
            }
            else if (auto* stressBoundaryPage = dynamic_cast<StressBoundaryPage*>(child->getPage()))
            {
                stressBoundaryPage->exportDatFile();
            }

            QJsonObject childObj;
            childObj["uuid"] = child->getUuid();
            childObj["title"] = child->text(0);
            childObj["type"] = child->getPage()->getPageType();
            childObj["state"] = child->getPage()->saveJson();
            QJsonArray childItems;
            for (ProjectNode* gc : child->getChildren())
            {
                if (!gc || !gc->getPage())
                    continue;
                QJsonObject gcObj;
                gcObj["uuid"] = gc->getUuid();
                gcObj["title"] = gc->text(0);
                gcObj["type"] = gc->getPage()->getPageType();
                gcObj["state"] = gc->getPage()->saveJson();
                childItems.append(gcObj);
            }
            childObj["children"] = childItems;
            boundaryItems.append(childObj);
        }
        analysisObj["children"] = boundaryItems;
        analysisItems.append(analysisObj);
    }
    root["items"] = analysisItems;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSolverProjectFileName));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    return true;
}

bool Solver::loadProject()
{
    newProject();
    if (!m_root)
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSolverProjectFileName));
    if (!file.open(QIODevice::ReadOnly))
        return true;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& item : root.value("items").toArray())
    {
        const QJsonObject analysisObj = item.toObject();
        const QString analysisType = analysisObj.value("type").toString();

        BasePage* analysisPage = nullptr;
        ProjectNode* analysisNode = nullptr;

        if (analysisType == "SeepageAnalysis")
        {
            auto* seepagePage = new SeepageWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("渗流分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), seepagePage);
            seepagePage->loadJson(analysisObj.value("state").toObject());
            connect(seepagePage, &SeepageWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSeepage(analysisNode);
                    });
            analysisPage = seepagePage;
        }
        else if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
        {
            auto* stressPage = new DamAnalysisWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("土石坝计算"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), stressPage);
            stressPage->loadJson(analysisObj.value("state").toObject());
            connect(stressPage, &DamAnalysisWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeStressStrain(analysisNode);
                    });
            analysisPage = stressPage;
        }
        else if (analysisType == "SlopeStabilityAnalysis")
        {
            auto* slopePage = new SlopeStabilityWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("边坡分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), slopePage);
            slopePage->loadJson(analysisObj.value("state").toObject());
            connect(slopePage, &SlopeStabilityWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSlopeStability(analysisNode);
                    });
            analysisPage = slopePage;
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            auto* cavePage = new CaveExcavationWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("洞室开挖"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), cavePage);
            cavePage->loadJson(analysisObj.value("state").toObject());
            connect(cavePage, &CaveExcavationWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeCaveExcavation(analysisNode);
                    });
            analysisPage = cavePage;
        }

        if (!analysisNode || !analysisPage)
            continue;

        bool hasFillStorageCycleNode = false;
        bool hasGeoStressRegressionNode = false;
        bool hasCaveSupportComputationNode = false;
        bool hasCaveBlockStabilityNode = false;
        for (const auto& childV : analysisObj.value("children").toArray())
        {
            const QJsonObject childObj = childV.toObject();
            const QString type = childObj.value("type").toString();

            BasePage* page = nullptr;
            if (type == "SeepageBoundaryUpstreamWater")
                page = new SeepageBoundaryPage(type, "上游水头边界", "JD_UPSEEP.DAT");
            else if (type == "SeepageBoundaryDownstreamWater")
                page = new SeepageBoundaryPage(type, "下游水头边界", "JD_DWSEEP.DAT");
            else if (type == "SeepageBoundaryNaturalSeepage")
                page = new SeepageBoundaryPage(type, "天然渗流水头边界", "JDFREE.DAT");
            else if (type == "StressBoundaryDamUpstream")
                page = new StressBoundaryPage(type, "上游水头边界", "FA_DAMUP.DAT");
            else if (type == "FillStorageCycle")
            {
                page = new FillStorageCyclePage();
                hasFillStorageCycleNode = true;
            }
            else if (type == "GeoStressRegressionAnalysis")
            {
                page = new GeoStressRegressionWidget();
                hasGeoStressRegressionNode = true;
            }
            else if (type == "GeoStressRegressionSideFace")
            {
                page = new GeoStressSideFacePage();
            }
            else if (type == "GeoStressRegressionMeasurePoint")
            {
                page = new GeoStressMeasurePointPage();
            }
            else if (type == "GeoStressRegressionOuterForce")
            {
                page = new GeoStressOuterForcePage();
            }
            else if (type == "CaveSupportComputation")
            {
                page = new CaveSupportComputationPage();
                hasCaveSupportComputationNode = true;
            }
            else if (type == "CaveExcavationBody")
            {
                page = new CaveExcavationBodyPage();
            }
            else if (type == "CaveBlockStabilityAnalysis")
            {
                page = new CaveBlockStabilityWidget();
                hasCaveBlockStabilityNode = true;
            }
            else if (type == "CaveBlockPlane")
            {
                page = new CaveBlockPlanePage();
            }

            if (!page)
                continue;

            auto* node = new ProjectNode(
                childObj.value("title").toString("边界"),
                childObj.value("uuid").toString(QUuid::createUuid().toString()), page);
            if (type == "FillStorageCycle")
                setupNodeRole(node, "FillStorageCycle");
            else if (type == "GeoStressRegressionAnalysis")
                setupNodeRole(node, "GeoStressRegression");
            else if (type == "GeoStressRegressionSideFace")
                setupNodeRole(node, "GeoStressRegressionSideFace");
            else if (type == "GeoStressRegressionMeasurePoint")
                setupNodeRole(node, "GeoStressRegressionMeasurePoint");
            else if (type == "GeoStressRegressionOuterForce")
                setupNodeRole(node, "GeoStressRegressionOuterForce");
            else if (type == "CaveSupportComputation")
                setupNodeRole(node, "CaveSupportComputation");
            else if (type == "CaveExcavationBody")
                setupNodeRole(node, "CaveExcavationBody");
            else if (type == "CaveBlockStabilityAnalysis")
                setupNodeRole(node, "CaveBlockStabilityAnalysis");
            else if (type == "CaveBlockPlane")
                setupNodeRole(node, "CaveBlockPlane");
            else if (type.startsWith("StressBoundary"))
                setupNodeRole(node, "StressBoundary");
            else if (type.startsWith("SeepageBoundary"))
                setupNodeRole(node, "SeepageBoundary");

            if (auto* geoPage = dynamic_cast<GeoStressRegressionWidget*>(page))
            {
                connect(geoPage, &GeoStressRegressionWidget::computeRequested, this,
                        [this, node]()
                        {
                            onComputeGeoStressRegression(node);
                        });
            }
            else if (auto* blockPage = dynamic_cast<CaveBlockStabilityWidget*>(page))
            {
                connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
                        [this, node]()
                        {
                            onComputeCaveBlockStability(node);
                        });
            }
            page->loadJson(childObj.value("state").toObject());
            analysisNode->addChild(node);

            if (type == "GeoStressRegressionAnalysis" || type == "CaveSupportComputation"
                || type == "CaveBlockStabilityAnalysis")
            {
                for (const auto& gcV : childObj.value("children").toArray())
                {
                    const QJsonObject gcObj = gcV.toObject();
                    const QString gcType = gcObj.value("type").toString();
                    BasePage* gcPage = nullptr;
                    if (gcType == "GeoStressRegressionSideFace")
                        gcPage = new GeoStressSideFacePage();
                    else if (gcType == "GeoStressRegressionMeasurePoint")
                        gcPage = new GeoStressMeasurePointPage();
                    else if (gcType == "GeoStressRegressionOuterForce")
                        gcPage = new GeoStressOuterForcePage();
                    else if (gcType == "CaveExcavationBody")
                        gcPage = new CaveExcavationBodyPage();
                    else if (gcType == "CaveBlockPlane")
                        gcPage = new CaveBlockPlanePage();
                    if (!gcPage)
                        continue;

                    auto* gcNode = new ProjectNode(
                        gcObj.value("title").toString("子项"),
                        gcObj.value("uuid").toString(QUuid::createUuid().toString()), gcPage);
                    if (gcType == "GeoStressRegressionSideFace")
                        setupNodeRole(gcNode, "GeoStressRegressionSideFace");
                    else if (gcType == "GeoStressRegressionMeasurePoint")
                        setupNodeRole(gcNode, "GeoStressRegressionMeasurePoint");
                    else if (gcType == "GeoStressRegressionOuterForce")
                        setupNodeRole(gcNode, "GeoStressRegressionOuterForce");
                    else if (gcType == "CaveExcavationBody")
                        setupNodeRole(gcNode, "CaveExcavationBody");
                    else if (gcType == "CaveBlockPlane")
                        setupNodeRole(gcNode, "CaveBlockPlane");

                    gcPage->loadJson(gcObj.value("state").toObject());
                    node->addChild(gcNode);
                }
            }
        }

        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis") && !hasFillStorageCycleNode)
        {
            addFillStorageCycleNode(analysisNode, false);
        }
        if ((analysisType == "CaveExcavationAnalysis")
            && !hasGeoStressRegressionNode)
        {
            addGeoStressRegressionNode(analysisNode, false);
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            for (ProjectNode* child : analysisNode->getChildren())
            {
                if (!child || !child->getPage()
                    || child->getPage()->getPageType() != "GeoStressRegressionAnalysis")
                    continue;
                if (countGeoStressChildren(child, "GeoStressRegressionSideFace") == 0)
                {
                    addGeoStressSideFaceNode(child, false);
                }
                if (countGeoStressChildren(child, "GeoStressRegressionMeasurePoint") == 0)
                    addGeoStressMeasurePointNode(child, false);
                if (countGeoStressChildren(child, "GeoStressRegressionOuterForce") == 0)
                    addGeoStressOuterForceNode(child, false);
            }
        }

        if (analysisType == "CaveExcavationAnalysis" && !hasCaveBlockStabilityNode)
        {
            addCaveBlockStabilityNode(analysisNode, false);
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            for (ProjectNode* child : analysisNode->getChildren())
            {
                if (!child || !child->getPage()
                    || child->getPage()->getPageType() != "CaveBlockStabilityAnalysis")
                    continue;
                if (countGeoStressChildren(child, "CaveBlockPlane") == 0)
                    addCaveBlockPlaneNode(child, false);
            }
        }

        if (analysisType == "CaveExcavationAnalysis" && !hasCaveSupportComputationNode)
        {
            addCaveSupportComputationNode(analysisNode, false);
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            for (ProjectNode* child : analysisNode->getChildren())
            {
                if (!child || !child->getPage()
                    || child->getPage()->getPageType() != "CaveSupportComputation")
                    continue;
                if (countGeoStressChildren(child, "CaveExcavationBody") == 0)
                    addCaveExcavationBodyNode(child, false);
            }
        }

        if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            setupNodeRole(analysisNode, "DamAnalysis");
        else if (analysisType == "SeepageAnalysis")
            setupNodeRole(analysisNode, "SeepageAnalysis");
        else if (analysisType == "SlopeStabilityAnalysis")
            setupNodeRole(analysisNode, "SlopeStabilityAnalysis");
        else if (analysisType == "CaveExcavationAnalysis")
            setupNodeRole(analysisNode, "CaveExcavationAnalysis");

        syncDamAnalysisMCase(analysisNode);

        m_root->addChild(analysisNode);
    }

    return true;
}

void Solver::closeProject()
{
    m_root = nullptr;
}

void Solver::initAction()
{
    auto& em = EventManager::instance();

    em.registerHandler(PublicConstants::kActionSeepageKey,
                       [this](const QVariantMap&)
                       {
                           onSeepageAction();
                       });
    em.registerHandler(PublicConstants::kActionStressStrainKey,
                       [this](const QVariantMap&)
                       {
                           onStressStrainAction();
                       });
    em.registerHandler(PublicConstants::kActionCaveExcavationKey,
                       [this](const QVariantMap&)
                       {
                           onCaveExcavationAction();
                       });
    em.registerHandler(PublicConstants::kActionSlopeStabilityKey,
                       [this](const QVariantMap&)
                       {
                           onSlopeStabilityAction();
                       });
}

void Solver::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node", this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();
            if (uuid.isEmpty())
                return;

            if (kind == "DamAnalysis")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增填筑-蓄水周期",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!analysisNode)
                            return;
                        addFillStorageCycleNode(analysisNode, true);
                        syncDamAnalysisMCase(analysisNode);
                    });
            }
            else if (kind == "CaveExcavationAnalysis")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增地应力回归计算",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!analysisNode)
                            return;
                        addGeoStressRegressionNode(analysisNode, true);
                    });
                ContextMenuHub::addAction(
                    menu, this, "新增开挖和加固计算",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!analysisNode)
                            return;
                        addCaveSupportComputationNode(analysisNode, true);
                    });
                ContextMenuHub::addAction(
                    menu, this, "新增洞室块体稳定性分析",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!analysisNode)
                            return;
                        addCaveBlockStabilityNode(analysisNode, true);
                    });
            }
            else if (kind == "CaveSupportComputation")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增开挖体",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!node)
                            return;
                        addCaveExcavationBodyNode(node, true);
                    });
            }
            else if (kind == "CaveBlockStabilityAnalysis")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增滑块面",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!node)
                            return;
                        addCaveBlockPlaneNode(node, true);
                    });
            }
            else if (kind == "GeoStressRegression")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增侧立面",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!node)
                            return;
                        addGeoStressSideFaceNode(node, true);
                    });
                ContextMenuHub::addAction(
                    menu, this, "新增实测地应力点",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!node)
                            return;
                        addGeoStressMeasurePointNode(node, true);
                    });
                ContextMenuHub::addAction(
                    menu, this, "新增外面力",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (!node)
                            return;
                        addGeoStressOuterForceNode(node, true);
                    });
            }

            if (kind != "SolverRoot")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "删除",
                    [this, uuid]()
                    {
                        removeNodeByUuid(uuid);
                    });
            }
        },
        0,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value("module").toString() == "Solver";
        });
}

void Solver::deleteNodeRecursively(ProjectNode* node) const
{
    if (!node)
        return;

    const auto children = node->getChildren();
    for (ProjectNode* child : children)
    {
        deleteNodeRecursively(child);
    }

    if (node->getPage())
        delete node->getPage();

    delete node;
}

void Solver::removeNodeByUuid(const QString& uuid)
{
    if (!m_root || uuid.isEmpty())
        return;

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node || node == m_root)
        return;

    if (node->data(0, ProjectNode::RoleModule).toString() != "Solver")
        return;

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (!parent)
        return;

    parent->removeChild(node);
    deleteNodeRecursively(node);

    if (parent->data(0, ProjectNode::RoleKind).toString() == "DamAnalysis")
        syncDamAnalysisMCase(parent);
}

void Solver::setupNodeRole(ProjectNode* node, const QString& kind) const
{
    if (!node)
        return;
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "Solver");
    node->setData(0, ProjectNode::RoleKind, kind);
}

int Solver::countFillStorageCycles(ProjectNode* analysisNode) const
{
    if (!analysisNode)
        return 0;

    int count = 0;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (dynamic_cast<FillStorageCyclePage*>(child->getPage()))
            ++count;
    }
    return count;
}

void Solver::syncDamAnalysisMCase(ProjectNode* analysisNode) const
{
    if (!analysisNode)
        return;

    auto* page = dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage());
    if (!page)
        return;

    page->setMCaseCount(countFillStorageCycles(analysisNode));
}

int Solver::countGeoStressChildren(ProjectNode* geostressNode, const QString& pageType) const
{
    if (!geostressNode)
        return 0;
    int count = 0;
    for (ProjectNode* child : geostressNode->getChildren())
    {
        if (child && child->getPage() && child->getPage()->getPageType() == pageType)
            ++count;
    }
    return count;
}

void Solver::addFillStorageCycleNode(ProjectNode* analysisNode, bool setCurrent) const
{
    if (!analysisNode)
        return;

    auto* fillStorageCyclePage = new FillStorageCyclePage();
    const int nextIdx = countFillStorageCycles(analysisNode) + 1;
    auto* fillStorageCycleNode = new ProjectNode(
        QString("填筑-蓄水周期(%1)").arg(nextIdx), QUuid::createUuid().toString(), fillStorageCyclePage);
    setupNodeRole(fillStorageCycleNode, "FillStorageCycle");
    analysisNode->addChild(fillStorageCycleNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(fillStorageCycleNode);
    }
}

void Solver::addGeoStressRegressionNode(ProjectNode* analysisNode, bool setCurrent)
{
    if (!analysisNode)
        return;

    auto* page = new GeoStressRegressionWidget();
    auto* node = new ProjectNode("地应力回归计算", QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegression");
    analysisNode->addChild(node);

    connect(page, &GeoStressRegressionWidget::computeRequested, this,
            [this, node]()
            {
                onComputeGeoStressRegression(node);
            });

    addGeoStressSideFaceNode(node, false);
    addGeoStressMeasurePointNode(node, false);
    addGeoStressOuterForceNode(node, false);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressSideFaceNode(ProjectNode* geostressNode, bool setCurrent) const
{
    if (!geostressNode)
        return;
    const int idx = countGeoStressChildren(geostressNode, "GeoStressRegressionSideFace") + 1;
    if (idx > 4)
        return;
    auto* page = new GeoStressSideFacePage();
    page->setFaceIndex(idx);
    auto* node = new ProjectNode(QString("侧立面(%1)").arg(idx), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegressionSideFace");
    geostressNode->addChild(node);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressMeasurePointNode(ProjectNode* geostressNode, bool setCurrent) const
{
    if (!geostressNode)
        return;
    const int idx = countGeoStressChildren(geostressNode, "GeoStressRegressionMeasurePoint") + 1;
    auto* page = new GeoStressMeasurePointPage();
    auto* node = new ProjectNode(QString("实测地应力(%1)").arg(idx), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegressionMeasurePoint");
    geostressNode->addChild(node);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressOuterForceNode(ProjectNode* geostressNode, bool setCurrent) const
{
    if (!geostressNode)
        return;
    const int idx = countGeoStressChildren(geostressNode, "GeoStressRegressionOuterForce") + 1;
    auto* page = new GeoStressOuterForcePage();
    auto* node = new ProjectNode(QString("外立面(%1)").arg(idx), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegressionOuterForce");
    geostressNode->addChild(node);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

GeoStressRegressionWidget::ExportPayload Solver::buildGeoStressPayload(ProjectNode* geostressNode) const
{
    GeoStressRegressionWidget::ExportPayload payload;
    if (!geostressNode)
        return payload;

    int measureIndex = 0;
    for (ProjectNode* child : geostressNode->getChildren())
    {
        if (!child || !child->getPage())
            continue;
        if (auto* side = dynamic_cast<GeoStressSideFacePage*>(child->getPage()))
        {
            side->exportDatFile();
            payload.sideFaceFiles.push_back(side->faceFile());
            const auto attr = side->attr();
            payload.faceRows.push_back({attr.idir, attr.height, attr.gama, attr.pgrout, attr.ii});
        }
        else if (auto* p = dynamic_cast<GeoStressMeasurePointPage*>(child->getPage()))
        {
            const auto point = p->point();
            ++measureIndex;
            payload.measureRows.push_back({point.x, point.y, point.sgx, point.sgy, point.txy, QString::number(measureIndex)});
        }
        else if (auto* f = dynamic_cast<GeoStressOuterForcePage*>(child->getPage()))
        {
            f->exportDatFiles();
            if (payload.forceFile.isEmpty())
                payload.forceFile = f->forceFile();
            payload.forceEnabled = f->forceEnabled();
        }
    }
    return payload;
}


void Solver::addCaveSupportComputationNode(ProjectNode* analysisNode, bool setCurrent) const
{
    if (!analysisNode)
        return;

    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (child && child->getPage()
            && child->getPage()->getPageType() == "CaveSupportComputation")
        {
            if (setCurrent)
            {
                if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    pm->setCurrentItem(child);
            }
            return;
        }
    }

    auto* page = new CaveSupportComputationPage();
    auto* node = new ProjectNode("开挖和加固计算", QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveSupportComputation");
    analysisNode->addChild(node);
    addCaveExcavationBodyNode(node, false);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addCaveExcavationBodyNode(ProjectNode* supportNode, bool setCurrent) const
{
    if (!supportNode)
        return;

    const int idx = countGeoStressChildren(supportNode, "CaveExcavationBody") + 1;
    auto* page = new CaveExcavationBodyPage();
    auto* node =
        new ProjectNode(QString("开挖体(%1)").arg(idx), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveExcavationBody");
    supportNode->addChild(node);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addCaveBlockStabilityNode(ProjectNode* analysisNode, bool setCurrent)
{
    if (!analysisNode)
        return;

    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (child && child->getPage()
            && child->getPage()->getPageType() == "CaveBlockStabilityAnalysis")
        {
            if (setCurrent)
            {
                if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    pm->setCurrentItem(child);
            }
            return;
        }
    }

    auto* page = new CaveBlockStabilityWidget();
    auto* node = new ProjectNode("洞室块体稳定性分析", QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveBlockStabilityAnalysis");
    analysisNode->addChild(node);
    connect(page, &CaveBlockStabilityWidget::computeRequested, this,
            [this, node]()
            {
                onComputeCaveBlockStability(node);
            });
    addCaveBlockPlaneNode(node, false);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addCaveBlockPlaneNode(ProjectNode* blockNode, bool setCurrent) const
{
    if (!blockNode)
        return;

    const int idx = countGeoStressChildren(blockNode, "CaveBlockPlane") + 1;
    auto* page = new CaveBlockPlanePage();
    auto* node = new ProjectNode(QString("滑块面(%1)").arg(idx), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveBlockPlane");
    blockNode->addChild(node);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::onSeepageAction()
{
    if (!m_root)
        return;

    auto* seepagePage = new SeepageWidget();
    const QString analysisName = QString("渗流分析(%1)").arg(nextSeepageIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), seepagePage);
    setupNodeRole(analysisNode, "SeepageAnalysis");

    connect(seepagePage, &SeepageWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeSeepage(analysisNode);
            });

    auto addBoundary = [&](const QString& name, const QString& pageType, const QString& fileName)
    {
        auto* page = new SeepageBoundaryPage(pageType, name, fileName);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        setupNodeRole(node, "SeepageBoundary");
        analysisNode->addChild(node);
    };

    addBoundary("上游水头边界", "SeepageBoundaryUpstreamWater", "JD_UPSEEP.DAT");
    addBoundary("下游水头边界", "SeepageBoundaryDownstreamWater", "JD_DWSEEP.DAT");
    addBoundary("天然渗流水头边界", "SeepageBoundaryNaturalSeepage", "JDFREE.DAT");

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onStressStrainAction()
{
    if (!m_root)
        return;

    auto* stressPage = new DamAnalysisWidget();
    const QString analysisName = QString("土石坝计算(%1)").arg(nextStressStrainIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), stressPage);
    setupNodeRole(analysisNode, "DamAnalysis");

    connect(stressPage, &DamAnalysisWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeStressStrain(analysisNode);
            });

    auto addBoundary = [&](const QString& name, const QString& pageType, const QString& fileName)
    {
        auto* page = new StressBoundaryPage(pageType, name, fileName);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        setupNodeRole(node, "StressBoundary");
        analysisNode->addChild(node);
    };

    addBoundary("上游水头边界", "StressBoundaryDamUpstream", "FA_DAMUP.DAT");

    addFillStorageCycleNode(analysisNode, false);
    syncDamAnalysisMCase(analysisNode);

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onCaveExcavationAction()
{
    if (!m_root)
        return;

    auto* cavePage = new CaveExcavationWidget();
    const QString analysisName = QString("洞室开挖(%1)").arg(nextCaveExcavationIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), cavePage);
    setupNodeRole(analysisNode, "CaveExcavationAnalysis");

    connect(cavePage, &CaveExcavationWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeCaveExcavation(analysisNode);
            });

    addGeoStressRegressionNode(analysisNode, false);
    addCaveSupportComputationNode(analysisNode, false);
    addCaveBlockStabilityNode(analysisNode, false);

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onSlopeStabilityAction()
{
    if (!m_root)
        return;

    auto* slopePage = new SlopeStabilityWidget();
    const QString analysisName = QString("边坡分析(%1)").arg(nextSlopeIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), slopePage);
    setupNodeRole(analysisNode, "SlopeStabilityAnalysis");

    connect(slopePage, &SlopeStabilityWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeSlopeStability(analysisNode);
            });

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onComputeSeepage(ProjectNode* analysisNode)
{
    if (!beginCommonCompute(analysisNode, "渗流分析"))
        return;

    bool ok = true;
    QStringList failedFiles;

    auto* seepagePage = dynamic_cast<SeepageWidget*>(analysisNode->getPage());
    if (!seepagePage || !seepagePage->exportIseepDatFile())
    {
        ok = false;
        failedFiles << "ISEEP.DAT";
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* boundaryPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage());
        if (!boundaryPage)
            continue;

        const bool oneOk = boundaryPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << child->getName();
    }

    if (ok)
    {
        QMessageBox::information(nullptr, "导出成功", "渗流计算输入文件导出成功（PREP/）。");
        launchSolverExecutable("渗流分析");
    }
    else
    {
        QMessageBox::warning(nullptr, "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
    }
}

void Solver::onComputeStressStrain(ProjectNode* analysisNode)
{
    if (!beginCommonCompute(analysisNode, "土石坝计算"))
        return;

    bool ok = true;
    QStringList failedFiles;

    auto* stressPage = dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage());
    QString cycleSectionText;
    int mcase = 0;
    QStringList cycleSections;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* fillStorageCyclePage = dynamic_cast<FillStorageCyclePage*>(child->getPage());
        if (!fillStorageCyclePage)
            continue;
        ++mcase;
        cycleSections << fillStorageCyclePage->sectionText(mcase);
    }
    cycleSectionText = cycleSections.join("\n");
    stressPage->setMCaseCount(mcase);

    if (!stressPage || !stressPage->exportIfillDatFile(mcase, cycleSectionText))
    {
        ok = false;
        failedFiles << "IFILL.DAT";
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* boundaryPage = dynamic_cast<StressBoundaryPage*>(child->getPage());
        if (!boundaryPage)
            continue;

        const bool oneOk = boundaryPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << child->getName();
    }

    if (ok)
    {
        QMessageBox::information(nullptr, "导出成功",
                                 "土石坝计算输入文件导出成功（IFILL.DAT 与 PREP/）。");
        launchSolverExecutable("土石坝计算");
    }
    else
        QMessageBox::warning(nullptr, "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
}

void Solver::onComputeCaveExcavation(ProjectNode* analysisNode)
{
    if (!beginCommonCompute(analysisNode, "洞室开挖"))
        return;

    bool ok = true;
    QStringList failedFiles;

    QString supportCaseSection;
    QVector<CaveExcavationBodyPage*> excavationBodies;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* supportPage = dynamic_cast<CaveSupportComputationPage*>(child->getPage());
        if (!supportPage)
            continue;
        supportCaseSection = supportPage->sectionText();
        for (ProjectNode* bodyNode : child->getChildren())
        {
            if (auto* bodyPage = dynamic_cast<CaveExcavationBodyPage*>(bodyNode->getPage()))
                excavationBodies.push_back(bodyPage);
        }
        break;
    }

    if (excavationBodies.isEmpty())
    {
        ok = false;
        failedFiles << "IIKE_E**-**.DAT";
        failedFiles << "IIKE_T01-**.DAT";
    }

    auto* cavePage = dynamic_cast<CaveExcavationWidget*>(analysisNode->getPage());
    if (!cavePage || !cavePage->exportICaveDatFile(supportCaseSection))
    {
        ok = false;
        failedFiles << "ICAVE.DAT";
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* geoPage = dynamic_cast<GeoStressRegressionWidget*>(child->getPage());
        if (!geoPage)
            continue;

        const bool oneOk = geoPage->exportIGEOSDatFile(buildGeoStressPayload(child));
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << "IGEOS.DAT";
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* blockPage = dynamic_cast<CaveBlockStabilityWidget*>(child->getPage());
        if (!blockPage)
            continue;

        QVector<CaveBlockPlanePage*> planePages;
        for (ProjectNode* planeNode : child->getChildren())
        {
            if (auto* planePage = dynamic_cast<CaveBlockPlanePage*>(planeNode->getPage()))
                planePages.push_back(planePage);
        }

        if (planePages.isEmpty() || !blockPage->exportDatFiles(planePages))
        {
            ok = false;
            failedFiles << "ICUT.DAT/PARA.DAT/MESG227.DAT/ICUTPLANE.DAT";
        }
        break;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
    {
        ok = false;
        failedFiles << "PREP";
    }
    else
    {
        auto writeReleKetoke = [&]() -> bool
        {
            auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            if (!meshSvc)
                return false;

            vtkSmartPointer<vtkUnstructuredGrid> grid = meshSvc->getVtkUnstructuredGridSolid();
            if (!grid || grid->GetNumberOfCells() <= 0)
                grid = meshSvc->getVtkUnstructuredGrid();
            if (!grid || grid->GetNumberOfCells() <= 0)
                return false;

            vtkDataArray* elemIdArr =
                vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

            struct ElemCellPair
            {
                int elemId = 0;
                vtkIdType cellId = -1;
            };

            QVector<ElemCellPair> elemCells;
            elemCells.reserve(int(grid->GetNumberOfCells()));
            for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
            {
                const int elemId =
                    elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
                elemCells.push_back({elemId, cellId});
            }
            std::sort(elemCells.begin(), elemCells.end(),
                      [](const ElemCellPair& a, const ElemCellPair& b)
                      { return a.elemId < b.elemId; });

            QHash<vtkIdType, QVector<int>> pointToElemIds;
            pointToElemIds.reserve(int(grid->GetNumberOfPoints() * 1.3));
            for (const ElemCellPair& ec : elemCells)
            {
                vtkCell* cell = grid->GetCell(ec.cellId);
                vtkIdList* pids = cell ? cell->GetPointIds() : nullptr;
                if (!pids)
                    continue;
                for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                    pointToElemIds[pids->GetId(i)].push_back(ec.elemId);
            }

            QVector<QVector<int>> neighborsByElem(elemCells.size());
            int maxRelated = 0;
            for (int i = 0; i < elemCells.size(); ++i)
            {
                const int selfElemId = elemCells[i].elemId;
                vtkCell* cell = grid->GetCell(elemCells[i].cellId);
                vtkIdList* pids = cell ? cell->GetPointIds() : nullptr;
                if (!pids)
                    continue;

                QSet<int> related;
                for (vtkIdType j = 0; j < pids->GetNumberOfIds(); ++j)
                {
                    const auto list = pointToElemIds.value(pids->GetId(j));
                    for (int elemId : list)
                    {
                        if (elemId != selfElemId)
                            related.insert(elemId);
                    }
                }

                QVector<int> rel = related.values().toVector();
                std::sort(rel.begin(), rel.end());
                maxRelated = qMax(maxRelated, rel.size());
                neighborsByElem[i] = std::move(rel);
            }

            QFile file(wd.filePath("PREP/RELE_KETOKE.DAT"));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;

            QTextStream os(&file);
            const int nelem = elemCells.size();
            os << QString("%1%2%3%4   NUMNP   MALIA  NLIA_MAX   IP\n")
                      .arg(nelem, 12)
                      .arg(maxRelated, 10)
                      .arg(maxRelated, 10)
                      .arg(nelem, 10);
            os << "NJM   LJM(1)    LJM(2)   LJM(3)  .........LJM(N)\n";

            for (int i = 0; i < elemCells.size(); ++i)
            {
                const QVector<int>& rel = neighborsByElem[i];
                os << QString("%1").arg(rel.size(), 4);
                for (int j = 0; j < maxRelated; ++j)
                {
                    const int v = (j < rel.size()) ? rel[j] : 0;
                    os << QString("%1").arg(v, 8);
                }
                os << QString("%1\n").arg(elemCells[i].elemId, 9);
            }
            return true;
        };

        if (!writeReleKetoke())
        {
            ok = false;
            failedFiles << "PREP/RELE_KETOKE.DAT";
        }

        const QString prepDir =
            QDir::toNativeSeparators(QDir(wd.filePath("PREP")).absolutePath()) + QDir::separator();
        auto writeIike = [&](const QString& fileName, const QVector<int>& kes, int iex1,
                             int iex2) -> bool
        {
            QFile f(wd.filePath("PREP/" + fileName));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;

            QTextStream os(&f);
            os << QString("%1  NUMKE\n").arg(kes.size(), 12);
            for (int i = 0; i < kes.size(); ++i)
                os << QString("%1%2\n").arg(kes[i], 8).arg(i + 1, 10);
            os << QString("    DIR=%1                                                    \n").arg(prepDir);
            os << " ISHIFT=       1\n";
            os << "   NJX=       0\n";
            os << QString(" IEX1=%1  IEX2=%2  \n").arg(iex1, 4).arg(iex2, 4);
            return true;
        };

        QSet<int> cumulative;
        for (int i = 0; i < excavationBodies.size(); ++i)
        {
            QVector<int> e = excavationBodies[i]->selectedElementIds();
            const QString idx = QString("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
            const QString eFile = QString("IIKE_E%1-%1.DAT").arg(idx);
            if (!writeIike(eFile, e, -(i + 1), -(i + 1)))
            {
                ok = false;
                failedFiles << eFile;
            }

            for (int ke : e)
                cumulative.insert(ke);
            QVector<int> t = cumulative.values().toVector();
            std::sort(t.begin(), t.end());
            const QString tFile = QString("IIKE_T01-%1.DAT").arg(idx);
            if (!writeIike(tFile, t, -1, -(i + 1)))
            {
                ok = false;
                failedFiles << tFile;
            }
        }
    }

    if (ok)
    {
        QMessageBox::information(nullptr, "导出成功",
                                 "洞室开挖输入文件导出成功。");
        launchSolverExecutable("洞室开挖");
    }
    else
        QMessageBox::warning(nullptr, "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
}

void Solver::onComputeSlopeStability(ProjectNode* analysisNode)
{
    if (!beginCommonCompute(analysisNode, "边坡分析"))
        return;

    auto* slopePage = dynamic_cast<SlopeStabilityWidget*>(analysisNode->getPage());
    if (slopePage && slopePage->exportIslopDatFile())
    {
        QMessageBox::information(nullptr, "导出成功", "边坡分析输入文件导出成功。");
        launchSolverExecutable("边坡分析");
    }
    else
        QMessageBox::warning(nullptr, "导出失败", "ISLOP.DAT 导出失败。");
}

void Solver::onComputeGeoStressRegression(ProjectNode* analysisNode)
{
    if (!beginCommonCompute(analysisNode, "地应力回归计算"))
        return;

    auto* page = dynamic_cast<GeoStressRegressionWidget*>(analysisNode->getPage());
    if (page && page->exportIGEOSDatFile(buildGeoStressPayload(analysisNode)))
    {
        QMessageBox::information(nullptr, "导出成功", "地应力回归计算输入文件导出成功。");
        launchSolverExecutable("地应力回归计算");
    }
    else
        QMessageBox::warning(nullptr, "导出失败", "IGEOS.DAT 导出失败。");
}

void Solver::onComputeCaveBlockStability(ProjectNode* analysisNode)
{
    if (!analysisNode || !analysisNode->getPage())
        return;

    auto* blockPage = dynamic_cast<CaveBlockStabilityWidget*>(analysisNode->getPage());
    if (!blockPage)
        return;

    QVector<CaveBlockPlanePage*> planePages;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (auto* planePage = dynamic_cast<CaveBlockPlanePage*>(child->getPage()))
            planePages.push_back(planePage);
    }

    if (planePages.isEmpty())
    {
        QMessageBox::warning(nullptr, "导出失败", "未定义滑块面，无法生成文件。");
        return;
    }

    if (blockPage->exportDatFiles(planePages))
    {
        QMessageBox::information(nullptr, "导出成功",
                                 "已在 SliderCase 文件夹生成 ICUT.DAT、PARA.DAT、MESG227.DAT、ICUTPLANE.DAT。");
        launchSolverExecutable("块体稳定分析");
    }
    else
        QMessageBox::warning(nullptr, "导出失败",
                             "生成滑块文件失败，请检查 SliderCase 目录与参数设置。");
}

bool Solver::beginCommonCompute(ProjectNode* analysisNode, const QString& analysisType)
{
    if (!analysisNode)
        return false;

    if (!analysisNode->getPage())
    {
        QMessageBox::warning(nullptr, "计算失败",
                             QString("%1页面不存在，无法执行计算。").arg(analysisType));
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
    {
        QMessageBox::warning(nullptr, "计算失败",
                             QString("%1无法创建 PREP 目录，无法执行计算。").arg(analysisType));
        return false;
    }

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (meshSvc)
    {
        const QString targetIiin = wd.filePath("PREP/IIIN.DAT");
        const QString sourceMeshPath = meshSvc->sourceMeshFilePath();
        const QFileInfo sourceInfo(sourceMeshPath);
        const bool sourceIsDat = sourceInfo.exists() && sourceInfo.suffix().compare("dat", Qt::CaseInsensitive) == 0;

        if (sourceIsDat)
        {
            // DAT 在导入时已经备份到 PREP/IIIN.DAT，这里只在缺失时做一次兜底复制。
            if (!QFile::exists(targetIiin) && !QFile::copy(sourceMeshPath, targetIiin))
            {
                QMessageBox::warning(nullptr, "计算失败",
                                     QString("%1缺少 PREP/IIIN.DAT，且兜底复制 DAT 网格失败。")
                                         .arg(analysisType));
                return false;
            }
        }
        else
        {
            QString iiinErr;
            if (!meshSvc->exportIiinDatFile(targetIiin, &iiinErr))
            {
                QMessageBox::warning(nullptr, "计算失败",
                                     QString("%1生成 PREP/IIIN.DAT 失败：%2")
                                         .arg(analysisType, iiinErr));
                return false;
            }
        }
    }

    auto* physicalFieldService = ServiceRegistry::instance().getService<PhysicalFieldService>();
    if (physicalFieldService)
    {
        physicalFieldService->generateIIIDFile();
        physicalFieldService->generateIIWLFile();
    }

    auto* materialManagerService = ServiceRegistry::instance().getService<MaterialManagerService>();
    if (materialManagerService && !materialManagerService->generateIMATFile())
        return false;

    return exportInouseDatFile() && exportMaMaskDatFile() && exportKetraceDatFile() &&
           exportJdtraceDatFile();
}

bool Solver::launchSolverExecutable(const QString& analysisType)
{
    const QString exePath =
        QDir::toNativeSeparators(
            QDir(QCoreApplication::applicationDirPath())
                .filePath("solver/PCCAE_CPU/PCCAE_CPU.exe"));
    if (!QFileInfo::exists(exePath))
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             QString("%1导出成功，但未找到求解器：\n%2")
                                 .arg(analysisType, exePath));
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QProcess::execute("taskkill", {"-f", "-im", "PCCAE_CPU.exe"});

    const int solverMode = solverModeFromAnalysisType(analysisType);
    if (solverMode > 0)
    {
        if (!updateIiiDirDat(projectDir, solverMode))
        {
            QMessageBox::warning(nullptr, "启动求解器失败",
                                 QString("%1更新 IIIDIR.DAT 失败。").arg(analysisType));
            return false;
        }

        if (!updatePrepDatFiles(projectDir, solverMode))
        {
            QMessageBox::warning(nullptr, "启动求解器失败",
                                 QString("%1更新 PREP/DAT 文件失败。").arg(analysisType));
            return false;
        }
    }

    if (m_solverProcess)
    {
        m_solverProcess->disconnect(this);
        m_solverProcess->kill();
        m_solverProcess->deleteLater();
        m_solverProcess = nullptr;
    }

    m_solverProcess = new QProcess(this);
    m_solverProcess->setWorkingDirectory(QDir(projectDir).absolutePath());
    m_solverProcess->setProgram(exePath);
    m_solverProcess->setArguments(QStringList());
    m_solverProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_solverProcess, &QProcess::readyReadStandardOutput, this, [this, analysisType]() {
        if (!m_solverProcess)
            return;
        const QString output = QString::fromLocal8Bit(m_solverProcess->readAllStandardOutput()).trimmed();
        if (!output.isEmpty())
            qInfo().noquote() << output;
    });

    connect(m_solverProcess, &QProcess::errorOccurred, this,
            [analysisType](QProcess::ProcessError error) {
                qWarning() << "求解器进程错误，分析类型=" << analysisType
                           << "错误码=" << static_cast<int>(error);
            });

    connect(m_solverProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, analysisType](int exitCode, QProcess::ExitStatus status) {
                qInfo() << "求解器退出，分析类型=" << analysisType << "退出码=" << exitCode
                        << "状态=" << (status == QProcess::NormalExit ? "正常退出" : "异常退出");
                if (m_solverProcess)
                {
                    m_solverProcess->deleteLater();
                    m_solverProcess = nullptr;
                }
            });

    m_solverProcess->start();
    if (!m_solverProcess->waitForStarted(5000))
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             QString("%1启动求解器进程失败：%2")
                                 .arg(analysisType, m_solverProcess->errorString()));
        qWarning() << "启动求解器失败:" << m_solverProcess->errorString();
        m_solverProcess->deleteLater();
        m_solverProcess = nullptr;
        return false;
    }

    qInfo() << "求解器已启动，分析类型=" << analysisType << "进程ID=" << m_solverProcess->processId();
    return true;
}

int Solver::solverModeFromAnalysisType(const QString& analysisType) const
{
    if (analysisType.contains("渗流") || analysisType.contains("土石坝"))
        return 8;
    if (analysisType.contains("洞室"))
        return 6;
    if (analysisType.contains("边坡"))
        return 7;
    return 0;
}

bool Solver::updateIiiDirDat(const QString& projectDir, int solverMode) const
{
    QFile file("D:/IIIDIR.DAT");
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), QString::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    const QString nativeProjectDir = QDir::toNativeSeparators(QDir(projectDir).absolutePath());
    const QString projectWithSlash = nativeProjectDir.endsWith('\\') ? nativeProjectDir : nativeProjectDir + "\\";
    const QString prepDir = projectWithSlash + "PREP";

    lines[0] = QString("'%1'").arg(projectWithSlash);

    for (int i = 0; i < lines.size(); ++i)
    {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.contains("\\PREP'") || trimmed.contains("/PREP'"))
            lines[i] = QString("'%1'").arg(prepDir);

        bool ok = false;
        trimmed.toInt(&ok);
        if (ok && i > 0)
        {
            const QString prev = lines[i - 1];
            if (prev.contains("\\PREP'") || prev.contains("/PREP'"))
                lines[i] = QString("%1").arg(solverMode, 12);
        }
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::updatePrepDatFiles(const QString& projectDir, int solverMode) const
{
    QFile file(QDir(projectDir).filePath("PREP/IIIN.DAT"));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), QString::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    lines[1].replace(QRegularExpression(R"(^\s*\d+)"), QString("%8").arg(solverMode));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

int Solver::nextStressStrainIndex() const
{
    if (!m_root)
        return 1;

    int maxIdx = 0;
    QRegularExpression re(QString(R"(^(?:土石坝计算|应力应变分析)\((\d+)\)$)"));

    for (ProjectNode* child : m_root->getChildren())
    {
        if (!child)
            continue;
        const QString name = child->getName();
        auto m = re.match(name);
        if (m.hasMatch())
        {
            bool ok = false;
            int idx = m.captured(1).toInt(&ok);
            if (ok)
                maxIdx = std::max(maxIdx, idx);
        }
    }

    return maxIdx + 1;
}

int Solver::nextCaveExcavationIndex() const
{
    if (!m_root)
        return 1;

    int maxIdx = 0;
    QRegularExpression re(QString(R"(^洞室开挖\((\d+)\)$)"));

    for (ProjectNode* child : m_root->getChildren())
    {
        if (!child)
            continue;
        const QString name = child->getName();
        auto m = re.match(name);
        if (m.hasMatch())
        {
            bool ok = false;
            int idx = m.captured(1).toInt(&ok);
            if (ok)
                maxIdx = std::max(maxIdx, idx);
        }
    }

    return maxIdx + 1;
}

int Solver::nextSeepageIndex() const
{
    if (!m_root)
        return 1;

    int maxIdx = 0;
    QRegularExpression re(QString(R"(^渗流分析\((\d+)\)$)"));

    for (ProjectNode* child : m_root->getChildren())
    {
        if (!child)
            continue;
        const QString name = child->getName();
        auto m = re.match(name);
        if (m.hasMatch())
        {
            bool ok = false;
            int idx = m.captured(1).toInt(&ok);
            if (ok)
                maxIdx = std::max(maxIdx, idx);
        }
    }

    return maxIdx + 1;
}

int Solver::nextSlopeIndex() const
{
    if (!m_root)
        return 1;

    int maxIdx = 0;
    QRegularExpression re(QString(R"(^(?:边坡分析|边坡稳定分析)\((\d+)\)$)"));

    for (ProjectNode* child : m_root->getChildren())
    {
        if (!child)
            continue;
        const QString name = child->getName();
        auto m = re.match(name);
        if (m.hasMatch())
        {
            bool ok = false;
            int idx = m.captured(1).toInt(&ok);
            if (ok)
                maxIdx = std::max(maxIdx, idx);
        }
    }

    return maxIdx + 1;
}

bool Solver::exportInouseDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/INOUSE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const int configuredNdim =
        qMax(2, PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt());

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : vtkSmartPointer<vtkUnstructuredGrid>{};
    if (!grid || grid->GetNumberOfPoints() <= 0)
    {
        os << "           0    NUM OF NO USE NODE\n";
        os << "           0    NUM OF NO USE ELEMENT\n";
        os << "       0    rod-num\n";
        os << QString("%1%2    NEBAR  NDIM\n").arg(0, 12).arg(configuredNdim, 12);
        return true;
    }

    const vtkIdType nPts = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();
    int meshMaxDim = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;
        meshMaxDim = qMax(meshMaxDim, cell->GetCellDimension());
    }
    const int ndim = qMax(2, qMin(configuredNdim, meshMaxDim > 0 ? meshMaxDim : configuredNdim));

    QSet<vtkIdType> usedPointIds;
    usedPointIds.reserve(static_cast<int>(nPts));
    QVector<int> noUseElems;
    noUseElems.reserve(static_cast<int>(nCells / 10 + 1));

    QVector<QVector<int>> pointToSolidElems(static_cast<int>(nPts));
    QVector<int> solidCellIds;
    solidCellIds.reserve(static_cast<int>(nCells));

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;
        vtkIdList* ptIds = cell->GetPointIds();
        const vtkIdType np = ptIds ? ptIds->GetNumberOfIds() : 0;
        if (np <= 0)
        {
            noUseElems.push_back(static_cast<int>(cid + 1));
            continue;
        }

        const bool isSolid = (cell->GetCellDimension() >= ndim);
        if (isSolid)
            solidCellIds.push_back(static_cast<int>(cid + 1));
        for (vtkIdType i = 0; i < np; ++i)
        {
            const vtkIdType pid = ptIds->GetId(i);
            usedPointIds.insert(pid);
            if (isSolid && pid >= 0 && pid < nPts)
            {
                pointToSolidElems[static_cast<int>(pid)].push_back(static_cast<int>(cid + 1));
            }
        }
    }

    vtkSmartPointer<vtkUnstructuredGrid> solidGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    solidGrid->SetPoints(grid->GetPoints());
    QVector<int> solidLocalToGlobal;
    solidLocalToGlobal.reserve(solidCellIds.size());
    for (int ke : std::as_const(solidCellIds))
    {
        vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
        if (!solid || !solid->GetPointIds())
            continue;
        solidGrid->InsertNextCell(solid->GetCellType(), solid->GetPointIds());
        solidLocalToGlobal.push_back(ke);
    }
    auto solidLocator = vtkSmartPointer<vtkStaticCellLocator>::New();
    if (solidGrid->GetNumberOfCells() > 0)
    {
        solidLocator->SetDataSet(solidGrid);
        solidLocator->BuildLocator();
    }

    QVector<int> noUseNodes;
    noUseNodes.reserve(static_cast<int>(nPts / 10 + 1));
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        if (!usedPointIds.contains(pid))
            noUseNodes.push_back(static_cast<int>(pid + 1));
    }

    struct BarRow
    {
        int ke = 0;
        int ke1 = 0;
        int ke2 = 0;
        int lx = 4;
        QVector<double> rst1;
        QVector<double> rst2;
    };
    QVector<BarRow> barRows;
    QVector<int> suspendNodeIds;

    auto findContainingSolid = [&](const QVector<int>& candidates, const double xyz[3]) -> QVector<int>
    {
        QVector<int> inside;
        inside.reserve(candidates.size());
        for (int ke : candidates)
        {
            vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
            if (!solid)
                continue;
            std::array<double, 3> closestPt{0.0, 0.0, 0.0};
            std::array<double, 3> pcoords{0.0, 0.0, 0.0};
            int subId = 0;
            double dist2 = 0.0;
            const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
            std::vector<double> weights(static_cast<size_t>(wn), 0.0);
            const int evalOk = solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId,
                                                       pcoords.data(), dist2, weights.data());
            constexpr double kInsideTol2 = 1.0e-14;
            if (evalOk == 1 || dist2 <= kInsideTol2)
                inside.push_back(ke);
        }
        return inside;
    };

    auto pickRelatedSolid = [&](vtkIdType pid, const double xyzSelf[3], const double xyzOther[3]) -> int
    {
        auto distanceToCell = [&](int ke, const double xyz[3]) -> double
        {
            vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
            if (!solid)
                return std::numeric_limits<double>::max();
            std::array<double, 3> closestPt{0.0, 0.0, 0.0};
            std::array<double, 3> pcoords{0.0, 0.0, 0.0};
            int subId = 0;
            double dist2 = std::numeric_limits<double>::max();
            const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
            std::vector<double> weights(static_cast<size_t>(wn), 0.0);
            solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId, pcoords.data(), dist2,
                                    weights.data());
            return dist2;
        };

        QVector<int> candidates;
        QVector<int> related;
        if (pid >= 0 && pid < nPts)
            related = pointToSolidElems[static_cast<int>(pid)];
        candidates = related;
        if (candidates.isEmpty())
        {
            if (solidLocator && solidGrid->GetNumberOfCells() > 0)
            {
                const vtkIdType localCell = solidLocator->FindCell(const_cast<double*>(xyzSelf));
                if (localCell >= 0 && localCell < solidLocalToGlobal.size())
                    return solidLocalToGlobal[static_cast<int>(localCell)];
            }
            return 0;
        }
        if (candidates.isEmpty())
            return 0;

        QVector<int> selfInside = findContainingSolid(candidates, xyzSelf);
        if (selfInside.isEmpty() && !related.isEmpty())
            selfInside = related;
        if (selfInside.isEmpty())
            return 0;
        if (selfInside.size() == 1)
            return selfInside.front();

        double bestDist2 = std::numeric_limits<double>::max();
        int bestKe = selfInside.front();
        for (int ke : selfInside)
        {
            const double dist2 = distanceToCell(ke, xyzOther);
            if (dist2 < bestDist2)
            {
                bestDist2 = dist2;
                bestKe = ke;
            }
        }
        return bestKe;
    };

    auto evaluateRst = [&](int ke, const double xyz[3], QVector<double>& rst)
    {
        rst.fill(0.0, ndim);
        if (ke <= 0)
            return;
        vtkCell* solid = grid->GetCell(static_cast<vtkIdType>(ke - 1));
        if (!solid)
            return;

        std::array<double, 3> closestPt{0.0, 0.0, 0.0};
        std::array<double, 3> pcoords{0.0, 0.0, 0.0};
        int subId = 0;
        double dist2 = 0.0;
        const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
        std::vector<double> weights(static_cast<size_t>(wn), 0.0);
        solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId, pcoords.data(), dist2,
                                weights.data());
        for (int d = 0; d < ndim && d < 3; ++d)
            rst[d] = 2.0 * pcoords[d] - 1.0;
    };

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != 1)
            continue;
        vtkIdList* ptIds = cell->GetPointIds();
        if (!ptIds || ptIds->GetNumberOfIds() < 2)
            continue;

        const vtkIdType p0 = ptIds->GetId(0);
        const vtkIdType p1 = ptIds->GetId(1);
        if (p0 < 0 || p0 >= nPts || p1 < 0 || p1 >= nPts)
            continue;

        const auto& rel0 = pointToSolidElems[static_cast<int>(p0)];
        const auto& rel1 = pointToSolidElems[static_cast<int>(p1)];
        const bool s0 = rel0.isEmpty();
        const bool s1 = rel1.isEmpty();

        double xyz0[3] = {0.0, 0.0, 0.0};
        double xyz1[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(p0, xyz0);
        grid->GetPoint(p1, xyz1);

        BarRow row;
        row.ke = static_cast<int>(cid + 1);
        row.ke1 = pickRelatedSolid(p0, xyz0, xyz1);
        row.ke2 = pickRelatedSolid(p1, xyz1, xyz0);
        if (!s0 && !s1)
            row.lx = 1;
        else if (s0 && !s1)
            row.lx = 2;
        else if (!s0 && s1)
            row.lx = 3;
        else
            row.lx = 4;
        evaluateRst(row.ke1, xyz0, row.rst1);
        evaluateRst(row.ke2, xyz1, row.rst2);
        barRows.push_back(row);

        if (s0)
            suspendNodeIds.push_back(static_cast<int>(p0 + 1));
        if (s1)
            suspendNodeIds.push_back(static_cast<int>(p1 + 1));
    }

    std::sort(noUseNodes.begin(), noUseNodes.end());
    std::sort(noUseElems.begin(), noUseElems.end());
    std::sort(suspendNodeIds.begin(), suspendNodeIds.end());
    suspendNodeIds.erase(std::unique(suspendNodeIds.begin(), suspendNodeIds.end()), suspendNodeIds.end());

    os << QString("%1    NUM OF NO USE NODE\n").arg(noUseNodes.size(), 12);
    for (int i = 0; i < noUseNodes.size(); ++i)
    {
        os << QString("%1%2\n").arg(noUseNodes.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1    NUM OF NO USE ELEMENT\n").arg(noUseElems.size(), 12);
    for (int i = 0; i < noUseElems.size(); ++i)
    {
        os << QString("%1%2\n").arg(noUseElems.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1    杆单元悬浮节点数\n").arg(suspendNodeIds.size(), 8);
    for (int i = 0; i < suspendNodeIds.size(); ++i)
    {
        os << QString("%1%2\n").arg(suspendNodeIds.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1%2    NEBAR  NDIM\n").arg(barRows.size(), 12).arg(ndim, 12);
    for (int i = 0; i < barRows.size(); ++i)
    {
        const auto& r = barRows.at(i);
        os << QString("%1%2%3%4").arg(r.ke, 7).arg(r.ke1, 8).arg(r.ke2, 8).arg(r.lx, 8);
        for (double v : r.rst1)
            os << QString("%1").arg(v, 16, 'E', 6);
        for (double v : r.rst2)
            os << QString("%1").arg(v, 16, 'E', 6);
        os << QString("%1\n").arg(i + 1, 8);
    }
    return true;
}

bool Solver::exportMaMaskDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());

    // 确保 PREP 文件夹存在
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

           // 共同的文件内容
    const QString fileContent = "   0   NUM\n";

           // 1. 生成 MA_MASK.DAT
    QFile file(wd.filePath("PREP/MA_MASK.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&file);
    os << fileContent;
    file.close(); // 显式关闭文件是个好习惯

           // 2. 生成 MA_MASK_CMM.DAT
    QFile fileCmm(wd.filePath("PREP/MA_MASK_CMM.DAT"));
    if (!fileCmm.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream osCmm(&fileCmm);
    osCmm << fileContent;
    fileCmm.close();

    return true;
}
bool Solver::exportKetraceDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/KETRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}

bool Solver::exportJdtraceDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/JDTRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}
