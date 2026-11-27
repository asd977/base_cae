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
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QInputDialog>
#include <QMap>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QTextStream>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

#include "BodyForcePage.h"
#include "PhysicalFieldSettingWidget.h"
#include "SeismicAndAddedMassPage.h"
#include "TemperatureLoadPage.h"
#include "MechanicalDofs.h"
#include "vtkCellData.h"
#include "vtkPointData.h"

// VTK
#include <BeamLoadPage.h>
#include <EventManager.h>
#include <LinearForcePage.h>
#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkCell.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include "DisplacementConstraintWidget.h"

namespace {
    constexpr int DIMENSION_2D = 2;
    constexpr int DIMENSION_3D = 3;
    constexpr int DOF_2D       = 2;
    constexpr int DOF_3D       = 3;
    constexpr int NCOM_2D      = 3;
    constexpr int NCOM_3D      = 6;
    constexpr double HASH_LOAD_FACTOR = 1.3;
}  // namespace

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

static void DeletePhysicalFieldNodeTree(ProjectNode* node)
{
    if (!node)
        return;
    const auto children = node->getChildren();
    for (ProjectNode* child : children)
    {
        node->removeChild(child);
        DeletePhysicalFieldNodeTree(child);
    }
    if (BasePage* page = node->getPage())
    {
        page->beforeUnload();
        delete page;
        node->setPage(nullptr);
    }
    delete node;
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

    // 单节点查看模式：只刷新本次发生变化的位移约束节点。
    // 非当前页面的约束不创建/保留方向符号；进入页面时再由 beforeLoad() 显示。
    QObject::connect(&PickManager::instance(), &PickManager::selectionChanged, this,
                     [this](const QString& domain, const QString& uuid, PickMode mode, GridKind)
                     {
                         if (m_loadingProject || domain != QStringLiteral("DisplacementConstraintWidget") ||
                             mode != PickMode::Node)
                             return;

                         ProjectNode* node = findNodeByUuid(m_node, uuid);
                         auto* page = node ? dynamic_cast<DisplacementConstraintWidget*>(node->getPage())
                                           : nullptr;
                         if (page)
                             page->refreshConstraintVisualization();
                     });
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
    auto tmp = std::make_unique<PhysicalFieldSettingWidget>();
    return tmp.release();
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

            // 根节点只负责创建约束/荷载对象；名称统一使用“新建...”。
            if (kind == "PhysicalFieldRoot")
            {
                ContextMenuHub::addAction(menu, this, "新建位移约束",
                                          [this]() { addDisplacementConstraint(); });

                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "新建现场地应力信息",
                                          [this]() { addGeoStressPointPage(); });
                ContextMenuHub::addAction(menu, this, "新建梯形边界荷载",
                                          [this]() { addGeoStressFitFacePage(); });

                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "新建体积力荷载",
                                          [this]() { addBodyForcePage(); });
                ContextMenuHub::addAction(menu, this, "新建集中力荷载",
                                          [this]() { addConcentratedForcePage(); });
                ContextMenuHub::addAction(menu, this, "新建线力荷载",
                                          [this]() { addLinearForcePage(); });
                ContextMenuHub::addAction(menu, this, "新建面力荷载",
                                          [this]() { addSurfaceForcePage(); });
                ContextMenuHub::addAction(menu, this, "新建变温荷载",
                                          [this]() { addTemperatureLoadPage(); });
                ContextMenuHub::addAction(menu, this, "新建梁单元荷载",
                                          [this]() { addBeamLoadPage(); });

                ContextMenuHub::addSeparator(menu);
                const bool hasSeismic = CountKind(m_node, "SeismicAndAddedMassPage") > 0;
                ContextMenuHub::addAction(
                    menu, this, hasSeismic ? "打开地震加速度" : "新建地震加速度",
                    [this]()
                    {
                        QString error;
                        if (!openSeismicAccelerationPage(&error) && !error.isEmpty())
                            QMessageBox::warning(nullptr, QStringLiteral("地震加速度"), error);
                    });
                return;
            }

            // 子节点只保留真正有效的通用操作。旧版“重建/隐藏显示”没有实现，移除。
            ContextMenuHub::addAction(menu, this, "重命名",
                                      [this, uuid]() { renameNodeByUuid(uuid); });
            ContextMenuHub::addAction(menu, this, "删除",
                                      [this, uuid]() { removeNodeByUuid(uuid); });
        },
        100,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value("module").toString() == "PhysicalField";
        });
}

void PhysicalField::renameNodeByUuid(const QString& uuid)
{
    if (uuid.isEmpty() || !m_node)
        return;

    ProjectNode* node = findNodeByUuid(m_node, uuid);
    if (!node || node == m_node)
        return;

    const QString currentName = node->text(0).trimmed();
    bool ok = false;
    QString newName = QInputDialog::getText(
        nullptr, QStringLiteral("重命名约束/荷载"), QStringLiteral("请输入新名称："),
        QLineEdit::Normal, currentName, &ok);
    if (!ok)
        return;

    newName = newName.trimmed();
    if (newName.isEmpty())
    {
        QMessageBox::warning(nullptr, QStringLiteral("名称无效"),
                             QStringLiteral("名称不能为空。"));
        return;
    }
    if (newName == currentName)
        return;

    if (auto* parent = dynamic_cast<ProjectNode*>(node->parent()))
    {
        for (ProjectNode* sibling : parent->getChildren())
        {
            if (sibling != node &&
                sibling->text(0).trimmed().compare(newName, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(nullptr, QStringLiteral("名称重复"),
                                     QStringLiteral("同一级中已存在“%1”，请使用其他名称。")
                                         .arg(newName));
                return;
            }
        }
    }

    node->setText(0, newName);
    node->setName(newName);
    if (BasePage* page = node->getPage())
        page->setDisplayName(newName);
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

    // 删除任意物理场节点时同步清掉其 PickManager 选区，避免残留孤立 uuid 污染同域页面或导出。
    if (BasePage* page = node->getPage())
    {
        const QString domain = page->getPageType();
        if (!domain.isEmpty())
        {
            PickManager::instance().clearSelection(domain, uuid, PickMode::Node);
            PickManager::instance().clearSelection(domain, uuid, PickMode::Edge);
            PickManager::instance().clearSelection(domain, uuid, PickMode::Face);
        }
        if (dynamic_cast<DisplacementConstraintWidget*>(page))
            PickManager::instance().clearNodeConstraintFlags(QStringLiteral("DisplacementConstraintWidget"), uuid);
    }

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (parent)
        parent->removeChild(node);

    if (node->getPage())
        delete node->getPage();

    delete node;
}

void PhysicalField::newProject()
{
    if (m_node)
        closeProject();
    auto physicalFieldSettingWidget = std::make_unique<PhysicalFieldSettingWidget>();
    physicalFieldSettingWidget->setDisplacementProvider(
        [this]()
        {
            QVector<DisplacementOverviewItem> result;
            QList<ProjectNode*> nodes;
            collectDisplacementNodes(m_node, nodes);
            for (ProjectNode* node : nodes)
            {
                if (!node) continue;
                auto* page = dynamic_cast<DisplacementConstraintWidget*>(node->getPage());
                if (!page) continue;
                DisplacementOverviewItem item;
                item.uuid = node->getUuid();
                item.title = node->text(0);
                // 工程恢复可能延迟载入页面：不能用默认 UI 状态覆盖已保存的方向。
                if (page->hasDeferredLoadJson())
                {
                    const QJsonObject saved = page->saveJsonForPersistence();
                    auto normFlag = [](const QString& s) { const int v = s.toInt(); return v == 2 ? 0 : v; };
                    item.ux = normFlag(saved.value(QStringLiteral("ux")).toString(QStringLiteral("1")));
                    item.uy = normFlag(saved.value(QStringLiteral("uy")).toString(QStringLiteral("1")));
                    item.uz = normFlag(saved.value(QStringLiteral("uz")).toString(QStringLiteral("1")));
                }
                else
                {
                    item.ux = page->uxFlag();
                    item.uy = page->uyFlag();
                    item.uz = page->uzFlag();
                }
                item.nodeIds = PickManager::instance().selectedNodeIds(
                    QStringLiteral("DisplacementConstraintWidget"), item.uuid);
                result.append(std::move(item));
            }
            return result;
        },
        [this](const QString& childUuid)
        {
            if (auto* target = findNodeByUuid(m_node, childUuid))
                if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    pm->setCurrentItem(target);
        });

    QString uuid = QUuid::createUuid().toString();
    m_node = new ProjectNode("约束与荷载", uuid, (BasePage*)physicalFieldSettingWidget.release());

    // 关键：把上下文写进 item data
    m_node->setData(0, ProjectNode::RoleUuid, m_node->getUuid());
    m_node->setData(0, ProjectNode::RoleModule, "PhysicalField");
    m_node->setData(0, ProjectNode::RoleKind, "PhysicalFieldRoot");

    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_node);
}

void PhysicalField::closeProject()
{
    ProjectNode* root = m_node;
    m_node = nullptr;
    if (!root)
        return;

    if (auto* parent = dynamic_cast<ProjectNode*>(root->parent()))
        parent->removeChild(root);
    else if (QTreeWidget* tree = root->treeWidget())
    {
        const int index = tree->indexOfTopLevelItem(root);
        if (index >= 0)
            tree->takeTopLevelItem(index);
    }
    DeletePhysicalFieldNodeTree(root);
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

    m_loadingProject = true;
    const QJsonArray items = root.value("items").toArray();
    for (const auto& v : items)
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

    if (items.isEmpty())
    {
        const QString iiidPath = QDir(SolverFilePaths::projectInputDirectory(projectDir))
                                     .filePath(QStringLiteral("IIID.DAT"));
        QString errorMessage;
        if (QFileInfo::exists(iiidPath) && importIIIDFile(iiidPath, &errorMessage))
        {
            qInfo().noquote() << QStringLiteral("已从工程计算数据恢复约束设置。");
            if (!saveProject())
                qWarning() << "failed to persist recovered physical field settings";
        }
        else if (!errorMessage.isEmpty())
        {
            qWarning().noquote() << QStringLiteral("约束设置恢复失败：%1").arg(errorMessage);
        }
    }

    // 工程打开后不常驻显示约束方向；点击某个位移约束节点时再显示该节点。
    m_loadingProject = false;

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
    // 地震加速度是全工程共享输入，只允许存在一份。动力工况通过 IFWAVE 引用它。
    QList<ProjectNode*> existingPages;
    CollectNodesByKind(m_node, "SeismicAndAddedMassPage", existingPages);
    if (!existingPages.isEmpty())
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existingPages.first());
        QMessageBox::information(
            nullptr, QStringLiteral("地震加速度已存在"),
            QStringLiteral("“约束与荷载”中只需要一份地震加速度表。已为你定位到现有节点。"));
        return;
    }

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

bool PhysicalField::openSeismicAccelerationPage(QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (!m_node)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("“约束与荷载”节点尚未初始化。");
        return false;
    }

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "SeismicAndAddedMassPage", pages);
    if (pages.size() > 1)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "“约束与荷载”中存在 %1 个“地震加速度”节点。请先删除多余节点后再设置。")
                                .arg(pages.size());
        return false;
    }

    if (pages.isEmpty())
    {
        addSeismicAndAddedMassPage();
        pages.clear();
        CollectNodesByKind(m_node, "SeismicAndAddedMassPage", pages);
    }

    if (pages.size() != 1)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建或定位“地震加速度”设置页面。");
        return false;
    }

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!pm)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("工程树服务不可用，无法打开“地震加速度”设置。");
        return false;
    }

    pm->setCurrentItem(pages.first());
    return true;
}

bool PhysicalField::validateSeismicAcceleration(QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "SeismicAndAddedMassPage", pages);
    if (pages.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "当前工况启用了地震波时程分析，但“约束与荷载”中尚未设置“地震加速度”。");
        return false;
    }
    if (pages.size() > 1)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "“约束与荷载”中存在 %1 个“地震加速度”节点。地震输入只能保留一份，请删除多余节点。")
                                .arg(pages.size());
        return false;
    }

    auto* page = dynamic_cast<SeismicAndAddedMassPage*>(pages.first()->getPage());
    if (!page)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("地震加速度页面未正确加载，请重新打开工程后重试。");
        return false;
    }

    QString detail;
    if (!page->validateWaveInput(&detail))
    {
        if (errorMessage)
            *errorMessage = detail;
        return false;
    }
    return true;
}

// -------------------- 普通工具函数：将位移约束按自由度合并到 flags --------------------
// 规则：1=自由，只表示当前约束不贡献该方向；0=固定，可与其他方向固定正常叠加；
// -10000=悬空杆件特殊状态，若与同一自由度上的其他非自由值同时出现，则记录为真正冲突。
static void MergePickedNodeIdsToFlags(const QVector<vtkIdType>& pickedNodeIds, int ux, int uy,
                                      int uz, const QHash<int, int>& rowOfNodeId,
                                      QVector<std::array<int, 3>>& flags,
                                      QSet<int>& conflictingNodeIds)
{
    const int incoming[3] = {ux, uy, uz};
    for (int i = 0; i < pickedNodeIds.size(); ++i)
    {
        const int nodeId = int(pickedNodeIds[i]);

        auto it = rowOfNodeId.find(nodeId);
        if (it == rowOfNodeId.end())
            continue;

        const int r = it.value();
        for (int axis = 0; axis < 3; ++axis)
        {
            const int value = incoming[axis];
            if (value == 1)
                continue; // 自由不会释放其他约束已经固定的自由度。

            int& current = flags[r][axis];
            if (current == 1)
            {
                current = value;
                continue;
            }
            if (current == value)
                continue;

            // 两个不同的非自由值不能静默覆盖。当前主要用于拦截
            // “悬空杆件节点(-10000)”与普通固定(0)落在同一自由度。
            conflictingNodeIds.insert(nodeId);
        }
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
        if (!p)
            continue;
        auto* page = dynamic_cast<BodyForcePage*>(p->getPage());
        if (!page)
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

    // SOLVER GE_FBG treats NFB==1 as global uniform body force (ignoring JMFB(1))
    // and NFB>1 as per-node local body force (requiring all element nodes in JMFB).
    QVector<BodyForcePage::Part3Row> finalRows;
    BodyForcePage::Part3Row mergedGlobal{0, 0.0, 0.0, 0.0};
    bool hasGlobal = false;
    QVector<BodyForcePage::Part3Row> localRows;
    for (const auto& r : allRows)
    {
        if (r.jd <= 0)
        {
            hasGlobal = true;
            mergedGlobal.fbx += r.fbx;
            mergedGlobal.fby += r.fby;
            mergedGlobal.fbz += r.fbz;
        }
        else
        {
            localRows.push_back(r);
        }
    }
    if (hasGlobal && localRows.isEmpty())
    {
        finalRows.push_back(mergedGlobal);
    }
    else if (!localRows.isEmpty())
    {
        // Merge overlapping local node rows by node_id.
        QMap<int, BodyForcePage::Part3Row> mergedByNode;
        if (hasGlobal)
        {
            // When global & local coexist, populate all mesh nodes with mergedGlobal
            // so GE_FBG does not zero out non-selected elements.
            auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            auto grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
            if (grid && grid->GetNumberOfPoints() > 0)
            {
                auto* nodeIdArr = grid->GetPointData() ? grid->GetPointData()->GetArray("node_id") : nullptr;
                for (vtkIdType p = 0; p < grid->GetNumberOfPoints(); ++p)
                {
                    const int nid = nodeIdArr ? static_cast<int>(nodeIdArr->GetTuple1(p)) : static_cast<int>(p + 1);
                    mergedByNode.insert(nid, BodyForcePage::Part3Row{nid, mergedGlobal.fbx, mergedGlobal.fby, mergedGlobal.fbz});
                }
            }
        }
        QSet<int> validMeshNodeIds;
        if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
        {
            for (const auto& grid : {meshSvc->getVtkUnstructuredGridSolid(),
                                     meshSvc->getVtkUnstructuredGridLine()})
            {
                vtkUnstructuredGrid* g = grid.GetPointer();
                if (!g) continue;
                vtkDataArray* nidArr = g->GetPointData() ? g->GetPointData()->GetArray("node_id") : nullptr;
                const vtkIdType nPts = g->GetNumberOfPoints();
                for (vtkIdType p = 0; p < nPts; ++p)
                    validMeshNodeIds.insert(nidArr ? static_cast<int>(nidArr->GetTuple1(p)) : static_cast<int>(p + 1));
            }
        }
        for (const auto& r : localRows)
        {
            if (!validMeshNodeIds.isEmpty() && !validMeshNodeIds.contains(r.jd))
                continue;
            auto& dst = mergedByNode[r.jd];
            if (dst.jd == 0)
                dst.jd = r.jd;
            dst.fbx += r.fbx;
            dst.fby += r.fby;
            dst.fbz += r.fbz;
        }
        for (auto it = mergedByNode.cbegin(); it != mergedByNode.cend(); ++it)
            finalRows.push_back(it.value());
        if (finalRows.size() == 1)
        {
            qWarning() << "Part3 BodyForce: single-node local body force cannot form a full element in GE_FBG; dropping.";
            finalRows.clear();
        }
    }

    const int NFB = finalRows.size();

    os << NFB << "\n";
    os << "JMFB\tFBX\tFBY\tFBZ\tII\n";

    for (const auto& r : finalRows)
    {
        os << r.jd << "\t" << r.fbx << "\t" << r.fby << "\t" << r.fbz << "\n";
    }
}

bool PhysicalField::hasPrescribedDisplacements() const
{
    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node,QStringLiteral("DisplacementConstraint"),pages);
    for(auto* node:pages)if(auto* page=dynamic_cast<DisplacementConstraintWidget*>(node->getPage()))
        if(page->hasPrescribedDisplacement())return true;
    return false;
}

bool PhysicalField::generateIIIDFile()
{
    m_constraintsValid=false; m_prescribedValues.clear();
    PickManager::instance().updateStructure();
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();

    vtkUnstructuredGrid* gSolid = PickManager::instance().solidGrid();
    vtkUnstructuredGrid* gLine = PickManager::instance().lineGrid();
    vtkUnstructuredGrid* gAll = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;

    if (!gSolid && !gLine && !gAll)
    {
        qWarning() << "generateIIIDFile: no available grid (solid/line/all are null)";
        return false;
    }

    auto findNodeIdArray = [](vtkUnstructuredGrid* g) -> vtkDataArray*
    {
        if (!g || !g->GetPointData())
            return nullptr;
        if (auto* a = vtkDataArray::SafeDownCast(g->GetPointData()->GetArray("node_id")))
            return a;
        return vtkDataArray::SafeDownCast(g->GetPointData()->GetArray("NodeId"));
    };

    vtkDataArray* idSolid = findNodeIdArray(gSolid);
    vtkDataArray* idLine = findNodeIdArray(gLine);
    vtkDataArray* idAll = findNodeIdArray(gAll);

    if ((gSolid && gSolid->GetNumberOfPoints() > 0 && !idSolid) ||
        (gLine && gLine->GetNumberOfPoints() > 0 && !idLine) ||
        (gAll && gAll->GetNumberOfPoints() > 0 && !idAll))
    {
        qWarning() << "generateIIIDFile: node id array missing on some grid";
        return false;
    }

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    qInfo()<<"NDIM "<<NDIM;
    const bool is3D = (NDIM == DIMENSION_3D);
    const auto beamNodes=planarBeamNodes(gAll);
    if(is3D&&!beamNodes.isEmpty())
    {
        QMessageBox::warning(nullptr,QStringLiteral("三维梁自由度尚未支持"),QStringLiteral("当前梁边界导出已验证二维平面梁。三维梁需要六个自由度，请先使用二维平面梁工况。"));
        return false;
    }
    const bool planarBeam=!is3D&&!beamNodes.isEmpty();
    const int MADOF = is3D||planarBeam ? 3 : 2;
    m_exportedDofs=MADOF;

    // 1) 收集所有 node_id（all + solid + line），去重并排序
    QVector<int> allNodeIds;
    allNodeIds.reserve((gAll ? int(gAll->GetNumberOfPoints()) : 0) +
                       (gSolid ? int(gSolid->GetNumberOfPoints()) : 0) +
                       (gLine ? int(gLine->GetNumberOfPoints()) : 0));

    if (gAll && idAll)
    {
        const int n = int(gAll->GetNumberOfPoints());
        for (int i = 0; i < n; ++i)
            allNodeIds.push_back(idAll->GetTuple1(i));
    }

    if (gSolid && idSolid)
    {
        const int n = int(gSolid->GetNumberOfPoints());
        for (int i = 0; i < n; ++i)
            allNodeIds.push_back(idSolid->GetTuple1(i));
    }
    if (gLine && idLine)
    {
        const int n = int(gLine->GetNumberOfPoints());
        for (int i = 0; i < n; ++i)
            allNodeIds.push_back(idLine->GetTuple1(i));
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
    {
        flags[r] = {1, 1, 1};
        if(planarBeam&&!beamNodes.contains(allNodeIds[r]))flags[r][2]=0;
    }

    // 4) 收集位移约束节点
    QList<ProjectNode*> bcNodes;
    collectDisplacementNodes(m_node, bcNodes);
    qInfo() << "bcNodes size:" << bcNodes.size();

    const QString domain = "DisplacementConstraintWidget"; // 必须与 activate 一致
    QSet<int> conflictingNodeIds;
    QSet<int> unsupportedRotationNodes;
    QMap<QPair<int,int>,double> prescribedByDof;

    // 5) 应用每个约束节点的选中点。允许不同位移约束共享 node_id，
    // 普通固定方向按自由度组合，不再依赖“后写覆盖前写”的顺序。
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
            uz = is3D ? page->uzFlag() : planarBeam?page->rotationFlag():1;
            qInfo() << "apply bc uuid=" << uuid << " ux,uy,uz=" << ux << uy << uz;
        }
        const auto collectValues=[&](const QVector<vtkIdType>& ids)
        {
            const int modes[]={ux,uy,uz};
            for(auto id:ids)
            {
                if(!rowOfNodeId.contains(int(id)))continue;
                if(!is3D&&page&&(page->rotationFlag()!=1||page->isPrescribed(2))&&!beamNodes.contains(int(id)))
                    unsupportedRotationNodes.insert(int(id));
                for(int axis=0;axis<MADOF;++axis)
                {
                    if(modes[axis]!=0)continue;
                    const double value=page&&page->isPrescribed(axis)?page->prescribedValue(axis):0.0;
                    const QPair<int,int> key(int(id),axis);
                    if(prescribedByDof.contains(key)&&std::abs(prescribedByDof[key]-value)>1.e-12)
                        conflictingNodeIds.insert(int(id));
                    else prescribedByDof[key]=value;
                }
            }
        };

        // solid picks
        {
            const QVector<vtkIdType> pickedSolid =
                PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Solid);

            qInfo() << uuid << " pickedSolid(node_id) " << pickedSolid;
            MergePickedNodeIdsToFlags(pickedSolid, ux, uy, uz, rowOfNodeId, flags, conflictingNodeIds);
            collectValues(pickedSolid);
        }

        // line picks（现在返回 node_id）
        {
            const QVector<vtkIdType> pickedLine =
                PickManager::instance().getIdList(domain, uuid, PickMode::Node, GridKind::Line);

            qInfo() << uuid << " pickedLine(node_id) " << pickedLine;
            MergePickedNodeIdsToFlags(pickedLine, ux, uy, uz, rowOfNodeId, flags, conflictingNodeIds);
            collectValues(pickedLine);
        }
    }

    if(!unsupportedRotationNodes.isEmpty())
    {
        QMessageBox::warning(nullptr,QStringLiteral("转角约束不适用"),QStringLiteral("绕 Z 转角约束仅适用于二维平面梁节点（KETYPE=21）。当前选择包含没有转角自由度的节点，请检查选择范围。"));
        return false;
    }
    if (!conflictingNodeIds.isEmpty())
    {
        QList<int> conflicts = conflictingNodeIds.values();
        std::sort(conflicts.begin(), conflicts.end());
        QStringList examples;
        const int showCount = std::min(10, int(conflicts.size()));
        for (int i = 0; i < showCount; ++i)
            examples.push_back(QString::number(conflicts.at(i)));

        QString detail = QStringLiteral(
            "检测到 %1 个节点在同一自由度上存在不兼容的位移约束。\n"
            "同方向的给定位移必须相同；普通 X/Y/Z 固定可叠加；悬空杆件与普通约束不能混合。")
                             .arg(conflictingNodeIds.size());
        if (!examples.isEmpty())
            detail += QStringLiteral("\n节点示例：%1%2")
                          .arg(examples.join(QStringLiteral("、")),
                               conflicts.size() > showCount ? QStringLiteral(" …") : QString());
        detail += QStringLiteral("\n请回到位移约束页面检查红色/共享节点后再计算。");

        QMessageBox::warning(nullptr, QStringLiteral("位移约束冲突"), detail);
        qWarning().noquote() << "generateIIIDFile:" << detail;
        return false;
    }

    for(auto it=prescribedByDof.cbegin();it!=prescribedByDof.cend();++it)
    {
        if(it.value()==0.0)continue;
        int index=m_prescribedValues.indexOf(it.value());
        if(index<0){index=m_prescribedValues.size();m_prescribedValues.append(it.value());}
        if(MADOF+index+1>=10000)return false;
        flags[rowOfNodeId[it.key().first]][it.key().second]=-(MADOF+index+1);
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

            const int nodeId = idAll->GetTuple1(int(pid));
            auto it = rowOfNodeId.find(nodeId);
            if (it == rowOfNodeId.end())
                continue;

            flags[it.value()] = {0, 0, 0};
        }
    }

    // 6) 写入当前计算输入目录。
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;
    const QString path = inputDir.filePath(QStringLiteral("IIID.DAT"));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "generateIIIDFile: open failed:" << path;
        return false;
    }

    QTextStream os(&f);
    os << NUMNP << "\t" << MADOF << "\n";
    os << NUMNP << "\t" << MADOF << "\n";

    for (int r = 0; r < NUMNP; ++r)
    {
        const int nodeId = allNodeIds[r];
        os << flags[r][0] << "\t" << flags[r][1] << "\t";
        if (MADOF==3)
            os << flags[r][2] << "\t";
        os << nodeId << "\n";
    }

    f.close();
    m_constraintsValid=true;
    qInfo() << "IIID generated:" << path;
    return true;
}

bool PhysicalField::importIIIDFile(const QString& filePath, QString* errorMessage)
{
    const QString fileTag = QStringLiteral("IID.DAT");

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

    const QRegularExpression spaceRe(QStringLiteral("\\s+"));

    auto splitFields = [&spaceRe](const QString& text) -> QStringList
    {
        return text.trimmed().split(spaceRe, Qt::SkipEmptyParts);
    };

    auto parseStrictInt = [](const QString& text, int& value) -> bool
    {
        const QString s = text.trimmed();

        if (s.isEmpty())
            return false;

        bool ok = false;
        const int v = s.toInt(&ok);

        if (!ok)
            return false;

        value = v;
        return true;
    };

    auto parseHeaderLine = [&splitFields, &parseStrictInt](const QString& text,
                                                           int& numNp,
                                                           int& maDof,
                                                           QString* err) -> bool
    {
        const QStringList fields = splitFields(text);

               // 正式支持两种文件头：
               // 1) NUMNP MADOF
               //    396 2
               //
               // 2) NUMNP MADOF + 字段名说明
               //    396 2 NUMNP MADOF
        if (fields.size() != 2 && fields.size() != 4)
        {
            if (err)
            {
                *err = QStringLiteral("格式错误，应为：节点数 自由度数，或：节点数 自由度数 NUMNP MADOF；实际为：%1")
                           .arg(text);
            }

            return false;
        }

        if (!parseStrictInt(fields.at(0), numNp) ||
            !parseStrictInt(fields.at(1), maDof))
        {
            if (err)
                *err = QStringLiteral("前两列必须为整数：%1").arg(text);

            return false;
        }

        if (fields.size() == 4)
        {
            if (fields.at(2).compare(QStringLiteral("NUMNP"), Qt::CaseInsensitive) != 0 ||
                fields.at(3).compare(QStringLiteral("MADOF"), Qt::CaseInsensitive) != 0)
            {
                if (err)
                    *err = QStringLiteral("第 3、4 列应为 NUMNP MADOF：%1").arg(text);

                return false;
            }
        }

        return true;
    };

    QString line;
    if (!readDataLine(line))
        return fail(QStringLiteral("%1 文件为空").arg(fileTag));

    int numNp = 0;
    int maDof = 0;

    QString headerError;
    if (!parseHeaderLine(line, numNp, maDof, &headerError))
        return fail(QStringLiteral("%1 首行%2").arg(fileTag, headerError));

    if (numNp < 0 || (maDof != 2 && maDof != 3))
    {
        return fail(QStringLiteral("%1 首行节点数或自由度数无效：%2")
                        .arg(fileTag, line));
    }

    QStringList parts;

    if (numNp > 0)
    {
        if (!readDataLine(line))
            return fail(QStringLiteral("%1 缺少约束数据").arg(fileTag));

        int secondNum = 0;
        int secondDof = 0;
        QString secondHeaderError;

        if (parseHeaderLine(line, secondNum, secondDof, &secondHeaderError))
        {
            if (secondNum != numNp || secondDof != maDof)
            {
                return fail(QStringLiteral("%1 第二行文件头与首行不一致：%2")
                                .arg(fileTag, line));
            }

            if (!readDataLine(line))
                return fail(QStringLiteral("%1 缺少约束数据行").arg(fileTag));
        }

        parts = splitFields(line);
    }

    QMap<QString, QVector<int>> groupedNodeIds;

    for (int rowIndex = 0; rowIndex < numNp; ++rowIndex)
    {
        const int expectedFieldCount = maDof + 1;

        if (parts.size() != expectedFieldCount)
        {
            return fail(QStringLiteral("%1 第 %2 条约束数据格式错误，应为 %3 列，实际为 %4 列：%5")
                            .arg(fileTag)
                            .arg(rowIndex + 1)
                            .arg(expectedFieldCount)
                            .arg(parts.size())
                            .arg(line));
        }

        int ux = 0;
        int uy = 0;
        int uz = 1;
        int nodeId = 0;

        if (!parseStrictInt(parts.at(0), ux) ||
            !parseStrictInt(parts.at(1), uy))
        {
            return fail(QStringLiteral("%1 第 %2 条约束数据包含非整数字段：%3")
                            .arg(fileTag)
                            .arg(rowIndex + 1)
                            .arg(line));
        }

        if (maDof == 3)
        {
            if (!parseStrictInt(parts.at(2), uz))
            {
                return fail(QStringLiteral("%1 第 %2 条约束数据包含非整数字段：%3")
                                .arg(fileTag)
                                .arg(rowIndex + 1)
                                .arg(line));
            }
        }

        if (!parseStrictInt(parts.at(maDof), nodeId))
        {
            return fail(QStringLiteral("%1 第 %2 条约束数据包含非整数字段：%3")
                            .arg(fileTag)
                            .arg(rowIndex + 1)
                            .arg(line));
        }

        if (!(ux == 1 && uy == 1 && uz == 1))
        {
            const QString key = QStringLiteral("%1,%2,%3").arg(ux).arg(uy).arg(uz);
            groupedNodeIds[key].push_back(nodeId);
        }

        if (rowIndex + 1 < numNp)
        {
            if (!readDataLine(line))
            {
                return fail(QStringLiteral("%1 数据行数量少于文件头声明的 %2 行")
                                .arg(fileTag)
                                .arg(numNp));
            }

            parts = splitFields(line);
        }
    }

           // IID.DAT 中记录的是 node_id；PickManager 的 setSelectedPointIds 需要的是各网格内
           // 的 VTK point id。因此导入前必须先把 node_id 映射回 Solid/Line 网格点索引。
    PickManager::instance().updateStructure();

    auto findNodeIdArray = [](vtkUnstructuredGrid* g) -> vtkDataArray*
    {
        if (!g || !g->GetPointData())
            return nullptr;

        if (auto* a = vtkDataArray::SafeDownCast(g->GetPointData()->GetArray("node_id")))
            return a;

        return vtkDataArray::SafeDownCast(g->GetPointData()->GetArray("NodeId"));
    };

    auto buildPointIdByNodeId = [&findNodeIdArray](vtkUnstructuredGrid* g)
    {
        QHash<int, vtkIdType> out;

        vtkDataArray* nodeIdArr = findNodeIdArray(g);
        if (!g || !nodeIdArr)
            return out;

        const vtkIdType nPts = g->GetNumberOfPoints();
        out.reserve(int(nPts * 1.3));

        for (vtkIdType pid = 0; pid < nPts; ++pid)
        {
            const int nodeId = nodeIdArr->GetTuple1(int(pid));

            if (!out.contains(nodeId))
                out.insert(nodeId, pid);
        }

        return out;
    };

    auto buildIsolatedNodeIds = [&findNodeIdArray](vtkUnstructuredGrid* g)
    {
        QSet<int> out;

        vtkDataArray* nodeIdArr = findNodeIdArray(g);
        if (!g || !nodeIdArr)
            return out;

        vtkNew<vtkIdList> cellIds;
        const vtkIdType nPts = g->GetNumberOfPoints();

        for (vtkIdType pid = 0; pid < nPts; ++pid)
        {
            cellIds->Reset();
            g->GetPointCells(pid, cellIds);

            if (cellIds->GetNumberOfIds() == 0)
                out.insert(nodeIdArr->GetTuple1(int(pid)));
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

           // 导入是生成 IID.DAT 的反向操作：先移除已有位移约束，再按文件内容重建。
    QList<ProjectNode*> oldConstraints;
    collectDisplacementNodes(m_node, oldConstraints);

    for (ProjectNode* node : oldConstraints)
    {
        if (!node)
            continue;

        const QString uuid = node->getUuid();
        const QString domain = node->getPage()
                                   ? node->getPage()->getPageType()
                                   : QStringLiteral("DisplacementConstraintWidget");

        PickManager::instance().setSelectedPointIds(domain,
                                                    uuid,
                                                    GridKind::Solid,
                                                    QVector<vtkIdType>(),
                                                    true);

        PickManager::instance().setSelectedPointIds(domain,
                                                    uuid,
                                                    GridKind::Line,
                                                    QVector<vtkIdType>(),
                                                    true);
        PickManager::instance().clearNodeConstraintFlags(domain, uuid);

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
                // 杆单元约束在 IID.DAT 中用 -10000 标识，只进入线单元网格。
                if (linePointIdByNodeId.contains(nodeId))
                    linePointIds.push_back(linePointIdByNodeId.value(nodeId));
                else
                    qWarning() << "importIIIDFile: bar node_id not found in line grid:" << nodeId;
            }
            else
            {
                // 普通约束只进入实体网格。
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

        const QString title = MakeIndexedTitle(m_node,
                                               QStringLiteral("DisplacementConstraint"),
                                               QStringLiteral("位移约束"));

        ProjectNode* node = createNodeByKind(QStringLiteral("DisplacementConstraint"),
                                             title,
                                             QUuid::createUuid().toString());

        if (!node || !node->getPage())
            continue;

        QJsonObject pageData;
        pageData["ux"] = flags.value(0, QStringLiteral("1"));
        pageData["uy"] = flags.value(1, QStringLiteral("1"));
        pageData["uz"] = flags.value(2, QStringLiteral("1"));

        node->getPage()->loadJson(pageData);

        const QString domain = node->getPage()->getPageType();

        PickManager::instance().setSelectedPointIds(domain,
                                                    node->getUuid(),
                                                    GridKind::Solid,
                                                    solidPointIds,
                                                    true);

        PickManager::instance().setSelectedPointIds(domain,
                                                    node->getUuid(),
                                                    GridKind::Line,
                                                    linePointIds,
                                                    true);

        lastCreated = node;
    }

    if (lastCreated)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(lastCreated);
    }

    qInfo() << "IID imported:" << filePath
            << "constraint groups=" << groupedNodeIds.size();

    return true;
}

bool PhysicalField::autoGenerateDisplacementConstraints(QString* message)
{
    auto fail = [message](const QString& text)
    {
        if (message)
            *message = text;
        return false;
    };

    if (!m_node)
        return fail(QStringLiteral("约束与荷载节点尚未初始化。"));

    vtkUnstructuredGrid* solidGrid = PickManager::instance().solidGrid();
    if (!solidGrid || solidGrid->GetNumberOfCells() <= 0 || solidGrid->GetNumberOfPoints() <= 0)
        return fail(QStringLiteral("请先加载或生成计算网格。"));

    const bool is2d = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2;
    int verticalAxis = 2;
    if (!is2d)
    {
        if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            verticalAxis = (meshSvc->verticalAxis() == 3) ? 3 : 2;
    }
    const bool yUp3d = (!is2d && verticalAxis == 2);
    double bounds[6]{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    solidGrid->GetBounds(bounds);
    const bool useYHeight = is2d || yUp3d;
    const double heightSpan = useYHeight ? (bounds[3] - bounds[2]) : (bounds[5] - bounds[4]);
    const double topCutoff = (useYHeight ? bounds[3] : bounds[5]) - std::max(0.0, heightSpan) * 0.05;

    auto geometry = vtkSmartPointer<vtkGeometryFilter>::New();
    geometry->SetInputData(solidGrid);
    geometry->PassThroughPointIdsOn();
    geometry->SetOriginalPointIdsName("vtkOriginalPointIds");
    geometry->Update();

    vtkPolyData* surface = geometry->GetOutput();
    if (!surface || surface->GetNumberOfCells() <= 0 || surface->GetNumberOfPoints() <= 0)
        return fail(QStringLiteral("未能提取网格外表面，无法自动识别边界。"));

    vtkDataArray* origPointIds = surface->GetPointData()
                                     ? surface->GetPointData()->GetArray("vtkOriginalPointIds")
                                     : nullptr;

    QSet<vtkIdType> bottomNodes;
    QSet<vtkIdType> xNodes;
    QSet<vtkIdType> yNodes;

    auto resolveSolidId = [&](vtkIdType surfacePid) -> vtkIdType
    {
        if (origPointIds && surfacePid >= 0 && surfacePid < origPointIds->GetNumberOfTuples())
            return static_cast<vtkIdType>(origPointIds->GetComponent(surfacePid, 0));
        return surfacePid;
    };

    const vtkIdType cellCount = surface->GetNumberOfCells();
    for (vtkIdType ci = 0; ci < cellCount; ++ci)
    {
        vtkCell* cell = surface->GetCell(ci);
        vtkIdList* ids = cell ? cell->GetPointIds() : nullptr;
        if (!ids || ids->GetNumberOfIds() < 3)
            continue;

        double p0[3], p1[3], p2[3];
        surface->GetPoint(ids->GetId(0), p0);
        surface->GetPoint(ids->GetId(1), p1);
        surface->GetPoint(ids->GetId(2), p2);

        double v1[3]{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        double v2[3]{p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        double n[3]{
            v1[1] * v2[2] - v1[2] * v2[1],
            v1[2] * v2[0] - v1[0] * v2[2],
            v1[0] * v2[1] - v1[1] * v2[0]
        };
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len <= 1.0e-12)
            continue;
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;

        double cz = 0.0;
        const vtkIdType nPts = ids->GetNumberOfIds();
        for (vtkIdType pi = 0; pi < nPts; ++pi)
        {
            double pt[3];
            surface->GetPoint(ids->GetId(pi), pt);
            cz += useYHeight ? pt[1] : pt[2];
        }
        cz /= double(nPts);

        const bool isBottom = (useYHeight ? (n[1] < -0.85) : (n[2] < -0.85)) && (cz <= topCutoff);
        const bool isXSide = std::abs(n[0]) > 0.85;
        const bool isYSide = !is2d && (yUp3d ? (std::abs(n[2]) > 0.85) : (std::abs(n[1]) > 0.85));

        if (!isBottom && !isXSide && !isYSide)
            continue;

        for (vtkIdType pi = 0; pi < nPts; ++pi)
        {
            const vtkIdType sid = resolveSolidId(ids->GetId(pi));
            if (sid < 0 || sid >= solidGrid->GetNumberOfPoints())
                continue;
            if (isBottom)
                bottomNodes.insert(sid);
            if (isXSide)
                xNodes.insert(sid);
            if (isYSide)
                yNodes.insert(sid);
        }
    }

    // Disjoint partition: SelectionStore is mutually exclusive across constraint nodes.
    QSet<vtkIdType> xyzSet = bottomNodes;
    QSet<vtkIdType> xySet;
    QSet<vtkIdType> xOnlySet;
    QSet<vtkIdType> yOnlySet;

    for (vtkIdType id : xNodes)
    {
        if (xyzSet.contains(id))
            continue;
        if (yNodes.contains(id))
            xySet.insert(id);
        else
            xOnlySet.insert(id);
    }
    for (vtkIdType id : yNodes)
    {
        if (xyzSet.contains(id) || xySet.contains(id))
            continue;
        yOnlySet.insert(id);
    }

    struct AutoGroup
    {
        QString title;
        int ux;
        int uy;
        int uz;
        QSet<vtkIdType> ids;
    };

    const QVector<AutoGroup> groups = {
        {is2d ? QStringLiteral("自动约束-底面(XY)") : QStringLiteral("自动约束-底面(XYZ)"),
         0, 0, is2d ? 1 : 0, xyzSet},
        {yUp3d ? QStringLiteral("自动约束-侧角交线(XZ)") : QStringLiteral("自动约束-侧角交线(XY)"),
         0, yUp3d ? 1 : 0, yUp3d ? 0 : 1, xySet},
        {QStringLiteral("自动约束-X侧边界"), 0, 1, 1, xOnlySet},
        {yUp3d ? QStringLiteral("自动约束-Z侧边界") : QStringLiteral("自动约束-Y侧边界"),
         1, yUp3d ? 1 : 0, yUp3d ? 0 : 1, yOnlySet},
    };

    int createdGroups = 0;
    int totalNodes = 0;
    ProjectNode* lastCreated = nullptr;
    for (const AutoGroup& g : groups)
    {
        if (g.ids.isEmpty())
            continue;

        QVector<vtkIdType> sortedIds;
        sortedIds.reserve(g.ids.size());
        for (vtkIdType id : g.ids)
            sortedIds.push_back(id);
        std::sort(sortedIds.begin(), sortedIds.end());

        ProjectNode* node = createNodeByKind(QStringLiteral("DisplacementConstraint"),
                                             g.title,
                                             QUuid::createUuid().toString());
        if (!node || !node->getPage())
            continue;

        QJsonObject pageData;
        pageData["ux"] = QString::number(g.ux);
        pageData["uy"] = QString::number(g.uy);
        pageData["uz"] = QString::number(g.uz);
        node->getPage()->loadJson(pageData);

        const QString domain = node->getPage()->getPageType();
        PickManager::instance().setSelectedPointIds(domain,
                                                    node->getUuid(),
                                                    GridKind::Solid,
                                                    sortedIds,
                                                    true);
        ++createdGroups;
        totalNodes += sortedIds.size();
        lastCreated = node;
    }

    if (createdGroups == 0)
        return fail(QStringLiteral("未识别到满足正交边界条件的底面或侧面节点。"));

    if (lastCreated)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(lastCreated);
    }

    if (message)
        *message = QStringLiteral("已自动生成 %1 组位移约束，共 %2 个边界节点。")
                       .arg(createdGroups)
                       .arg(totalNodes);
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
                                    vtkUnstructuredGrid* g, vtkDataArray* nodeIdArr)
{
    if (!g || !nodeIdArr || wanted.isEmpty())
        return;

    const vtkIdType nPts = g->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        const int nodeId = nodeIdArr->GetTuple1(int(pid));
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

    vtkDataArray* idSolid =
        gSolid ? vtkDataArray::SafeDownCast(gSolid->GetPointData()->GetArray("node_id")) : nullptr;
    vtkDataArray* idLine =
        gLine ? vtkDataArray::SafeDownCast(gLine->GetPointData()->GetArray("node_id")) : nullptr;

    if (!gSolid && !gLine)
    {
        qWarning() << "IIWL Part1: both solidGrid and lineGrid are null";
        return;
    }
    if ((gSolid && gSolid->GetNumberOfPoints()>0 && !idSolid) || (gLine && gLine->GetNumberOfPoints()>0 && !idLine))
    {
        qWarning() << "IIWL Part1: node_id array missing on some grid";
        return;
    }

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const int NCOM = (NDIM == DIMENSION_2D) ? 4 : 6;

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

    // SOLVER READ_IIWL 2D order: XP YP SGX SGY TXY SGZ II (NCOM=3 reads first 3 stress cols)
    if (NDIM == DIMENSION_2D)
        os << "XP\tYP\tSGX\tSGY\tTXY\tSGZ\tII\n";
    else
        os << "XP\tYP\tZP\tSGX\tSGY\tSGZ\tTXY\tTYZ\tTZX\tII\n";

    // ✅ II：顺序号（1..NUM_GEO）
    for (int i = 0; i < nodeOrder.size(); ++i)
    {
        const int nodeId = nodeOrder[i];
        const XYZ xyz = xyzOf.value(nodeId, XYZ{0, 0, 0});
        const StressVal s = stressOfNode.value(nodeId, StressVal{});

        const int II = i + 1;

        if (NDIM == DIMENSION_2D)
        {
            os << fmtE(xyz.x) << "\t" << fmtE(xyz.y) << "\t" << fmtE(s.sx) << "\t" << fmtE(s.sy)
               << "\t" << fmtE(s.sxy) << "\t" << fmtE(s.sz) << "\t" << II << "\n";
        }
        else
        {
            os << fmtE(xyz.x) << "\t" << fmtE(xyz.y) << "\t" << fmtE(xyz.z) << "\t" << fmtE(s.sx)
               << "\t" << fmtE(s.sy) << "\t" << fmtE(s.sz) << "\t" << fmtE(s.sxy) << "\t"
               << fmtE(s.syz) << "\t" << fmtE(s.sxz) << "\t" << II << "\n";
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

    int exportedFitFaceIndex = 0;
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
        // SOLVER READ_IIWL strictly requires II == 1..NFA_GEO.
        const int ii = ++exportedFitFaceIndex;

        os << fname << "\t" << idir << "\t" << fmtE(height) << "\t" << fmtE(rou) << "\t"
           << fmtE(pgrout) << "\t" << ii << "\n";
    }
}

namespace
{
struct SelectedLoadBoundary { int element; QVector<int> nodes; };
QVector<SelectedLoadBoundary> loadBoundaries(const QString& domain, const QString& uuid, bool edge)
{
    auto& picks=PickManager::instance();QVector<SelectedLoadBoundary> result;
    for (const auto kind : {GridKind::Solid, GridKind::Line})
    {
        auto* grid=kind==GridKind::Solid?picks.solidGrid():picks.lineGrid();
        if(!grid)continue;
        auto* ids=grid->GetPointData()->GetArray("node_id");
        auto* elements=grid->GetCellData()->GetArray("elem_id");
        if(!ids||!elements)continue;
        auto keys=picks.getIdList(domain,uuid,edge?PickMode::Edge:PickMode::Face,kind);
        std::sort(keys.begin(),keys.end());
        for (auto key : keys)
        {
            const auto ci=key>>4;const int local=int(key&15);
            if(ci<0||ci>=grid->GetNumberOfCells())continue;
            auto* cell=grid->GetCell(ci);if(!cell)continue;
            vtkCell* part=nullptr;
            if(edge)
            {
                if(cell->GetCellDimension()==1)part=cell;
                else if(local<cell->GetNumberOfEdges())part=cell->GetEdge(local);
            }
            else if(cell->GetCellDimension()==2||local==15)part=cell;
            else if(local<cell->GetNumberOfFaces())part=cell->GetFace(local);
            if(!part)continue;
            const int count=int(part->GetNumberOfPoints());QVector<int> order;
            // VTK puts midside nodes after corners. Native line/face loads
            // require the perimeter order, including each midside node.
            if(edge&&count==3)order={0,2,1};
            else if(!edge&&count==8)order={0,4,1,5,2,6,3,7};
            else if(!edge&&count==6)order={0,3,1,4,2,5};
            else for(int j=0;j<count;++j)order.append(j);
            SelectedLoadBoundary record{int(elements->GetTuple1(ci)),{}};
            for(int j:order)record.nodes.append(int(ids->GetTuple1(part->GetPointId(j))));
            result.append(record);
        }
    }
    return result;
}
}

void PhysicalField::writeIIWL_Part4_PointForce(QTextStream& os)
{
    PickManager::instance().updateStructure();

    vtkUnstructuredGrid* gSolid = PickManager::instance().solidGrid();
    vtkUnstructuredGrid* gLine = PickManager::instance().lineGrid();

    vtkDataArray* nodeIdSolid =
        gSolid ? vtkDataArray::SafeDownCast(gSolid->GetPointData()->GetArray("node_id")) : nullptr;
    vtkDataArray* nodeIdLine =
        gLine ? vtkDataArray::SafeDownCast(gLine->GetPointData()->GetArray("node_id")) : nullptr;

    vtkDataArray* elemIdSolid =
        gSolid ? vtkDataArray::SafeDownCast(gSolid->GetCellData()->GetArray("elem_id")) : nullptr;
    vtkDataArray* elemIdLine =
        gLine ? vtkDataArray::SafeDownCast(gLine->GetCellData()->GetArray("elem_id")) : nullptr;

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QString domain = "PointForcePage";

    if (!gSolid && !gLine)
    {
        qWarning() << "IIWL Part4: both solidGrid and lineGrid are null";
        return;
    }
    if ((gSolid && gSolid->GetNumberOfPoints()>0 && !nodeIdSolid) || (gLine && gLine->GetNumberOfPoints()>0 && !nodeIdLine))
    {
        qWarning() << "IIWL Part4: node_id array missing";
        return;
    }

    // ---------- 建 node_id -> pid 的快速映射 ----------
    auto buildMapNodeIdToPid = [](vtkUnstructuredGrid* g, vtkDataArray* nodeIdArr)
    {
        QHash<int, vtkIdType> mp;
        if (!g || !nodeIdArr)
            return mp;
        mp.reserve(int(g->GetNumberOfPoints() * 1.3));
        const vtkIdType n = g->GetNumberOfPoints();
        for (vtkIdType pid = 0; pid < n; ++pid)
        {
            mp.insert(nodeIdArr->GetTuple1(int(pid)), pid);
        }
        return mp;
    };

    const QHash<int, vtkIdType> mapSolid = buildMapNodeIdToPid(gSolid, nodeIdSolid);
    const QHash<int, vtkIdType> mapLine = buildMapNodeIdToPid(gLine, nodeIdLine);

    // ---------- 给定 pid，选一个“所属单元 elem_id”（稳定规则：最小 elem_id）
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
        vtkIdType bestCell = cellIds->GetId(0);

        for (vtkIdType i = 0; i < cellIds->GetNumberOfIds(); ++i)
        {
            vtkIdType cid = cellIds->GetId(i);
            if (elemIdArr)
            {
                int eid = elemIdArr->GetTuple1(int(cid));
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
        const double fz = (NDIM == DIMENSION_3D) ? page->fzValue() : 0.0;

        const double mx = (NDIM == DIMENSION_3D) ? page->mxValue() : 0.0;
        const double my = (NDIM == DIMENSION_3D) ? page->myValue() : 0.0;
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
            vtkDataArray* elemArr = nullptr;
            vtkIdType pid = -1;

            // For 2D nodes with non-zero moment M, prefer the line (KETYPE=21 beam) grid
            // so MAKEFC dispatches to FC1D2N_BEAM instead of dropping M on a 2-DOF solid cell.
            const bool preferBeam = (NDIM == DIMENSION_2D && std::abs(mz) > 1.0e-12);
            auto itL = mapLine.find(nodeId);
            auto itS = mapSolid.find(nodeId);
            if (preferBeam && itL != mapLine.end())
            {
                g = gLine;
                elemArr = elemIdLine;
                pid = itL.value();
            }
            else if (itS != mapSolid.end())
            {
                g = gSolid;
                elemArr = elemIdSolid;
                pid = itS.value();
            }
            else if (itL != mapLine.end())
            {
                g = gLine;
                elemArr = elemIdLine;
                pid = itL.value();
            }
            if (!g || pid < 0)
                continue;

            double p[3];
            g->GetPoint(pid, p);
            const int jmfc = chooseElemIdForPid(g, elemArr, pid);

            if (NDIM == DIMENSION_2D)
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

    if (NDIM == DIMENSION_3D)
    {
        // MAKEFL in the deployed solver passes an uninitialized local NFT
        // instead of NFL. Constant edge tractions have an equivalent nodal
        // representation, so integrate them before exporting native loads.
        QList<ProjectNode*> linePages;
        CollectNodesByKind(m_node, "LinearForcePage", linePages);
        constexpr double xi[]={-.906179845938664,-.538469310105683,0.,.538469310105683,.906179845938664};
        constexpr double weight[]={.236926885056189,.478628670499366,.568888888888889,.478628670499366,.236926885056189};
        for (auto* node : linePages)
        {
            auto* page=dynamic_cast<LinearForcePage*>(node->getPage());if(!page)continue;
            for (const auto& boundary : loadBoundaries("LinearForcePage",node->getUuid(),true))
            {
                const int count=boundary.nodes.size();if(count!=2&&count!=3)continue;
                QSet<int> wanted;for(int id:boundary.nodes)wanted.insert(id);
                QHash<int,XYZ> coordinates;
                FillXYZForWantedNodeIds(wanted,coordinates,gSolid,nodeIdSolid);
                FillXYZForWantedNodeIds(wanted,coordinates,gLine,nodeIdLine);
                QVector<double> nodalWeight(count,0.0);
                for(int k=0;k<5;++k)
                {
                    const double t=xi[k];
                    const QVector<double> shape=count==2?QVector<double>{(1-t)/2,(1+t)/2}
                        :QVector<double>{t*(t-1)/2,1-t*t,t*(t+1)/2};
                    const QVector<double> derivative=count==2?QVector<double>{-.5,.5}
                        :QVector<double>{t-.5,-2*t,t+.5};
                    XYZ tangent;
                    for(int j=0;j<count;++j)
                    {
                        const auto p=coordinates[boundary.nodes[j]];
                        tangent.x+=derivative[j]*p.x;tangent.y+=derivative[j]*p.y;tangent.z+=derivative[j]*p.z;
                    }
                    const double jac=std::sqrt(tangent.x*tangent.x+tangent.y*tangent.y+tangent.z*tangent.z);
                    for(int j=0;j<count;++j)nodalWeight[j]+=shape[j]*jac*weight[k];
                }
                for(int j=0;j<count;++j)
                {
                    const auto p=coordinates[boundary.nodes[j]];
                    rows3.push_back({boundary.element,p.x,p.y,p.z,page->fxValue()*nodalWeight[j],
                        page->fyValue()*nodalWeight[j],page->fzValue()*nodalWeight[j],0,0,0});
                }
            }
        }
    }
    const int NFC = (NDIM == DIMENSION_2D) ? rows2.size() : rows3.size();

    os << "\n";
    os << QString("%1\tNFC\n").arg(NFC);

    if (NDIM == DIMENSION_2D)
        os << "JMFC\tXC\tYC\tFX\tFY\tM\tII(BLANK)\n";
    else
        os << "JMFC\tXC\tYC\tZC\tFX\tFY\tFZ\tMX\tMY\tMZ\tII(BLANK)\n";

    // II(BLANK) = 序号 1..NFC
    if (NDIM == DIMENSION_2D)
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
    // 3D line traction is integrated into nodal forces in Part4;
    // 2D edge traction is written in Part6.
    os << "0 NFL\n";
}

bool PhysicalField::writeIIWL_Part7_TemperatureLoad(QTextStream& os)
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
                if (rows[idx].alfa != alfa || rows[idx].dt != dt)
                {
                    QMessageBox::warning(nullptr, QStringLiteral("请检查变温荷载"),
                        QStringLiteral("节点 %1 被赋予不同的温差或膨胀系数，请删除重复设置。").arg(nodeId));
                    return false;
                }
            }
        }
    }

    const QString project = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QSaveFile alphaFile(QDir(SolverFilePaths::outputInputDirectory(project)).filePath(QStringLiteral("THERMAL_ALPHA.DAT")));
    if (!alphaFile.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream alphaStream(&alphaFile);
    alphaStream << rows.size() << " NODE_ALPHA\n";
    for (const auto& row : rows)
        alphaStream << row.nodeId << ' ' << QString::number(row.alfa, 'g', 17) << '\n';
    alphaStream.flush();
    if (!alphaFile.commit()) return false;

    // NFW=1 means a uniform temperature over the entire native mesh. Use
    // every mesh node (zero outside the selection) for a local nodal field.
    if (!rows.isEmpty())
    {
        for (auto* grid : {PickManager::instance().solidGrid(), PickManager::instance().lineGrid()})
        {
            if (!grid || grid->GetNumberOfPoints()==0) continue;
            auto* ids = grid->GetPointData()->GetArray("node_id");
            if (!ids) return false;
            for (vtkIdType i=0; i<grid->GetNumberOfPoints(); ++i)
            {
                const int id = int(ids->GetTuple1(i));
                if (!idxOfNode.contains(id))
                {
                    idxOfNode.insert(id, rows.size());
                    rows.push_back({id, 0.0, 0.0});
                }
            }
        }
    }
    const int NFW = rows.size();

    os << "\n";
    os << QString("%1\tNFW\n").arg(NFW);

    os << "JMFW\tDT\tII\n";

    qInfo() << "IIWL Part7 FINAL NFW=" << NFW;

    for (int i = 0; i < rows.size(); ++i)
    {
        const int II = i + 1;
        os << rows[i].nodeId << "\t" << fmtE(rows[i].dt) << "\t" << II
           << "\n";
    }
    return true;
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
        const double fz = (NDIM == DIMENSION_3D) ? page->fzValue() : 0.0;

        // ✅ 面力：一般在 solidGrid（优先 solid）
        for (const auto& boundary : loadBoundaries(domainFace, uuid, NDIM == DIMENSION_2D))
        {
            const int ke = boundary.element;
            const QVector<int>& nds = boundary.nodes;
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

    if (NDIM == DIMENSION_2D)
    {
        QList<ProjectNode*> linePages;
        CollectNodesByKind(m_node, "LinearForcePage", linePages);
        for (auto* node : linePages)
        {
            auto* page=dynamic_cast<LinearForcePage*>(node->getPage());if(!page)continue;
            for (const auto& boundary : loadBoundaries("LinearForcePage",node->getUuid(),true))
                recs.push_back({boundary.element,boundary.nodes,page->fxValue(),page->fyValue(),0.0});
        }
    }
    const int NFT = recs.size();
    const int LXJMET = 1; // 已知面荷载作用单元（我们输出 KE）

    os << "\n";
    os << NFT << "\t" << "NFT" << "\n";
    os << "0 0 0 IFUSHE NFT_A NFT_B\n";
    os << "0 0 0 0.0 0.0 IFWEST NFT_C NFT_D HWUP_TOP HWUP_BOT\n";

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

        const int maxLdType = (NDIM == DIMENSION_2D) ? 3 : 4;
        if (ldType < 1 || ldType > maxLdType)
        {
            qWarning() << "[Part8] invalid beam LDTYPE=" << ldType << "(allowed 1.." << maxLdType << "); skipping page" << uuid;
            continue;
        }
        const int safeIdir = (NDIM == DIMENSION_2D) ? 1 : qBound(1, idir, 3);
        auto consumeNodes = [&](vtkUnstructuredGrid* g, vtkDataArray* elemIdArr,
                                const QHash<int, vtkIdType>& map, const QVector<vtkIdType>& nodeIds,
                                const char* tag)
        {
            if (!g)
                return;
            // Deduplicate beam element IDs so selecting both endpoints does not double-load a beam.
            QSet<int> emittedElems;
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
                if (ke <= 0 || emittedElems.contains(ke))
                    continue;
                emittedElems.insert(ke);

                Row r;
                r.jmfg = ke;
                r.xp = pt[0];
                r.yp = pt[1];
                r.zp = (NDIM == DIMENSION_3D) ? pt[2] : 0.0;
                r.ldType = ldType;
                r.a = a;
                r.p = p;
                r.idir = safeIdir;
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
                QSet<int> emittedElems;

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
                    if (ke <= 0 || emittedElems.contains(ke))
                        continue;
                    emittedElems.insert(ke);

                    Row r;
                    r.jmfg = ke;
                    r.xp = pt[0];
                    r.yp = pt[1];
                    r.zp = (NDIM == DIMENSION_3D) ? pt[2] : 0.0;
                    r.ldType = ldType;
                    r.a = a;
                    r.p = p;
                    r.idir = safeIdir;
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
    if (NDIM == DIMENSION_2D)
        os << "JMFG\tXP\tYP\tLDTYPE\tA\tP\tIDIR\tII\n";
    else
        os << "JMFG\tXP\tYP\tZP\tLDTYPE\tA\tP\tIDIR\tII\n";

    qInfo() << "IIWL Part8 FINAL NFG=" << NFG;

    for (int i = 0; i < rows.size(); ++i)
    {
        const int II = i + 1;
        const Row& r = rows[i];

        if (NDIM == DIMENSION_2D)
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
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return;

    // 先移除上一次生成的波形，避免节点缺失/重复/数据无效时误用旧文件。
    for (const QString& fileName : {QStringLiteral("WAVE-AX.DAT"),
                                    QStringLiteral("WAVE-AY.DAT"),
                                    QStringLiteral("WAVE-AZ.DAT")})
        QFile::remove(inputDir.filePath(fileName));

    QList<ProjectNode*> pages;
    CollectNodesByKind(m_node, "SeismicAndAddedMassPage", pages);
    qInfo() << "WAVE pages.size=" << pages.size();

    if (pages.size() != 1)
    {
        qWarning() << "generateWaveFiles: expected exactly one seismic page, actual="
                   << pages.size();
        return;
    }

    auto* page = dynamic_cast<SeismicAndAddedMassPage*>(pages.first()->getPage());
    if (!page)
    {
        qWarning() << "generateWaveFiles: seismic page is invalid";
        return;
    }

    QString validationError;
    if (!page->validateWaveInput(&validationError))
    {
        qWarning().noquote() << "generateWaveFiles:" << validationError;
        return;
    }

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
        const QString path = inputDir.filePath(fileName);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            qWarning() << "generateWaveFiles: open failed:" << path;
            return;
        }

        QTextStream out(&file);
        const int N = series.size();
        // SOLVER wave readers ignore column 2 (AM) and hardcode AMP_A*=1.0;
        // pre-multiply non-zero amp directly into the acceleration samples
        // (preserve legacy amp==0.0 projects as unscaled 1.0).
        const double effectiveAmp = (std::abs(amp) > 1.0e-12) ? amp : 1.0;
        out << QString("%1      %2 NUM  AM\n").arg(N, 6).arg(f4(1.0), 10);
        out << QString("   T(S)           %1(m/S^2)       II\n").arg(axisLabel);

        for (int i = 0; i < N; ++i)
        {
            out << QString("%1       %2%3\n")
                       .arg(f6(series[i].first), 13)
                       .arg(f6(series[i].second * effectiveAmp), 15)
                       .arg(i + 1, 10);
        }

        file.close();
        qInfo() << "WAVE generated :" << path << "N=" << N << "AMP=" << amp;
    };

    QVector<QPair<double, double>> sx = page->seriesX();
    QVector<QPair<double, double>> sy = page->seriesY();
    QVector<QPair<double, double>> sz = page->seriesZ();
    const double axAmp = page->ampX();
    const double ayAmp = page->ampY();
    const double azAmp = page->ampZ();

    writeOneFile("WAVE-AX.DAT", sx, axAmp, "AX");
    writeOneFile("WAVE-AY.DAT", sy, ayAmp, "AY");
    writeOneFile("WAVE-AZ.DAT", sz, azAmp, "AZ");
}

bool PhysicalField::generateIIWLFile()
{
    if(!m_constraintsValid)return false;
    auto* meshSvc=ServiceRegistry::instance().getService<MeshModuleControllerService>();
    const auto beamNodes=planarBeamNodes(meshSvc?meshSvc->getVtkUnstructuredGrid():nullptr);
    const int ndim=PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    QList<ProjectNode*> forcePages;CollectNodesByKind(m_node,"PointForcePage",forcePages);
    for(auto* node:forcePages)
    {
        auto* page=dynamic_cast<PointForcePage*>(node->getPage());if(!page)continue;
        if(page->mxValue()==0&&page->myValue()==0&&page->mzValue()==0)continue;
        for(auto kind:{GridKind::Solid,GridKind::Line})
            for(auto id:PickManager::instance().getIdList("PointForcePage",node->getUuid(),PickMode::Node,kind))
                if(ndim!=2||!beamNodes.contains(int(id))||page->mxValue()!=0||page->myValue()!=0)
                {
                    QMessageBox::warning(nullptr,QStringLiteral("节点力矩不适用"),QStringLiteral("当前已验证的节点力矩为二维平面梁的绕 Z 力矩。所选节点或力矩方向不匹配，无法导出。"));
                    return false;
                }
    }
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;
    const QString path = inputDir.filePath(QStringLiteral("IIWL.DAT"));

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "generateIIWLFile: open failed:" << path;
        return false;
    }

    QTextStream os(&f);

    writeIIWL_Part1_GeoStressPoint(os);
    writeIIWL_Part2_GeoStressFitFace(os);
    const int dimension = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    os << m_exportedDofs+m_prescribedValues.size() << " NIC\nII CONA CONAP CONAPP\n";
    for (int i=1; i<=m_exportedDofs; ++i) os << i << " 0.0 0.0 0.0\n";
    for(int i=0;i<m_prescribedValues.size();++i)
        os << m_exportedDofs+i+1 << ' ' << QString::number(m_prescribedValues[i],'g',17) << " 0.0 0.0\n";
    writeIIWL_Part3_BodyForce(os);
    writeIIWL_Part4_PointForce(os);
    writeIIWL_Part5_LinearForce(os);
    os << "0 0 " << (dimension == 3 ? 6 : 4) << " NFS MAGSS MACOM\nJMFS MATYPE IG STRESS\n";
    writeIIWL_Part6_FaceLoad(os);
    if (!writeIIWL_Part7_TemperatureLoad(os)) return false;
    writeIIWL_Part8_BeamSpecialDistributedLoad(os);
    os << "0 IMODE_DYNA\n0 1.0 NTBX AMP_AX\n0 1.0 NTBY AMP_AY\n0 1.0 NTBZ AMP_AZ\n";

    os.flush();
    if (!f.commit()) return false;
    qInfo() << "IIWL generated :" << path;

    generateWaveFiles();
    return true;
}
