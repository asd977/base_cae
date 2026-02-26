#include "Solver.h"

#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"

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
#include <algorithm>
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

    m_root = new ProjectNode("计算分析", QUuid::createUuid().toString(), nullptr);
    ProjectManagerService* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
}

bool Solver::saveProject()
{
    if (!m_root) return true;

    QJsonObject root;
    root["module"] = "Solver";
    root["root_title"] = "计算分析";

    QJsonArray analysisItems;
    for (ProjectNode* analysisNode : m_root->getChildren())
    {
        if (!analysisNode) continue;

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
            if (!child || !child->getPage()) continue;

            auto* boundaryPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage());
            if (boundaryPage)
            {
                boundaryPage->exportDatFile();
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

    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSolverProjectFileName));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    return true;
}

bool Solver::loadProject()
{
    newProject();
    if (!m_root) return false;

    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSolverProjectFileName));
    if (!file.open(QIODevice::ReadOnly)) return true;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& item : root.value("items").toArray())
    {
        const QJsonObject analysisObj = item.toObject();
        auto* seepagePage = new SeepageWidget();
        auto* analysisNode = new ProjectNode(analysisObj.value("title").toString("渗流分析"),
                                             analysisObj.value("uuid").toString(QUuid::createUuid().toString()),
                                             seepagePage);
        seepagePage->loadJson(analysisObj.value("state").toObject());
        connect(seepagePage, &SeepageWidget::computeRequested, this, [this, analysisNode]() {
            onComputeSeepage(analysisNode);
        });

        for (const auto& childV : analysisObj.value("children").toArray())
        {
            const QJsonObject childObj = childV.toObject();
            const QString type = childObj.value("type").toString();

            SeepageBoundaryPage* page = nullptr;
            if (type == "SeepageBoundaryUpstreamWater")
            {
                page = new SeepageBoundaryPage(type, "上游迎水面", "JD_UPSEEP.DAT");
            }
            else if (type == "SeepageBoundaryDownstreamWater")
            {
                page = new SeepageBoundaryPage(type, "下游迎水面", "JD_DWSEEP.DAT");
            }
            if (!page) continue;

            page->loadJson(childObj.value("state").toObject());
            auto* node = new ProjectNode(childObj.value("title").toString("边界"),
                                         childObj.value("uuid").toString(QUuid::createUuid().toString()),
                                         page);
            analysisNode->addChild(node);
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

    m_root->addChild(analysisNode);
}


void Solver::onComputeSeepage(ProjectNode* analysisNode)
{
    if (!analysisNode) return;

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
        if (!boundaryPage) continue;

        const bool oneOk = boundaryPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk) failedFiles << child->getName();
    }

    if (ok)
    {
        QMessageBox::information(nullptr, "导出成功", "渗流计算输入文件导出成功（PREP/）。");
    }
    else
    {
        QMessageBox::warning(nullptr,
                             "导出失败",
                             QString("以下文件导出失败：\n%1").arg(failedFiles.join("\n")));
    }
}

int Solver::nextSeepageIndex() const
{
    if (!m_root) return 1;

    int maxIdx = 0;
    QRegularExpression re(QStringLiteral(R"(^渗流分析\((\d+)\)$)"));

    for (ProjectNode* child : m_root->getChildren())
    {
        if (!child) continue;
        const QString name = child->getName();
        auto m = re.match(name);
        if (m.hasMatch())
        {
            bool ok = false;
            int idx = m.captured(1).toInt(&ok);
            if (ok) maxIdx = std::max(maxIdx, idx);
        }
    }

    return maxIdx + 1;
}
