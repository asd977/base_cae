#include "PhySicalField.h"

#include <ContextMenuHub.h>
#include <DisplacementConstraintWidget.h>
#include <FaceLoadPage.h>
#include <GeoStressFitFacePage.h>
#include <GeoStressPointPage.h>
#include <LoadSpecificationWidget.h>
#include <PointForcePage.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/ProjectManagerService.h>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QVector>
#include <algorithm>
#include <array>

#include "BodyForcePage.h"
#include "PhysicalFieldSettingWidget.h"
#include "SeismicAndAddedMassPage.h"
#include "TemperatureLoadPage.h"
#include "vtkCellData.h"
#include "vtkPointData.h"

// VTK
#include <BeamLoadPage.h>
#include <EventManager.h>
#include <LinearForcePage.h>
#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include "DisplacementConstraintWidget.h"

static int CountKind(ProjectNode* root, const QString& kind)
{
    if (!root)
        return 0;
    int cnt = 0;
    const QString k = root->data(0, ProjectNode::RoleKind).toString();
    if (k == kind)
        cnt++;

    for (int i = 0; i < root->childCount(); ++i)
        cnt += CountKind((ProjectNode*)root->child(i), kind);

    return cnt;
}

static void CollectNodesByKind(ProjectNode* root, const QString& kind, QList<ProjectNode*>& out)
{
    if (!root)
        return;

    if (root->data(0, ProjectNode::RoleKind).toString() == kind)
        out.push_back(root);

    for (int i = 0; i < root->childCount(); ++i)
        CollectNodesByKind((ProjectNode*)root->child(i), kind, out);
}

static inline void setSci(QTextStream& os)
{
    os.setRealNumberNotation(QTextStream::ScientificNotation);
    os.setRealNumberPrecision(12);
}

static QString MakeIndexedTitle(ProjectNode* root, const QString& kind, const QString& base)
{
    const int idx = CountKind(root, kind) + 1;   // 已有数量 + 1
    return QString("%1(%2)").arg(base).arg(idx); // 位移约束(1)
}

static inline QString quoteIfNeeded(const QString& s)
{
    QString t = s.trimmed();
    if (t.isEmpty())
        return QString("\"\"");
    if (t.startsWith("\"") && t.endsWith("\""))
        return t;
    return QString("\"%1\"").arg(t);
}

PhysicalField::PhysicalField()
{
    ServiceRegistry::instance().registerService<PhysicalFieldService>(this);
    initAction();
    intitTreeMenu();
}

void PhysicalField::initAction()
{
    auto& em = EventManager::instance();

    em.registerHandler(PublicConstants::kActionPhysicalDisplacementConstraint,
                       [this](const QVariantMap&)
                       {
                           addDisplacementConstraint();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalGeoStressPoint,
                       [this](const QVariantMap&)
                       {
                           addGeoStressPointPage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalGeoStressFitFace,
                       [this](const QVariantMap&)
                       {
                           addGeoStressFitFacePage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalBodyForce,
                       [this](const QVariantMap&)
                       {
                           addBodyForcePage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalConcentratedForce,
                       [this](const QVariantMap&)
                       {
                           addConcentratedForcePage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalLinearForce,
                       [this](const QVariantMap&)
                       {
                           addLinearForcePage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalSurfaceForce,
                       [this](const QVariantMap&)
                       {
                           addSurfaceForcePage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalTemperatureLoad,
                       [this](const QVariantMap&)
                       {
                           addTemperatureLoadPage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalBeamLoad,
                       [this](const QVariantMap&)
                       {
                           addBeamLoadPage();
                       });
    em.registerHandler(PublicConstants::kActionPhysicalSeismicAndAddedMass,
                       [this](const QVariantMap&)
                       {
                           addSeismicAndAddedMassPage();
                       });
}

BasePage* PhysicalField::getPhysicalFiledSettingWidget()
{
    PhysicalFieldSettingWidget* tmp = new PhysicalFieldSettingWidget;
    return (BasePage*)tmp;
}

void PhysicalField::intitTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node", this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();

            ContextMenuHub::addSeparator(menu);

            // ===== 根节点：PhysicalFieldRoot -> 添加子项 =====
            if (kind == "PhysicalFieldRoot")
            {
                // 约束
                ContextMenuHub::addAction(menu, this, "位移约束",
                                          [this, uuid]
                                          {
                                              this->addDisplacementConstraint();
                                          });

                ContextMenuHub::addSeparator(menu);

                // IIWL.DAT - Part1/2
                ContextMenuHub::addAction(menu, this, "现场地应力信息",
                                          [this, uuid]
                                          {
                                              this->addGeoStressPointPage(); // GeoStressPointPage
                                          });

                ContextMenuHub::addAction(
                    menu, this, "梯形边界荷载",
                    [this, uuid]
                    {
                        this->addGeoStressFitFacePage(); // GeoStressFitFacePage
                    });

                ContextMenuHub::addSeparator(menu);

                // IIWL.DAT - Part3/4/6/7/9-11
                ContextMenuHub::addAction(menu, this, "体积力荷载",
                                          [this, uuid]
                                          {
                                              this->addBodyForcePage(); // BodyForcePage
                                          });

                ContextMenuHub::addAction(
                    menu, this, "集中力荷载",
                    [this, uuid]
                    {
                        this->addConcentratedForcePage(); // ConcentratedForcePage
                    });

                ContextMenuHub::addAction(menu, this, "线力荷载",
                                          [this, uuid]
                                          {
                                              this->addLinearForcePage(); // ConcentratedForcePage
                                          });

                ContextMenuHub::addAction(menu, this, "面力荷载",
                                          [this, uuid]
                                          {
                                              this->addSurfaceForcePage(); // SurfaceForcePage
                                          });

                ContextMenuHub::addAction(menu, this, "变温荷载",
                                          [this, uuid]
                                          {
                                              this->addTemperatureLoadPage(); // TemperatureLoadPage
                                          });

                ContextMenuHub::addAction(menu, this, "梁单元荷载",
                                          [this, uuid]
                                          {
                                              this->addBeamLoadPage(); // addBeamLoadPage
                                          });

                ContextMenuHub::addSeparator(menu);

                ContextMenuHub::addAction(
                    menu, this, "地震加速度表",
                    [this, uuid]
                    {
                        this->addSeismicAndAddedMassPage(); // SeismicAndAddedMassPage
                    });

                return;
            }

            // ===== 子节点：通用操作 =====
            ContextMenuHub::addAction(menu, this, "重建",
                                      [this, uuid]
                                      {
                                          // rebuild(uuid);
                                      });

            ContextMenuHub::addAction(menu, this, "隐藏/显示",
                                      [this, uuid]
                                      {
                                          // toggle(uuid);
                                      });

            // 约束与荷载子节点支持删除
            if (kind != "PhysicalFieldRoot")
            {
                ContextMenuHub::addAction(menu, this, "删除",
                                          [this, uuid]
                                          {
                                              removeNodeByUuid(uuid);
                                          });
            }
        },
        100,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value("module").toString() == "PhysicalField";
        });
}

void PhysicalField::removeNodeByUuid(const QString& uuid)
{
    if (uuid.isEmpty() || !m_node)
        return;

    ProjectNode* node = findNodeByUuid(m_node, uuid);
    if (!node || node == m_node)
        return;

    if (!node->getChildren().isEmpty())
        return;

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (parent)
        parent->removeChild(node);

    if (node->getPage())
        delete node->getPage();

    delete node;
}

void PhysicalField::newProject()
{
    auto* physicalFieldSettingWidget = new PhysicalFieldSettingWidget;

    QString uuid = QUuid::createUuid().toString();
    m_node = new ProjectNode("约束与荷载", uuid, (BasePage*)physicalFieldSettingWidget);

    // 关键：把上下文写进 item data
    m_node->setData(0, ProjectNode::RoleUuid, m_node->getUuid());
    m_node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    m_node->setData(0, ProjectNode::RoleKind, "PhysicalFieldRoot");

    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_node);
}

bool PhysicalField::writeJsonFile(const QString& filePath, const QJsonObject& obj) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

bool PhysicalField::readJsonFile(const QString& filePath, QJsonObject& out) const
{
    QFile f(filePath);
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}

bool PhysicalField::saveProject()
{
    if (!m_node)
        return true;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPhysicalFieldProjectFileName);

    QJsonObject root;
    root["module"] = "PhysicalField";
    root["root_title"] = m_node->text(0);
    root["root_uuid"] = m_node->getUuid();

    QJsonArray items;
    for (ProjectNode* node : m_node->getChildren())
    {
        if (!node || !node->getPage())
            continue;

        const QString uuid = node->data(0, ProjectNode::RoleUuid).toString().isEmpty()
                                 ? node->getUuid()
                                 : node->data(0, ProjectNode::RoleUuid).toString();

        const QString domain = node->getPage()->getPageType();
        const QString kind = node->data(0, ProjectNode::RoleKind).toString();

        auto toJson = [](const QVector<vtkIdType>& ids)
        {
            QJsonArray a;
            for (auto id : ids)
                a.append(static_cast<qint64>(id));
            return a;
        };

        QJsonObject item;
        item["uuid"] = uuid;
        item["title"] = node->text(0);
        item["kind"] = kind;
        item["domain"] = domain;
        item["node_solid"] =
            toJson(PickManager::instance().getSelectedPointIds(domain, uuid, GridKind::Solid));
        item["node_line"] =
            toJson(PickManager::instance().getSelectedPointIds(domain, uuid, GridKind::Line));
        item["edge_solid"] = toJson(
            PickManager::instance().getIdList(domain, uuid, PickMode::Edge, GridKind::Solid));
        item["edge_line"] =
            toJson(PickManager::instance().getIdList(domain, uuid, PickMode::Edge, GridKind::Line));
        item["face_solid"] = toJson(
            PickManager::instance().getIdList(domain, uuid, PickMode::Face, GridKind::Solid));
        item["face_line"] =
            toJson(PickManager::instance().getIdList(domain, uuid, PickMode::Face, GridKind::Line));
        item["page_data"] = node->getPage()->saveJson();
        items.append(item);
    }
    root["items"] = items;
    return writeJsonFile(filePath, root);
}

ProjectNode* PhysicalField::createNodeByKind(const QString& kind, const QString& title,
                                             const QString& uuid)
{
    if (!m_node)
        return nullptr;

    BasePage* page = nullptr;
    QString roleKind = kind;

    if (kind == "DisplacementConstraint")
        page = new DisplacementConstraintWidget();
    else if (kind == "GeoStressPointPage")
        page = new GeoStressPointPage();
    else if (kind == "GeoStressFitFacePage")
        page = new GeoStressFitFacePage();
    else if (kind == "BodyForcePage")
        page = new BodyForcePage();
    else if (kind == "PointForcePage")
        page = new PointForcePage();
    else if (kind == "LinearForcePage")
        page = new LinearForcePage();
    else if (kind == "FaceLoadPage")
        page = new FaceLoadPage();
    else if (kind == "TemperatureLoadPage")
        page = new TemperatureLoadPage();
    else if (kind == "BeamLoadPage")
        page = new BeamLoadPage();
    else if (kind == "SeismicAndAddedMassPage")
        page = new SeismicAndAddedMassPage();
    else
        return nullptr;

    auto* node = new ProjectNode(title, uuid, page);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, roleKind);
    m_node->addChild(node);
    return node;
}

void PhysicalField::restoreNodeSelection(ProjectNode* node, const QJsonObject& item)
{
    if (!node || !node->getPage())
        return;

    const QString domain = node->getPage()->getPageType();
    const QString uuid = node->data(0, ProjectNode::RoleUuid).toString();

    auto fromJson = [](const QJsonArray& arr)
    {
        QVector<vtkIdType> out;
        out.reserve(arr.size());
        for (const auto& v : arr)
            out.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
        return out;
    };

    PickManager::instance().setSelectedPointIds(domain, uuid, GridKind::Solid,
                                                fromJson(item.value("node_solid").toArray()), true);
    PickManager::instance().setSelectedPointIds(domain, uuid, GridKind::Line,
                                                fromJson(item.value("node_line").toArray()), true);
    PickManager::instance().setSelectedEdgeKeys(domain, uuid, GridKind::Solid,
                                                fromJson(item.value("edge_solid").toArray()), true);
    PickManager::instance().setSelectedEdgeKeys(domain, uuid, GridKind::Line,
                                                fromJson(item.value("edge_line").toArray()), true);
    PickManager::instance().setSelectedFaceKeys(domain, uuid, GridKind::Solid,
                                                fromJson(item.value("face_solid").toArray()), true);
    PickManager::instance().setSelectedFaceKeys(domain, uuid, GridKind::Line,
                                                fromJson(item.value("face_line").toArray()), true);
}

bool PhysicalField::loadProject()
{
    newProject();
    if (!m_node)
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPhysicalFieldProjectFileName);

    QJsonObject root;
    if (!readJsonFile(filePath, root))
    {
        return true;
    }

    for (const auto& v : root.value("items").toArray())
    {
        const QJsonObject item = v.toObject();
        const QString kind = item.value("kind").toString();
        const QString title = item.value("title").toString();
        const QString uuid = item.value("uuid").toString(QUuid::createUuid().toString());
        ProjectNode* node = createNodeByKind(kind, title, uuid);
        if (!node)
            continue;
        node->getPage()->loadJson(item.value("page_data").toObject());
        restoreNodeSelection(node, item);
    }

    return true;
}

void PhysicalField::addDisplacementConstraint()
{
    auto* displacementConstraintWidget = new DisplacementConstraintWidget;
    const QString title = MakeIndexedTitle(m_node, "DisplacementConstraint", "位移约束");

    ProjectNode* node2 = new ProjectNode(title, QUuid::createUuid().toString(),
                                         (BasePage*)displacementConstraintWidget);
    node2->setData(0, ProjectNode::RoleUuid, node2->getUuid());
    node2->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node2->setData(0, ProjectNode::RoleKind, "DisplacementConstraint");
    m_node->addChild(node2);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node2);
}

void PhysicalField::addGeoStressPointPage()
{
    auto* geoStressPointPage = new GeoStressPointPage;
    const QString title = MakeIndexedTitle(m_node, "GeoStressPointPage", "现场地应力信息");

    ProjectNode* node3 =
        new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)geoStressPointPage);
    node3->setData(0, ProjectNode::RoleUuid, node3->getUuid());
    node3->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node3->setData(0, ProjectNode::RoleKind, "GeoStressPointPage");
    m_node->addChild(node3);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node3);
}

void PhysicalField::addGeoStressFitFacePage()
{
    auto* geoStressFitFacePage = new GeoStressFitFacePage;
    const QString title = MakeIndexedTitle(m_node, "GeoStressFitFacePage", "梯形边界荷载");
    ProjectNode* node3 =
        new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)geoStressFitFacePage);
    node3->setData(0, ProjectNode::RoleUuid, node3->getUuid());
    node3->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node3->setData(0, ProjectNode::RoleKind, "GeoStressFitFacePage");
    m_node->addChild(node3);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node3);
}

void PhysicalField::addBodyForcePage()
{
    auto* page = new BodyForcePage;
    const QString title = MakeIndexedTitle(m_node, "BodyForcePage", "体积力荷载");

    ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)page);

    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "BodyForcePage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PhysicalField::addConcentratedForcePage()
{
    auto* page = new PointForcePage;
    const QString title = MakeIndexedTitle(m_node, "PointForcePage", "集中力荷载");

    ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "PointForcePage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PhysicalField::addLinearForcePage()
{
    auto* page = new LinearForcePage;
    const QString title = MakeIndexedTitle(m_node, "LinearForcePage", "线力荷载");

    ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "LinearForcePage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PhysicalField::addSurfaceForcePage()
{
    auto* page = new FaceLoadPage;
    const QString title = MakeIndexedTitle(m_node, "FaceLoadPage", "面力荷载");

    ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "FaceLoadPage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PhysicalField::addTemperatureLoadPage()
{
    auto* page = new TemperatureLoadPage;
    const QString title = MakeIndexedTitle(m_node, "TemperatureLoadPage", "变温荷载");

    ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "TemperatureLoadPage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PhysicalField::addBeamLoadPage()
{
    auto* page = new BeamLoadPage;
    const QString title = MakeIndexedTitle(m_node, "BeamLoadPage", "梁单元荷载");

    ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "BeamLoadPage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PhysicalField::addSeismicAndAddedMassPage()
{
    auto* page = new SeismicAndAddedMassPage;
    ProjectNode* node =
        new ProjectNode("地震加速度", QUuid::createUuid().toString(), (BasePage*)page);
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    node->setData(0, ProjectNode::RoleKind, "SeismicAndAddedMassPage");
    m_node->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

// -------------------- 普通工具函数：应用选中点到 flags（无 lambda）
// --------------------
static void ApplyPickedNodeIdsToFlags(const QVector<vtkIdType>& pickedNodeIds, int ux, int uy,
                                      int uz, const QHash<int, int>& rowOfNodeId,
                                      QVector<std::array<int, 3>>& flags)
{
    for (int i = 0; i < pickedNodeIds.size(); ++i)
    {
        const int nodeId = int(pickedNodeIds[i]);

        auto it = rowOfNodeId.find(nodeId);
        if (it == rowOfNodeId.end())
            continue;

        const int r = it.value();
        flags[r][0] = ux;
        flags[r][1] = uy;
        flags[r][2] = uz;
    }
}

ProjectNode* PhysicalField::findNodeByUuid(ProjectNode* root, const QString& uuid) const
{
    if (!root)
        return nullptr;

    const QString u = root->data(0, ProjectNode::RoleUuid).toString();
    if (u == uuid)
        return root;

    for (int i = 0; i < root->childCount(); ++i)
    {
        ProjectNode* hit = findNodeByUuid((ProjectNode*)root->child(i), uuid);
        if (hit)
            return hit;
    }
    return nullptr;
}

// -------------------- 递归收集 kind == "DisplacementConstraint" 的节点
// --------------------
void PhysicalField::collectDisplacementNodes(ProjectNode* root, QList<ProjectNode*>& out)
{
    if (!root)
        return;

    const QString kind = root->data(0, ProjectNode::RoleKind).toString();
    if (kind == "DisplacementConstraint")
        out.push_back(root);

    for (int i = 0; i < root->childCount(); ++i)
        collectDisplacementNodes((ProjectNode*)root->child(i), out);
}

void PhysicalField::writeIIWL_Part3_BodyForce(QTextStream& os)
{
    setSci(os);

    QList<ProjectNode*> nodes;

    CollectNodesByKind(m_node, "BodyForcePage", nodes);

    // 没有体积力页：按规范写 NFB=0，仍写说明行
    if (nodes.isEmpty())
    {
        os << 0 << "\n";
        os << "JMFB\tFBX\tFBY\tFBZ\tII\n";
        return;
    }

    QVector<BodyForcePage::Part3Row> allRows;
    allRows.reserve(256);

    QSet<int> iiSet;
    for (auto* p : nodes)
    {
        auto* page = dynamic_cast<BodyForcePage*>(p->getPage());
        if (!p)
            continue;
        iiSet.insert(page->ii());

        const auto rows = page->buildPart3Rows();
        for (const auto& r : rows)
            allRows.push_back(r);
    }

    int II = 1;
    if (!iiSet.isEmpty())
        II = *iiSet.begin();
    if (iiSet.size() > 1)
        qWarning() << "Part3 BodyForce: multiple II values found, using" << II;

    const int NFB = allRows.size();

    os << NFB << "\n";
    os << "JMFB\tFBX\tFBY\tFBZ\tII\n";

    // 数据行：JD FBX FBY FBZ（规范里数据行不含 II；II 在说明行体现）
    // 如果你求解器要求每行也带 II，你把下面行末加 "\t" << II
    for (const auto& r : allRows)
    {
        os << r.jd << "\t" << r.fbx << "\t" << r.fby << "\t" << r.fbz << "\n";
    }
}

void PhysicalField::generateIIIDFile()
{
    PickManager::instance().updateStructure();
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();

    vtkUnstructuredGrid* gSolid = PickManager::instance().solidGrid();
    vtkUnstructuredGrid* gLine = PickManager::instance().lineGrid();
    vtkUnstructuredGrid* gAll = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;

    if (!gSolid && !gLine && !gAll)
    {
        qWarning() << "generateIIIDFile: no available grid (solid/line/all are null)";
        return;
    }

    auto findNodeIdArray = [](vtkUnstructuredGrid* g) -> vtkIntArray*
    {
        if (!g || !g->GetPointData())
            return nullptr;
        if (auto* a = vtkIntArray::SafeDownCast(g->GetPointData()->GetArray("node_id")))
            return a;
        return vtkIntArray::SafeDownCast(g->GetPointData()->GetArray("NodeId"));
    };

    vtkIntArray* idSolid = findNodeIdArray(gSolid);
    vtkIntArray* idLine = findNodeIdArray(gLine);
    vtkIntArray* idAll = findNodeIdArray(gAll);

    if ((gSolid && !idSolid) || (gLine && !idLine) || (gAll && !idAll))
    {
        qWarning() << "generateIIIDFile: node id array missing on some grid";
        return;
    }

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    qInfo()<<"NDIM "<<NDIM;
    const bool is3D = (NDIM == 3);
    const int MADOF = is3D ? 3 : 2;

    // 1) 收集所有 node_id（all + solid + line），去重并排序
    QVector<int> allNodeIds;
    allNodeIds.reserve((gAll ? int(gAll->GetNumberOfPoints()) : 0) +
                       (gSolid ? int(gSolid->GetNumberOfPoints()) : 0) +
                       (gLine ? int(gLine->GetNumberOfPoints()) : 0));

    if (gAll)
    {
        const int n = int(gAll->GetNumberOfPoints());
        for (int i = 0; i < n; ++i)
            allNodeIds.push_back(idAll->GetValue(i));
    }

    if (gSolid)
    {
        const int n = int(gSolid->GetNumberOfPoints());
        for (int i = 0; i < n; ++i)
            allNodeIds.push_back(idSolid->GetValue(i));
    }
    if (gLine)
    {
        const int n = int(gLine->GetNumberOfPoints());
        for (int i = 0; i < n; ++i)
            allNodeIds.push_back(idLine->GetValue(i));
    }

    std::sort(allNodeIds.begin(), allNodeIds.end());
    allNodeIds.erase(std::unique(allNodeIds.begin(), allNodeIds.end()), allNodeIds.end());

    const int NUMNP = allNodeIds.size();

    // 2) node_id -> 输出行号
    QHash<int, int> rowOfNodeId;
    rowOfNodeId.reserve(int(NUMNP * 1.3));
    for (int r = 0; r < NUMNP; ++r)
        rowOfNodeId.insert(allNodeIds[r], r);

    // 3) 默认全自由：2D => 1 1；3D => 1 1 1
    QVector<std::array<int, 3>> flags(NUMNP);
    for (int r = 0; r < NUMNP; ++r)
        flags[r] = {1, 1, 1};

    // 4) 收集位移约束节点
    QList<ProjectNode*> bcNodes;
    collectDisplacementNodes(m_node, bcNodes);
    qInfo() << "bcNodes size:" << bcNodes.size();

    const QString domain = "DisplacementConstraintWidget"; // 必须与 activate 一致

    // 5) 应用每个约束节点的选中点
    for (int k = 0; k < bcNodes.size(); ++k)
    {
        ProjectNode* n = bcNodes[k];
        if (!n)
            continue;

        const QString uuid = n->getUuid();

        int ux = 1, uy = 1, uz = 1;
        DisplacementConstraintWidget* page =
            dynamic_cast<DisplacementConstraintWidget*>(n->getPage());
        if (page)
        {
            ux = page->uxFlag();
            uy = page->uyFlag();
            uz = is3D ? page->uzFlag() : 1;
            qInfo() << "apply bc uuid=" << uuid << " ux,uy,uz=" << ux << uy << uz;
        }

        // solid picks
        {
            const QVector<vtkIdType> pickedSolid =
                PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Solid);

            qInfo() << uuid << " pickedSolid(node_id) " << pickedSolid;
            ApplyPickedNodeIdsToFlags(pickedSolid, ux, uy, uz, rowOfNodeId, flags);
        }

        // line picks（现在返回 node_id）
        {
            const QVector<vtkIdType> pickedLine =
                PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Line);

            qInfo() << uuid << " pickedLine(node_id) " << pickedLine;
            ApplyPickedNodeIdsToFlags(pickedLine, ux, uy, uz, rowOfNodeId, flags);
        }
    }

    // 5.5) 孤立点（未被任何 cell 使用）强制输出全 0 约束
    if (gAll && idAll)
    {
        vtkNew<vtkIdList> cellIds;
        const vtkIdType nPts = gAll->GetNumberOfPoints();
        for (vtkIdType pid = 0; pid < nPts; ++pid)
        {
            cellIds->Reset();
            gAll->GetPointCells(pid, cellIds);
            if (cellIds->GetNumberOfIds() > 0)
                continue;

            const int nodeId = idAll->GetValue(int(pid));
            auto it = rowOfNodeId.find(nodeId);
            if (it == rowOfNodeId.end())
                continue;

            flags[it.value()] = {0, 0, 0};
        }
    }

    // 6) 写文件到 工程目录/PREP/IIID.DAT
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP"))
        wd.mkpath("PREP");
    const QString path = wd.filePath("PREP/IIID.DAT");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "generateIIIDFile: open failed:" << path;
        return;
    }

    QTextStream os(&f);
    os << NUMNP << "\t" << MADOF << "\n";
    os << NUMNP << "\t" << MADOF << "\n";

    for (int r = 0; r < NUMNP; ++r)
    {
        const int nodeId = allNodeIds[r];
        os << flags[r][0] << "\t" << flags[r][1] << "\t";
        if (is3D)
            os << flags[r][2] << "\t";
        os << nodeId << "\n";
    }

    f.close();
    qInfo() << "IIID generated:" << path;
}


bool PhysicalField::importIIIDFile(const QString& filePath, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        qWarning() << "importIIIDFile:" << message;
        return false;
    };

    if (!m_node)
        return fail(QStringLiteral("约束与荷载节点尚未初始化"));

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("无法打开文件：%1").arg(filePath));

    QTextStream is(&f);
    auto readDataLine = [&is](QString& out) -> bool
    {
        while (!is.atEnd())
        {
            out = is.readLine().trimmed();
            if (out.isEmpty())
                continue;
            if (out.startsWith('#') || out.startsWith('!'))
                continue;
            return true;
        }
        return false;
    };

    QString line;
    if (!readDataLine(line))
        return fail(QStringLiteral("IIID.DAT 文件为空"));

    const QStringList header = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
    if (header.size() < 2)
        return fail(QStringLiteral("IIID.DAT 首行格式错误，应为：节点数 自由度数"));

    bool okNum = false, okDof = false;
    const int numNp = header.at(0).toInt(&okNum);
    const int maDof = header.at(1).toInt(&okDof);
    if (!okNum || !okDof || numNp < 0 || (maDof != 2 && maDof != 3))
        return fail(QStringLiteral("IIID.DAT 首行节点数或自由度数无效"));

    // 生成逻辑会写两行相同文件头；导入时兼容有/无第二行文件头两种格式。
    if (!readDataLine(line))
        return fail(QStringLiteral("IIID.DAT 缺少约束数据"));

    QStringList parts = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
    if (parts.size() == 2)
    {
        bool okHeader2Num = false, okHeader2Dof = false;
        const int secondNum = parts.at(0).toInt(&okHeader2Num);
        const int secondDof = parts.at(1).toInt(&okHeader2Dof);
        if (!okHeader2Num || !okHeader2Dof || secondNum != numNp || secondDof != maDof)
            return fail(QStringLiteral("IIID.DAT 第二行文件头与首行不一致"));
        if (!readDataLine(line) && numNp > 0)
            return fail(QStringLiteral("IIID.DAT 缺少约束数据行"));
        parts = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
    }

    QMap<QString, QVector<int>> groupedNodeIds;
    int parsedRows = 0;
    while (parsedRows < numNp)
    {
        if (parts.size() < maDof + 1)
            return fail(QStringLiteral("IIID.DAT 第 %1 条约束数据格式错误").arg(parsedRows + 1));

        bool okUx = false, okUy = false, okUz = true, okNode = false;
        const int ux = parts.at(0).toInt(&okUx);
        const int uy = parts.at(1).toInt(&okUy);
        const int uz = (maDof == 3) ? parts.at(2).toInt(&okUz) : 1;
        const int nodeId = parts.at(maDof).toInt(&okNode);
        if (!okUx || !okUy || !okUz || !okNode)
            return fail(QStringLiteral("IIID.DAT 第 %1 条约束数据包含非整数字段").arg(parsedRows + 1));

        if (!(ux == 1 && uy == 1 && uz == 1))
        {
            const QString key = QStringLiteral("%1,%2,%3").arg(ux).arg(uy).arg(uz);
            groupedNodeIds[key].push_back(nodeId);
        }

        ++parsedRows;
        if (parsedRows >= numNp)
            break;
        if (!readDataLine(line))
            return fail(QStringLiteral("IIID.DAT 数据行数量少于文件头声明的 %1 行").arg(numNp));
        parts = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
    }

    // IIID.DAT 中记录的是 node_id；PickManager 的 setSelectedPointIds 需要的是各网格内
    // 的 VTK point id。因此导入前必须先把 node_id 映射回 Solid/Line 网格点索引。
    PickManager::instance().updateStructure();

    auto findNodeIdArray = [](vtkUnstructuredGrid* g) -> vtkIntArray*
    {
        if (!g || !g->GetPointData())
            return nullptr;
        if (auto* a = vtkIntArray::SafeDownCast(g->GetPointData()->GetArray("node_id")))
            return a;
        return vtkIntArray::SafeDownCast(g->GetPointData()->GetArray("NodeId"));
    };

    auto buildPointIdByNodeId = [&findNodeIdArray](vtkUnstructuredGrid* g)
    {
        QHash<int, vtkIdType> out;
        vtkIntArray* nodeIdArr = findNodeIdArray(g);
        if (!g || !nodeIdArr)
            return out;

        const vtkIdType nPts = g->GetNumberOfPoints();
        out.reserve(int(nPts * 1.3));
        for (vtkIdType pid = 0; pid < nPts; ++pid)
        {
            const int nodeId = nodeIdArr->GetValue(int(pid));
            if (!out.contains(nodeId))
                out.insert(nodeId, pid);
        }
        return out;
    };

    auto buildIsolatedNodeIds = [&findNodeIdArray](vtkUnstructuredGrid* g)
    {
        QSet<int> out;
        vtkIntArray* nodeIdArr = findNodeIdArray(g);
        if (!g || !nodeIdArr)
            return out;

        vtkNew<vtkIdList> cellIds;
        const vtkIdType nPts = g->GetNumberOfPoints();
        for (vtkIdType pid = 0; pid < nPts; ++pid)
        {
            cellIds->Reset();
            g->GetPointCells(pid, cellIds);
            if (cellIds->GetNumberOfIds() == 0)
                out.insert(nodeIdArr->GetValue(int(pid)));
        }
        return out;
    };

    const QHash<int, vtkIdType> solidPointIdByNodeId =
        buildPointIdByNodeId(PickManager::instance().solidGrid());
    const QHash<int, vtkIdType> linePointIdByNodeId =
        buildPointIdByNodeId(PickManager::instance().lineGrid());

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* gAll = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
    const QSet<int> isolatedNodeIds = buildIsolatedNodeIds(gAll);

    // 导入是生成 IIID.DAT 的反向操作：先移除已有位移约束，再按文件内容重建。
    QList<ProjectNode*> oldConstraints;
    collectDisplacementNodes(m_node, oldConstraints);
    for (ProjectNode* node : oldConstraints)
    {
        if (!node)
            continue;
        const QString uuid = node->getUuid();
        const QString domain = node->getPage() ? node->getPage()->getPageType()
                                                : QStringLiteral("DisplacementConstraintWidget");
        PickManager::instance().setSelectedPointIds(domain, uuid, GridKind::Solid, QVector<vtkIdType>(), true);
        PickManager::instance().setSelectedPointIds(domain, uuid, GridKind::Line, QVector<vtkIdType>(), true);
        if (auto* parent = static_cast<ProjectNode*>(node->parent()))
            parent->removeChild(node);
        if (node->getPage())
            delete node->getPage();
        delete node;
    }

    ProjectNode* lastCreated = nullptr;
    for (auto it = groupedNodeIds.cbegin(); it != groupedNodeIds.cend(); ++it)
    {
        const QStringList flags = it.key().split(',');
        const bool isBarConstraint = flags.contains(QStringLiteral("-10000"));

        QVector<vtkIdType> solidPointIds;
        QVector<vtkIdType> linePointIds;
        for (const int nodeId : it.value())
        {
            if (isolatedNodeIds.contains(nodeId))
                continue;

            if (isBarConstraint)
            {
                // 杆单元约束在 IIID.DAT 中用 -10000 标识；不是 -10000 的约束
                // 只进入 lineGrid，不再检查实体网格。
                if (linePointIdByNodeId.contains(nodeId))
                    linePointIds.push_back(linePointIdByNodeId.value(nodeId));
                else
                    qWarning() << "importIIIDFile: bar node_id not found in line grid:" << nodeId;
            }
            else
            {
                if (solidPointIdByNodeId.contains(nodeId))
                    solidPointIds.push_back(solidPointIdByNodeId.value(nodeId));
                else
                    qWarning() << "importIIIDFile: node_id not found in solid grid:" << nodeId;
            }
        }

        std::sort(solidPointIds.begin(), solidPointIds.end());
        solidPointIds.erase(std::unique(solidPointIds.begin(), solidPointIds.end()),
                            solidPointIds.end());
        std::sort(linePointIds.begin(), linePointIds.end());
        linePointIds.erase(std::unique(linePointIds.begin(), linePointIds.end()),
                           linePointIds.end());

        if (solidPointIds.isEmpty() && linePointIds.isEmpty())
        {
            qWarning() << "importIIIDFile: skip empty constraint group:" << it.key();
            continue;
        }

        const QString title = MakeIndexedTitle(m_node, QStringLiteral("DisplacementConstraint"),
                                               QStringLiteral("位移约束"));
        ProjectNode* node = createNodeByKind(QStringLiteral("DisplacementConstraint"), title,
                                             QUuid::createUuid().toString());
        if (!node || !node->getPage())
            continue;

        QJsonObject pageData;
        pageData["ux"] = flags.value(0, QStringLiteral("1"));
        pageData["uy"] = flags.value(1, QStringLiteral("1"));
        pageData["uz"] = flags.value(2, QStringLiteral("1"));
        node->getPage()->loadJson(pageData);

        const QString domain = node->getPage()->getPageType();
        PickManager::instance().setSelectedPointIds(domain, node->getUuid(), GridKind::Solid,
                                                    solidPointIds, true);
        PickManager::instance().setSelectedPointIds(domain, node->getUuid(), GridKind::Line,
                                                    linePointIds, true);
        lastCreated = node;
    }

    if (lastCreated)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(lastCreated);
    }

    qInfo() << "IIID imported:" << filePath << "constraint groups=" << groupedNodeIds.size();
    return true;
}

QString PhysicalField::pageTitleByUuid(const QString& uuid) const
{
    ProjectNode* n = findNodeByUuid(m_node, uuid);
    return n ? n->text(0) : QString();
}

// -------------------- helpers --------------------
static QString fmtE(double v)
{
    // 0.000000E+00 风格
    return QString("%1").arg(v, 0, 'E', 6).toUpper();
}

struct XYZ
{
    double x = 0, y = 0, z = 0;
};

static void FillXYZForWantedNodeIds(QSet<int>& wanted, QHash<int, XYZ>& xyzOf,
                                    vtkUnstructuredGrid* g, vtkIntArray* nodeIdArr)
{
    if (!g || !nodeIdArr || wanted.isEmpty())
        return;

    const vtkIdType nPts = g->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        const int nodeId = nodeIdArr->GetValue(int(pid));
        if (!wanted.contains(nodeId))
            continue;

        double p[3];
        g->GetPoint(pid, p);

        XYZ xyz;
        xyz.x = p[0];
        xyz.y = p[1];
        xyz.z = p[2];
        xyzOf.insert(nodeId, xyz);

        wanted.remove(nodeId);
        if (wanted.isEmpty())
            break;
    }
}

void PhysicalField::writeIIWL_Part1_GeoStressPoint(QTextStream& os)
{
    PickManager::instance().updateStructure();

    vtkUnstructuredGrid* gSolid = PickManager::instance().solidGrid();
    vtkUnstructuredGrid* gLine = PickManager::instance().lineGrid();

    vtkIntArray* idSolid =
        gSolid ? vtkIntArray::SafeDownCast(gSolid->GetPointData()->GetArray("node_id")) : nullptr;
    vtkIntArray* idLine =
        gLine ? vtkIntArray::SafeDownCast(gLine->GetPointData()->GetArray("node_id")) : nullptr;

    if (!gSolid && !gLine)
    {
        qWarning() << "IIWL Part1: both solidGrid and lineGrid are null";
        return;
    }
    if ((gSolid && !idSolid) || (gLine && !idLine))
    {
        qWarning() << "IIWL Part1: node_id array missing on some grid";
        return;
    }

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const int NCOM = (NDIM == 2) ? 3 : 6;

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "GeoStressPointPage", pages);

    const QString domain = "GeoStressPointPage";

    struct StressVal
    {
        double sx = 0, sy = 0, sz = 0, sxy = 0, sxz = 0, syz = 0;
    };
    QHash<int, StressVal> stressOfNode;
    QVector<int> nodeOrder;
    QSet<int> seen;

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;
        const QString uuid = n->getUuid();
        auto* page = dynamic_cast<GeoStressPointPage*>(n->getPage());
        if (!page)
            continue;

        StressVal s;
        s.sx = page->sxValue();
        s.sy = page->syValue();
        s.sz = page->szValue();
        s.sxy = page->sxyValue();
        s.sxz = page->sxzValue();
        s.syz = page->syzValue();

        const QVector<vtkIdType> pickedSolid =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Solid);
        const QVector<vtkIdType> pickedLine =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Line);

        QSet<int> idsThisPage;
        for (auto v : pickedSolid)
            idsThisPage.insert(int(v));
        for (auto v : pickedLine)
            idsThisPage.insert(int(v));

        for (int nodeId : idsThisPage)
        {
            if (seen.contains(nodeId))
                continue;
            seen.insert(nodeId);
            stressOfNode.insert(nodeId, s);
            nodeOrder.push_back(nodeId);
        }
    }

    const int NUM_GEO = nodeOrder.size();

    QHash<int, XYZ> xyzOf;
    xyzOf.reserve(int(NUM_GEO * 1.3));

    QSet<int> wanted;
    wanted.reserve(int(NUM_GEO * 1.3));
    for (int id : nodeOrder)
        wanted.insert(id);

    FillXYZForWantedNodeIds(wanted, xyzOf, gSolid, idSolid);
    FillXYZForWantedNodeIds(wanted, xyzOf, gLine, idLine);

    if (!wanted.isEmpty())
        qWarning() << "IIWL Part1: some node_id cannot find xyz:" << wanted.values();

    // ---- Part1 输出 ----
    os << QString("%1\t%2\t%3\tNUM_GEO\tNDIM\tNCOM\n").arg(NUM_GEO).arg(NDIM).arg(NCOM);

    if (NDIM == 2)
        os << "XP\tYP\tSGX\tSGY\tTXY\tII\n";
    else
        os << "XP\tYP\tZP\tSGX\tSGY\tSGZ\tTYZ\tTZX\tTXY\tII\n";

    // ✅ II：顺序号（1..NUM_GEO）
    for (int i = 0; i < nodeOrder.size(); ++i)
    {
        const int nodeId = nodeOrder[i];
        const XYZ xyz = xyzOf.value(nodeId, XYZ{0, 0, 0});
        const StressVal s = stressOfNode.value(nodeId, StressVal{});

        const int II = i + 1;

        if (NDIM == 2)
        {
            os << fmtE(xyz.x) << "\t" << fmtE(xyz.y) << "\t" << fmtE(s.sx) << "\t" << fmtE(s.sy)
               << "\t" << fmtE(s.sxy) << "\t" << II << "\n";
        }
        else
        {
            os << fmtE(xyz.x) << "\t" << fmtE(xyz.y) << "\t" << fmtE(xyz.z) << "\t" << fmtE(s.sx)
               << "\t" << fmtE(s.sy) << "\t" << fmtE(s.sz) << "\t" << fmtE(s.syz) << "\t"
               << fmtE(s.sxz) << "\t" << fmtE(s.sxy) << "\t" << II << "\n";
        }
    }
}

void PhysicalField::writeIIWL_Part2_GeoStressFitFace(QTextStream& os)
{
    // 找到所有 GeoStressFitFacePage
    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "GeoStressFitFacePage", pages);

    const int NFA_GEO = pages.size();

    os << "\n"; // Part1 与 Part2 分隔（可选）
    os << QString("%1\tNFA_GEO\n").arg(NFA_GEO);
    os << "FNAME\tIDIR\tHEIGHT\tGAMA\tPGROUT\tII\n";

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;
        auto* page = dynamic_cast<GeoStressFitFacePage*>(n->getPage());
        if (!page)
            continue;

        page->exportLeftDatFile();

        const QString fname = quoteIfNeeded(page->fname());
        const int idir = page->idir();
        const double height = page->height();
        const double rou = page->rou();
        const double pgrout = page->pgrout();
        const int ii = page->ii();

        os << fname << "\t" << idir << "\t" << fmtE(height) << "\t" << fmtE(rou) << "\t"
           << fmtE(pgrout) << "\t" << ii << "\n";
    }
}

void PhysicalField::writeIIWL_Part4_PointForce(QTextStream& os)
{
    PickManager::instance().updateStructure();

    vtkUnstructuredGrid* gSolid = PickManager::instance().solidGrid();
    vtkUnstructuredGrid* gLine = PickManager::instance().lineGrid();

    vtkIntArray* nodeIdSolid =
        gSolid ? vtkIntArray::SafeDownCast(gSolid->GetPointData()->GetArray("node_id")) : nullptr;
    vtkIntArray* nodeIdLine =
        gLine ? vtkIntArray::SafeDownCast(gLine->GetPointData()->GetArray("node_id")) : nullptr;

    vtkIntArray* elemIdSolid =
        gSolid ? vtkIntArray::SafeDownCast(gSolid->GetCellData()->GetArray("elem_id")) : nullptr;
    vtkIntArray* elemIdLine =
        gLine ? vtkIntArray::SafeDownCast(gLine->GetCellData()->GetArray("elem_id")) : nullptr;

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QString domain = "PointForcePage";

    if (!gSolid && !gLine)
    {
        qWarning() << "IIWL Part4: both solidGrid and lineGrid are null";
        return;
    }
    if ((gSolid && !nodeIdSolid) || (gLine && !nodeIdLine))
    {
        qWarning() << "IIWL Part4: node_id array missing";
        return;
    }

    // ---------- 建 node_id -> pid 的快速映射 ----------
    auto buildMapNodeIdToPid = [](vtkUnstructuredGrid* g, vtkIntArray* nodeIdArr)
    {
        QHash<int, vtkIdType> mp;
        if (!g || !nodeIdArr)
            return mp;
        mp.reserve(int(g->GetNumberOfPoints() * 1.3));
        const vtkIdType n = g->GetNumberOfPoints();
        for (vtkIdType pid = 0; pid < n; ++pid)
        {
            mp.insert(nodeIdArr->GetValue(int(pid)), pid);
        }
        return mp;
    };

    const QHash<int, vtkIdType> mapSolid = buildMapNodeIdToPid(gSolid, nodeIdSolid);
    const QHash<int, vtkIdType> mapLine = buildMapNodeIdToPid(gLine, nodeIdLine);

    // ---------- 给定 pid，选一个“所属单元 elem_id”（稳定规则：最小 elem_id）
    // ----------
    auto chooseElemIdForPid = [](vtkUnstructuredGrid* g, vtkIntArray* elemIdArr,
                                 vtkIdType pid) -> int
    {
        if (!g || pid < 0 || pid >= g->GetNumberOfPoints())
            return 0;

        vtkSmartPointer<vtkIdList> cellIds = vtkSmartPointer<vtkIdList>::New();
        g->GetPointCells(pid, cellIds);
        if (!cellIds || cellIds->GetNumberOfIds() <= 0)
            return 0;

        int bestElem = INT_MAX;
        vtkIdType bestCell = cellIds->GetId(0);

        for (vtkIdType i = 0; i < cellIds->GetNumberOfIds(); ++i)
        {
            vtkIdType cid = cellIds->GetId(i);
            if (elemIdArr)
            {
                int eid = elemIdArr->GetValue(int(cid));
                if (eid < bestElem)
                {
                    bestElem = eid;
                    bestCell = cid;
                }
            }
            else
            {
                // 没有 elem_id 就退化用 cellId（+1 可选）
                if (cid < bestCell)
                    bestCell = cid;
            }
        }

        if (elemIdArr)
            return bestElem;
        return int(bestCell); // 或 bestCell+1，看你格式习惯
    };

    // ---------- 收集所有 PointForcePage ----------
    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "PointForcePage", pages);
    qInfo() << "pages.size() " << pages.size();

    struct Row2D
    {
        int jmfc = 0;
        double x = 0, y = 0, fx = 0, fy = 0, m = 0;
    };
    struct Row3D
    {
        int jmfc = 0;
        double x = 0, y = 0, z = 0, fx = 0, fy = 0, fz = 0, mx = 0, my = 0, mz = 0;
    };

    QVector<Row2D> rows2;
    QVector<Row3D> rows3;

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;
        const QString uuid = n->getUuid();

        auto* page = dynamic_cast<PointForcePage*>(n->getPage());
        if (!page)
            continue;

        // 2D: FX FY M
        // 3D: FX FY FZ MX MY MZ
        const double fx = page->fxValue();
        const double fy = page->fyValue();
        const double fz = (NDIM == 3) ? page->fzValue() : 0.0;

        const double mx = (NDIM == 3) ? page->mxValue() : 0.0;
        const double my = (NDIM == 3) ? page->myValue() : 0.0;
        const double mz = page->mzValue(); // 2D 用作 M；3D 用作 MZ

        // 你当前 getIdList(Node) 返回的是 node_id（见你 Part1 的实现）
        const QVector<vtkIdType> idsSolid =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Solid);
        const QVector<vtkIdType> idsLine =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Line);

        qInfo() << "output idsSolid " << idsSolid << " " << idsLine;

        QSet<int> nodeIds;
        for (auto v : idsSolid)
            nodeIds.insert(int(v));
        for (auto v : idsLine)
            nodeIds.insert(int(v));

        QVector<int> nodeList = nodeIds.values().toVector();
        std::sort(nodeList.begin(), nodeList.end());

        for (int nodeId : nodeList)
        {
            // 优先在 solid 找 pid；找不到再去 line
            vtkUnstructuredGrid* g = nullptr;
            vtkIntArray* elemArr = nullptr;
            vtkIdType pid = -1;

            auto itS = mapSolid.find(nodeId);
            if (itS != mapSolid.end())
            {
                g = gSolid;
                elemArr = elemIdSolid;
                pid = itS.value();
            }
            else
            {
                auto itL = mapLine.find(nodeId);
                if (itL != mapLine.end())
                {
                    g = gLine;
                    elemArr = elemIdLine;
                    pid = itL.value();
                }
            }
            if (!g || pid < 0)
                continue;

            double p[3];
            g->GetPoint(pid, p);
            const int jmfc = chooseElemIdForPid(g, elemArr, pid);

            if (NDIM == 2)
            {
                Row2D r;
                r.jmfc = jmfc;
                r.x = p[0];
                r.y = p[1];
                r.fx = fx;
                r.fy = fy;
                r.m = mz; // 2D 的 M
                rows2.push_back(r);
            }
            else
            {
                Row3D r;
                r.jmfc = jmfc;
                r.x = p[0];
                r.y = p[1];
                r.z = p[2];
                r.fx = fx;
                r.fy = fy;
                r.fz = fz;
                r.mx = mx;
                r.my = my;
                r.mz = mz;
                rows3.push_back(r);
            }
        }
    }

    const int NFC = (NDIM == 2) ? rows2.size() : rows3.size();

    os << "\n";
    os << QString("%1\tNFC\n").arg(NFC);

    if (NDIM == 2)
        os << "JMFC\tXC\tYC\tFX\tFY\tM\tII(BLANK)\n";
    else
        os << "JMFC\tXC\tYC\tZC\tFX\tFY\tFZ\tMX\tMY\tMZ\tII(BLANK)\n";

    // II(BLANK) = 序号 1..NFC
    if (NDIM == 2)
    {
        for (int i = 0; i < rows2.size(); ++i)
        {
            const auto& r = rows2[i];
            const int II = i + 1;
            os << r.jmfc << "\t" << fmtE(r.x) << "\t" << fmtE(r.y) << "\t" << fmtE(r.fx) << "\t"
               << fmtE(r.fy) << "\t" << fmtE(r.m) << "\t" << II << "\n";
        }
    }
    else
    {
        for (int i = 0; i < rows3.size(); ++i)
        {
            const auto& r = rows3[i];
            const int II = i + 1;
            os << r.jmfc << "\t" << fmtE(r.x) << "\t" << fmtE(r.y) << "\t" << fmtE(r.z) << "\t"
               << fmtE(r.fx) << "\t" << fmtE(r.fy) << "\t" << fmtE(r.fz) << "\t" << fmtE(r.mx)
               << "\t" << fmtE(r.my) << "\t" << fmtE(r.mz) << "\t" << II << "\n";
        }
    }
}

void PhysicalField::writeIIWL_Part5_LinearForce(QTextStream& os)
{
    PickManager::instance().updateStructure();

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QString domainLine = "LinearForcePage";

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "LinearForcePage", pages);

    qInfo() << "IIWL Part5 LinearForce pages.size=" << pages.size();

    struct Rec
    {
        int ke = 0;
        QVector<int> nodeIds;
        double fx = 0, fy = 0, fz = 0;
    };
    QVector<Rec> recs;

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;
        const QString uuid = n->getUuid();
        auto* page = dynamic_cast<LinearForcePage*>(n->getPage());
        if (!page)
            continue;

        const double fx = page->fxValue();
        const double fy = page->fyValue();
        const double fz = (NDIM == 3) ? page->fzValue() : 0.0;

        // ✅ 线力：用 edgeNodesByKe（优先 lineGrid）
        QHash<int, QVector<int>> nodesByKe =
            PickManager::instance().getEdgeNodesByKe(domainLine, uuid, GridKind::Line);
        if (nodesByKe.isEmpty())
            nodesByKe = PickManager::instance().getEdgeNodesByKe(domainLine, uuid, GridKind::Solid);

        qInfo() << "[Part5] uuid=" << uuid << "KE keys=" << nodesByKe.keys();

        for (auto it = nodesByKe.begin(); it != nodesByKe.end(); ++it)
        {
            const int ke = it.key();
            const QVector<int>& nds = it.value();
            if (nds.isEmpty())
                continue;

            Rec r;
            r.ke = ke;
            r.nodeIds = nds;
            r.fx = fx;
            r.fy = fy;
            r.fz = fz;
            recs.push_back(r);
        }
    }

    const int NFL = recs.size();
    const int LXJMEL = 1; // 已知线荷载作用单元（我们输出 KE）

    os << "\n";
    os << NFL << "\t" << "NFL" << "\n";

    qInfo() << "IIWL Part5 NFL=" << NFL << "LXJMEL=" << LXJMEL;

    // 格式：KE  (线力序号)  NLIJD
    //       J  Fx Fy Fz    (NLIJD 行)
    for (int i = 0; i < recs.size(); ++i)
    {
        const int idx = i + 1;
        const Rec& r = recs[i];
        const int NLIJD = r.nodeIds.size();

        qInfo() << "[Part5] write idx=" << idx << "ke=" << r.ke << "NLIJD=" << NLIJD;

        os << r.ke << "\t" << idx << "\t" << NLIJD << "\n";
        for (int nodeId : r.nodeIds)
        {
            os << nodeId << "\t" << fmtE(r.fx) << "\t" << fmtE(r.fy) << "\t" << fmtE(r.fz) << "\n";
        }
    }
}

void PhysicalField::writeIIWL_Part7_TemperatureLoad(QTextStream& os)
{
    PickManager::instance().updateStructure();

    const QString domain = "TemperatureLoadPage";

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "TemperatureLoadPage", pages);

    qInfo() << "IIWL Part7 TemperatureLoad pages.size=" << pages.size();

    struct Row
    {
        int nodeId = 0;    // JMFW
        double alfa = 0.0; // ALFA
        double dt = 0.0;   // DT
    };

    QVector<Row> rows;
    rows.reserve(256);

    // 去重：同 nodeId 只输出一次（重复时覆盖）
    QHash<int, int> idxOfNode; // nodeId -> index

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;

        const QString uuid = n->getUuid();
        auto* page = dynamic_cast<TemperatureLoadPage*>(n->getPage());
        if (!page)
            continue;

        const double alfa = page->alfaValue();
        const double dt = page->dtValue();

        // Node 模式返回 node_id（不是 pid）
        const QVector<vtkIdType> idsSolid =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Solid);
        const QVector<vtkIdType> idsLine =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Line);

        QSet<int> nodeIds;
        for (auto v : idsSolid)
            nodeIds.insert(int(v));
        for (auto v : idsLine)
            nodeIds.insert(int(v));

        QVector<int> nodeList = nodeIds.values().toVector();
        std::sort(nodeList.begin(), nodeList.end());

        qInfo() << "Part7 uuid=" << uuid << "alfa=" << alfa << "dt=" << dt << "nodes=" << nodeList;

        for (int nodeId : nodeList)
        {
            auto it = idxOfNode.find(nodeId);
            if (it == idxOfNode.end())
            {
                Row r;
                r.nodeId = nodeId;
                r.alfa = alfa;
                r.dt = dt;
                idxOfNode.insert(nodeId, rows.size());
                rows.push_back(r);
            }
            else
            {
                const int idx = it.value();
                qWarning() << "IIWL Part7 duplicate nodeId=" << nodeId << "override (alfa,dt) old=("
                           << rows[idx].alfa << "," << rows[idx].dt << ") new=(" << alfa << ","
                           << dt << ")";
                rows[idx].alfa = alfa;
                rows[idx].dt = dt;
            }
        }
    }

    const int NFW = rows.size();

    os << "\n";
    os << QString("%1\tNFW\n").arg(NFW);

    // ✅ 说明行：JMFW ALFA DT II
    os << "JMFW\tALFA\tDT\tII\n";

    qInfo() << "IIWL Part7 FINAL NFW=" << NFW;

    for (int i = 0; i < rows.size(); ++i)
    {
        const int II = i + 1;
        os << rows[i].nodeId << "\t" << fmtE(rows[i].alfa) << "\t" << fmtE(rows[i].dt) << "\t" << II
           << "\n";
    }
}

void PhysicalField::writeIIWL_Part6_FaceLoad(QTextStream& os)
{
    PickManager::instance().updateStructure();

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QString domainFace = "FaceLoadPage";

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "FaceLoadPage", pages);

    qInfo() << "IIWL Part6 FaceLoad pages.size=" << pages.size();

    struct Rec
    {
        int ke = 0;
        QVector<int> nodeIds;
        double fx = 0, fy = 0, fz = 0;
    };
    QVector<Rec> recs;

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;
        const QString uuid = n->getUuid();
        auto* page = dynamic_cast<FaceLoadPage*>(n->getPage());
        if (!page)
            continue;

        // ✅ FaceLoadPage 需要有 fx/fy/fz getter（没有就补）
        const double fx = page->fxValue();
        const double fy = page->fyValue();
        const double fz = (NDIM == 3) ? page->fzValue() : 0.0;

        // ✅ 面力：一般在 solidGrid（优先 solid）
        QHash<int, QVector<int>> nodesByKe =
            PickManager::instance().getFaceNodesByKe(domainFace, uuid, GridKind::Solid);
        if (nodesByKe.isEmpty())
            nodesByKe = PickManager::instance().getFaceNodesByKe(domainFace, uuid, GridKind::Line);

        qInfo() << "[Part6] uuid=" << uuid << "KE keys=" << nodesByKe.keys();

        for (auto it = nodesByKe.begin(); it != nodesByKe.end(); ++it)
        {
            const int ke = it.key();
            const QVector<int>& nds = it.value();
            if (nds.isEmpty())
                continue;

            Rec r;
            r.ke = ke;
            r.nodeIds = nds;
            r.fx = fx;
            r.fy = fy;
            r.fz = fz;
            recs.push_back(r);
        }
    }

    const int NFT = recs.size();
    const int LXJMET = 1; // 已知面荷载作用单元（我们输出 KE）

    os << "\n";
    os << NFT << "\t" << "NFT" << "\n";

    qInfo() << "IIWL Part6 NFT=" << NFT << "LXJMET=" << LXJMET;

    // 格式：KE  (面力序号)  NFAJD
    //       J  Fx Fy Fz    (NFAJD 行)
    for (int i = 0; i < recs.size(); ++i)
    {
        const int idx = i + 1;
        const Rec& r = recs[i];
        const int NFAJD = r.nodeIds.size();

        qInfo() << "[Part6] write idx=" << idx << "ke=" << r.ke << "NFAJD=" << NFAJD;

        os << r.ke << "\t" << idx << "\t" << NFAJD << "\n";
        for (int nodeId : r.nodeIds)
        {
            os << nodeId << "\t" << fmtE(r.fx) << "\t" << fmtE(r.fy) << "\t" << fmtE(r.fz) << "\n";
        }
    }
}

void PhysicalField::writeIIWL_Part8_BeamSpecialDistributedLoad(QTextStream& os)
{
    PickManager::instance().updateStructure();

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QString domain = "BeamLoadPage"; // 必须和页面 BasePage("BeamLoadPage")
                                           // / activate(getPageType()) 一致

    vtkUnstructuredGrid* gLine = PickManager::instance().lineGrid();
    vtkUnstructuredGrid* gSolid = PickManager::instance().solidGrid();

    vtkDataArray* nodeIdLine =
        (gLine ? vtkDataArray::SafeDownCast(gLine->GetPointData()->GetArray("node_id")) : nullptr);
    vtkDataArray* nodeIdSolid =
        (gSolid ? vtkDataArray::SafeDownCast(gSolid->GetPointData()->GetArray("node_id"))
                : nullptr);

    vtkDataArray* elemIdLine =
        (gLine ? vtkDataArray::SafeDownCast(gLine->GetCellData()->GetArray("elem_id")) : nullptr);
    vtkDataArray* elemIdSolid =
        (gSolid ? vtkDataArray::SafeDownCast(gSolid->GetCellData()->GetArray("elem_id")) : nullptr);

    qInfo() << "IIWL Part8 BeamLoad NDIM=" << NDIM << "gLine=" << (void*)gLine
             << "nodeIdLine=" << (void*)nodeIdLine << "elemIdLine=" << (void*)elemIdLine
             << "gSolid=" << (void*)gSolid << "nodeIdSolid=" << (void*)nodeIdSolid
             << "elemIdSolid=" << (void*)elemIdSolid;

    // ---------- node_id -> pid 映射 ----------
    auto buildMapNodeIdToPid = [](vtkUnstructuredGrid* g, vtkDataArray* nodeIdArr)
    {
        QHash<int, vtkIdType> mp;
        if (!g || !nodeIdArr)
            return mp;
        mp.reserve(int(g->GetNumberOfPoints() * 1.3));
        const vtkIdType n = g->GetNumberOfPoints();
        for (vtkIdType pid = 0; pid < n; ++pid)
        {
            int nid = int(nodeIdArr->GetTuple1(pid));
            mp.insert(nid, pid);
        }
        return mp;
    };

    const QHash<int, vtkIdType> mapLine = buildMapNodeIdToPid(gLine, nodeIdLine);
    const QHash<int, vtkIdType> mapSolid = buildMapNodeIdToPid(gSolid, nodeIdSolid);

    // ---------- 给定 pid 选一个“所属单元 KE”（相邻 cell 里取最小 elem_id，稳定）
    // ----------
    auto chooseElemIdForPid = [](vtkUnstructuredGrid* g, vtkDataArray* elemIdArr,
                                 vtkIdType pid) -> int
    {
        if (!g || pid < 0 || pid >= g->GetNumberOfPoints())
            return 0;

        vtkSmartPointer<vtkIdList> cellIds = vtkSmartPointer<vtkIdList>::New();
        g->GetPointCells(pid, cellIds);
        if (!cellIds || cellIds->GetNumberOfIds() <= 0)
            return 0;

        int bestElem = INT_MAX;

        for (vtkIdType i = 0; i < cellIds->GetNumberOfIds(); ++i)
        {
            vtkIdType cid = cellIds->GetId(i);
            if (elemIdArr)
            {
                int eid = int(elemIdArr->GetTuple1(cid));
                if (eid < bestElem)
                    bestElem = eid;
            }
            else
            {
                // 没有 elem_id 就退化用 cellId+1
                int eid = int(cid) + 1;
                if (eid < bestElem)
                    bestElem = eid;
            }
        }
        return (bestElem == INT_MAX) ? 0 : bestElem;
    };

    // edgeKey 解包（若你还没改页面拾取模式，仍选 Edge，这里做兜底）
    auto edgeKeyCellId = [](vtkIdType edgeKey) -> vtkIdType
    {
        return (edgeKey >> 4);
    };

    // ---------- 收集页面 ----------
    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "BeamLoadPage", pages);
    qInfo() << "IIWL Part8 BeamLoad pages.size=" << pages.size();

    struct Row
    {
        int jmfg = 0;
        double xp = 0, yp = 0, zp = 0;
        int ldType = 0;
        double a = 0;
        double p = 0;
        int idir = 0;
    };

    QVector<Row> rows;

    for (ProjectNode* n : pages)
    {
        if (!n)
            continue;
        const QString uuid = n->getUuid();

        auto* page = dynamic_cast<BeamLoadPage*>(n->getPage());
        if (!page)
            continue;

        const int ldType = page->ldTypeValue();
        const double a = page->aValue();
        const double p = page->pValue();
        const int idir = page->idirValue();

        // ✅ 现在应当用 Node 取点（getIdList(Node) 返回 node_id）
        const QVector<vtkIdType> pickedNodeLine =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Line);
        const QVector<vtkIdType> pickedNodeSolid =
            PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Solid);

        qInfo() << "[Part8] uuid=" << uuid << "pickedNodeLine(node_id)=" << pickedNodeLine
                 << "pickedNodeSolid(node_id)=" << pickedNodeSolid << "params: ldType=" << ldType
                 << "a=" << a << "p=" << p << "idir=" << idir;

        bool wroteByNode = false;

        auto consumeNodes = [&](vtkUnstructuredGrid* g, vtkDataArray* elemIdArr,
                                const QHash<int, vtkIdType>& map, const QVector<vtkIdType>& nodeIds,
                                const char* tag)
        {
            if (!g)
                return;
            for (vtkIdType v : nodeIds)
            {
                const int nodeId = int(v);
                auto it = map.find(nodeId);
                if (it == map.end())
                {
                    qWarning() << "[Part8]" << tag << "nodeId not found in map:" << nodeId;
                    continue;
                }
                vtkIdType pid = it.value();

                double pt[3] = {0, 0, 0};
                g->GetPoint(pid, pt);

                const int ke = chooseElemIdForPid(g, elemIdArr, pid);

                Row r;
                r.jmfg = ke;
                r.xp = pt[0];
                r.yp = pt[1];
                r.zp = (NDIM == 3) ? pt[2] : 0.0;
                r.ldType = ldType;
                r.a = a;
                r.p = p;
                r.idir = idir;
                rows.push_back(r);

                wroteByNode = true;

                qInfo() << "[Part8]" << tag << "nodeId=" << nodeId << "pid=" << (long long)pid
                         << "ke=" << ke << "xyz=" << r.xp << r.yp << r.zp;
            }
        };

        // 优先 lineGrid
        if (!pickedNodeLine.isEmpty() && gLine)
            consumeNodes(gLine, elemIdLine, mapLine, pickedNodeLine, "LINE");
        else if (!pickedNodeSolid.isEmpty() && gSolid)
            consumeNodes(gSolid, elemIdSolid, mapSolid, pickedNodeSolid, "SOLID");

        // ---- 兜底：如果你还在用 Edge 选梁单元，至少也能写出一条（取每条 edge
        // 的第一个点作为 XP/YP/ZP）----
        if (!wroteByNode)
        {
            const QVector<vtkIdType> pickedEdgeLine =
                PickManager::instance().getIdList(domain, uuid, PickMode::Edge, GridKind::Line);
            const QVector<vtkIdType> pickedEdgeSolid =
                PickManager::instance().getIdList(domain, uuid, PickMode::Edge, GridKind::Solid);

            qInfo() << "[Part8] fallback pickedEdgeLine(edgeKey)=" << pickedEdgeLine
                     << "pickedEdgeSolid(edgeKey)=" << pickedEdgeSolid;

            auto consumeEdges = [&](vtkUnstructuredGrid* g, vtkDataArray* elemIdArr,
                                    const QVector<vtkIdType>& edgeKeys, const char* tag)
            {
                if (!g)
                    return;
                const vtkIdType nCells = g->GetNumberOfCells();

                for (vtkIdType edgeKey : edgeKeys)
                {
                    const vtkIdType cellId = edgeKeyCellId(edgeKey);
                    if (cellId < 0 || cellId >= nCells)
                        continue;

                    vtkCell* cell = g->GetCell(cellId);
                    if (!cell)
                        continue;

                    vtkIdList* ids = cell->GetPointIds();
                    if (!ids || ids->GetNumberOfIds() <= 0)
                        continue;

                    vtkIdType pid0 = ids->GetId(0);
                    double pt[3];
                    g->GetPoint(pid0, pt);

                    int ke = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : (int(cellId) + 1);

                    Row r;
                    r.jmfg = ke;
                    r.xp = pt[0];
                    r.yp = pt[1];
                    r.zp = (NDIM == 3) ? pt[2] : 0.0;
                    r.ldType = ldType;
                    r.a = a;
                    r.p = p;
                    r.idir = idir;
                    rows.push_back(r);

                    qInfo() << "[Part8]" << tag << "edgeKey=" << (long long)edgeKey
                             << "cellId=" << (long long)cellId << "ke=" << ke << "xyz=" << r.xp
                             << r.yp << r.zp;
                }
            };

            if (!pickedEdgeLine.isEmpty() && gLine)
                consumeEdges(gLine, elemIdLine, pickedEdgeLine, "LINE_EDGE");
            else if (!pickedEdgeSolid.isEmpty() && gSolid)
                consumeEdges(gSolid, elemIdSolid, pickedEdgeSolid, "SOLID_EDGE");
        }
    }

    const int NFG = rows.size();

    os << "\n";
    os << QString("%1\tNFG\n").arg(NFG);

    // ✅ 2D/3D 头按实际输出
    if (NDIM == 2)
        os << "JMFG\tXP\tYP\tLDTYPE\tA\tP\tIDIR\tII\n";
    else
        os << "JMFG\tXP\tYP\tZP\tLDTYPE\tA\tP\tIDIR\tII\n";

    qInfo() << "IIWL Part8 FINAL NFG=" << NFG;

    for (int i = 0; i < rows.size(); ++i)
    {
        const int II = i + 1;
        const Row& r = rows[i];

        if (NDIM == 2)
        {
            os << r.jmfg << "\t" << fmtE(r.xp) << "\t" << fmtE(r.yp) << "\t" << r.ldType << "\t"
               << fmtE(r.a) << "\t" << fmtE(r.p) << "\t" << r.idir << "\t" << II << "\n";
        }
        else
        {
            os << r.jmfg << "\t" << fmtE(r.xp) << "\t" << fmtE(r.yp) << "\t" << fmtE(r.zp) << "\t"
               << r.ldType << "\t" << fmtE(r.a) << "\t" << fmtE(r.p) << "\t" << r.idir << "\t" << II
               << "\n";
        }
    }
}

void PhysicalField::generateWaveFiles()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP"))
        wd.mkpath("PREP");

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "SeismicAndAddedMassPage", pages);
    qInfo() << "WAVE pages.size=" << pages.size();

    SeismicAndAddedMassPage* page = nullptr;
    if (!pages.isEmpty())
        page = dynamic_cast<SeismicAndAddedMassPage*>(pages.first()->getPage());

    auto f6 = [](double v)
    {
        return QString::number(v, 'f', 6);
    };
    auto f4 = [](double v)
    {
        return QString::number(v, 'f', 4);
    };

    auto writeOneFile = [&](const QString& fileName, const QVector<QPair<double, double>>& series,
                            double amp, const QString& axisLabel)
    {
        const QString path = wd.filePath(QString("PREP/%1").arg(fileName));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            qWarning() << "generateWaveFiles: open failed:" << path;
            return;
        }

        QTextStream out(&file);
        const int N = series.size();
        out << QString("%1      %2 NUM  AM\n").arg(N, 6).arg(f4(amp), 10);
        out << QString("   T(S)           %1(m/S^2)       II\n").arg(axisLabel);

        for (int i = 0; i < N; ++i)
        {
            out << QString("%1       %2%3\n")
                       .arg(f6(series[i].first), 13)
                       .arg(f6(series[i].second), 15)
                       .arg(i + 1, 10);
        }

        file.close();
        qInfo() << "WAVE generated :" << path << "N=" << N << "AMP=" << amp;
    };

    QVector<QPair<double, double>> sx, sy, sz;
    double axAmp = 0.0;
    double ayAmp = 0.0;
    double azAmp = 0.0;
    if (page)
    {
        sx = page->seriesX();
        axAmp = page->ampX();
        sy = page->seriesY();
        ayAmp = page->ampY();
        sz = page->seriesZ();
        azAmp = page->ampZ();
    }
    else
    {
        qInfo() << "WAVE: no seismic page, output empty files";
    }

    writeOneFile("WAVE-AX.DAT", sx, axAmp, "AX");
    writeOneFile("WAVE-AY.DAT", sy, ayAmp, "AY");
    writeOneFile("WAVE-AZ.DAT", sz, azAmp, "AZ");
}

void PhysicalField::generateIIWLFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP"))
        wd.mkpath("PREP");
    const QString path = wd.filePath("PREP/IIWL.DAT");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "generateIIWLFile: open failed:" << path;
        return;
    }

    QTextStream os(&f);

    writeIIWL_Part1_GeoStressPoint(os);
    writeIIWL_Part2_GeoStressFitFace(os);
    writeIIWL_Part3_BodyForce(os);
    writeIIWL_Part4_PointForce(os);
    writeIIWL_Part5_LinearForce(os);
    writeIIWL_Part6_FaceLoad(os);
    writeIIWL_Part7_TemperatureLoad(os);
    writeIIWL_Part8_BeamSpecialDistributedLoad(os);

    f.close();
    qInfo() << "IIWL generated :" << path;

    generateWaveFiles();
}
