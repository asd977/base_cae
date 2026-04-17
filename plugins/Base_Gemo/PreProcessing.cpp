
#include "PreProcessing.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <GeomManagerInstance.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/PhysicalFieldService.h>
#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <vtkAppendPolyData.h>
#include <vtkAutoInit.h>
#include <vtkCamera.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkCleanPolyData.h>
#include <vtkCollectionIterator.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkFollower.h>
#include <vtkGlyph3D.h>
#include <vtkHexahedron.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationObjectBaseVectorKey.h>
#include <vtkLODActor.h>
#include <vtkLight.h>
#include <vtkLightCollection.h>
#include <vtkLine.h>
#include <vtkMapper.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkQuad.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSphereSource.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVectorText.h>
#include <vtkVertexGlyphFilter.h>

#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMetaObject>
#include <QString>
#include <QToolButton>
#include <QUuid>
#include <STEPControl_Writer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "BaseGeometryDialog\Boxdialog.h"
#include "BaseGeometryDialog\CircleDialog.h"
#include "BaseGeometryDialog\ConeDialog.h"
#include "BaseGeometryDialog\CylinderDialog.h"
#include "BaseGeometryDialog\EllipseDialog.h"
#include "BaseGeometryDialog\PolygonDialog.h"
#include "BaseGeometryDialog\RectangleDialog.h"
#include "BaseGeometryDialog\SphereDialog.h"
#include "BaseGeometryDialog\TorusDialog.h"
#include "GeometryCleanup.h"
#include "GeometryImportWidget.h"
#include "OccBooleanCommonWidget.h"
#include "OccBooleanCutWidget.h"
#include "OccBooleanFuseWidget.h"
#include "vtkActor.h"
#include "vtkCamera.h"
#include "vtkRenderer.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PreProcessing::PreProcessing()
{
    ServiceRegistry::instance().registerService<PreProcessingService>(this);

    // Ribbon 事件绑定依赖 UiFrameService，必须先取服务再注册。
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();

    initPageFactory();
    initRibbonActionConn();
    initTreeMenu();
}

void PreProcessing::initPageFactory()
{
    m_pageFactory.clear();
    // 网格
    // m_pageFactory[PublicConstants::kPagePreprocessMesh2d] = [this](QString
    // title) { return m_preProcessingWidget->newTetrahedronGridWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessMesh3d] = [this](QString
    // title) { return m_preProcessingWidget->newHexahedronGridWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessImportGrid] = [this](QString
    // title) { return m_preProcessingWidget->newGridImportWidget(title); };
    // // 几何
    // m_pageFactory[PublicConstants::kPagePreprocessImportGeometry] =
    // [this](QString title) { return
    // m_preProcessingWidget->newGeometryImportWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessArc] = [this](QString title){
    // return m_preProcessingWidget->newGeometryArcWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessBezier2d] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryTwoBsrWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessBezier3d] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryThreeBsrWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessCone] = [this](QString title)
    // { return m_preProcessingWidget->newGeometryConeWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessCylinder] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryCylinderWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessEllipse] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryEllipseWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessHelix] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryHelixWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessCircle] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryCircleWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessLine] = [this](QString title)
    // { return m_preProcessingWidget->newGeometryLineWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessTorus] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryTorusWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessSquare] = [this](QString
    // title) { return m_preProcessingWidget->newGeometrySquareWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessSphere] = [this](QString
    // title) { return m_preProcessingWidget->newGeometrySphereWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessRectangle] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryRectangleWidget(title);
    // }; m_pageFactory[PublicConstants::kPagePreprocessPolygon] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryPolygonWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessInterCurve] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryInterCurveWidget(title);
    // }; m_pageFactory[PublicConstants::kPagePreprocessPoint] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryPointWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessParametricCurve] =
    // [this](QString title) { return
    // m_preProcessingWidget->newGeometryParamCurveWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessBox] = [this](QString title)
    // { return m_preProcessingWidget->newGeometryBoxWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessHyperbola] = [this](QString
    // title) { return m_preProcessingWidget->newGeometryHyperbolaWidget(title);
    // }; m_pageFactory[PublicConstants::kPagePreprocessParabola] = [this](QString
    // title) { return
    // m_preProcessingWidget->newGeometryParabolaEdgeWidget(title); };
}

void PreProcessing::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node", this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();

            ContextMenuHub::addSeparator(menu);

            ContextMenuHub::addAction(menu, this, "隐藏/显示",
                                      [this, uuid]
                                      {
                                          this->toggleVisible(uuid);
                                      });

            ContextMenuHub::addAction(menu, this, "删除",
                                      [this, uuid]
                                      {
                                          this->removeNode(uuid);
                                      });
            ContextMenuHub::addAction(menu, this, "重命名",
                                      [this, uuid]
                                      {
                                          this->renameNode(uuid);
                                      });
        },
        100,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value("module").toString() == "Geometry";
        });
}

void PreProcessing::toggleVisible(const QString& uuid)
{
    if (uuid.isEmpty())
        return;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return;

    GeomShapeObject* object = mgr->getGeomShapeObject(uuid);
    if (!object)
        return;

    const bool currentVisible =
        (object->getSurfActor() && object->getSurfActor()->GetVisibility() != 0) ||
        (object->getEdgeActor() && object->getEdgeActor()->GetVisibility() != 0);
    const bool targetVisible = !currentVisible;

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->setVisible(uuid + "_face", targetVisible, false);
        vis->setVisible(uuid + "_edge", targetVisible, true);
    }
    else
    {
        if (object->getSurfActor())
            object->getSurfActor()->SetVisibility(targetVisible);
        if (object->getEdgeActor())
            object->getEdgeActor()->SetVisibility(targetVisible);
    }

    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(uuid, !targetVisible);
    }
}

void PreProcessing::removeNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode)
        return;

    GeometryCleanup::removeExistingGeometry(uuid);

    for (int i = 0; i < m_geomNode->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_geomNode->child(i));
        if (!node)
            continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        if (nodeUuid != uuid)
            continue;

        if (BasePage* page = node->getPage())
        {
            page->deleteLater();
            node->setPage(nullptr);
        }

        m_geomNode->removeChild(node);
        delete node;
        break;
    }
}

void PreProcessing::renameNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode)
        return;

    for (int i = 0; i < m_geomNode->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_geomNode->child(i));
        if (!node)
            continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        if (nodeUuid != uuid)
            continue;

        const QString currentName = node->text(0);
        bool ok = false;
        const QString newName = QInputDialog::getText(
            nullptr, tr("重命名几何"), tr("请输入新名称："), QLineEdit::Normal, currentName, &ok);

        if (!ok)
            return;
        const QString trimmedName = newName.trimmed();
        if (trimmedName.isEmpty() || trimmedName == currentName)
            return;

        node->setText(0, trimmedName);
        node->setName(trimmedName);
        if (BasePage* page = node->getPage())
        {
            page->setDisplayName(trimmedName);
        }
        if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
        {
            if (GeomShapeObject* object = mgr->getGeomShapeObject(uuid))
            {
                object->setName(trimmedName);
            }
        }
        return;
    }
}

BasePage* PreProcessing::createPageByType(const QString& type, const QString& title)
{
    auto it = m_pageFactory.find(type);
    return it == m_pageFactory.end() ? nullptr : it.value()(title);
}

void PreProcessing::newProject()
{
    m_geomNode = new ProjectNode("几何");
    m_geomNode->setData(0, ProjectNode::RoleModule, "Geometry");
    m_geomNode->setData(0, ProjectNode::RoleKind, "GeometryRoot");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_geomNode);
}

bool PreProcessing::saveProject()
{
    if (!m_geomNode)
        return true;
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPreProcessingProjectFileName);

    QJsonObject root;
    root["module"] = "PreProcessing";
    root["root_title"] = "前处理";

    QJsonArray geomItems;
    const int n1 = m_geomNode->childCount();
    for (int i = 0; i < n1; ++i)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_geomNode->child(i));
        if (!node)
            continue;
        BasePage* page = node->getPage();

        QJsonObject item;
        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        item["uuid"] = nodeUuid;
        item["title"] = node->text(0);
        item["type"] = page->getPageType();
        item["module"] = node->data(0, ProjectNode::RoleModule).toString();
        item["kind"] = node->data(0, ProjectNode::RoleKind).toString();

        bool isVisible = false;
        if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
        {
            if (GeomShapeObject* object = mgr->getGeomShapeObject(nodeUuid))
            {
                const bool surfVisible =
                    object->getSurfActor() && object->getSurfActor()->GetVisibility() != 0;
                const bool edgeVisible =
                    object->getEdgeActor() && object->getEdgeActor()->GetVisibility() != 0;
                isVisible = surfVisible || edgeVisible;
            }
        }
        item["visible"] = isVisible;
        item["state"] = page->saveJson();
        geomItems.append(item);
    }
    root["geom_items"] = geomItems;

    return writeJsonFile(filePath, root);
}

bool PreProcessing::loadProject()
{
    bool success = false;
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    m_geomNode = new ProjectNode("几何");
    m_geomNode->setData(0, ProjectNode::RoleModule, "Geometry");
    m_geomNode->setData(0, ProjectNode::RoleKind, "GeometryRoot");
    projectManagerService->addTreeItem(m_geomNode);
    success = loadChildProject(m_geomNode, "geom_items");

    return success;
}

bool PreProcessing::loadChildProject(ProjectNode* rootNode, const QString& name)
{
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPreProcessingProjectFileName);
    QJsonObject root;
    if (!readJsonFile(filePath, root))
    {
        return true;
    }

    const QJsonArray items = root.value(name).toArray();
    for (const auto& v : items)
    {
        const auto obj = v.toObject();
        const QString id = obj.value("uuid").toString(QUuid::createUuid().toString());
        const QString title = obj.value("title").toString("未命名");
        const QString type = obj.value("type").toString();
        const QString kind = obj.value("kind").toString();
        const QJsonObject state = obj.value("state").toObject();

        ProjectNode* node = createGeometryNodeByType(type, kind, title, id);
        if (!node)
        {
            continue;
        }

        BasePage* page = node->getPage();
        if (page)
        {
            page->loadJson(state);
            const bool isBooleanPage =
                (type == PublicConstants::kPagePostProcessOccBoolean || type == "occboolen");
            if (isBooleanPage)
            {
                const QString resultBrep = state.value("resultBrep").toString();
                if (!resultBrep.isEmpty())
                {
                    QMetaObject::invokeMethod(page, "restoreSavedResult", Qt::DirectConnection);
                }
            }
            else if (!QMetaObject::invokeMethod(page, "onOkClicked", Qt::DirectConnection))
            {
                QMetaObject::invokeMethod(page, "onImportButtonClicked", Qt::DirectConnection);
            }
        }

        applyNodeVisualState(node, obj);
    }
    return true;
}

ProjectNode* PreProcessing::createGeometryNodeByType(const QString& type, const QString& kind,
                                                     const QString& title, const QString& uuid)
{
    if (!m_geomNode)
    {
        return nullptr;
    }

    BasePage* page = nullptr;
    QString nodeKind = kind;

    if (type == PublicConstants::kPagePreprocessImportGeometry)
    {
        page = new GeometryImportWidget();
        nodeKind = "ImportGeometry";
    }
    else if (type == PublicConstants::kPagePreprocessBox)
    {
        page = new BoxDialog();
        nodeKind = "Box";
    }
    else if (type == PublicConstants::kPagePreprocessSphere)
    {
        page = new SphereDialog();
        nodeKind = "Sphere";
    }
    else if (type == PublicConstants::kPagePreprocessCone)
    {
        page = new ConeDialog();
        nodeKind = "Cone";
    }
    else if (type == PublicConstants::kPagePreprocessCylinder)
    {
        page = new CylinderDialog();
        nodeKind = "Cylinder";
    }
    else if (type == PublicConstants::kPagePreprocessTorus)
    {
        page = new TorusDialog();
        nodeKind = "Torus";
    }
    else if (type == PublicConstants::kPagePreprocessCircle)
    {
        page = new CircleDialog();
        nodeKind = "Circle";
    }
    else if (type == PublicConstants::kPagePreprocessEllipse)
    {
        page = new EllipseDialog();
        nodeKind = "Ellipse";
    }
    else if (type == PublicConstants::kPagePreprocessPolygon)
    {
        page = new PolygonDialog();
        nodeKind = "Polygon";
    }
    else if (type == PublicConstants::kPagePreprocessRectangle)
    {
        page = new RectangleDialog();
        nodeKind = "Rectangle";
    }
    else if (type == PublicConstants::kPagePostProcessOccBoolean || type == "occboolen")
    {
        if (nodeKind == "BooleanUnion")
        {
            page = new OccBooleanFuseWidget();
            nodeKind = "BooleanFuse";
        }
        else if (nodeKind == "BooleanCut")
        {
            page = new OccBooleanCutWidget();
        }
        else if (nodeKind == "BooleanCommon")
        {
            page = new OccBooleanCommonWidget();
        }
        else
        {
            page = new OccBooleanFuseWidget();
            nodeKind = "BooleanFuse";
        }
        if (kind.isEmpty())
            nodeKind = "BooleanFuse";
    }

    if (!page)
    {
        return nullptr;
    }

    auto* node = new ProjectNode(title, uuid, page);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    page->setDisplayName(title);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, nodeKind);
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
    return node;
}

void PreProcessing::applyNodeVisualState(ProjectNode* node, const QJsonObject& itemState) const
{
    if (!node)
    {
        return;
    }

    const QString uuid = node->data(0, ProjectNode::RoleUuid).toString();
    const bool isVisible = itemState.value("visible").toBool(true);
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->setVisible(uuid + "_face", isVisible, false);
        vis->setVisible(uuid + "_edge", isVisible, true);
    }

    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(uuid, !isVisible);
    }
}

void PreProcessing::closeProject()
{
    if (!m_geomNode && !m_gridNode)
        return;

    while (m_geomNode && m_geomNode->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_geomNode->child(0));
        if (node)
        {
            if (auto* page = node->getPage())
            {
                page->deleteLater();

                node->setPage(nullptr);
            }
            delete m_geomNode->takeChild(0);
        }
    }

    while (m_gridNode && m_gridNode->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_gridNode->child(0));
        if (node)
        {
            if (auto* page = node->getPage())
            {
                page->deleteLater();

                node->setPage(nullptr);
            }
            delete m_gridNode->takeChild(0);
        }
    }

    // 从工程树移除根节点
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
    }
    if (m_geomNode)
    {
        delete m_geomNode;
        m_geomNode = nullptr;
    }

    if (m_gridNode)
    {
        delete m_gridNode;
        m_gridNode = nullptr;
    }
}

void PreProcessing::generateStepFile()
{
    // 1. 初始化容器
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    // 2. 获取单例数据
    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    QHash<QString, GeomShapeObject*> tmp = mgr->getShapMap();

    bool hasShape = false;

    // 3. 遍历并添加形状
    // 使用 QHash 的迭代器或 values() 列表
    for (GeomShapeObject* shapeObj : tmp.values())
    {
        if (shapeObj && !shapeObj->getShape()->IsNull()) // 假设 getShape() 返回 TopoDS_Shape
        {
            builder.Add(compound, *(shapeObj->getShape()));
            hasShape = true;
        }
    }

    if (!hasShape)
    {
        // 如果没有有效形状，直接返回避免生成空文件
        return;
    }

    // 4. 配置 STEP 写入器
    STEPControl_Writer stepWriter;

    // 设置模式：优先实体 BRep，失败时回退到 AsIs，兼容单体/多体场景
    IFSelect_ReturnStatus transferStatus =
        stepWriter.Transfer(compound, STEPControl_ManifoldSolidBrep);
    if (transferStatus != IFSelect_RetDone)
    {
        transferStatus = stepWriter.Transfer(compound, STEPControl_AsIs);
    }

    if (transferStatus != IFSelect_RetDone)
    {
        return;
    }

    // 5. 文件路径处理
    // 假设 currentWorPath 是类成员，这里拼接完整路径
    QString fileName = "ExportedModel.step";
    QString currentWorPath = QCoreApplication::applicationDirPath();
    QString dataDir = currentWorPath + "/data";
    QDir().mkpath(dataDir);
    QString filePath = dataDir + "/" + fileName;

    // 避免旧文件残留导致后续误读
    QFile::remove(filePath);

    // 6. 执行写入操作
    IFSelect_ReturnStatus writeStatus = stepWriter.Write(filePath.toLocal8Bit().constData());

    if (writeStatus != IFSelect_RetDone)
    {
        return;
    }
}

bool PreProcessing::writeJsonFile(const QString& filePath, const QJsonObject& obj)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QJsonDocument doc(obj);
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool PreProcessing::readJsonFile(const QString& filePath, QJsonObject& out)
{
    QFile f(filePath);
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}

void PreProcessing::initRibbonActionConn()
{
    if (!m_uiFrameService)
        return;

    auto& em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId, this,
                           [this, fn](const QVariantMap&)
                           {
                               fn();
                           });
    };

    // --- Geometry ---
    reg(PublicConstants::kActionPreprocessImportGeometry,
        [this]
        {
            newGeometryImportWidget();
        });
    reg(PublicConstants::kActionPreprocessExportGeometry,
        [this]
        {
            exportGeom();
        });

    reg(PublicConstants::kActionPreprocessBox,
        [this]
        {
            newGeometryBoxWidget();
        }); // 矩形
    reg(PublicConstants::kActionPreprocessSphere,
        [this]
        {
            newGeometrySphereWidget();
        }); // 球
    reg(PublicConstants::kActionPreprocessCone,
        [this]
        {
            newGeometryConeWidget();
        }); // 圆锥体
    reg(PublicConstants::kActionPreprocessCylinder,
        [this]
        {
            newGeometryCylinderWidget();
        }); // 圆柱体
    reg(PublicConstants::kActionPreprocessTorus,
        [this]
        {
            newGeometryTorusWidget();
        }); // 圆环

    reg(PublicConstants::kActionGeomCreatePlanarCircle,
        [this]
        {
            newGeometryCircleWidget();
        }); // 圆形
    reg(PublicConstants::kActionGeomCreateEllipse,
        [this]
        {
            newGeometryEllipseWidget();
        }); // 椭圆形
    reg(PublicConstants::kActionGeomCreatePlanarPolygon,
        [this]
        {
            newGeometryPolygonWidget();
        }); // 多边形
    reg(PublicConstants::kActionGeomCreatePlanarRect,
        [this]
        {
            newGeometryRectangleWidget();
        }); // 矩形

    reg(PublicConstants::kActionGeomFuse,
        [this]
        {
            newOccBooleanFuseWidget();
        }); // fuse
    reg(PublicConstants::kActionGeomUnion,
        [this]
        {
            newOccBooleanFuseWidget(QObject::tr("联合体"));
        }); // union keep domains
    reg(PublicConstants::kActionGeomCut,
        [this]
        {
            newOccBooleanCutWidget();
        }); // cut
    reg(PublicConstants::kActionGeomCommon,
        [this]
        {
            newOccBooleanCommonWidget();
        }); // common
}

BasePage* PreProcessing::newGeometryImportWidget(QString title)
{
    if (!m_geomNode)
        return nullptr;
    auto* w = new GeometryImportWidget();
    const QString uuid = w->getUuid();
    auto* node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    w->setDisplayName(title);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "ImportGeometry");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
    return nullptr;
}

bool PreProcessing::exportGeom()
{
    // 更稳：直接从 GeomManager 导出（避免你本地 map 和 manager 生命周期不一致）
    QString filePath =
        QFileDialog::getSaveFileName(nullptr, tr("导出几何"), QDir::homePath(),
                                     tr("Geometry Files (*.step *.stp *.iges *.igs *.brep)"));
    if (filePath.isEmpty())
        return false;

    QStringList validExtensions = {".step", ".stp", ".iges", ".igs", ".brep"};
    QString extension;
    for (const QString& ext : validExtensions)
    {
        if (filePath.endsWith(ext, Qt::CaseInsensitive))
        {
            extension = ext.toLower();
            break;
        }
    }
    if (extension.isEmpty())
    {
        filePath = filePath.section('.', 0, 0) + ".step";
        extension = ".step";
    }

    bool success = false;

    // 你当前工程里 step 多形状导出已经写了这个函数（更可靠）
    if (extension == ".step" || extension == ".stp")
    {
        success = writeMultipleShapesStepFile(filePath);
    }
    else
    {
        // 其他格式你原来走 GemoRWUtilInstance，这里先保留“失败提示”，你后续再补全
        success = false;
    }

    // if (m_outputService)
    // {
    //     m_outputService->outputLog(success ?
    //     QString("几何导出成功!【%1】").arg(filePath)
    //                                        :
    //                                        QString("几何导出失败!【%1】").arg(filePath));
    // }
    return success;
}

bool PreProcessing::writeMultipleShapesStepFile(const QString& filePath)
{
    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    QHash<QString, GeomShapeObject*> shapes = mgr->getShapMap();
    for (auto it = shapes.begin(); it != shapes.end(); ++it)
    {
        GeomShapeObject* obj = it.value();
        qDebug() << it.key();
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;
        builder.Add(compound, *obj->getShape());
    }

    STEPControl_Writer writer;
    IFSelect_ReturnStatus status = writer.Transfer(compound, STEPControl_AsIs);
    if (status != IFSelect_RetDone)
        return false;

    qDebug() << filePath;
    status = writer.Write(filePath.toUtf8().constData());
    return (status == IFSelect_RetDone);
}

void PreProcessing::newGeometryBoxWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new BoxDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Box");
    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Box");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometrySphereWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new SphereDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Sphere");
    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Sphere");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometryConeWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new ConeDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Cone");
    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Cone");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometryTorusWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new TorusDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Torus");
    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Torus");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometryCylinderWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new CylinderDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Cylinder");
    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Cylinder");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

BasePage* PreProcessing::newGeometryCircleWidget(QString title)
{
    if (!m_geomNode)
        return nullptr;

    auto* w = new CircleDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Circle");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Circle");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

BasePage* PreProcessing::newGeometryEllipseWidget(QString title)
{
    if (!m_geomNode)
        return nullptr;

    auto* w = new EllipseDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Ellipse");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Ellipse");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

BasePage* PreProcessing::newGeometryPolygonWidget(QString title)
{
    if (!m_geomNode)
        return nullptr;

    auto* w = new PolygonDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Polygon");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Polygon");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

BasePage* PreProcessing::newGeometryRectangleWidget(QString title)
{
    if (!m_geomNode)
        return nullptr;

    auto* w = new RectangleDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Rectangle");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "Rectangle");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

void PreProcessing::newOccBooleanFuseWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new OccBooleanFuseWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanFuse");
    auto* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "BooleanFuse");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newOccBooleanCutWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new OccBooleanCutWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanCut");
    auto* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "BooleanCut");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newOccBooleanCommonWidget(QString title)
{
    if (!m_geomNode)
        return;
    auto* w = new OccBooleanCommonWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanCommon");
    auto* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, "BooleanCommon");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

QString PreProcessing::makeUniqueName(const QString& baseName, const QString& typeKey)
{
    QString key = typeKey.isEmpty() ? baseName : typeKey;
    int count = m_nameCounters.value(key, 0) + 1;
    m_nameCounters.insert(key, count);
    return QString("%1(%2)").arg(baseName).arg(count);
}

namespace
{
bool buildIndexedShapesFromAllGeometry(int dim, TopTools_IndexedMapOfShape& indexed)
{
    indexed.Clear();

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || mgr->isEmpty())
        return false;

    const auto map = mgr->getShapMap();
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    bool hasShape = false;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        GeomShapeObject* obj = it.value();
        if (!obj || !obj->getShape())
            continue;
        builder.Add(compound, *obj->getShape());
        hasShape = true;
    }
    if (!hasShape)
        return false;

    TopAbs_ShapeEnum shapeType = TopAbs_EDGE;
    if (dim == 2)
        shapeType = TopAbs_FACE;
    else if (dim == 3)
        shapeType = TopAbs_SOLID;

    TopExp::MapShapes(compound, shapeType, indexed);
    return indexed.Extent() > 0;
}
} // namespace

QStringList PreProcessing::listEntityIds(int dim)
{
    QStringList out;
    TopTools_IndexedMapOfShape indexed;
    if (!buildIndexedShapesFromAllGeometry(dim, indexed))
        return out;

    for (int i = 1; i <= indexed.Extent(); ++i)
        out << QString::number(i);
    return out;
}

bool PreProcessing::entityCenter(int dim, int id, QVector3D& center)
{
    center = QVector3D();
    if (id <= 0)
        return false;

    TopTools_IndexedMapOfShape indexed;
    if (!buildIndexedShapesFromAllGeometry(dim, indexed))
        return false;

    if (id > indexed.Extent())
        return false;

    const TopoDS_Shape target = indexed(id);
    GProp_GProps props;
    if (dim == 1)
    {
        BRepGProp::LinearProperties(target, props);
    }
    else if (dim == 2)
    {
        BRepGProp::SurfaceProperties(target, props);
    }
    else
    {
        BRepGProp::VolumeProperties(target, props);
    }

    const gp_Pnt c = props.CentreOfMass();
    center = QVector3D(c.X(), c.Y(), c.Z());
    return true;
}

bool PreProcessing::entityMeasure(int dim, int id, double& measure)
{
    measure = 0.0;
    if (id <= 0)
        return false;

    TopTools_IndexedMapOfShape indexed;
    if (!buildIndexedShapesFromAllGeometry(dim, indexed))
        return false;

    if (id > indexed.Extent())
        return false;

    const TopoDS_Shape target = indexed(id);
    GProp_GProps props;
    if (dim == 1)
    {
        BRepGProp::LinearProperties(target, props);
    }
    else if (dim == 2)
    {
        BRepGProp::SurfaceProperties(target, props);
    }
    else
    {
        BRepGProp::VolumeProperties(target, props);
    }

    measure = props.Mass();
    return measure > 0.0;
}

bool PreProcessing::highlightEntity(int dim, int id)
{
    return highlightEntities(dim, QList<int>{id}, false);
}

bool PreProcessing::highlightEntities(int dim, const QList<int>& ids, bool hoverPreview)
{
    if (!hoverPreview)
        clearEntityHighlight();
    else
        clearEntityHoverHighlight();

    QList<int> validIds;
    for (int id : ids)
        if (id > 0)
            validIds.push_back(id);
    if (validIds.isEmpty())
        return false;

    TopTools_IndexedMapOfShape indexed;
    if (!buildIndexedShapesFromAllGeometry(dim, indexed))
        return false;

    vtkNew<vtkAppendPolyData> appender;
    bool anyValid = false;
    for (int id : validIds)
    {
        if (id > indexed.Extent())
            continue;
        TopoDS_Shape target = indexed(id);
        IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(target);
        vtkNew<IVtkTools_ShapeDataSource> src;
        src->SetShape(occShape);

        vtkNew<IVtkTools_DisplayModeFilter> dm;
        dm->SetInputConnection(src->GetOutputPort());
        dm->SetDisplayMode(dim == 1 ? DM_Wireframe : DM_Shading);
        dm->SetSmoothShading(true);
        dm->SetFaceBoundaryDraw(true);
        dm->Update();
        appender->AddInputData(dm->GetOutput());
        anyValid = true;
    }
    if (!anyValid)
        return false;
    appender->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(appender->GetOutput());
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    if (hoverPreview)
    {
        actor->GetProperty()->SetColor(1.0, 0.2, 0.2);
        actor->GetProperty()->SetOpacity(1.0);
        actor->GetProperty()->SetLineWidth(5.0);
    }
    else if (dim == 1)
    {
        actor->GetProperty()->SetColor(0.15, 0.95, 0.25);
        actor->GetProperty()->SetLineWidth(4.0);
    }
    else if (dim == 2)
    {
        actor->GetProperty()->SetColor(0.2, 0.95, 0.35);
        actor->GetProperty()->SetOpacity(0.85);
    }
    else
    {
        actor->GetProperty()->SetColor(0.2, 0.8, 1.0);
        actor->GetProperty()->SetOpacity(0.55);
    }

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis)
        return false;
    const QString propId =
        hoverPreview ? "preproc.entity.highlight.hover" : "preproc.entity.highlight";
    vis->addProp(propId, actor, "Geometry", "entity.highlight", false, true);
    return true;
}

void PreProcessing::clearEntityHoverHighlight()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp("preproc.entity.highlight.hover", true);
    }
}

void PreProcessing::clearEntityHighlight()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp("preproc.entity.highlight", true);
        vis->removeProp("preproc.entity.highlight.hover", true);
    }
}
