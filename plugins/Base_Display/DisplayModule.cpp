#include "DisplayModule.h"

#include <EventManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QUuid>

#include "DisplayInfoPage.h"

DisplayModule::DisplayModule()
{
    auto& em = EventManager::instance();
    em.registerHandler(PublicConstants::kActionNewProject, this,
                       [this](const QVariantMap&)
                       {
                           clearTree();
                           initializeTree();
                       });
    em.registerHandler(PublicConstants::kActionOpenProject, this,
                       [this](const QVariantMap&)
                       {
                           clearTree();
                           initializeTree();
                       });
    em.registerHandler(PublicConstants::kEventLoadComplete, this,
                       [this](const QVariantMap&)
                       {
                           initializeTree();
                       });
}

void DisplayModule::initializeTree()
{
    if (m_root)
        return;

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!pm)
        return;

    m_root = new ProjectNode(QString::fromUtf8("Base_Display"));
    setupNodeRole(m_root, "DisplayRoot");
    m_root->setPage(new DisplayInfoPage(QString::fromUtf8("Base_Display 模块"),
                                        {QString::fromUtf8("仅用于展示树节点与页面布局"),
                                         QString::fromUtf8("当前页面不执行计算逻辑") }));
    pm->addTreeItem(m_root);

    ProjectNode* acoustic = appendNode(m_root, QString::fromUtf8("1 声学场"), "Acoustic", {});
    ProjectNode* acousticIn = appendNode(acoustic, QString::fromUtf8("1.1 输入"), "AcousticInput", {});
    appendNode(acousticIn, QString::fromUtf8("声压级参考压力"), "AcousticReferencePressure",
               {QString::fromUtf8("空气参考压力"), QString::fromUtf8("水参考压力"),
                QString::fromUtf8("用户定义参考压力")});
    appendNode(acousticIn, QString::fromUtf8("压力声学设置"), "AcousticSettings",
               {QString::fromUtf8("温度"), QString::fromUtf8("绝对压力"), QString::fromUtf8("流体模型"),
                QString::fromUtf8("声速"), QString::fromUtf8("密度")});
    appendNode(acousticIn, QString::fromUtf8("初始值设置"), "AcousticInitial",
               {QString::fromUtf8("声压")});

    ProjectNode* acousticBoundary = appendNode(acousticIn, QString::fromUtf8("边界条件"), "AcousticBoundary", {});
    appendNode(acousticBoundary, QString::fromUtf8("平面波辐射"), "PlaneWave",
               {QString::fromUtf8("选择边界")});
    appendNode(acousticBoundary, QString::fromUtf8("周期性条件"), "Periodic",
               {QString::fromUtf8("连续性"), QString::fromUtf8("反周期性"),
                QString::fromUtf8("Floquet 周期"), QString::fromUtf8("循环对称")});
    appendNode(acousticBoundary, QString::fromUtf8("背景压力场"), "BackgroundPressure",
               {QString::fromUtf8("压力场类型"), QString::fromUtf8("压力幅值"),
                QString::fromUtf8("声速"), QString::fromUtf8("波方向"),
                QString::fromUtf8("选择域")});

    ProjectNode* acousticOut = appendNode(acoustic, QString::fromUtf8("1.2 输出"), "AcousticOutput", {});
    appendNode(acousticOut, QString::fromUtf8("声压"), "AcousticPressure", {});

    ProjectNode* solid = appendNode(m_root, QString::fromUtf8("2 固体力学场"), "Solid", {});
    ProjectNode* solidIn = appendNode(solid, QString::fromUtf8("2.1 输入"), "SolidInput", {});
    appendNode(solidIn, QString::fromUtf8("线弹性材料设置"), "LinearElastic",
               {QString::fromUtf8("杨氏模量"), QString::fromUtf8("泊松比"), QString::fromUtf8("密度"),
                QString::fromUtf8("支持以波速描述线弹性材料性能")});
    appendNode(solidIn, QString::fromUtf8("超弹性材料设置"), "HyperElastic",
               {QString::fromUtf8("仅保留：Mooney-Rivlin 双参数（默认）"),
                QString::fromUtf8("Yeoh"), QString::fromUtf8("Ogden")});
    appendNode(solidIn, QString::fromUtf8("粘弹性材料设置"), "Viscoelastic",
               {QString::fromUtf8("复模量（频率相关函数）"),
                QString::fromUtf8("函数类型：插值函数 / 二阶拟合函数")});
    appendNode(solidIn, QString::fromUtf8("弹塑性材料设置"), "ElasticPlastic", {});
    appendNode(solidIn, QString::fromUtf8("初始值设置"), "SolidInitial",
               {QString::fromUtf8("位移场"), QString::fromUtf8("结构速度场")});

    ProjectNode* solidBoundary = appendNode(solidIn, QString::fromUtf8("边界条件"), "SolidBoundary", {});
    appendNode(solidBoundary, QString::fromUtf8("辊支撑"), "RollerSupport",
               {QString::fromUtf8("按工况区分使用条件")});
    appendNode(solidBoundary, QString::fromUtf8("周期性条件"), "SolidPeriodic",
               {QString::fromUtf8("按工况区分使用条件")});

    ProjectNode* solidOut = appendNode(solid, QString::fromUtf8("2.3 输出"), "SolidOutput", {});
    appendNode(solidOut, QString::fromUtf8("应力"), "Stress", {});

    ProjectNode* solver = appendNode(m_root, QString::fromUtf8("3 求解器定义"), "SolverDefinition", {});
    appendNode(solver, QString::fromUtf8("3.1 频域"), "FrequencyDomain", {});
    appendNode(solver, QString::fromUtf8("3.2 时域"), "TimeDomain", {});

    ProjectNode* optimization = appendNode(m_root, QString::fromUtf8("4 优化模块"), "Optimization", {});
    appendNode(optimization, QString::fromUtf8("4.1 输入"), "OptimizationInput", {});
    appendNode(optimization, QString::fromUtf8("4.2 输出"), "OptimizationOutput", {});

    ProjectNode* fatigue = appendNode(m_root, QString::fromUtf8("5 疲劳模块"), "Fatigue", {});
    appendNode(fatigue, QString::fromUtf8("5.1 输入"), "FatigueInput", {});
    ProjectNode* fatigueOut = appendNode(fatigue, QString::fromUtf8("5.2 输出"), "FatigueOutput", {});
    appendNode(fatigueOut, QString::fromUtf8("失效循环次数"), "FailureCycles", {});
    appendNode(fatigueOut, QString::fromUtf8("疲劳利用率因子"), "FatigueUtilization", {});
    appendNode(fatigueOut, QString::fromUtf8("疲劳使用因子"), "FatigueUsage", {});

    ProjectNode* others = appendNode(m_root, QString::fromUtf8("其它"), "Others", {});
    appendNode(others, QString::fromUtf8("变量定义"), "Variables", {});
    appendNode(others, QString::fromUtf8("网格设置"), "Mesh", {});
    appendNode(others, QString::fromUtf8("频率设置"), "FrequencySetting", {});
    appendNode(others, QString::fromUtf8("关注结果显示设置"), "ResultFocus", {});
}

void DisplayModule::clearTree()
{
    if (!m_root)
        return;

    while (m_root->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(0));
        delete m_root->takeChild(0);
        Q_UNUSED(node);
    }
    delete m_root;
    m_root = nullptr;
}

void DisplayModule::setupNodeRole(ProjectNode* node, const QString& kind) const
{
    if (!node)
        return;
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "Display");
    node->setData(0, ProjectNode::RoleKind, kind);
}

ProjectNode* DisplayModule::appendNode(ProjectNode* parent, const QString& title, const QString& kind,
                                       const QStringList& details) const
{
    auto* node = new ProjectNode(title, QUuid::createUuid().toString(),
                                 new DisplayInfoPage(title, details));
    setupNodeRole(node, kind);
    parent->addChild(node);
    return node;
}
