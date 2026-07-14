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
#include <QWidget>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
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

namespace
{
QStringList caveIcaveLineKeys()
{
    return {"line_3", "line_5", "line_7", "line_10", "line_11", "line_18", "line_19",
            "line_20", "line_21", "line_22", "line_23", "line_24", "line_25"};
}

bool hasAnyIcaveLineValue(const QJsonObject& obj)
{
    for (const QString& key : caveIcaveLineKeys())
    {
        if (!obj.value(key).toString().trimmed().isEmpty())
            return true;
    }
    return false;
}

bool hasAnyIcaveLineKey(const QJsonObject& obj)
{
    for (const QString& key : caveIcaveLineKeys())
    {
        if (obj.contains(key))
            return true;
    }
    return false;
}
}

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
        const QJsonObject analysisState = analysisObj.value("state").toObject();
        const QString analysisType = analysisObj.value("type").toString();

        BasePage* analysisPage = nullptr;
        ProjectNode* analysisNode = nullptr;

        if (analysisType == "SeepageAnalysis")
        {
            auto* seepagePage = new SeepageWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("渗流分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), seepagePage);
            seepagePage->loadJson(analysisState);
            connect(seepagePage, &SeepageWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSeepage(analysisNode);
                    });
            connectStopRequest(seepagePage);
            analysisPage = seepagePage;
        }
        else if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
        {
            auto* stressPage = new DamAnalysisWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("土石坝计算"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), stressPage);
            stressPage->loadJson(analysisState);
            connect(stressPage, &DamAnalysisWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeStressStrain(analysisNode);
                    });
            connectStopRequest(stressPage);
            analysisPage = stressPage;
        }
        else if (analysisType == "SlopeStabilityAnalysis")
        {
            auto* slopePage = new SlopeStabilityWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("边坡分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), slopePage);
            slopePage->loadJson(analysisState);
            connect(slopePage, &SlopeStabilityWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSlopeStability(analysisNode);
                    });
            connectStopRequest(slopePage);
            analysisPage = slopePage;
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            auto* cavePage = new CaveExcavationWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("洞室开挖"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), cavePage);
            cavePage->loadJson(analysisState);
            analysisPage = cavePage;
        }
        else if (analysisType == "CaveBlockStabilityAnalysis")
        {
            auto* blockPage = new CaveBlockStabilityWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("块体稳定计算"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), blockPage);
            blockPage->loadJson(analysisState);
            connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeCaveBlockStability(analysisNode);
                    });
            connectStopRequest(blockPage);
            analysisPage = blockPage;
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
            else if (type == "CaveBlockStabilityAnalysis")
            {
                page = new CaveBlockStabilityWidget();
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
            connectStopRequest(geoPage);
            }
            else if (auto* supportPage = dynamic_cast<CaveSupportComputationPage*>(page))
            {
                connect(supportPage, &CaveSupportComputationPage::computeRequested, this,
                        [this, analysisNode]()
                        {
                            onComputeCaveExcavation(analysisNode);
                        });
            connectStopRequest(supportPage);
            }
            else if (auto* blockPage = dynamic_cast<CaveBlockStabilityWidget*>(page))
            {
                connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
                        [this, node]()
                        {
                            onComputeCaveBlockStability(node);
                        });
            connectStopRequest(blockPage);
            }
            const QJsonObject childState = childObj.value("state").toObject();
            page->loadJson(childState);
            if (analysisType == "CaveExcavationAnalysis" && type == "CaveSupportComputation"
                && !hasAnyIcaveLineKey(childState) && hasAnyIcaveLineValue(analysisState))
            {
                if (auto* supportPage = dynamic_cast<CaveSupportComputationPage*>(page))
                    supportPage->loadLegacyCaveJson(analysisState);
            }
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
        if (analysisType == "CaveBlockStabilityAnalysis"
            && countGeoStressChildren(analysisNode, "CaveBlockPlane") == 0)
            addCaveBlockPlaneNode(analysisNode, false);

        if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            setupNodeRole(analysisNode, "DamAnalysis");
        else if (analysisType == "SeepageAnalysis")
            setupNodeRole(analysisNode, "SeepageAnalysis");
        else if (analysisType == "SlopeStabilityAnalysis")
            setupNodeRole(analysisNode, "SlopeStabilityAnalysis");
        else if (analysisType == "CaveExcavationAnalysis")
            setupNodeRole(analysisNode, "CaveExcavationAnalysis");
        else if (analysisType == "CaveBlockStabilityAnalysis")
            setupNodeRole(analysisNode, "CaveBlockStabilityAnalysis");

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
    em.registerHandler(PublicConstants::kActionCaveBlockStabilityKey,
                       [this](const QVariantMap&)
                       {
                           onCaveBlockStabilityAction();
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
            connectStopRequest(page);

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
            for (const auto& point : p->points())
            {
                ++measureIndex;
                payload.measureRows.push_back({point.x, point.y, point.z, point.sgx, point.sgy, point.sgz,
                                               point.txy, point.tyz, point.tzx, QString::number(measureIndex)});
            }
        }
        else if (auto* f = dynamic_cast<GeoStressOuterForcePage*>(child->getPage()))
        {
            // 不再无条件生成 FTLOAD.DAT。零外力或未选择作用边/面时，
            // IGEOS 的 ITAG_FT 必须保持 0，避免求解器读取无效或旧的 FTLOAD.DAT。
            const int enabled = f->forceEnabled();
            if (enabled == 0)
                continue;

            // 只有文件成功生成后才允许启用 FTLOAD。
            if (!f->exportDatFiles())
                continue;

            if (payload.forceFile.isEmpty())
                payload.forceFile = f->forceFile();
            payload.forceEnabled = 1;
        }
    }
    return payload;
}


void Solver::addCaveSupportComputationNode(ProjectNode* analysisNode, bool setCurrent)
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
    connect(page, &CaveSupportComputationPage::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeCaveExcavation(analysisNode);
            });
            connectStopRequest(page);
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
            connectStopRequest(page);
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
            connectStopRequest(seepagePage);

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
            connectStopRequest(stressPage);

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

    addGeoStressRegressionNode(analysisNode, false);
    addCaveSupportComputationNode(analysisNode, false);

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onCaveBlockStabilityAction()
{
    if (!m_root)
        return;

    auto* blockPage = new CaveBlockStabilityWidget();
    const QString analysisName = QString("块体稳定计算(%1)").arg(nextCaveBlockStabilityIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), blockPage);
    setupNodeRole(analysisNode, "CaveBlockStabilityAnalysis");

    connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeCaveBlockStability(analysisNode);
            });
            connectStopRequest(blockPage);

    addCaveBlockPlaneNode(analysisNode, false);

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
            connectStopRequest(slopePage);

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onComputeSeepage(ProjectNode* analysisNode)
{
    try
    {
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile iautoFile(QDir(projectDir).absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(nullptr, "计算失败", "渗流分析写入 MESG_IAUTO.DAT 失败。");
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "4\n";
    }

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
    catch (const std::exception& e)
    {
        qWarning() << "渗流分析计算失败:" << e.what();
    }
}

void Solver::onComputeStressStrain(ProjectNode* analysisNode)
{
    try
    {
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile iautoFile(QDir(projectDir).absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(nullptr, "计算失败", "土石坝计算写入 MESG_IAUTO.DAT 失败。");
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "3\n";
    }

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
    catch (const std::exception& e)
    {
        qWarning() << "土石坝计算失败:" << e.what();
    }
}

void Solver::onComputeCaveExcavation(ProjectNode* analysisNode)
{
    try
    {
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    // 先收集开挖级，再生成 IIIN/ICAVE。旧代码先导出 IIIN，导致开挖层号来不及写入。
    CaveSupportComputationPage* supportPage = nullptr;
    QVector<CaveExcavationBodyPage*> excavationBodies;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* candidate = dynamic_cast<CaveSupportComputationPage*>(child->getPage());
        if (!candidate)
            continue;

        supportPage = candidate;
        for (ProjectNode* bodyNode : child->getChildren())
        {
            if (auto* bodyPage = dynamic_cast<CaveExcavationBodyPage*>(bodyNode->getPage()))
                excavationBodies.push_back(bodyPage);
        }
        break;
    }

    if (!supportPage)
    {
        QMessageBox::warning(nullptr, "计算失败", "未找到洞室开挖计算设置页面。");
        return;
    }
    if (excavationBodies.isEmpty())
    {
        QMessageBox::warning(nullptr, "计算失败", "请至少创建一个开挖体，并为其选择区域分组。");
        return;
    }

    const int caveCalculationType = supportPage->calculationTypeCode();
    const int caveLoadMethod = supportPage->excavationLoadMethodCode();
    if (caveCalculationType < 1 || caveCalculationType > 3)
    {
        QMessageBox::warning(nullptr, "计算失败",
                             "计算类型代码无效：应为 1（静力）、2（动力）或 3（静力+动力）。");
        return;
    }
    if (caveLoadMethod < 1 || caveLoadMethod > 2)
    {
        QMessageBox::warning(nullptr, "计算失败",
                             "开挖荷载来源代码无效：应为 1（按开挖面）或 2（按开挖单元）。");
        return;
    }

    auto* caveMeshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkSmartPointer<vtkUnstructuredGrid> caveGrid =
        caveMeshSvc ? caveMeshSvc->getVtkUnstructuredGridSolid() : nullptr;
    if (!caveGrid || caveGrid->GetNumberOfCells() <= 0)
        caveGrid = caveMeshSvc ? caveMeshSvc->getVtkUnstructuredGrid() : nullptr;

    bool hasTetrahedron = false;
    if (caveGrid)
    {
        for (vtkIdType cid = 0; cid < caveGrid->GetNumberOfCells(); ++cid)
        {
            vtkCell* cell = caveGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == 3 && cell->GetCellType() == VTK_TETRA)
            {
                hasTetrahedron = true;
                break;
            }
        }
    }
    if (hasTetrahedron)
    {
        QMessageBox::warning(nullptr, "计算失败",
                             "当前旧版 Fortran AUTOCAVE 不完整支持四节点四面体开挖。"
                             "按面法与按单元法都会在旧开挖预处理阶段发生格式不兼容。"
                             "请使用六面体网格后再计算。");
        return;
    }

    const QDir projectRoot(QDir(projectDir).absolutePath());
    const QString stressFile = caveLoadMethod == 1
                                   ? projectRoot.filePath("GEOSTR/MODSTR.TXT")
                                   : projectRoot.filePath("GEOSTR/ISGMAR.TXT");
    if (!QFileInfo::exists(stressFile))
    {
        QMessageBox::warning(nullptr, "计算失败",
                             QString("开挖计算缺少地应力结果文件：\n%1\n请先完成地应力回归计算。")
                                 .arg(stressFile));
        return;
    }

    const bool needsDynamicFiles = caveCalculationType != 1 ||
                                   supportPage->modalAnalysisEnabled() ||
                                   supportPage->timeHistoryEnabled();
    if (needsDynamicFiles && !QFileInfo::exists(projectRoot.filePath("PREP/TDTRACE.DAT")))
    {
        QMessageBox::warning(nullptr, "计算失败",
                             "当前设置包含动力、模态或时程任务，但 PREP/TDTRACE.DAT 不存在。"
                             "普通静力开挖请把计算类型设为 1，并关闭模态及时程分析。");
        return;
    }

    QVector<QVector<int>> selectedSourceElements;
    selectedSourceElements.reserve(excavationBodies.size());
    QSet<int> assignedSourceElements;
    for (int level = 0; level < excavationBodies.size(); ++level)
    {
        QVector<int> elemIds = excavationBodies[level]->selectedElementIds();
        std::sort(elemIds.begin(), elemIds.end());
        elemIds.erase(std::unique(elemIds.begin(), elemIds.end()), elemIds.end());

        if (elemIds.isEmpty())
        {
            QMessageBox::warning(nullptr, "计算失败",
                                 QString("第 %1 级开挖体没有选择任何实体单元。").arg(level + 1));
            return;
        }

        for (int elemId : elemIds)
        {
            if (assignedSourceElements.contains(elemId))
            {
                QMessageBox::warning(nullptr, "计算失败",
                                     QString("单元 %1 被重复分配到多个开挖级，请删除重复选择。")
                                         .arg(elemId));
                return;
            }
            assignedSourceElements.insert(elemId);
        }
        selectedSourceElements.push_back(elemIds);
    }

    // ICAVE 中的 IEXCA_MAX、IEXCA_STA、IEXCA_END 与实际开挖体数量保持一致。
    supportPage->setExcavationLevelCount(excavationBodies.size());
    const QString supportCaseSection = supportPage->sectionText();

    QFile iautoFile(QDir(projectDir).absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(nullptr, "计算失败", "洞室开挖写入 MESG_IAUTO.DAT 失败。");
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "5\n";
    }

    if (!beginCommonCompute(analysisNode, "洞室开挖"))
        return;

    bool ok = true;
    QStringList failedFiles;

    if (!supportPage->exportICaveDatFile(supportCaseSection))
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

            const int targetDim =
                PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
            const QFileInfo sourceInfo(meshSvc->sourceMeshFilePath());
            const bool keepSourceElementIds =
                sourceInfo.exists() &&
                sourceInfo.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;

            QVector<ElemCellPair> elemCells;
            elemCells.reserve(int(grid->GetNumberOfCells()));
            int exportedElementIndex = 0;
            for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (!cell)
                    continue;
                const int cellDim = cell->GetCellDimension();
                if (cellDim != targetDim && cellDim != 1)
                    continue;

                ++exportedElementIndex;
                const int sourceElemId =
                    elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
                const int calculationElemId =
                    keepSourceElementIds ? sourceElemId : exportedElementIndex;
                elemCells.push_back({calculationElemId, cellId});
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

        struct CaveElemContext
        {
            vtkSmartPointer<vtkUnstructuredGrid> grid;
            vtkDataArray* elemIdArr = nullptr;
            vtkDataArray* nodeIdArr = nullptr;
            QHash<int, vtkIdType> sourceElemToCellId;
            QHash<int, int> sourceElemToCalculationElemId;
            QHash<int, vtkIdType> calculationElemToCellId;
            int targetDim = 3;
            bool keepSourceElementIds = false;
        };

        auto buildCaveElemContext = [&]() -> CaveElemContext
        {
            CaveElemContext ctx;
            ctx.targetDim =
                PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();

            auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            if (!meshSvc)
                return ctx;

            const QFileInfo sourceInfo(meshSvc->sourceMeshFilePath());
            ctx.keepSourceElementIds =
                sourceInfo.exists() &&
                sourceInfo.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;

            ctx.grid = meshSvc->getVtkUnstructuredGridSolid();
            if (!ctx.grid || ctx.grid->GetNumberOfCells() <= 0)
                ctx.grid = meshSvc->getVtkUnstructuredGrid();

            if (!ctx.grid || ctx.grid->GetNumberOfCells() <= 0)
                return ctx;

            ctx.elemIdArr =
                vtkDataArray::SafeDownCast(ctx.grid->GetCellData()->GetArray("elem_id"));

            if (ctx.grid->GetPointData())
            {
                ctx.nodeIdArr =
                    vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("node_id"));
                if (!ctx.nodeIdArr)
                    ctx.nodeIdArr =
                        vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("nodeId"));
                if (!ctx.nodeIdArr)
                    ctx.nodeIdArr =
                        vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("point_id"));
                if (!ctx.nodeIdArr)
                    ctx.nodeIdArr =
                        vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("global_node_id"));
            }

            ctx.sourceElemToCellId.reserve(int(ctx.grid->GetNumberOfCells()));
            ctx.sourceElemToCalculationElemId.reserve(int(ctx.grid->GetNumberOfCells()));
            ctx.calculationElemToCellId.reserve(int(ctx.grid->GetNumberOfCells()));

            int exportedElementIndex = 0;
            for (vtkIdType cellId = 0; cellId < ctx.grid->GetNumberOfCells(); ++cellId)
            {
                vtkCell* cell = ctx.grid->GetCell(cellId);
                if (!cell)
                    continue;

                const int cellDim = cell->GetCellDimension();
                if (cellDim != ctx.targetDim && cellDim != 1)
                    continue;

                ++exportedElementIndex;

                // 杆/线单元会导出，但不能作为开挖单元。
                if (cellDim != ctx.targetDim)
                    continue;

                const int sourceElemId =
                    ctx.elemIdArr ? int(ctx.elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
                if (sourceElemId <= 0)
                    continue;

                // 直接导入 IIIN.DAT 时保留原单元号；其他网格导出 IIIN 时按导出顺序重新编号。
                const int calculationElemId =
                    ctx.keepSourceElementIds ? sourceElemId : exportedElementIndex;

                if (!ctx.sourceElemToCellId.contains(sourceElemId))
                    ctx.sourceElemToCellId.insert(sourceElemId, cellId);
                if (!ctx.sourceElemToCalculationElemId.contains(sourceElemId))
                    ctx.sourceElemToCalculationElemId.insert(sourceElemId, calculationElemId);
                if (!ctx.calculationElemToCellId.contains(calculationElemId))
                    ctx.calculationElemToCellId.insert(calculationElemId, cellId);
            }

            return ctx;
        };

        CaveElemContext caveCtx = buildCaveElemContext();

        auto normalizeExcavationElems = [&](const QVector<int>& rawSourceElemIds) -> QVector<int>
        {
            QSet<int> uniq;
            for (int sourceElemId : rawSourceElemIds)
            {
                // 将界面保存的原始 elem_id 转成 IIIN/IIKE 实际使用的计算单元号。
                const auto it = caveCtx.sourceElemToCalculationElemId.constFind(sourceElemId);
                if (it != caveCtx.sourceElemToCalculationElemId.constEnd())
                    uniq.insert(it.value());
            }

            QVector<int> out = uniq.values().toVector();
            std::sort(out.begin(), out.end());
            return out;
        };

        if (!caveCtx.grid || caveCtx.calculationElemToCellId.isEmpty())
        {
            QMessageBox::warning(nullptr, "计算失败", "无法建立洞室开挖单元与 IIIN 单元号的对应关系。");
            return;
        }

        QVector<QVector<int>> calculationElementsByLevel;
        calculationElementsByLevel.reserve(selectedSourceElements.size());
        for (int level = 0; level < selectedSourceElements.size(); ++level)
        {
            const QVector<int> calculationIds =
                normalizeExcavationElems(selectedSourceElements[level]);
            if (calculationIds.size() != selectedSourceElements[level].size())
            {
                QMessageBox::warning(
                    nullptr, "计算失败",
                    QString("第 %1 级开挖体中有单元无法映射到 IIIN.DAT，请重新导入网格后再试。")
                        .arg(level + 1));
                return;
            }
            calculationElementsByLevel.push_back(calculationIds);
        }

        auto patchCaveIiinLayers = [&](const QString& filePath) -> bool
        {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return false;

            QStringList lines =
                QString::fromLocal8Bit(file.readAll())
                    .split(QRegularExpression(QStringLiteral("\\r?\\n")),
                           Qt::KeepEmptyParts);
            file.close();

            if (lines.size() < 2)
                return false;

            static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
            auto integerMatches = [&](const QString& line)
            {
                QVector<QRegularExpressionMatch> out;
                auto it = intRe.globalMatch(line);
                while (it.hasNext())
                    out.push_back(it.next());
                return out;
            };

            const QVector<QRegularExpressionMatch> headerMatches = integerMatches(lines.at(0));
            if (headerMatches.size() < 4)
                return false;

            const int numnp = headerMatches.at(0).captured().toInt();
            const int nelem = headerMatches.at(2).captured().toInt();
            const int manod = headerMatches.at(3).captured().toInt();
            const int firstElemLine = 2 + numnp;
            const int layerTokenIndex = manod + 3;
            if (numnp < 0 || nelem <= 0 || manod <= 0 ||
                lines.size() < firstElemLine + nelem)
                return false;

            QHash<int, int> layerByCalculationElem;
            for (auto it = caveCtx.calculationElemToCellId.constBegin();
                 it != caveCtx.calculationElemToCellId.constEnd(); ++it)
            {
                layerByCalculationElem.insert(it.key(), 1);
            }
            for (int level = 0; level < calculationElementsByLevel.size(); ++level)
            {
                for (int calculationElemId : calculationElementsByLevel[level])
                    layerByCalculationElem[calculationElemId] = -(level + 1);
            }

            int changed = 0;
            for (int i = 0; i < nelem; ++i)
            {
                QString& line = lines[firstElemLine + i];
                const QVector<QRegularExpressionMatch> matches = integerMatches(line);
                if (matches.size() <= layerTokenIndex)
                    return false;

                const int calculationElemId = matches.at(0).captured().toInt();
                const auto layerIt = layerByCalculationElem.constFind(calculationElemId);
                if (layerIt == layerByCalculationElem.constEnd())
                    continue;

                const QRegularExpressionMatch& layerMatch = matches.at(layerTokenIndex);
                QString replacement = QString::number(layerIt.value());
                if (layerMatch.capturedLength() > replacement.size())
                {
                    replacement = replacement.rightJustified(
                        layerMatch.capturedLength(), QLatin1Char(' '));
                }
                line.replace(layerMatch.capturedStart(), layerMatch.capturedLength(), replacement);
                ++changed;
            }

            if (changed != layerByCalculationElem.size())
                return false;

            if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
                return false;
            file.write(lines.join(QStringLiteral("\n")).toLocal8Bit());
            return true;
        };

        if (!patchCaveIiinLayers(wd.filePath("PREP/IIIN.DAT")))
        {
            QMessageBox::warning(
                nullptr, "计算失败",
                "设置 IIIN.DAT 开挖层号失败。普通围岩应为 1，开挖级应为 -1、-2……");
            return;
        }

        // 删除上一轮遗留的开挖文件，避免旧级数文件干扰本次计算。
        QDir prepDirectory(wd.filePath("PREP"));
        const QStringList staleFilters = {
            QStringLiteral("IIKE_E*.DAT"), QStringLiteral("IIKE_T*.DAT"),
            QStringLiteral("IIJX_E*.DAT"), QStringLiteral("IIJX_T*.DAT")};
        for (const QString& staleName :
             prepDirectory.entryList(staleFilters, QDir::Files | QDir::NoSymLinks))
        {
            prepDirectory.remove(staleName);
        }

        auto nodeIdOfPoint = [&](vtkIdType pointId) -> int
        {
            if (caveCtx.nodeIdArr && pointId >= 0 &&
                pointId < caveCtx.nodeIdArr->GetNumberOfTuples())
            {
                const int nodeId = int(caveCtx.nodeIdArr->GetTuple1(pointId));
                if (nodeId > 0)
                    return nodeId;
            }
            return int(pointId + 1);
        };

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

        struct IijxRow
        {
            int ke1 = 0;
            int ke2 = -1;
            QVector<int> nodeIds;
            int jat = 0;
        };

        auto writeIijx = [&](const QString& fileName, const QVector<int>& kes, int iex1,
                             int iex2) -> bool
        {
            QFile f(wd.filePath("PREP/" + fileName));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;

            QVector<IijxRow> rows;

            if (caveCtx.grid)
            {
                QHash<vtkIdType, int> cellToCalculationElemId;
                cellToCalculationElemId.reserve(caveCtx.calculationElemToCellId.size());
                for (auto it = caveCtx.calculationElemToCellId.constBegin();
                     it != caveCtx.calculationElemToCellId.constEnd(); ++it)
                {
                    cellToCalculationElemId.insert(it.value(), it.key());
                }

                if (caveCtx.targetDim == 2)
                {
                    struct EdgeOwner
                    {
                        int ke = 0;
                        QVector<vtkIdType> pointIds;
                    };

                    std::map<std::vector<vtkIdType>, std::vector<EdgeOwner>> edgeOwners;

                    for (vtkIdType cellId = 0; cellId < caveCtx.grid->GetNumberOfCells();
                         ++cellId)
                    {
                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int ownerKe = cellToCalculationElemId.value(cellId, 0);
                        if (ownerKe <= 0)
                            continue;

                        const int edgeCount = cell->GetNumberOfEdges();
                        for (int edgeId = 0; edgeId < edgeCount; ++edgeId)
                        {
                            vtkCell* edge = cell->GetEdge(edgeId);
                            if (!edge || !edge->GetPointIds() ||
                                edge->GetPointIds()->GetNumberOfIds() < 2)
                                continue;

                            EdgeOwner owner;
                            owner.ke = ownerKe;
                            owner.pointIds = {edge->GetPointId(0), edge->GetPointId(1)};

                            std::vector<vtkIdType> key{
                                owner.pointIds[0], owner.pointIds[1]};
                            std::sort(key.begin(), key.end());
                            edgeOwners[key].push_back(owner);
                        }
                    }

                    QSet<int> selectedKes;
                    for (int ke : kes)
                        selectedKes.insert(ke);

                    std::set<std::vector<vtkIdType>> writtenEdges;
                    for (int ke : kes)
                    {
                        const vtkIdType cellId =
                            caveCtx.calculationElemToCellId.value(ke, -1);
                        if (cellId < 0)
                            continue;

                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int edgeCount = cell->GetNumberOfEdges();
                        for (int edgeId = 0; edgeId < edgeCount; ++edgeId)
                        {
                            vtkCell* edge = cell->GetEdge(edgeId);
                            if (!edge || !edge->GetPointIds() ||
                                edge->GetPointIds()->GetNumberOfIds() < 2)
                                continue;

                            const vtkIdType p1 = edge->GetPointId(0);
                            const vtkIdType p2 = edge->GetPointId(1);
                            std::vector<vtkIdType> key{p1, p2};
                            std::sort(key.begin(), key.end());

                            if (writtenEdges.find(key) != writtenEdges.end())
                                continue;

                            const auto ownerIt = edgeOwners.find(key);
                            if (ownerIt == edgeOwners.end())
                                continue;

                            bool hasOtherSelectedOwner = false;
                            int oppositeKe = -1;
                            for (const EdgeOwner& owner : ownerIt->second)
                            {
                                if (owner.ke == ke)
                                    continue;

                                if (selectedKes.contains(owner.ke))
                                {
                                    hasOtherSelectedOwner = true;
                                    break;
                                }

                                if (oppositeKe < 0)
                                    oppositeKe = owner.ke;
                            }

                            if (hasOtherSelectedOwner)
                                continue;

                            writtenEdges.insert(key);

                            IijxRow row;
                            row.ke1 = ke;
                            row.ke2 = oppositeKe;
                            row.nodeIds = {nodeIdOfPoint(p1), nodeIdOfPoint(p2)};
                            rows.push_back(row);
                        }
                    }
                }
                else
                {
                    struct FaceOwner
                    {
                        int ke = 0;
                        QVector<vtkIdType> pointIds;
                    };

                    std::map<std::vector<vtkIdType>, std::vector<FaceOwner>> faceOwners;

                    for (vtkIdType cellId = 0; cellId < caveCtx.grid->GetNumberOfCells();
                         ++cellId)
                    {
                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int ownerKe = cellToCalculationElemId.value(cellId, 0);
                        if (ownerKe <= 0)
                            continue;

                        const int faceCount = cell->GetNumberOfFaces();
                        for (int faceId = 0; faceId < faceCount; ++faceId)
                        {
                            vtkCell* face = cell->GetFace(faceId);
                            if (!face || !face->GetPointIds())
                                continue;

                            const vtkIdType faceNodeCount =
                                face->GetPointIds()->GetNumberOfIds();
                            if (faceNodeCount < 3 || faceNodeCount > 4)
                                continue;

                            FaceOwner owner;
                            owner.ke = ownerKe;
                            owner.pointIds.reserve(int(faceNodeCount));

                            std::vector<vtkIdType> key;
                            key.reserve(size_t(faceNodeCount));
                            for (vtkIdType i = 0; i < faceNodeCount; ++i)
                            {
                                const vtkIdType pid = face->GetPointId(i);
                                owner.pointIds.push_back(pid);
                                key.push_back(pid);
                            }
                            std::sort(key.begin(), key.end());
                            faceOwners[key].push_back(owner);
                        }
                    }

                    QSet<int> selectedKes;
                    for (int ke : kes)
                        selectedKes.insert(ke);

                    std::set<std::vector<vtkIdType>> writtenFaces;
                    for (int ke : kes)
                    {
                        const vtkIdType cellId =
                            caveCtx.calculationElemToCellId.value(ke, -1);
                        if (cellId < 0)
                            continue;

                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int faceCount = cell->GetNumberOfFaces();
                        for (int faceId = 0; faceId < faceCount; ++faceId)
                        {
                            vtkCell* face = cell->GetFace(faceId);
                            if (!face || !face->GetPointIds())
                                continue;

                            const vtkIdType faceNodeCount =
                                face->GetPointIds()->GetNumberOfIds();
                            if (faceNodeCount < 3 || faceNodeCount > 4)
                                continue;

                            QVector<vtkIdType> orientedPointIds;
                            orientedPointIds.reserve(int(faceNodeCount));
                            std::vector<vtkIdType> key;
                            key.reserve(size_t(faceNodeCount));

                            for (vtkIdType i = 0; i < faceNodeCount; ++i)
                            {
                                const vtkIdType pid = face->GetPointId(i);
                                orientedPointIds.push_back(pid);
                                key.push_back(pid);
                            }
                            std::sort(key.begin(), key.end());

                            if (writtenFaces.find(key) != writtenFaces.end())
                                continue;

                            const auto ownerIt = faceOwners.find(key);
                            if (ownerIt == faceOwners.end())
                                continue;

                            bool hasOtherSelectedOwner = false;
                            int oppositeKe = -1;
                            for (const FaceOwner& owner : ownerIt->second)
                            {
                                if (owner.ke == ke)
                                    continue;

                                if (selectedKes.contains(owner.ke))
                                {
                                    hasOtherSelectedOwner = true;
                                    break;
                                }

                                if (oppositeKe < 0)
                                    oppositeKe = owner.ke;
                            }

                            // 两侧都属于开挖体的是内部面，不应写入 IIJX。
                            if (hasOtherSelectedOwner)
                                continue;

                            writtenFaces.insert(key);

                            IijxRow row;
                            row.ke1 = ke;
                            row.ke2 = oppositeKe;
                            row.nodeIds.reserve(int(faceNodeCount));
                            for (vtkIdType pid : orientedPointIds)
                                row.nodeIds.push_back(nodeIdOfPoint(pid));

                            // 四面体边界面保留 3 个节点；六面体边界面保留 4 个节点。
                            // MAKOD 必须等于 2 + 实际面节点数，否则 EXCAIIWL 会在 IIWL 中
                            // 生成错误的 NFAJD。
                            if (row.nodeIds.size() < 3 || row.nodeIds.size() > 4)
                                continue;

                            rows.push_back(row);
                        }
                    }
                }
            }

            int faceNodeCount = (caveCtx.targetDim == 2 ? 2 : 0);
            for (const IijxRow& row : rows)
                faceNodeCount = qMax(faceNodeCount, row.nodeIds.size());

            if (caveCtx.targetDim == 3 && faceNodeCount == 0)
                faceNodeCount = 3;

            // IIJX 的 MAKOD = KE1、KE2 两列 + 实际面节点数。
            // 四面体：3 节点面 -> MAKOD=5 -> 后续 IIWL 中 NFAJD=3。
            // 六面体：4 节点面 -> MAKOD=6 -> 后续 IIWL 中 NFAJD=4。
            const int makod = faceNodeCount + 2;

            // 若同一文件同时存在三角面和四边形面，统一按最大节点数输出；
            // 只有这种混合情况才补重复节点。
            if (caveCtx.targetDim == 3 && faceNodeCount == 4)
            {
                for (IijxRow& row : rows)
                {
                    while (row.nodeIds.size() < 4 && !row.nodeIds.isEmpty())
                        row.nodeIds.push_back(row.nodeIds.last());
                }
            }

            QTextStream os(&f);
            os << QString("%1%2     NJX   MAKOD\n")
                      .arg(rows.size(), 9)
                      .arg(makod, 9);

            if (caveCtx.targetDim == 2)
                os << "    KJX     KE1     KE2     JD1     JD2  JAT\n";
            else if (faceNodeCount == 3)
                os << "    KJX     KE1     KE2     JD1     JD2     JD3  JAT\n";
            else
                os << "    KJX     KE1     KE2     JD1     JD2     JD3     JD4  JAT\n";

            for (int i = 0; i < rows.size(); ++i)
            {
                const IijxRow& row = rows[i];

                os << QString("%1%2%3")
                          .arg(i + 1, 9)
                          .arg(row.ke1, 9)
                          .arg(row.ke2, 9);

                for (int nodeId : row.nodeIds)
                    os << QString("%1").arg(nodeId, 9);

                // 通用 READ_IIJX 会读取 JAT；ARCHFTVECT 在读满所需字段后会丢弃本行尾部 JAT。
                os << QString("%1\n").arg(row.jat, 10);
            }

            os << QString("    DIR=%1                                                    \n").arg(prepDir);
            os << " ISHIFT=       2\n";
            os << QString("   NJX=%1\n").arg(rows.size(), 8);
            os << QString(" IEX1=%1  IEX2=%2  \n").arg(iex1, 4).arg(iex2, 4);
            return !rows.isEmpty();
        };

        QSet<int> cumulative;
        for (int i = 0; i < excavationBodies.size(); ++i)
        {
            const QVector<int>& e = calculationElementsByLevel[i];

            const QString idx = QString("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
            const QString eFile = QString("IIKE_E%1-%1.DAT").arg(idx);
            if (!writeIike(eFile, e, -(i + 1), -(i + 1)))
            {
                ok = false;
                failedFiles << eFile;
            }

            const QString ejxFile = QString("IIJX_E%1-%1.DAT").arg(idx);
            if (!writeIijx(ejxFile, e, -(i + 1), -(i + 1)))
            {
                ok = false;
                failedFiles << ejxFile;
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

            const QString tjxFile = QString("IIJX_T01-%1.DAT").arg(idx);
            if (!writeIijx(tjxFile, t, -1, -(i + 1)))
            {
                ok = false;
                failedFiles << tjxFile;
            }
        }


        auto hasPositiveRecordCount = [&](const QString& fileName) -> bool
        {
            QFile file(wd.filePath("PREP/" + fileName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return false;
            const QString firstLine = QString::fromLocal8Bit(file.readLine());
            const QRegularExpressionMatch match =
                QRegularExpression(QStringLiteral("[-+]?\\d+")).match(firstLine);
            return match.hasMatch() && match.captured().toInt() > 0;
        };

        const QString lastIndex =
            QString("%1").arg(excavationBodies.size(), 2, 10, QLatin1Char('0'));
        const QString expectedTotalIike = QString("IIKE_T01-%1.DAT").arg(lastIndex);
        const QString expectedTotalIijx = QString("IIJX_T01-%1.DAT").arg(lastIndex);
        if (!hasPositiveRecordCount(expectedTotalIike))
        {
            ok = false;
            failedFiles << expectedTotalIike + QStringLiteral("（不存在或无开挖单元）");
        }
        if (!QFileInfo::exists(wd.filePath("PREP/" + expectedTotalIijx)))
        {
            ok = false;
            failedFiles << expectedTotalIijx;
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
    catch (const std::exception& e)
    {
        qWarning() << "洞室开挖计算失败:" << e.what();
    }
}

void Solver::onComputeSlopeStability(ProjectNode* analysisNode)
{
    try
    {
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile iautoFile(QDir(projectDir).absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(nullptr, "计算失败", "边坡分析写入 MESG_IAUTO.DAT 失败。");
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "8\n";
    }

    if (!beginCommonCompute(analysisNode, "边坡分析"))
        return;

    if (!forceSlopeIiinLayersToOne(projectDir))
    {
        QMessageBox::warning(nullptr, "计算失败",
                             "边坡分析设置 IIIN.DAT 分层为 1 失败。");
        return;
    }

    auto* slopePage = dynamic_cast<SlopeStabilityWidget*>(analysisNode->getPage());
    if (slopePage && slopePage->exportIslopDatFile())
    {
        QMessageBox::information(nullptr, "导出成功", "边坡分析输入文件导出成功。");
        launchSolverExecutable("边坡分析");
    }
    else
        QMessageBox::warning(nullptr, "导出失败", "ISLOP.DAT 导出失败。");
    }
    catch (const std::exception& e)
    {
        qWarning() << "边坡分析计算失败:" << e.what();
    }
}

void Solver::onComputeGeoStressRegression(ProjectNode* analysisNode)
{
    try
    {
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile iautoFile(QDir(projectDir).absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(nullptr, "计算失败", "地应力回归计算写入 MESG_IAUTO.DAT 失败。");
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "6\n";
    }

    if (!beginCommonCompute(analysisNode, "地应力回归计算"))
        return;

    auto* page = dynamic_cast<GeoStressRegressionWidget*>(analysisNode->getPage());
    const GeoStressRegressionWidget::ExportPayload payload = buildGeoStressPayload(analysisNode);
    if (page && page->calculationModeCode() == 1 && payload.measureRows.isEmpty())
    {
        QMessageBox::warning(nullptr, "计算失败",
                             "已选择代码 1（实测地应力多元回归），但没有定义任何实测点。");
        return;
    }

    if (page && page->exportIGEOSDatFile(payload))
    {
        QMessageBox::information(nullptr, "导出成功", "地应力回归计算输入文件导出成功。");
        launchSolverExecutable("地应力回归计算");
    }
    else
        QMessageBox::warning(nullptr, "导出失败", "IGEOS.DAT 导出失败。");
    }
    catch (const std::exception& e)
    {
        qWarning() << "地应力回归计算失败:" << e.what();
    }
}

void Solver::onComputeCaveBlockStability(ProjectNode* analysisNode)
{
    try
    {
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
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
        launchCaveBlockSolverExecutable();
    }
    else
        QMessageBox::warning(nullptr, "导出失败",
                             "生成滑块文件失败，请检查 SliderCase 目录与参数设置。");
    }
    catch (const std::exception& e)
    {
        qWarning() << "洞室块体稳定性计算失败:" << e.what();
    }
}


void Solver::connectStopRequest(QObject* page) const
{
    if (!page || page->metaObject()->indexOfSignal("stopRequested()") < 0)
        return;

    connect(page, SIGNAL(stopRequested()), this, SLOT(stopSolverExecutable()), Qt::UniqueConnection);
}

void Solver::setComputeWidgetRunning(QWidget* widget, bool computing) const
{
    if (!widget)
        return;

    QMetaObject::invokeMethod(widget, "setComputing", Qt::DirectConnection, Q_ARG(bool, computing));
}

void Solver::finishSolverProcess(bool terminateProcess)
{
    if (terminateProcess)
        QProcess::execute("taskkill", {"-f", "-im", "PCCAE_CPU.exe"});

    if (m_solverProcess)
    {
        m_solverProcess->disconnect(this);
        if (terminateProcess && m_solverProcess->state() != QProcess::NotRunning)
        {
            m_solverProcess->kill();
            m_solverProcess->waitForFinished(3000);
        }
        m_solverProcess->deleteLater();
        m_solverProcess = nullptr;
    }

    setComputeWidgetRunning(m_activeComputeWidget, false);
    m_activeComputeWidget = nullptr;
}

void Solver::stopSolverExecutable()
{
    finishSolverProcess(true);
}

bool Solver::beginCommonCompute(ProjectNode* analysisNode, const QString& analysisType)
{
    try
    {
    if (!analysisNode)
        return false;

    if (!analysisNode->getPage())
    {
        QMessageBox::warning(nullptr, "计算失败",
                             QString("%1页面不存在，无法执行计算。").arg(analysisType));
        return false;
    }

    if (m_solverProcess)
        finishSolverProcess(true);
    else
        QProcess::execute("taskkill", {"-f", "-im", "PCCAE_CPU.exe"});

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
    catch (const std::exception& e)
    {
        qWarning() << "beginCommonCompute 失败:" << e.what();
        return false;
    }
}


bool Solver::launchCaveBlockSolverExecutable()
{
    const QString exePath =
        QDir::toNativeSeparators(
            QDir(QCoreApplication::applicationDirPath())
                .filePath("solver/PCCAE_CPU/PCCAE_CPU.exe"));
    if (!QFileInfo::exists(exePath))
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             QString("块体稳定分析导出成功，但未找到求解器：\n%1")
                                 .arg(exePath));
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString sliderDir = QDir(projectDir).absoluteFilePath("SliderCase");
    if (!QDir(sliderDir).exists())
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             QString("块体稳定分析缺少 SliderCase 目录：\n%1").arg(sliderDir));
        return false;
    }

    if (!updateCaveBlockIiiDirDat(sliderDir))
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             "块体稳定分析更新 IIIDIR.DAT 失败。");
        return false;
    }

    if (m_solverProcess)
        finishSolverProcess(true);
    else
        QProcess::execute("taskkill", {"-f", "-im", "PCCAE_CPU.exe"});

    m_activeComputeWidget = m_pendingComputeWidget;
    m_pendingComputeWidget = nullptr;

    m_solverProcess = new QProcess(this);
    // 滑块计算仍使用 PCCAE_CPU.exe；运行前把 D:/IIIDIR.DAT 指向 SliderCase
    // 并写入滑块需要的 6 工况，使求解器进入滑块流程而不是 MESG_IAUTO 自动分发流程。
    m_solverProcess->setWorkingDirectory(QDir(sliderDir).absolutePath());
    m_solverProcess->setProgram(exePath);
    m_solverProcess->setArguments(QStringList());
    m_solverProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_solverProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_solverProcess)
            return;
        const QString output = QString::fromLocal8Bit(m_solverProcess->readAllStandardOutput()).trimmed();
        if (!output.isEmpty())
        {
            qInfo().noquote() << ("__RAW__" + output);
            if (output.contains("结束进程"))
                finishSolverProcess(true);
        }
    });

    connect(m_solverProcess, &QProcess::errorOccurred, this,
            [](QProcess::ProcessError) {});

    connect(m_solverProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                finishSolverProcess(false);
            });

    m_solverProcess->start();
    if (!m_solverProcess->waitForStarted(5000))
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             QString("块体稳定分析启动求解器进程失败：%1")
                                 .arg(m_solverProcess->errorString()));

        finishSolverProcess(false);
        return false;
    }

    setComputeWidgetRunning(m_activeComputeWidget, true);
    return true;
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
    if (m_solverProcess)
        finishSolverProcess(true);
    else
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

        int verticalAxis = 2;
        if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            verticalAxis = meshSvc->verticalAxis();

        if (!updatePrepDatFiles(projectDir, solverMode, verticalAxis))
        {
            QMessageBox::warning(nullptr, "启动求解器失败",
                                 QString("%1更新 PREP/DAT 文件失败。").arg(analysisType));
            return false;
        }
    }

    m_activeComputeWidget = m_pendingComputeWidget;
    m_pendingComputeWidget = nullptr;

    m_solverProcess = new QProcess(this);
    m_solverProcess->setWorkingDirectory(QDir(projectDir).absolutePath());
    m_solverProcess->setProgram(exePath);
    m_solverProcess->setArguments(QStringList());
    m_solverProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_solverProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_solverProcess)
            return;
        const QString output = QString::fromLocal8Bit(m_solverProcess->readAllStandardOutput()).trimmed();
        if (!output.isEmpty())
        {
            qInfo().noquote() << ("__RAW__" + output);
            if (output.contains("结束进程"))
                finishSolverProcess(true);
        }
    });

    connect(m_solverProcess, &QProcess::errorOccurred, this,
            [](QProcess::ProcessError) {});

    connect(m_solverProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                finishSolverProcess(false);
            });

    m_solverProcess->start();
    if (!m_solverProcess->waitForStarted(5000))
    {
        QMessageBox::warning(nullptr, "启动求解器失败",
                             QString("%1启动求解器进程失败：%2")
                                 .arg(analysisType, m_solverProcess->errorString()));
        
        finishSolverProcess(false);
        return false;
    }

    setComputeWidgetRunning(m_activeComputeWidget, true);
    return true;
}

int Solver::solverModeFromAnalysisType(const QString& analysisType) const
{
    if (analysisType.contains("渗流") || analysisType.contains("土石坝"))
        return 8;
    if (analysisType.contains("洞室") ||
        analysisType.contains("地应力") ||
        analysisType.contains("开挖")) {
        return 6;
    }
    if (analysisType.contains("边坡"))
        return 7;
    return 0;
}


bool Solver::updateCaveBlockIiiDirDat(const QString& sliderDir) const
{
    QFile file("D:/IIIDIR.DAT");
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    QString nativeSliderDir = QDir::toNativeSeparators(QDir(sliderDir).absolutePath());
    if (!nativeSliderDir.endsWith('\\'))
        nativeSliderDir += "\\";

    lines[0] = QString("'%1'").arg(nativeSliderDir);

    bool updatedModeLine = false;
    for (int i = lines.size() - 1; i >= 1; --i)
    {
        const QString trimmed = lines[i].trimmed();
        const bool quotedPath = trimmed.startsWith("'") && trimmed.endsWith("'") &&
                                (trimmed.contains('\\') || trimmed.contains('/'));
        if (quotedPath)
        {
            lines[i] = QString("'%1'").arg(nativeSliderDir);
            if (i + 1 < lines.size())
            {
                bool ok = false;
                lines[i + 1].trimmed().toInt(&ok);
                if (ok)
                {
                    lines[i + 1] = QString("%1   ").arg(6, 12);
                    updatedModeLine = true;
                }
            }
            break;
        }
    }

    if (!updatedModeLine)
        return false;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::updateIiiDirDat(const QString& projectDir, int solverMode) const
{
    QFile file("D:/IIIDIR.DAT");
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    const QString nativeProjectDir = QDir::toNativeSeparators(QDir(projectDir).absolutePath());
    const QString projectWithSlash = QDir(nativeProjectDir).absolutePath() + QDir::separator();
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

bool Solver::updatePrepDatFiles(const QString& projectDir, int solverMode, int verticalAxis) const
{
    QFile file(QDir(projectDir).filePath("PREP/IIIN.DAT"));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    const int safeVerticalAxis = (verticalAxis >= 1 && verticalAxis <= 3) ? verticalAxis : 2;
    const QRegularExpression headerRe(R"(^\s*\d+\s+\d+)");
    const QRegularExpressionMatch match = headerRe.match(lines[1]);
    if (match.hasMatch())
    {
        const QString replacement = QString("%1%2").arg(solverMode, 8).arg(safeVerticalAxis, 8);
        lines[1].replace(0, match.capturedLength(0), replacement);
    }
    else
    {
        lines[1].replace(QRegularExpression(R"(^\s*\d+)"), QString("%8").arg(solverMode));
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::forceSlopeIiinLayersToOne(const QString& projectDir) const
{
    QFile file(QDir(projectDir).filePath("PREP/IIIN.DAT"));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();

    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
    auto intsInLine = [](const QString& line) -> QVector<int>
    {
        QVector<int> values;
        QRegularExpressionMatchIterator it = intRe.globalMatch(line);
        while (it.hasNext())
        {
            bool ok = false;
            const int value = it.next().captured(0).toInt(&ok);
            if (ok)
                values.push_back(value);
            if (values.size() >= 5)
                break;
        }
        return values;
    };

    const QVector<int> header = intsInLine(lines.at(0));
    if (header.size() < 4)
        return false;

    const int numnp = header.at(0);
    const int nelem = header.at(2);
    const int manod = header.at(3);
    if (numnp < 0 || nelem < 0 || manod <= 0)
        return false;

    const int firstElemLine = 2 + numnp;
    if (firstElemLine < 0 || lines.size() < firstElemLine + nelem)
        return false;

    const int layerTokenIndex = manod + 3; // elem id + manod nodes + KETYPE + MATYPE + LAYER
    for (int i = 0; i < nelem; ++i)
    {
        QString& line = lines[firstElemLine + i];

        QVector<QRegularExpressionMatch> matches;
        QRegularExpressionMatchIterator it = intRe.globalMatch(line);
        while (it.hasNext())
            matches.push_back(it.next());

        if (matches.size() <= layerTokenIndex)
            return false;

        const QRegularExpressionMatch& layerMatch = matches.at(layerTokenIndex);
        QString replacement = QStringLiteral("1");
        if (layerMatch.capturedLength(0) > replacement.size())
            replacement = replacement.rightJustified(layerMatch.capturedLength(0), QLatin1Char(' '));
        line.replace(layerMatch.capturedStart(0), layerMatch.capturedLength(0), replacement);
    }

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

int Solver::nextCaveBlockStabilityIndex() const
{
    if (!m_root)
        return 1;

    int maxIdx = 0;
    QRegularExpression re(QString(R"(^块体稳定计算\((\d+)\)$)"));

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
