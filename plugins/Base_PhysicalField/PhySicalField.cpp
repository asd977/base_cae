#include "PhySicalField.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <ContextMenuHub.h>
#include <DisplacementConstraintWidget.h>
#include <LoadSpecificationWidget.h>
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
}
