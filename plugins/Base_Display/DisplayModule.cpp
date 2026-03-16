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
                                        QString::fromUtf8("该模块用于展示页面结构与参数项，不执行功能计算。"),
                                        {QString::fromUtf8("所有页面均由 .ui 绘制"),
                                         QString::fromUtf8("仅做展示，不联动真实求解") }));
    pm->addTreeItem(m_root);

    ProjectNode* acoustic = appendNode(m_root, QString::fromUtf8("1 声学场"), "Acoustic",
                                       QString::fromUtf8("声学场展示模块"));
    ProjectNode* acousticIn = appendNode(acoustic, QString::fromUtf8("1.1 输入"), "AcousticInput",
                                         QString::fromUtf8("声学场输入参数设置"));
    appendNode(acousticIn, QString::fromUtf8("声压级参考压力"), "AcousticReferencePressure",
               QString::fromUtf8("参考压力来源选择"),
               {QString::fromUtf8("空气参考压力"), QString::fromUtf8("水参考压力"),
                QString::fromUtf8("用户定义参考压力")});
    appendNode(acousticIn, QString::fromUtf8("压力声学设置"), "AcousticSettings",
               QString::fromUtf8("压力声学基础参数"),
               {QString::fromUtf8("温度"), QString::fromUtf8("绝对压力"), QString::fromUtf8("流体模型"),
                QString::fromUtf8("声速"), QString::fromUtf8("密度")});
    appendNode(acousticIn, QString::fromUtf8("初始值设置"), "AcousticInitial",
               QString::fromUtf8("初始条件"), {QString::fromUtf8("声压")});

    ProjectNode* acousticBoundary = appendNode(
        acousticIn, QString::fromUtf8("边界条件"), "AcousticBoundary",
        QString::fromUtf8("边界条件选择与录入"),
        {QString::fromUtf8("平面波辐射：选择边界"), QString::fromUtf8("周期性条件：连续性/反周期性/Floquet周期/循环对称"),
         QString::fromUtf8("背景压力场：选择域 + 场参数")},
        {QString::fromUtf8("序号"), QString::fromUtf8("边界名称"), QString::fromUtf8("内容"),
         QString::fromUtf8("备注")},
        {{QString::fromUtf8("1"), QString::fromUtf8("平面波辐射"), QString::fromUtf8("边界选择"),
          QString::fromUtf8("选择哪些边界")},
         {QString::fromUtf8("2"), QString::fromUtf8("周期性条件"),
          QString::fromUtf8("连续性/反周期性/Floquet周期/循环对称"), QString()},
         {QString::fromUtf8("3"), QString::fromUtf8("背景压力场"),
          QString::fromUtf8("压力场类型/压力幅值/声速/波方向"), QString::fromUtf8("选择哪个域")}});
    Q_UNUSED(acousticBoundary);

    ProjectNode* acousticOut = appendNode(acoustic, QString::fromUtf8("1.2 输出"), "AcousticOutput",
                                          QString::fromUtf8("声学场输出展示"));
    appendNode(acousticOut, QString::fromUtf8("声压"), "AcousticPressure",
               QString::fromUtf8("输出结果：声压"));

    ProjectNode* solid = appendNode(m_root, QString::fromUtf8("2 固体力学场"), "Solid",
                                    QString::fromUtf8("固体力学场展示模块"));
    ProjectNode* solidIn = appendNode(solid, QString::fromUtf8("2.1 输入"), "SolidInput",
                                      QString::fromUtf8("固体力学输入参数设置"));
    appendNode(solidIn, QString::fromUtf8("线弹性材料设置"), "LinearElastic",
               QString::fromUtf8("线弹性参数"),
               {QString::fromUtf8("杨氏模量"), QString::fromUtf8("泊松比"), QString::fromUtf8("密度"),
                QString::fromUtf8("支持以波速描述线弹性材料性能")});
    appendNode(solidIn, QString::fromUtf8("超弹性材料设置"), "HyperElastic",
               QString::fromUtf8("超弹性材料模型（按要求保留三项）"),
               {QString::fromUtf8("Mooney-Rivlin 双参数（默认）"), QString::fromUtf8("Yeoh"),
                QString::fromUtf8("Ogden")});
    appendNode(solidIn, QString::fromUtf8("粘弹性材料设置"), "Viscoelastic",
               QString::fromUtf8("复模量（频率相关函数）"),
               {QString::fromUtf8("材料数据为频率相关函数"),
                QString::fromUtf8("函数可选：插值函数 / 二阶拟合函数")});
    appendNode(solidIn, QString::fromUtf8("弹塑性材料设置"), "ElasticPlastic",
               QString::fromUtf8("弹塑性参数展示"));
    appendNode(solidIn, QString::fromUtf8("初始值设置"), "SolidInitial",
               QString::fromUtf8("初始条件"),
               {QString::fromUtf8("位移场"), QString::fromUtf8("结构速度场")});

    ProjectNode* solidBoundary = appendNode(
        solidIn, QString::fromUtf8("边界条件"), "SolidBoundary",
        QString::fromUtf8("辊支撑与周期性条件需按工况区分"),
        {QString::fromUtf8("需要区分什么条件下使用辊支撑"),
         QString::fromUtf8("需要区分什么条件下使用周期性条件")},
        {QString::fromUtf8("序号"), QString::fromUtf8("边界名称"), QString::fromUtf8("内容"),
         QString::fromUtf8("备注")},
        {{QString::fromUtf8("1"), QString::fromUtf8("辊支撑"), QString(), QString()},
         {QString::fromUtf8("2"), QString::fromUtf8("周期性条件"), QString(), QString()}});
    Q_UNUSED(solidBoundary);

    ProjectNode* solidOut = appendNode(solid, QString::fromUtf8("2.3 输出"), "SolidOutput",
                                       QString::fromUtf8("固体力学输出展示"));
    appendNode(solidOut, QString::fromUtf8("应力"), "Stress", QString::fromUtf8("输出结果：应力"));

    ProjectNode* solver = appendNode(m_root, QString::fromUtf8("3 求解器定义"), "SolverDefinition",
                                     QString::fromUtf8("求解器模式配置"));
    appendNode(solver, QString::fromUtf8("3.1 频域"), "FrequencyDomain",
               QString::fromUtf8("频域求解设置"));
    appendNode(solver, QString::fromUtf8("3.2 时域"), "TimeDomain",
               QString::fromUtf8("时域求解设置"));

    ProjectNode* optimization = appendNode(m_root, QString::fromUtf8("4 优化模块"), "Optimization",
                                           QString::fromUtf8("优化流程展示"));
    appendNode(optimization, QString::fromUtf8("4.1 输入"), "OptimizationInput",
               QString::fromUtf8("优化输入参数"));
    appendNode(optimization, QString::fromUtf8("4.2 输出"), "OptimizationOutput",
               QString::fromUtf8("优化输出结果"));

    ProjectNode* fatigue = appendNode(m_root, QString::fromUtf8("5 疲劳模块"), "Fatigue",
                                      QString::fromUtf8("疲劳分析流程展示"));
    appendNode(fatigue, QString::fromUtf8("5.1 输入"), "FatigueInput",
               QString::fromUtf8("疲劳输入参数"));
    ProjectNode* fatigueOut = appendNode(fatigue, QString::fromUtf8("5.2 输出"), "FatigueOutput",
                                         QString::fromUtf8("疲劳输出结果"));
    appendNode(fatigueOut, QString::fromUtf8("失效循环次数"), "FailureCycles");
    appendNode(fatigueOut, QString::fromUtf8("疲劳利用率因子"), "FatigueUtilization");
    appendNode(fatigueOut, QString::fromUtf8("疲劳使用因子"), "FatigueUsage");

    ProjectNode* others = appendNode(m_root, QString::fromUtf8("其它"), "Others",
                                     QString::fromUtf8("其他设置项"));
    appendNode(others, QString::fromUtf8("变量定义"), "Variables");
    appendNode(others, QString::fromUtf8("网格设置"), "Mesh");
    appendNode(others, QString::fromUtf8("频率设置"), "FrequencySetting");
    appendNode(others, QString::fromUtf8("关注结果显示设置"), "ResultFocus");
}

void DisplayModule::clearTree()
{
    if (!m_root)
        return;

    while (m_root->childCount() > 0)
        delete m_root->takeChild(0);

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
                                       const QString& intro, const QStringList& details,
                                       const QStringList& tableHeaders,
                                       const QVector<QStringList>& tableRows) const
{
    auto* node =
        new ProjectNode(title, QUuid::createUuid().toString(),
                        new DisplayInfoPage(title, intro, details, tableHeaders, tableRows));
    setupNodeRole(node, kind);
    parent->addChild(node);
    return node;
}
