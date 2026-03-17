#include "DisplayManager.h"

#include <EventManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QUuid>

#include "SolidMechanicsSettingPage.h"
#include "SolidBoundaryConditionPage.h"
#include "SolidResultOutputPage.h"
#include "AcousticParameterPage.h"
#include "AcousticBoundaryConditionPage.h"
#include "AcousticResultOutputPage.h"
#include "SolverFrequencyDefinitionPage.h"
#include "SolverTimeDefinitionPage.h"
#include "SolverVariableDefinitionPage.h"
#include "SolverMeshSettingPage.h"
#include "SolverResultFocusPage.h"
#include "TopologyOptimizationInputPage.h"
#include "TopologyObjectiveFunctionPage.h"
#include "TopologyConstraintPage.h"
#include "TopologyResultOutputPage.h"
#include "ParameterOptimizationInputPage.h"
#include "ParameterOptimizationProcessPage.h"
#include "ParameterOptimizationResultPage.h"
#include "FatigueInputSettingPage.h"
#include "FatigueResultOutputPage.h"
#include "VisualizationManageViewPage.h"
#include "VisualizationInteractiveAnalysisPage.h"
#include "VisualizationExpressionPage.h"
#include "VisualizationStyleAssistPage.h"

DisplayManager::DisplayManager()
{
    auto& em = EventManager::instance();
    em.registerHandler(PublicConstants::kActionNewProject, this,
                       [this](const QVariantMap&){ clearTree(); initializeTree(); });
    em.registerHandler(PublicConstants::kActionOpenProject, this,
                       [this](const QVariantMap&){ clearTree(); initializeTree(); });
    em.registerHandler(PublicConstants::kEventLoadComplete, this,
                       [this](const QVariantMap&){ initializeTree(); });
}

void DisplayManager::initializeTree()
{
    if (m_root) return;
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!pm) return;

    auto addNode = [this](ProjectNode* parent, const QString& title, const QString& kind, BasePage* page)
    {
        auto* node = new ProjectNode(title, QUuid::createUuid().toString(), page);
        setupNodeRole(node, kind);
        if (parent) parent->addChild(node);
        return node;
    };

    m_root = addNode(nullptr, QString::fromUtf8("Base_Display"), "DisplayRoot", new SolidMechanicsSettingPage());
    pm->addTreeItem(m_root);

    auto* solid = addNode(m_root, QString::fromUtf8("固体力学场"), "SolidRoot", new SolidMechanicsSettingPage());
    addNode(solid, QString::fromUtf8("固体力学场设置页"), "SolidSetting", new SolidMechanicsSettingPage());
    addNode(solid, QString::fromUtf8("固体力学场边界条件设置页"), "SolidBoundary", new SolidBoundaryConditionPage());
    addNode(solid, QString::fromUtf8("固体力学场结果输出设置页"), "SolidOutput", new SolidResultOutputPage());

    auto* acoustic = addNode(m_root, QString::fromUtf8("声学场"), "AcousticRoot", new AcousticParameterPage());
    addNode(acoustic, QString::fromUtf8("声学场参数设置页"), "AcousticSetting", new AcousticParameterPage());
    addNode(acoustic, QString::fromUtf8("声学场边界条件设置页"), "AcousticBoundary", new AcousticBoundaryConditionPage());
    addNode(acoustic, QString::fromUtf8("声学场结果输出设置页"), "AcousticOutput", new AcousticResultOutputPage());

    auto* solver = addNode(m_root, QString::fromUtf8("求解器定义"), "SolverRoot", new SolverFrequencyDefinitionPage());
    addNode(solver, QString::fromUtf8("频域求解定义页"), "SolverFreq", new SolverFrequencyDefinitionPage());
    addNode(solver, QString::fromUtf8("时域求解定义页"), "SolverTime", new SolverTimeDefinitionPage());
    addNode(solver, QString::fromUtf8("变量定义页"), "SolverVar", new SolverVariableDefinitionPage());
    addNode(solver, QString::fromUtf8("网格设置页"), "SolverMesh", new SolverMeshSettingPage());
    addNode(solver, QString::fromUtf8("关注结果显示设置页"), "SolverFocus", new SolverResultFocusPage());

    auto* topo = addNode(m_root, QString::fromUtf8("拓扑优化模块"), "TopoRoot", new TopologyOptimizationInputPage());
    addNode(topo, QString::fromUtf8("拓扑优化输入设置页"), "TopoInput", new TopologyOptimizationInputPage());
    addNode(topo, QString::fromUtf8("目标函数设置页"), "TopoObj", new TopologyObjectiveFunctionPage());
    addNode(topo, QString::fromUtf8("约束条件设置页"), "TopoConstraint", new TopologyConstraintPage());
    addNode(topo, QString::fromUtf8("拓扑优化结果输出页"), "TopoOutput", new TopologyResultOutputPage());

    auto* param = addNode(m_root, QString::fromUtf8("参数优化模块"), "ParamRoot", new ParameterOptimizationInputPage());
    addNode(param, QString::fromUtf8("参数优化输入设置页"), "ParamInput", new ParameterOptimizationInputPage());
    addNode(param, QString::fromUtf8("参数优化流程设置页"), "ParamProcess", new ParameterOptimizationProcessPage());
    addNode(param, QString::fromUtf8("参数优化结果输出页"), "ParamOutput", new ParameterOptimizationResultPage());

    auto* fatigue = addNode(m_root, QString::fromUtf8("疲劳模块"), "FatigueRoot", new FatigueInputSettingPage());
    addNode(fatigue, QString::fromUtf8("疲劳输入设置页"), "FatigueInput", new FatigueInputSettingPage());
    addNode(fatigue, QString::fromUtf8("疲劳结果输出页"), "FatigueOutput", new FatigueResultOutputPage());

    auto* vis = addNode(m_root, QString::fromUtf8("结果可视化设置"), "VisRoot", new VisualizationManageViewPage());
    addNode(vis, QString::fromUtf8("结果管理与视图展示设置页"), "VisManage", new VisualizationManageViewPage());
    addNode(vis, QString::fromUtf8("交互分析设置页"), "VisInteractive", new VisualizationInteractiveAnalysisPage());
    addNode(vis, QString::fromUtf8("结果表达方式设置页"), "VisExpression", new VisualizationExpressionPage());
    addNode(vis, QString::fromUtf8("显示样式与辅助显示设置页"), "VisStyle", new VisualizationStyleAssistPage());
}

void DisplayManager::clearTree()
{
    if (!m_root) return;
    while (m_root->childCount() > 0) delete m_root->takeChild(0);
    delete m_root;
    m_root = nullptr;
}

void DisplayManager::setupNodeRole(ProjectNode* node, const QString& kind) const
{
    if (!node) return;
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "Display");
    node->setData(0, ProjectNode::RoleKind, kind);
}
