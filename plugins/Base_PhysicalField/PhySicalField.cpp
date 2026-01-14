#include "PhySicalField.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <ContextMenuHub.h>
#include <DisplacementConstraintWidget.h>
#include <LoadSpecificationWidget.h>
#include <AddedMassPage.h>
#include <BeamLoadPage.h>
#include <BodyForcePage.h>
#include <GeoFitPage.h>
#include <GeoMeasurePage.h>
#include <LineForcePage.h>
#include <PointForcePage.h>
#include <SeismicPage.h>
#include <SurfaceForcePage.h>
#include <TemperaturePage.h>
#include <ServiceRegistry.h>

PhysicalField::PhysicalField()
{
    ServiceRegistry::instance().registerService<PhysicalFieldService>(this);
    intitTreeMenu();
}

BasePage *PhysicalField::getPhysicalFiledSettingWidget()
{
    PhysicalFieldSettingWidget *tmp = new PhysicalFieldSettingWidget;
    return (BasePage*)tmp;
}

void PhysicalField::intitTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node",
        this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid   = ctx.value("uuid").toString();
            const QString kind   = ctx.value("kind").toString();
            // const QString module = ctx.value("module").toString();

            ContextMenuHub::addSeparator(menu);

            // ===== 1) 根节点：PhysicalFieldRoot -> 添加子项 =====
            if (kind == "PhysicalFieldRoot")
            {
                ContextMenuHub::addAction(menu, this, "添加位移约束", [this, uuid]{
                    // uuid 是根节点 uuid，可用来找到根节点，再 addChild
                    this->addDisplacementConstraint();
                });

                ContextMenuHub::addAction(menu, this, "添加荷载设置", [this, uuid]{
                    this->addLoadSpecification();
                });

                return; // 根节点就不显示下面那些重建/隐藏/删除（你想显示也可以去掉 return）
            }

            // ===== 2) 子节点：位移约束 / 荷载设置 =====
            ContextMenuHub::addAction(menu, this, "重建", [this, uuid]{
                // rebuild(uuid);
            });

            ContextMenuHub::addAction(menu, this, "隐藏/显示", [this, uuid]{
                // toggle(uuid);
            });

            if (kind == "DisplacementConstraint" || kind == "LoadSpecification")
            {
                ContextMenuHub::addAction(menu, this, "删除", [this, uuid]{
                    // removeNode(uuid);
                });
            }

            if (kind == "LoadSpecification")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "添加地应力测点页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("地应力测点"), new GeoMeasurePage, QStringLiteral("LoadSpecGeoMeasure"));
                });
                ContextMenuHub::addAction(menu, this, "添加地应力拟合页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("地应力拟合"), new GeoFitPage, QStringLiteral("LoadSpecGeoFit"));
                });
                ContextMenuHub::addAction(menu, this, "添加体积力页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("体积力"), new BodyForcePage, QStringLiteral("LoadSpecBodyForce"));
                });
                ContextMenuHub::addAction(menu, this, "添加集中力页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("集中力"), new PointForcePage, QStringLiteral("LoadSpecPointForce"));
                });
                ContextMenuHub::addAction(menu, this, "添加线力页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("线力"), new LineForcePage, QStringLiteral("LoadSpecLineForce"));
                });
                ContextMenuHub::addAction(menu, this, "添加面力页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("面力"), new SurfaceForcePage, QStringLiteral("LoadSpecSurfaceForce"));
                });
                ContextMenuHub::addAction(menu, this, "添加变温页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("变温"), new TemperaturePage, QStringLiteral("LoadSpecTemperature"));
                });
                ContextMenuHub::addAction(menu, this, "添加梁单元页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("梁单元"), new BeamLoadPage, QStringLiteral("LoadSpecBeamLoad"));
                });
                ContextMenuHub::addAction(menu, this, "添加地震页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("地震"), new SeismicPage, QStringLiteral("LoadSpecSeismic"));
                });
                ContextMenuHub::addAction(menu, this, "添加附加质量页面", [this, uuid]{
                    addLoadSpecificationPage(uuid, QStringLiteral("附加质量"), new AddedMassPage, QStringLiteral("LoadSpecAddedMass"));
                });
            }
        },
        100,
        [](const QVariantMap& ctx)->bool{
            return ctx.value("module").toString() == "PhysicalField";
        }
        );
}

void PhysicalField::newProject()
{
    auto *physicalFieldSettingWidget = new PhysicalFieldSettingWidget;

    m_node = new ProjectNode("物理场", QUuid::createUuid().toString(), (BasePage*)physicalFieldSettingWidget);

    // 关键：把上下文写进 item data
    m_node->setData(0, ProjectNode::RoleUuid,   m_node->getUuid());
    m_node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    m_node->setData(0, ProjectNode::RoleKind,   "PhysicalFieldRoot");

    auto *projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_node);

}

void PhysicalField::addDisplacementConstraint()
{
    auto *displacementConstraintWidget = new DisplacementConstraintWidget;
    ProjectNode *node2 = new ProjectNode("位移约束", QUuid::createUuid().toString(), (BasePage*)displacementConstraintWidget);
    node2->setData(0, ProjectNode::RoleUuid,   node2->getUuid());
    node2->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node2->setData(0, ProjectNode::RoleKind,   "DisplacementConstraint");
    m_node->addChild(node2);
}

void PhysicalField::addLoadSpecification()
{
    auto *loadSpecificationWidget = new LoadSpecificationWidget;
    ProjectNode *node3 = new ProjectNode("荷载设置", QUuid::createUuid().toString(), (BasePage*)loadSpecificationWidget);
    node3->setData(0, ProjectNode::RoleUuid,   node3->getUuid());
    node3->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node3->setData(0, ProjectNode::RoleKind,   "LoadSpecification");
    m_node->addChild(node3);

    addLoadSpecificationPages(node3);
}

void PhysicalField::addLoadSpecificationPage(const QString &parentUuid, const QString &title, BasePage *page, const QString &kind)
{
    auto *parentNode = findNodeByUuid(m_node, parentUuid);
    if (!parentNode) {
        delete page;
        return;
    }

    if (hasChildKind(parentNode, kind)) {
        delete page;
        return;
    }

    auto *node = new ProjectNode(title, QUuid::createUuid().toString(), page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, kind);
    parentNode->addChild(node);
}

void PhysicalField::addLoadSpecificationPages(ProjectNode *parentNode)
{
    if (!parentNode)
        return;

    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("地应力测点"), new GeoMeasurePage, QStringLiteral("LoadSpecGeoMeasure"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("地应力拟合"), new GeoFitPage, QStringLiteral("LoadSpecGeoFit"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("体积力"), new BodyForcePage, QStringLiteral("LoadSpecBodyForce"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("集中力"), new PointForcePage, QStringLiteral("LoadSpecPointForce"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("线力"), new LineForcePage, QStringLiteral("LoadSpecLineForce"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("面力"), new SurfaceForcePage, QStringLiteral("LoadSpecSurfaceForce"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("变温"), new TemperaturePage, QStringLiteral("LoadSpecTemperature"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("梁单元"), new BeamLoadPage, QStringLiteral("LoadSpecBeamLoad"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("地震"), new SeismicPage, QStringLiteral("LoadSpecSeismic"));
    addLoadSpecificationPage(parentNode->getUuid(), QStringLiteral("附加质量"), new AddedMassPage, QStringLiteral("LoadSpecAddedMass"));
}

ProjectNode *PhysicalField::findNodeByUuid(ProjectNode *root, const QString &uuid)
{
    if (!root)
        return nullptr;
    if (root->getUuid() == uuid)
        return root;
    for (auto *child : root->getChildren()) {
        if (auto *match = findNodeByUuid(child, uuid))
            return match;
    }
    return nullptr;
}

bool PhysicalField::hasChildKind(ProjectNode *parentNode, const QString &kind) const
{
    if (!parentNode)
        return false;
    for (auto *child : parentNode->getChildren()) {
        if (child->data(0, ProjectNode::RoleKind).toString() == kind)
            return true;
    }
    return false;
}
