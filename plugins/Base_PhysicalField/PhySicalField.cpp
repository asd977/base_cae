#include "PhySicalField.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <ContextMenuHub.h>
#include <DisplacementConstraintWidget.h>
#include <FaceLoadPage.h>
#include <GeoStressFitFacePage.h>
#include <GeoStressPointPage.h>
#include <LoadSpecificationWidget.h>
#include <PointForcePage.h>
#include <ServiceRegistry.h>
#include "BodyForcePage.h"
#include "TemperatureLoadPage.h"
#include "SeismicAndAddedMassPage.h"


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
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();

            ContextMenuHub::addSeparator(menu);

            // ===== 根节点：PhysicalFieldRoot -> 添加子项 =====
            if (kind == "PhysicalFieldRoot")
            {
                // 约束
                ContextMenuHub::addAction(menu, this, "添加位移约束", [this, uuid]{
                    this->addDisplacementConstraint();
                });

                ContextMenuHub::addSeparator(menu);

                // IIWL.DAT - Part1/2
                ContextMenuHub::addAction(menu, this, "现场地应力信息（测点）", [this, uuid]{
                    this->addGeoStressPointPage();      // GeoStressPointPage
                });

                ContextMenuHub::addAction(menu, this, "梯形边界荷载（拟合条件）", [this, uuid]{
                    this->addGeoStressFitFacePage();    // GeoStressFitFacePage
                });

                ContextMenuHub::addSeparator(menu);

                // IIWL.DAT - Part3/4/6/7/9-11
                ContextMenuHub::addAction(menu, this, "体积力荷载（体力）", [this, uuid]{
                    this->addBodyForcePage();           // BodyForcePage
                });

                ContextMenuHub::addAction(menu, this, "集中力荷载", [this, uuid]{
                    this->addConcentratedForcePage();   // ConcentratedForcePage
                });

                // Part5 线力：先不做
                // ContextMenuHub::addAction(menu, this, "线力荷载", ...);

                ContextMenuHub::addAction(menu, this, "面力荷载", [this, uuid]{
                    this->addSurfaceForcePage();        // SurfaceForcePage
                });

                ContextMenuHub::addAction(menu, this, "变温荷载", [this, uuid]{
                    this->addTemperatureLoadPage();     // TemperatureLoadPage
                });

                ContextMenuHub::addSeparator(menu);

                ContextMenuHub::addAction(menu, this, "地震加速度表", [this, uuid]{
                    this->addSeismicAndAddedMassPage(); // SeismicAndAddedMassPage
                });

                return;
            }

            // ===== 子节点：通用操作 =====
            ContextMenuHub::addAction(menu, this, "重建", [this, uuid]{
                // rebuild(uuid);
            });

            ContextMenuHub::addAction(menu, this, "隐藏/显示", [this, uuid]{
                // toggle(uuid);
            });

            // 允许删除的 kind（你可以继续加）
            if (kind == "DisplacementConstraint"
                || kind == "GeoStressPointPage"
                || kind == "GeoStressFitFacePage"
                || kind == "BodyForcePage"
                || kind == "ConcentratedForcePage"
                || kind == "SurfaceForcePage"
                || kind == "TemperatureLoadPage"
                || kind == "SeismicAndAddedMassPage"
                || kind == "LoadSpecification")
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

void PhysicalField::addGeoStressPointPage()
{
    auto *geoStressPointPage = new GeoStressPointPage;
    ProjectNode *node3 = new ProjectNode("现场地应力信息", QUuid::createUuid().toString(), (BasePage*)geoStressPointPage);
    node3->setData(0, ProjectNode::RoleUuid,   node3->getUuid());
    node3->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node3->setData(0, ProjectNode::RoleKind,   "GeoStressPointPage");
    m_node->addChild(node3);
}

void PhysicalField::addGeoStressFitFacePage()
{
    auto *geoStressFitFacePage = new GeoStressFitFacePage;
    ProjectNode *node3 = new ProjectNode("梯形边界荷载", QUuid::createUuid().toString(), (BasePage*)geoStressFitFacePage);
    node3->setData(0, ProjectNode::RoleUuid,   node3->getUuid());
    node3->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node3->setData(0, ProjectNode::RoleKind,   "GeoStressFitFacePage");
    m_node->addChild(node3);
}

void PhysicalField::addBodyForcePage()
{
    auto *page = new BodyForcePage;
    ProjectNode *node = new ProjectNode("体积力荷载", QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid,   node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind,   "BodyForcePage");
    m_node->addChild(node);
}

void PhysicalField::addConcentratedForcePage()
{
    auto *page = new PointForcePage;
    ProjectNode *node = new ProjectNode("集中力荷载", QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid,   node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind,   "ConcentratedForcePage");
    m_node->addChild(node);
}

void PhysicalField::addSurfaceForcePage()
{
    auto *page = new FaceLoadPage;
    ProjectNode *node = new ProjectNode("面力荷载", QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid,   node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind,   "SurfaceForcePage");
    m_node->addChild(node);
}

void PhysicalField::addTemperatureLoadPage()
{
    auto *page = new TemperatureLoadPage;
    ProjectNode *node = new ProjectNode("变温荷载", QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid,   node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind,   "TemperatureLoadPage");
    m_node->addChild(node);
}

void PhysicalField::addSeismicAndAddedMassPage()
{
    auto *page = new SeismicAndAddedMassPage;
    ProjectNode *node = new ProjectNode("地震加速度", QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid,   node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind,   "SeismicAndAddedMassPage");
    m_node->addChild(node);
}
