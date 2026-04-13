#include "Solver.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>

#include "FillStorageCyclePage.h"
#include "CaveExcavationBodyPage.h"
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
            page->loadJson(childObj.value("state").toObject());
            analysisNode->addChild(node);

            if (type == "GeoStressRegressionAnalysis" || type == "CaveSupportComputation")
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
    auto* page = new GeoStressSideFacePage();
    auto* node = new ProjectNode(idx == 1 ? QString("侧立面") : QString("侧立面(%1)").arg(idx), QUuid::createUuid().toString(), page);
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
    auto* node = new ProjectNode(idx == 1 ? QString("实测地应力") : QString("实测地应力(%1)").arg(idx), QUuid::createUuid().toString(), page);
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
    auto* node = new ProjectNode(idx == 1 ? QString("外面力") : QString("外面力(%1)").arg(idx), QUuid::createUuid().toString(), page);
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
            payload.measureRows.push_back({point.x, point.y, point.sgx, point.sgy, point.txy, point.ii});
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
    if (!analysisNode)
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
    }
    else
    {
        QMessageBox::warning(nullptr, "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
    }
}

void Solver::onComputeStressStrain(ProjectNode* analysisNode)
{
    if (!analysisNode)
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
        QMessageBox::information(nullptr, "导出成功",
                                 "土石坝计算输入文件导出成功（IFILL.DAT 与 PREP/）。");
    else
        QMessageBox::warning(nullptr, "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
}

void Solver::onComputeCaveExcavation(ProjectNode* analysisNode)
{
    if (!analysisNode)
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
        QMessageBox::information(nullptr, "导出成功",
                                 "洞室开挖输入文件导出成功（ICAVE.DAT、IGEOS.DAT、IIKE_E**-**.DAT、IIKE_T01-**.DAT）。");
    else
        QMessageBox::warning(nullptr, "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
}

void Solver::onComputeSlopeStability(ProjectNode* analysisNode)
{
    if (!analysisNode)
        return;

    auto* slopePage = dynamic_cast<SlopeStabilityWidget*>(analysisNode->getPage());
    if (slopePage && slopePage->exportIslopDatFile())
        QMessageBox::information(nullptr, "导出成功", "边坡分析输入文件导出成功（ISLOP.DAT）。");
    else
        QMessageBox::warning(nullptr, "导出失败", "ISLOP.DAT 导出失败。");
}

void Solver::onComputeGeoStressRegression(ProjectNode* analysisNode)
{
    if (!analysisNode)
        return;

    auto* page = dynamic_cast<GeoStressRegressionWidget*>(analysisNode->getPage());
    if (page && page->exportIGEOSDatFile(buildGeoStressPayload(analysisNode)))
        QMessageBox::information(nullptr, "导出成功", "地应力回归计算输入文件导出成功（IGEOS.DAT）。");
    else
        QMessageBox::warning(nullptr, "导出失败", "IGEOS.DAT 导出失败。");
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
