#include "Solver.h"

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
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

#include "FillStorageCyclePage.h"
#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"
#include "StressBoundaryPage.h"
#include "DamAnalysisWidget.h"

Solver::Solver()
{
    ServiceRegistry::instance().registerService<SolverService>(this);
    initAction();
}

void Solver::newProject()
{
    if (m_root)
        return;

    m_root = new ProjectNode("计算分析", QUuid::createUuid().toString(), nullptr);
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

        if (!analysisNode || !analysisPage)
            continue;

        bool hasFillStorageCycleNode = false;
        for (const auto& childV : analysisObj.value("children").toArray())
        {
            const QJsonObject childObj = childV.toObject();
            const QString type = childObj.value("type").toString();

            BasePage* page = nullptr;
            if (type == "SeepageBoundaryUpstreamWater")
                page = new SeepageBoundaryPage(type, "上游迎水面", "JD_UPSEEP.DAT");
            else if (type == "SeepageBoundaryDownstreamWater")
                page = new SeepageBoundaryPage(type, "下游迎水面", "JD_DWSEEP.DAT");
            else if (type == "StressBoundaryDamUpstream")
                page = new StressBoundaryPage(type, "上游迎水面", "FA_DAMUP.DAT");
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
            page->loadJson(childObj.value("state").toObject());
            analysisNode->addChild(node);
        }

        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis") && !hasFillStorageCycleNode)
        {
            auto* fillStorageCyclePage = new FillStorageCyclePage();
            auto* fillStorageCycleNode = new ProjectNode(
                "填筑-蓄水周期", QUuid::createUuid().toString(), fillStorageCyclePage);
            analysisNode->addChild(fillStorageCycleNode);
        }

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
}

void Solver::onSeepageAction()
{
    if (!m_root)
        return;

    auto* seepagePage = new SeepageWidget();
    const QString analysisName = QString("渗流分析(%1)").arg(nextSeepageIndex());
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), seepagePage);

    connect(seepagePage, &SeepageWidget::computeRequested, this,
            [this, analysisNode]()
            {
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

    connect(stressPage, &DamAnalysisWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeStressStrain(analysisNode);
            });

    auto addBoundary = [&](const QString& name, const QString& pageType, const QString& fileName)
    {
        auto* page = new StressBoundaryPage(pageType, name, fileName);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        analysisNode->addChild(node);
    };

    addBoundary("上游迎水面", "StressBoundaryDamUpstream", "FA_DAMUP.DAT");
    addBoundary("坝体中心面节点", "StressBoundaryDamMid", "FA_DAMMID.DAT");

    auto* fillStorageCyclePage = new FillStorageCyclePage();
    auto* fillStorageCycleNode =
        new ProjectNode("填筑-蓄水周期", QUuid::createUuid().toString(), fillStorageCyclePage);
    analysisNode->addChild(fillStorageCycleNode);

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
        cycleSections << fillStorageCyclePage->sectionText();
    }
    cycleSectionText = cycleSections.join("\n");

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
