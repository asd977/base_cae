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
#include <QStringList>
#include <algorithm>

#include "FillStorageCyclePage.h"
#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"
#include "StressBoundaryPage.h"
#include "DamAnalysisWidget.h"
#include "GeoStressRegressionWidget.h"
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
                analysisObj.value("title").toString("边坡稳定分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), slopePage);
            slopePage->loadJson(analysisObj.value("state").toObject());
            connect(slopePage, &SlopeStabilityWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSlopeStability(analysisNode);
                    });
            analysisPage = slopePage;
        }
        else if (analysisType == "GeoStressRegressionAnalysis")
        {
            auto* geoStressPage = new GeoStressRegressionWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("地应力回归计算"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), geoStressPage);
            geoStressPage->loadJson(analysisObj.value("state").toObject());
            connect(geoStressPage, &GeoStressRegressionWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeGeoStressRegression(analysisNode);
                    });
            analysisPage = geoStressPage;
        }

        if (!analysisNode || !analysisPage)
            continue;

        bool hasFillStorageCycleNode = false;
        for (const auto& childV : analysisObj.value("children").toArray())
        {
            const QJsonObject childObj = childV.toObject();
            const QString type = childObj.value("type").toString();

            BasePage* page = nullptr;
            if (type == "SeepageBoundaryUpstreamWater")
                page = new SeepageBoundaryPage(type, "上游水头边界", "JD_UPSEEP.DAT");
            else if (type == "SeepageBoundaryDownstreamWater")
                page = new SeepageBoundaryPage(type, "下游水头边界", "JD_DWSEEP.DAT");
            else if (type == "StressBoundaryDamUpstream")
                page = new StressBoundaryPage(type, "上游水头边界", "FA_DAMUP.DAT");
            else if (type == "StressBoundaryDamMid")
                page = new StressBoundaryPage(type, "坝体中心面节点", "FA_DAMMID.DAT");
            else if (type == "FillStorageCycle")
            {
                page = new FillStorageCyclePage();
                hasFillStorageCycleNode = true;
            }

            if (!page)
                continue;

            auto* node = new ProjectNode(
                childObj.value("title").toString("边界"),
                childObj.value("uuid").toString(QUuid::createUuid().toString()), page);
            if (type == "FillStorageCycle")
                setupNodeRole(node, "FillStorageCycle");
            else if (type.startsWith("StressBoundary"))
                setupNodeRole(node, "StressBoundary");
            else if (type.startsWith("SeepageBoundary"))
                setupNodeRole(node, "SeepageBoundary");
            page->loadJson(childObj.value("state").toObject());
            analysisNode->addChild(node);
        }

        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis") && !hasFillStorageCycleNode)
        {
            addFillStorageCycleNode(analysisNode, false);
        }

        if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            setupNodeRole(analysisNode, "DamAnalysis");
        else if (analysisType == "SeepageAnalysis")
            setupNodeRole(analysisNode, "SeepageAnalysis");
        else if (analysisType == "SlopeStabilityAnalysis")
            setupNodeRole(analysisNode, "SlopeStabilityAnalysis");
        else if (analysisType == "GeoStressRegressionAnalysis")
            setupNodeRole(analysisNode, "GeoStressRegressionAnalysis");

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
    em.registerHandler(PublicConstants::kActionSlopeStabilityKey,
                       [this](const QVariantMap&)
                       {
                           onSlopeStabilityAction();
                       });
    em.registerHandler(PublicConstants::kActionGeoStressRegressionKey,
                       [this](const QVariantMap&)
                       {
                           onGeoStressRegressionAction();
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
        },
        0);
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
    addBoundary("坝体中心面节点", "StressBoundaryDamMid", "FA_DAMMID.DAT");

    addFillStorageCycleNode(analysisNode, false);
    syncDamAnalysisMCase(analysisNode);

    m_root->addChild(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onSlopeStabilityAction()
{
    if (!m_root)
        return;

    auto* slopePage = new SlopeStabilityWidget();
    const QString analysisName = QString("边坡稳定分析(%1)").arg(nextSlopeIndex());
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

void Solver::onGeoStressRegressionAction()
{
    if (!m_root)
        return;

    auto* page = new GeoStressRegressionWidget();
    const QString analysisName = QString("地应力回归计算(%1)").arg(nextGeoStressRegressionIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), page);
    setupNodeRole(analysisNode, "GeoStressRegressionAnalysis");

    connect(page, &GeoStressRegressionWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeGeoStressRegression(analysisNode);
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

void Solver::onComputeSlopeStability(ProjectNode* analysisNode)
{
    if (!analysisNode)
        return;

    auto* slopePage = dynamic_cast<SlopeStabilityWidget*>(analysisNode->getPage());
    if (slopePage && slopePage->exportIslopDatFile())
        QMessageBox::information(nullptr, "导出成功", "边坡稳定分析输入文件导出成功（ISLOP.DAT）。");
    else
        QMessageBox::warning(nullptr, "导出失败", "ISLOP.DAT 导出失败。");
}

void Solver::onComputeGeoStressRegression(ProjectNode* analysisNode)
{
    if (!analysisNode)
        return;

    auto* page = dynamic_cast<GeoStressRegressionWidget*>(analysisNode->getPage());
    if (page && page->exportIGEOSDatFile())
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
    QRegularExpression re(QString(R"(^边坡稳定分析\((\d+)\)$)"));

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

int Solver::nextGeoStressRegressionIndex() const
{
    if (!m_root)
        return 1;

    int maxIdx = 0;
    QRegularExpression re(QString(R"(^地应力回归计算\((\d+)\)$)"));

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
