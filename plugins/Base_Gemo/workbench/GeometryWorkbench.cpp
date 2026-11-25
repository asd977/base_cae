#include "GeometryWorkbench.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <GeometryPickManager.h>
#include "GeometryEntityTools.h"
#include "GeometrySelectionVisuals.h"
#include "GeometryViewportPickController.h"
#include "CadDocument.h"
#include <GeometryDisplayManager.h>
#include <GeometryDisplayStyle.h>
#include <OccToVtkConverter.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/PhysicalFieldService.h>
#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkSmartPointer.h>
#include <vtkProp.h>
#include <vtkRenderer.h>

#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <IGESControl_Writer.hxx>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QModelIndex>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QToolButton>
#include <QUuid>
#include <QVector>
#include <QVector3D>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <SketchPanel.h>
#include "../terrain/TerrainSurfaceBuilder.h"
#include <SurfaceDomainData.h>
#include <GeometryProjectStorage.h>
#include <MeshWorkingPaths.h>
#include <QDateTime>
#include <PartitionFaceOrder.h>

#include "GeometryBuildUtils.h"
#include "FeatureBuilder.h"
#include "ModifyBuilder.h"
#include "GeometryFeatureRegistry.h"
#include "FeatureData.h"
#include <BasePage.h>
#include <GeometryServiceAccess.h>
#include <ExtrudePanel.h>
#include <PatternPanel.h>
#include <GeometryInspectionDialogs.h>
#include <GeometryPanelUi.h>
#include <FeatureOperationPanels.h>
#include <ImportExportPanel.h>
#include <ImportGeometryBuilder.h>
#include "GeometryCleanup.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
constexpr int kRoleTargetUuid = ProjectNode::RoleDimmed + 1;
constexpr int kRoleGeometryVisible = kRoleTargetUuid + 1;
constexpr int kRoleGeometryConsumed = kRoleTargetUuid + 2;
constexpr int kRoleGeometryActiveResult = kRoleTargetUuid + 3;
constexpr int kRoleGeometryInputUuids = kRoleTargetUuid + 4;
constexpr int kRoleGeometryDerivedCount = kRoleTargetUuid + 5;
constexpr int kRoleGeometryMissingInputCount = kRoleTargetUuid + 6;
constexpr int kRoleGeometryConsumedInputUuids = kRoleTargetUuid + 7;
constexpr int kRoleGeometryRestoreError = kRoleTargetUuid + 8;
constexpr int kRoleStepImportUuid = kRoleTargetUuid + 9;
constexpr int kRoleStepAssemblyPath = kRoleTargetUuid + 10;

using PartitionFaceView = GeometryPartitionTopology::Face;
QVector<PartitionFaceView> sortedPartitionFaces(const TopoDS_Shape& region)
{
    return GeometryPartitionTopology::orderedFaces(region);
}

struct PartitionEdgeView
{
    TopoDS_Shape shape;
    double cx = 0.0, cy = 0.0, cz = 0.0;
    double length = 0.0;
    double xmin = 0.0, ymin = 0.0, zmin = 0.0;
    double xmax = 0.0, ymax = 0.0, zmax = 0.0;
};

QVector<PartitionEdgeView> sortedPartitionEdges(const TopoDS_Shape& region)
{
    QVector<PartitionEdgeView> edges;
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(region, TopAbs_EDGE, edgeMap);
    edges.reserve(edgeMap.Extent());
    for (int i = 1; i <= edgeMap.Extent(); ++i)
    {
        PartitionEdgeView item;
        item.shape = edgeMap(i);
        GProp_GProps props;
        BRepGProp::LinearProperties(TopoDS::Edge(item.shape), props);
        const gp_Pnt c = props.CentreOfMass();
        item.cx = c.X(); item.cy = c.Y(); item.cz = c.Z();
        item.length = props.Mass();
        Bnd_Box box;
        BRepBndLib::Add(item.shape, box);
        if (!box.IsVoid())
            box.Get(item.xmin, item.ymin, item.zmin, item.xmax, item.ymax, item.zmax);
        edges.push_back(item);
    }
    auto lessWithTol = [](double a, double b) -> int
    {
        const double scale = std::max({1.0, std::abs(a), std::abs(b)});
        const double eps = scale * 1e-10;
        if (a < b - eps) return -1;
        if (a > b + eps) return 1;
        return 0;
    };
    std::sort(edges.begin(), edges.end(), [&](const PartitionEdgeView& a, const PartitionEdgeView& b)
    {
        const double av[] = {a.cx,a.cy,a.cz,a.length,a.xmin,a.ymin,a.zmin,a.xmax,a.ymax,a.zmax};
        const double bv[] = {b.cx,b.cy,b.cz,b.length,b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax};
        for (int i = 0; i < 10; ++i)
        {
            const int c = lessWithTol(av[i], bv[i]);
            if (c < 0) return true;
            if (c > 0) return false;
        }
        return false;
    });
    return edges;
}

vtkSmartPointer<vtkActor> makePartitionRegionPreviewActor(const TopoDS_Shape& shape,
                                                             bool selected)
{
    if (shape.IsNull())
        return nullptr;

    const auto display = OCC::BuildDisplayPolyData(shape, 0.35, 18.0, true, false);
    if (!display.surface || display.surface->GetNumberOfPolys() <= 0)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(display.surface);
    mapper->ScalarVisibilityOff();
    // Region 是临时 OCC 重建面，和原 CAD 外表面大量共面。显式使用
    // slope-aware polygon offset，避免再次出现“橙/灰条纹”式深度争抢。
    mapper->SetResolveCoincidentTopologyToPolygonOffset();
    // 已选面比 Region 上下文再向前偏一点，确保 Source/Target 持续高亮不会被
    // 同一张临时 OCC 表面盖住。
    if (selected)
        mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-2.0, -4.0);
    else
        mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -2.0);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOn();
    auto* prop = actor->GetProperty();
    prop->SetRepresentationToSurface();
    prop->SetInterpolationToFlat();
    prop->LightingOff();
    prop->SetAmbient(1.0);
    prop->SetDiffuse(0.0);
    prop->SetSpecular(0.0);
    // 分块预览必须能看清边界。之前只显示半透明面，复杂模型中块与块的
    // 轮廓很容易融在一起；这里始终显示深色边线，选中块再提高不透明度。
    prop->EdgeVisibilityOn();
    prop->SetEdgeColor(0.12, 0.12, 0.12);
    prop->SetLineWidth(selected ? 2.2 : 1.4);
    if (selected)
    {
        prop->SetColor(1.0, 0.58, 0.12);
        prop->SetOpacity(0.62);
    }
    else
    {
        prop->SetColor(0.26, 0.74, 0.82);
        prop->SetOpacity(0.16);
    }
    return actor;
}

vtkSmartPointer<vtkActor> makePartitionEdgePreviewActor(const TopoDS_Shape& shape, bool selected)
{
    if (shape.IsNull()) return nullptr;
    const auto display = OCC::BuildDisplayPolyData(shape, 0.25, 15.0, false, true);
    if (!display.edges || display.edges->GetNumberOfLines() <= 0) return nullptr;
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(display.edges);
    mapper->ScalarVisibilityOff();
    mapper->SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(-4.0, -8.0);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOn();
    auto* prop = actor->GetProperty();
    prop->LightingOff();
    // Persistent selection must be visible above the ordinary CAD outline without
    // turning a circular edge into an orange pipe.  A medium-width flat stroke is
    // used for selected edges; hover is one step stronger.  Tube rendering stays off.
    prop->SetColor(selected ? 1.0 : 0.18, selected ? 0.46 : 0.72, selected ? 0.04 : 0.95);
    prop->SetLineWidth(selected ? 3.2 : 1.6);
    prop->SetOpacity(selected ? 1.0 : 0.72);
    prop->SetRenderLinesAsTubes(0);
    return actor;
}

vtkSmartPointer<vtkActor> makePartitionCutPlanePreviewActor(const QVariantMap& plane,
                                                              const TopoDS_Shape& targetShape)
{
    QVector3D origin = plane.value(QStringLiteral("origin")).value<QVector3D>();
    QVector3D normal = plane.value(QStringLiteral("normal")).value<QVector3D>();
    if (normal.lengthSquared() <= 1.0e-16f)
        return nullptr;
    normal.normalize();

    QVector3D center = origin;
    double diagonal = 10.0;
    if (!targetShape.IsNull())
    {
        Bnd_Box box;
        BRepBndLib::Add(targetShape, box);
        if (!box.IsVoid())
        {
            double xmin=0.0,ymin=0.0,zmin=0.0,xmax=0.0,ymax=0.0,zmax=0.0;
            box.Get(xmin,ymin,zmin,xmax,ymax,zmax);
            center = QVector3D(float((xmin+xmax)*0.5), float((ymin+ymax)*0.5), float((zmin+zmax)*0.5));
            const double dx=xmax-xmin, dy=ymax-ymin, dz=zmax-zmin;
            diagonal = std::max(1.0e-6, std::sqrt(dx*dx+dy*dy+dz*dz));
        }
    }

    // planeOrigin for X/Y/Z rules intentionally stores only the constrained
    // coordinate. Project the target-solid centre onto that plane so the visible
    // cutter sits over the actual body instead of around world origin.
    const float signedDistance = QVector3D::dotProduct(center - origin, normal);
    const QVector3D planeCenter = center - normal * signedDistance;
    const QVector3D helper = std::abs(normal.z()) < 0.85f
        ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D u = QVector3D::crossProduct(normal, helper);
    if (u.lengthSquared() <= 1.0e-16f)
        u = QVector3D::crossProduct(normal, QVector3D(1.0f, 0.0f, 0.0f));
    u.normalize();
    QVector3D v = QVector3D::crossProduct(normal, u);
    v.normalize();
    const float half = float(std::max(1.0, diagonal * 0.72));

    const QVector3D corners[4] = {
        planeCenter - u*half - v*half,
        planeCenter + u*half - v*half,
        planeCenter + u*half + v*half,
        planeCenter - u*half + v*half
    };
    auto points = vtkSmartPointer<vtkPoints>::New();
    for (const QVector3D& c : corners) points->InsertNextPoint(c.x(), c.y(), c.z());
    auto polys = vtkSmartPointer<vtkCellArray>::New();
    polys->InsertNextCell(4);
    for (vtkIdType i=0; i<4; ++i) polys->InsertCellPoint(i);
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);
    poly->SetPolys(polys);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(poly);
    mapper->ScalarVisibilityOff();
    mapper->SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-3.0, -6.0);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    auto* prop = actor->GetProperty();
    prop->LightingOff();
    prop->SetColor(1.0, 0.82, 0.10);
    prop->SetOpacity(0.34);
    prop->EdgeVisibilityOn();
    prop->SetEdgeColor(1.0, 0.48, 0.02);
    prop->SetLineWidth(3.0);
    return actor;
}

void clearGeometryNodeDecoration(QTreeWidgetItem* item);

void removeGeometryContainerCheckBox(ProjectNode* node)
{
    if (!node)
        return;
    node->setFlags(node->flags() &
                   ~(Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate | Qt::ItemIsUserTristate));
    node->setData(0, Qt::CheckStateRole, QVariant());
}

void reparentProjectNode(ProjectNode* node, ProjectNode* newParent)
{
    if (!node || !newParent || node == newParent)
        return;

    auto* oldParent = static_cast<ProjectNode*>(node->parent());
    if (oldParent == newParent)
        return;
    if (oldParent)
        oldParent->removeChild(node);
    newParent->addChild(node);
}

ProjectNode* createStepAssemblyGroup(ProjectNode* parent,
                                     const QString& title,
                                     const QString& importUuid,
                                     const QString& assemblyPath)
{
    if (!parent)
        return nullptr;

    auto* group = new ProjectNode(title, QString(), nullptr);
    clearGeometryNodeDecoration(group);
    group->setData(0, ProjectNode::RoleModule, QStringLiteral("Geometry"));
    group->setData(0, ProjectNode::RoleKind, QStringLiteral("StepAssemblyGroup"));
    group->setData(0, kRoleStepImportUuid, importUuid);
    group->setData(0, kRoleStepAssemblyPath, assemblyPath);
    group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    group->setToolTip(0, assemblyPath.isEmpty() ? title : assemblyPath);
    parent->addChild(group);
    group->setExpanded(false);
    return group;
}

void clearStepAssemblyGroups(ProjectNode* geometryRoot,
                             const QString& importUuid,
                             const QList<ProjectNode*>& partNodes)
{
    if (!geometryRoot || importUuid.isEmpty())
        return;

    // 先把零件节点移回几何根节点，再删除旧分组；否则删除分组时会连同零件一起删除。
    for (ProjectNode* partNode : partNodes)
        reparentProjectNode(partNode, geometryRoot);

    for (int i = geometryRoot->childCount() - 1; i >= 0; --i)
    {
        auto* child = static_cast<ProjectNode*>(geometryRoot->child(i));
        if (!child ||
            child->data(0, ProjectNode::RoleKind).toString() !=
                QStringLiteral("StepAssemblyGroup") ||
            child->data(0, kRoleStepImportUuid).toString() != importUuid)
        {
            continue;
        }
        geometryRoot->removeChild(child);
        delete child;
    }
}

QStringList stepAssemblyGroupSegments(const QString& assemblyPath)
{
    QStringList segments =
        assemblyPath.split(QStringLiteral(" / "), Qt::SkipEmptyParts);
    for (QString& segment : segments)
        segment = segment.simplified();
    segments.removeAll(QString());
    // assemblyPath 最后一段是零件自身名称，树分组只使用其父装配路径。
    if (!segments.isEmpty())
        segments.removeLast();
    return segments;
}

void moveStepPartIntoAssemblyTree(ProjectNode* partNode,
                                  ProjectNode* rootGroup,
                                  const QString& importUuid,
                                  const QString& assemblyPath,
                                  QHash<QString, ProjectNode*>& groupsByPath)
{
    if (!partNode || !rootGroup)
        return;

    ProjectNode* parent = rootGroup;
    QString cumulativePath;
    const QStringList segments = stepAssemblyGroupSegments(assemblyPath);
    for (const QString& segment : segments)
    {
        cumulativePath = cumulativePath.isEmpty()
            ? segment
            : cumulativePath + QStringLiteral(" / ") + segment;
        ProjectNode* group = groupsByPath.value(cumulativePath, nullptr);
        if (!group)
        {
            group = createStepAssemblyGroup(parent, segment, importUuid, cumulativePath);
            if (!group)
                break;
            groupsByPath.insert(cumulativePath, group);
        }
        parent = group;
    }
    reparentProjectNode(partNode, parent);
}

void initializeGeometryNodeVisibility(ProjectNode* node, bool visible)
{
    if (!node)
        return;

    node->setData(0, kRoleGeometryVisible, visible);
    node->setData(0, ProjectNode::RoleDimmed, !visible);
    node->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
}

bool runtimeGeometryVisibilityMatches(const QString& uuid, bool visible)
{
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = manager ? manager->getGeomShapeObject(uuid) : nullptr;
    if (!object)
        return true;

    // A visible logical node without a VTK presentation is not synchronized.
    // Hidden nodes may deliberately have no actors until their first show.
    if (visible && !object->hasActors())
        return false;

    const int expectedVisibility = visible ? 1 : 0;

    if (vtkActor* surfaceActor = object->getSurfActor())
    {
        if (surfaceActor->GetVisibility() != expectedVisibility)
            return false;
    }

    // Entity edge actors may intentionally be hidden while their topology is
    // represented by the shared edge batch. Their raw visibility is therefore
    // not an authoritative object-visibility signal. Pure wire/sketch objects
    // have no surface, so their edge actor remains authoritative.
    if (!object->getSurfActor() || !visible)
    {
        if (vtkActor* edgeActor = object->getEdgeActor())
        {
            if (edgeActor->GetVisibility() != expectedVisibility)
                return false;
        }
    }

    return true;
}

class ScopeExit
{
public:
    explicit ScopeExit(std::function<void()> callback)
        : m_callback(std::move(callback))
    {
    }

    ~ScopeExit()
    {
        if (m_callback)
            m_callback();
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    std::function<void()> m_callback;
};

QString stableNodeName(ProjectNode* node)
{
    if (!node)
        return QString();

    const QString storedName = node->getName().trimmed();
    if (!storedName.isEmpty())
        return storedName;

    return node->text(0).trimmed();
}

bool isGenericLoadedTitle(const QString& title)
{
    return title.isEmpty() ||
           title == QObject::tr("几何") ||
           title == QObject::tr("二维草图") ||
           title == QObject::tr("草图");
}

int workPlaneNumber(const QString& title)
{
    static const QRegularExpression re(QStringLiteral("^\\s*工作平面\\s*(\\d+)\\s*$"));
    const QRegularExpressionMatch match = re.match(title);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

bool hasRuntimeShape(const QString& uuid)
{
    if (uuid.isEmpty())
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    return object && object->getShape() && !object->getShape()->IsNull();
}

void clearGeometryNodeDecoration(QTreeWidgetItem* item)
{
    if (!item)
        return;

    // 几何模型树不使用节点图标。某些树刷新、节点复用或外部委托会把
    // DecorationRole 残留为灰色圆点，因此在所有创建和刷新路径中统一清空。
    if (item->data(0, Qt::DecorationRole).isValid())
        item->setData(0, Qt::DecorationRole, QVariant());
}

bool readCachedBrep(const QString& brep, TopoDS_Shape& shape)
{
    shape.Nullify();
    if (brep.isEmpty())
        return false;

    try
    {
        BRep_Builder builder;
        std::istringstream iss(brep.toStdString());
        BRepTools::Read(shape, iss, builder);
        return !shape.IsNull();
    }
    catch (const Standard_Failure& failure)
    {
        qWarning() << "Failed to read cached BRep:"
                   << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
    }
    catch (const std::exception& error)
    {
        qWarning() << "Failed to read cached BRep:" << error.what();
    }
    catch (...)
    {
        qWarning() << "Failed to read cached BRep: unknown exception";
    }
    return false;
}

}

GeometryWorkbench::GeometryWorkbench()
{
    ServiceRegistry::instance().registerService<PreProcessingService>(this);
    GeometryPickManager::instance().setActorFactory(GeometrySelectionVisuals::buildPickActor);
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();

    initRibbonActionConn();
    initTreeMenu();
}

GeometryWorkbench::~GeometryWorkbench()
{
    closeProject();
    if (ServiceRegistry::instance().getService<PreProcessingService>() == this)
        ServiceRegistry::instance().registerService<PreProcessingService>(nullptr);
}

void GeometryWorkbench::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node", this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();
            ProjectNode* currentNode = findGeometryNodeByUuid(uuid);

            auto addAction = [this](QMenu* target, const QString& text, auto slot)
            {
                QAction* action = target->addAction(text);
                connect(action, &QAction::triggered, this, slot);
                return action;
            };

            const auto addDisplayActions=[&](const QStringList& ids) {
                if(ids.isEmpty())return;
                auto* modes=menu->addMenu(tr("显示方式"));
                const QStringList labels{tr("实体"),tr("线框"),tr("实体带边线")};
                for(int mode=0;mode<labels.size();++mode)
                    addAction(modes,labels[mode],[ids,mode]{GeometryDisplayManager::setPresentation(ids,mode);});
                auto* alpha=menu->addMenu(tr("透明度"));
                for(int percent:{100,75,50,25})
                    addAction(alpha,percent==100?tr("不透明"):tr("不透明度 %1%").arg(percent),
                        [ids,percent]{GeometryDisplayManager::setPresentation(ids,-1,percent/100.0);});
                addAction(menu,tr("隔离显示"),[ids]{GeometryDisplayManager::isolateObjects(ids);});
                addAction(menu,tr("恢复显示与透明度"),[]{GeometryDisplayManager::restorePresentation();});
                menu->addSeparator();
            };

            if (kind == "GeometryRoot")
            {
                addDisplayActions(activeGeometryUuids());
                addAction(menu, tr("二维草图"), [this]
                {
                    newGeometryWidget("Sketch2D", PublicConstants::kPageGeomSketch2D, tr("二维草图"));
                });
                addAction(menu, tr("等高线地形"), [this]
                {
                    newGeometryWidget("TerrainSurface", PublicConstants::kPageGeomTerrainSurface, tr("等高线地形"));
                });
                addAction(menu, tr("导入几何"), [this]
                {
                    newGeometryWidget("ImportGeometry", PublicConstants::kPagePreprocessImportGeometry, tr("导入几何"));
                });
                menu->addSeparator();

                auto* primitiveMenu = menu->addMenu(tr("基本体"));
                addAction(primitiveMenu, tr("长方体"), [this]
                {
                    newGeometryWidget("Box", PublicConstants::kPagePreprocessBox, tr("长方体"));
                });
                addAction(primitiveMenu, tr("圆柱体"), [this]
                {
                    newGeometryWidget("Cylinder", PublicConstants::kPagePreprocessCylinder, tr("圆柱体"));
                });
                addAction(primitiveMenu, tr("球体"), [this]
                {
                    newGeometryWidget("Sphere", PublicConstants::kPagePreprocessSphere, tr("球体"));
                });
                addAction(primitiveMenu, tr("圆锥体"), [this]
                {
                    newGeometryWidget("Cone", PublicConstants::kPagePreprocessCone, tr("圆锥体"));
                });
                addAction(primitiveMenu, tr("圆环"), [this]
                {
                    newGeometryWidget("Torus", PublicConstants::kPagePreprocessTorus, tr("圆环"));
                });
                auto* templateMenu = menu->addMenu(tr("专用几何"));
                addAction(templateMenu, tr("重力坝"), [this]
                {
                    newGeometryWidget("GravityDam", PublicConstants::kPagePreprocessGravityDam,
                                      tr("重力坝"));
                });
                addAction(templateMenu, tr("土石坝"), [this]
                {
                    newGeometryWidget("EarthRockfillDam",
                                      PublicConstants::kPagePreprocessEarthRockfillDam,
                                      tr("土石坝"));
                });
                addAction(templateMenu, tr("边坡"), [this]
                {
                    newGeometryWidget("SlopeTemplate",
                                      PublicConstants::kPagePreprocessSlopeTemplate,
                                      tr("边坡"));
                });
                addAction(templateMenu, tr("洞室"), [this]
                {
                    newGeometryWidget("CavernTemplate",
                                      PublicConstants::kPagePreprocessCavernTemplate,
                                      tr("洞室"));
                });
                return;
            }

            if (kind == "GeometryInputRef")
            {
                const QString targetUuid =
                    currentNode ? currentNode->data(0, kRoleTargetUuid).toString() : QString();
                if (targetUuid.isEmpty())
                    return;

                addAction(menu, tr("预览来源对象"), [this, targetUuid]
                {
                    setSourcePreviewUuids(QStringList{targetUuid});
                });
                addAction(menu, tr("跳到来源对象"), [this, targetUuid]
                {
                    GeometryDisplayManager::show(targetUuid, false);
                    refreshGeometryNodePresentation();
                    highlightGeometryNode(targetUuid, true);
                    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    {
                        if (ProjectNode* targetNode = pm->findNodeByUuid(targetUuid))
                            pm->setCurrentItem(targetNode);
                    }
                });
                return;
            }

            if (!currentNode)
                return;

            QStringList selectedUuids = currentGeometrySelectionUuids();
            selectedUuids.removeAll(uuid);
            selectedUuids.prepend(uuid);

            QList<ProjectNode*> selectedNodes;
            for (const QString& selectedUuid : selectedUuids)
            {
                ProjectNode* selectedNode = findGeometryNodeByUuid(selectedUuid);
                if (selectedNode && selectedNode->getPage() && hasRuntimeShape(selectedUuid))
                    selectedNodes.push_back(selectedNode);
            }

            addDisplayActions(selectedUuids);
            const bool multiple = selectedNodes.size() >= 2;
            bool allSolidLike = multiple;
            for (ProjectNode* selectedNode : selectedNodes)
            {
                if (isExtrudableProfileNode(selectedNode))
                {
                    allSolidLike = false;
                    break;
                }
            }

            if (multiple && allSolidLike)
            {
                // 保留模型树当前的多选集合。布尔页面会直接读取这些 UUID，
                // 不能在创建页面前调用 setCurrentTreeSelection()，否则会把多选折叠为单选。
                addAction(menu, tr("并集"), [this]
                {
                    newGeometryWidget("BooleanFuse", PublicConstants::kPagePostProcessOccBoolean, tr("并集"));
                });
                addAction(menu, tr("差集"), [this]
                {
                    newGeometryWidget("BooleanCut", PublicConstants::kPagePostProcessOccBoolean, tr("差集"));
                });
                addAction(menu, tr("交集"), [this]
                {
                    newGeometryWidget("BooleanCommon", PublicConstants::kPagePostProcessOccBoolean, tr("交集"));
                });
                menu->addSeparator();
                addAction(menu, tr("删除"), [this, selectedUuids]
                {
                    for (const QString& selectedUuid : selectedUuids)
                    {
                        if (findGeometryNodeByUuid(selectedUuid))
                            removeNode(selectedUuid);
                    }
                });
                return;
            }

            const bool profileNodeForMenu = isExtrudableProfileNode(currentNode);
            if (profileNodeForMenu)
            {
                addAction(menu, tr("拉伸"), [this, uuid] { newExtrudeWidget(uuid); });
                addAction(menu, tr("旋转生成"), [this, uuid] { newRevolveWidget(uuid); });
                addAction(menu, tr("扫掠"), [this, uuid] { newSweepWidget(uuid, QString()); });
                addAction(menu, tr("拉伸切除"), [this, uuid] { newCutExtrudeWidget(uuid, QString()); });
                menu->addSeparator();
                addAction(menu, tr("编辑参数"), [this, uuid] { editNodeParameters(uuid); });
                addAction(menu, tr("适合窗口"), [this, uuid] { fitNode(uuid); });
                addAction(menu, tr("对象信息"), [this, uuid] { showNodeInfo(uuid); });
                if (!cachedGeometryInputUuids(currentNode).isEmpty())
                    addAction(menu, tr("显示来源对象"), [this, uuid] { showGeometryInputs(uuid); });
                menu->addSeparator();
                addAction(menu, tr("复制"), [this, uuid] { duplicateNode(uuid); });
                addAction(menu, tr("重命名"), [this, uuid] { renameNode(uuid); });
                addAction(menu, tr("删除"), [this, uuid] { removeNode(uuid); });
                return;
            }

            addAction(menu, tr("对象变换"), [this, uuid] { newTransformWidget(uuid); });
            addAction(menu, tr("镜像"), [this, uuid] { newMirrorWidget(uuid); });
            addAction(menu, tr("阵列"), [this, uuid] { newPatternWidget(uuid); });
            addAction(menu, tr("分割"), [this, uuid] { newSplitWidget(uuid, QString()); });
            menu->addSeparator();
            addAction(menu, tr("编辑参数"), [this, uuid] { editNodeParameters(uuid); });
            addAction(menu, tr("适合窗口"), [this, uuid] { fitNode(uuid); });
            addAction(menu, tr("对象信息"), [this, uuid] { showNodeInfo(uuid); });
            if (!cachedGeometryInputUuids(currentNode).isEmpty())
                addAction(menu, tr("显示来源对象"), [this, uuid] { showGeometryInputs(uuid); });
            menu->addSeparator();
            addAction(menu, tr("复制"), [this, uuid] { duplicateNode(uuid); });
            addAction(menu, tr("重命名"), [this, uuid] { renameNode(uuid); });
            addAction(menu, tr("删除"), [this, uuid] { removeNode(uuid); });
        },
        100,
        [](const QVariantMap& ctx) -> bool
        {
            const QString kind = ctx.value("kind").toString();
            if (ctx.value("module").toString() != "Geometry" || kind == "GeometryGroup")
                return false;
            // 几何根是创建命令的入口，本身没有对象 UUID。
            return kind == "GeometryRoot" || !ctx.value("uuid").toString().isEmpty();
        });
}

void GeometryWorkbench::toggleVisible(const QString& uuid)
{
    if (uuid.isEmpty())
        return;

    setNodeVisible(uuid, !GeometryDisplayManager::isVisible(uuid));
}

void GeometryWorkbench::setNodeVisible(const QString& uuid, bool visible)
{
    if (uuid.isEmpty())
        return;

    clearFeaturePreviewForGeometryVisibilityChange(uuid);

    // 先提交模型树的逻辑可见状态，再更新 VTK。
    // GeometryDisplayManager::setVisible() 会联动 ProjectManager/树刷新；
    // 如果此时 kRoleGeometryVisible 仍是旧值，itemChanged 会再次进入显隐流程，
    // 形成递归重入并可能导致崩溃。
    if (ProjectNode* node = findGeometryNodeByUuid(uuid))
    {
        m_updatingGeometryTree = true;
        node->setData(0, kRoleGeometryVisible, visible);
        node->setData(0, ProjectNode::RoleDimmed, !visible);
        node->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        node->setToolTip(0, geometryNodeTooltip(node));
        m_updatingGeometryTree = false;
    }

    GeometryDisplayManager::setVisible(uuid, visible, false);

    // 草图编辑器使用独立预览 Actor。模型树复选框改变时必须同时同步编辑层，
    // 否则会出现“未勾选仍有蓝线”或“勾选后普通 Actor 与预览重叠”的状态。
    if (ProjectNode* node = findGeometryNodeByUuid(uuid))
    {
        if (auto* sketch = dynamic_cast<Sketch2DDialog*>(node->getPage()))
            sketch->syncVisibilityFromTree(visible);
    }

    GeometrySelectionVisuals::refreshHighlights(false);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
    commitGeometryHistory();
}

void GeometryWorkbench::fitNode(const QString& uuid)
{
    if (uuid.isEmpty())
        return;

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || !vis->renderer())
        return;

    auto expandBounds = [](vtkProp* prop, double bounds[6], bool& hasBounds)
    {
        if (!prop)
            return;

        double* propBounds = prop->GetBounds();
        if (!propBounds)
            return;
        if (propBounds[0] > propBounds[1] || propBounds[2] > propBounds[3] ||
            propBounds[4] > propBounds[5])
        {
            return;
        }

        if (!hasBounds)
        {
            for (int i = 0; i < 6; ++i)
                bounds[i] = propBounds[i];
            hasBounds = true;
            return;
        }

        bounds[0] = std::min(bounds[0], propBounds[0]);
        bounds[1] = std::max(bounds[1], propBounds[1]);
        bounds[2] = std::min(bounds[2], propBounds[2]);
        bounds[3] = std::max(bounds[3], propBounds[3]);
        bounds[4] = std::min(bounds[4], propBounds[4]);
        bounds[5] = std::max(bounds[5], propBounds[5]);
    };

    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool hasBounds = false;
    expandBounds(vis->getProp(GeometryDisplayManager::facePropId(uuid)), bounds, hasBounds);
    expandBounds(vis->getProp(GeometryDisplayManager::edgePropId(uuid)), bounds, hasBounds);

    if (!hasBounds)
        return;

    GeometryDisplayManager::show(uuid, false);
    refreshGeometryNodePresentation();
    setCurrentTreeSelection(uuid);
    vis->renderer()->ResetCamera(bounds);
    vis->render();
}

void GeometryWorkbench::editNodeParameters(const QString& uuid, bool fit)
{
    if (uuid.isEmpty())
        return;

    ProjectNode* node = findGeometryNodeByUuid(uuid);
    if (!node)
        return;

    const QStringList inputs = cachedGeometryInputUuids(node);
    QStringList missingInputs;
    for (const QString& inputUuid : inputs)
    {
        if (!inputUuid.isEmpty() && !findGeometryNodeByUuid(inputUuid))
            missingInputs.push_back(inputUuid);
    }
    if (!missingInputs.isEmpty())
    {
        const auto ret = QMessageBox::warning(
            nullptr,
            tr("来源已删除"),
            tr("来源缺失。打开参数页后可重新选择来源。\n\n继续编辑？"),
            QMessageBox::Cancel | QMessageBox::Yes,
            QMessageBox::Yes);
        if (ret != QMessageBox::Yes)
            return;
    }

    GeometryDisplayManager::show(uuid, false);
    refreshGeometryNodePresentation();

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        if (ProjectNode* targetNode = pm->findNodeByUuid(uuid))
            pm->setCurrentItem(targetNode);
    }

    setCurrentTreeSelection(uuid);
    if (fit)
        fitNode(uuid);
    else if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

QStringList GeometryWorkbench::directDerivedGeometryUuids(const QString& uuid) const
{
    QStringList result;
    if (uuid.isEmpty() || !m_geomNode)
        return result;

    QList<ProjectNode*> nodes;
    collectGeometryLeafNodes(m_geomNode, nodes);
    for (ProjectNode* node : nodes)
    {
        if (!node)
            continue;

        const QString nodeUuid = geometryNodeUuid(node);
        if (nodeUuid.isEmpty() || nodeUuid == uuid)
            continue;

        const QStringList inputs = cachedGeometryInputUuids(node);
        if (inputs.contains(uuid) && !result.contains(nodeUuid))
            result.push_back(nodeUuid);
    }
    return result;
}

void GeometryWorkbench::previewDerivedGeometry(const QString& uuid)
{
    const QStringList derived = directDerivedGeometryUuids(uuid);
    if (derived.isEmpty())
        return;

    for (const QString& derivedUuid : derived)
        GeometryDisplayManager::show(derivedUuid, false);

    refreshGeometryNodePresentation();
    setSourcePreviewUuids(derived);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

void GeometryWorkbench::editFirstDerivedGeometry(const QString& uuid)
{
    const QStringList derived = directDerivedGeometryUuids(uuid);
    if (derived.isEmpty())
        return;

    editNodeParameters(derived.front(), true);
}

void GeometryWorkbench::duplicateNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode)
        return;

    ProjectNode* sourceNode = findGeometryNodeByUuid(uuid);
    if (!sourceNode || !sourceNode->getPage())
        return;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* sourceObject = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!sourceObject || !sourceObject->getShape() || sourceObject->getShape()->IsNull())
    {
        QMessageBox::information(nullptr,
                                 tr("无法复制"),
                                 tr("对象尚未生成有效几何。"));
        return;
    }

    BasePage* sourcePage = sourceNode->getPage();
    const QString pageType = sourcePage->getPageType();
    const QString kind = sourceNode->data(0, ProjectNode::RoleKind).toString();
    const QString copyKind = GeometryFeatures::normalizeKind(pageType, kind);
    const QString sourceName = sourceNode->getName().isEmpty() ? sourceNode->text(0) : sourceNode->getName();
    const QString copyName = makeUniqueName(tr("%1_副本").arg(sourceName), kind + QStringLiteral("_copy"));

    BasePage* copyPage = GeometryFeatures::createPage(pageType, kind);

    if (!copyPage)
    {
        QMessageBox::information(nullptr,
                                 tr("无法复制"),
                                 tr("该类型暂不支持复制。"));
        return;
    }

    // 复制采用重合快照，不再只平移运行时 BRep。
    // 这样页面参数与实际几何始终一致；需要错开位置时再使用“对象变换”。
    const QJsonObject copiedState = sourcePage->saveJson();
    copyPage->loadJson(copiedState);

    const QString copyUuid = copyPage->getUuid();
    auto* copyNode = new ProjectNode(copyName, copyUuid, copyPage);
    clearGeometryNodeDecoration(copyNode);
    copyPage->setDisplayName(copyName);
    copyNode->setData(0, ProjectNode::RoleUuid, copyUuid);
    copyNode->setData(0, ProjectNode::RoleModule, QStringLiteral("Geometry"));
    copyNode->setData(0, ProjectNode::RoleKind, copyKind.isEmpty() ? kind : copyKind);
    copyNode->setFlags(copyNode->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    initializeGeometryNodeVisibility(copyNode, true);
    copyNode->setData(0, kRoleGeometryConsumed, false);
    copyNode->setData(0, kRoleGeometryActiveResult, false);
    addGeometryNodeToTree(copyNode);

    TopoDS_Shape* copiedShape = new TopoDS_Shape(*sourceObject->getShape());
    auto* copiedObject = new GeomShapeObject(copyUuid, copiedShape, copyName);
    mgr->appendGeomShapeObject(copiedObject);
    GeometryDisplayManager::registerObject(copyUuid, copiedObject, false, false);
    GeometryDisplayManager::show(copyUuid, false);
    updateGeometryNodeDependencyCache(copyNode);

    refreshGeometryNodePresentation();

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(copyNode);
    fitNode(copyUuid);
    commitGeometryHistory(copyUuid);
}

void GeometryWorkbench::removeNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode)
        return;

    ProjectNode* node = findGeometryNodeByUuid(uuid);
    if (!node || node == m_geomNode)
        return;

    QString splitParentUuid;
    int splitPartIndex = -1;
    if (node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("SplitPart"))
    {
        const QJsonObject state = geometryNodeState(node);
        splitParentUuid = state.value(QStringLiteral("splitUuid")).toString();
        splitPartIndex = state.value(QStringLiteral("partIndex")).toInt(-1);
    }

    const QStringList derived = directDerivedGeometryUuids(uuid);
    if (!derived.isEmpty())
    {
        QStringList names;
        for (const QString& derivedUuid : derived)
        {
            if (ProjectNode* derivedNode = findGeometryNodeByUuid(derivedUuid))
                names << (derivedNode->getName().isEmpty() ? derivedNode->text(0) : derivedNode->getName());
        }

        const auto ret = QMessageBox::warning(
            nullptr,
            tr("确认删除"),
            tr("“%1”被以下对象引用：\n\n%2\n\n删除后派生对象会保留，但来源会缺失。继续删除？")
                .arg(node->getName().isEmpty() ? node->text(0) : node->getName(),
                     names.isEmpty() ? tr("未命名派生对象") : names.join(QLatin1Char('\n'))),
            QMessageBox::Cancel | QMessageBox::Yes,
            QMessageBox::Cancel);
        if (ret != QMessageBox::Yes)
            return;
    }

    if (!splitParentUuid.isEmpty() && splitPartIndex >= 0)
    {
        if (ProjectNode* parentNode = findGeometryNodeByUuid(splitParentUuid))
        {
            if (auto* splitPage = dynamic_cast<SplitDialog*>(parentNode->getPage()))
                splitPage->setPartSuppressed(splitPartIndex, true);
        }
    }

    // 删除当前节点前先切到稳定的根节点。这样旧页面会先执行 beforeUnload，
    // currentItemChanged 也不会收到一个正在析构的 previous 指针。
    if (QTreeWidget* tree = m_geomNode->treeWidget())
    {
        if (tree->currentItem() == node)
        {
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(m_geomNode);
        }
    }

    removeGeometryLeafNode(node);
    clearTreeSelectionHighlight();
    // Whole-object source highlighting is a logical state and is visibility-aware.
    // Keep it while hiding/showing objects so the same input is highlighted again
    // when it becomes visible. Only transient sub-shape overlays are cleared here.
    GeometrySelectionVisuals::clearTreePickSelection();
    refreshGeometryNodePresentation();
    commitGeometryHistory();
}


void GeometryWorkbench::renameNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode)
        return;

    ProjectNode* node = findGeometryNodeByUuid(uuid);
    if (!node)
        return;

    const QString currentName = node->getName();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        nullptr, tr("重命名"), tr("新名称："), QLineEdit::Normal, currentName, &ok);
    if (ok && !newName.isEmpty() && newName != currentName)
    {
        node->setText(0, newName);
        node->setName(newName);
        if (BasePage* page = node->getPage())
            page->setDisplayName(newName);
        if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
        {
            if (GeomShapeObject* obj = mgr->getGeomShapeObject(uuid))
                obj->setName(newName);
        }
        refreshGeometryNodePresentation();
        if (hasRuntimeShape(uuid))
            commitGeometryHistory();
    }
}


void GeometryWorkbench::showNodeInfo(const QString& uuid)
{
    ProjectNode* node = findGeometryNodeByUuid(uuid);
    if (!node)
        return;

    FeatureData feature;
    feature.uuid = uuid;
    feature.title = node->getName().isEmpty() ? node->text(0) : node->getName();
    feature.kind = node->data(0, ProjectNode::RoleKind).toString();
    feature.module = node->data(0, ProjectNode::RoleModule).toString();
    feature.visible = GeometryDisplayManager::isVisible(uuid);
    if (BasePage* page = node->getPage())
    {
        feature.type = page->getPageType();
        feature.state = page->saveJson();
    }
    feature.inputUuids = FeatureData::collectInputUuids(feature.state, uuid);

    const QString typeText = FeatureData::typeLabel(feature.type, feature.kind);
    const QStringList derived = directDerivedGeometryUuids(uuid);

    QStringList inputNames;
    for (const QString& inputUuid : feature.inputUuids)
    {
        if (ProjectNode* inputNode = findGeometryNodeByUuid(inputUuid))
            inputNames << (inputNode->getName().isEmpty() ? inputNode->text(0) : inputNode->getName());
        else
        {
            inputNames << tr("来源已删除");
            feature.missingInputUuids.push_back(inputUuid);
        }
    }

    QStringList derivedNames;
    for (const QString& derivedUuid : derived)
    {
        if (ProjectNode* derivedNode = findGeometryNodeByUuid(derivedUuid))
            derivedNames << (derivedNode->getName().isEmpty() ? derivedNode->text(0) : derivedNode->getName());
    }

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    const bool hasShape = object && object->getShape() && !object->getShape()->IsNull();
    // Runtime shape is sufficient here. Serializing a complex BRep just to open
    // the information dialog caused a noticeable pause and allocated a large string.
    const bool hasCachedBrep = hasShape;

    QString text;
    text += tr("名称：%1\n").arg(feature.title);
    text += tr("类型：%1\n").arg(typeText.isEmpty() ? feature.kind : typeText);
    text += tr("显示：%1\n").arg(feature.visible ? tr("显示") : tr("隐藏"));
    text += tr("几何：%1\n").arg(hasShape ? tr("已生成") : tr("未生成"));
    text += tr("来源：%1\n").arg(inputNames.isEmpty() ? tr("无") : inputNames.join(QStringLiteral(", ")));
    text += tr("派生：%1\n").arg(derivedNames.isEmpty() ? tr("无") : derivedNames.join(QStringLiteral(", ")));
    text += tr("参数：%1\n").arg(FeatureData::parameterSummary(feature.type, feature.kind, feature.state));
    text += tr("编辑状态：%1").arg(FeatureData::editabilityText(feature.type, feature.kind,
                                                        feature.inputUuids, feature.missingInputUuids,
                                                        hasShape, hasCachedBrep));

    QMessageBox::information(nullptr, tr("对象信息"), text);
}

BasePage* GeometryWorkbench::createPageByType(const QString& type, const QString& title)
{
    Q_UNUSED(title);
    BasePage* page = GeometryFeatures::createPage(type);
    if (page)
        GeometryPanelUi::apply(page);
    return page;
}

void GeometryWorkbench::newProject()
{
    closeProject();

    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!projectManagerService)
        return;

    m_geomNode = new ProjectNode(tr("几何"));
    m_geomNode->setData(0, ProjectNode::RoleModule, "Geometry");
    m_geomNode->setData(0, ProjectNode::RoleKind, "GeometryRoot");
    removeGeometryContainerCheckBox(m_geomNode);
    projectManagerService->addTreeItem(m_geomNode);
    installGeometryTreeHooks();
    resetGeometryHistory();
}

GeometryWorkbench::GeometryHistorySnapshot GeometryWorkbench::captureGeometryHistorySnapshot(
    const QString& changedUuid, bool preserveCommittedState) const
{
    GeometryHistorySnapshot snapshot;
    snapshot.items = serializeGeometryItems(false, changedUuid, preserveCommittedState);

    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    if (!manager)
        return snapshot;

    for (const QJsonValue& value : snapshot.items)
    {
        const QString uuid = value.toObject().value(QStringLiteral("uuid")).toString();
        GeomShapeObject* object = manager->getGeomShapeObject(uuid);
        if (!object || !object->getShape() || object->getShape()->IsNull())
            continue;
        // TopoDS_Shape 是共享底层 TShape 的轻量句柄。历史记录无需反复写入大型 BREP 字符串，
        // 同时清除当前显示对象后，快照仍能安全持有旧几何。
        snapshot.shapes.insert(uuid, *(object->getShape()));
    }
    return snapshot;
}

void GeometryWorkbench::resetGeometryHistory()
{
    m_geometryUndoHistory.clear();
    m_geometryRedoHistory.clear();
    m_geometryHistoryCurrent = captureGeometryHistorySnapshot();
}

void GeometryWorkbench::commitGeometryHistory(const QString& changedUuid)
{
    if (m_loadingProject || m_restoringGeometryHistory || !m_geomNode)
        return;

    GeometryHistorySnapshot next = captureGeometryHistorySnapshot(changedUuid, true);
    bool sameShapes = next.shapes.size() == m_geometryHistoryCurrent.shapes.size();
    if (sameShapes)
    {
        for (auto it = next.shapes.cbegin(); it != next.shapes.cend(); ++it)
        {
            const auto committed = m_geometryHistoryCurrent.shapes.constFind(it.key());
            if (committed == m_geometryHistoryCurrent.shapes.cend() ||
                !it.value().IsSame(committed.value()))
            {
                sameShapes = false;
                break;
            }
        }
    }
    if (next.items == m_geometryHistoryCurrent.items && sameShapes)
        return;

    m_geometryUndoHistory.push_back(m_geometryHistoryCurrent);
    constexpr int kMaximumGeometryHistory = 20;
    if (m_geometryUndoHistory.size() > kMaximumGeometryHistory)
        m_geometryUndoHistory.remove(0);
    m_geometryRedoHistory.clear();
    m_geometryHistoryCurrent = std::move(next);
}

void GeometryWorkbench::commitSketchHistory(const QString& uuid)
{
    if (!uuid.isEmpty())
        commitGeometryHistory(uuid);
}

bool GeometryWorkbench::restoreGeometryHistorySnapshot(const GeometryHistorySnapshot& snapshot)
{
    if (!m_geomNode)
        return false;

    const GeometryHistorySnapshot rollbackSnapshot =
        m_geometryHistoryCurrent.items.isEmpty() && m_geometryHistoryCurrent.shapes.isEmpty()
            ? captureGeometryHistorySnapshot(QString(), true)
            : m_geometryHistoryCurrent;
    QTreeWidget* treeWidget = m_geomNode->treeWidget();
    const bool oldUpdatesEnabled = treeWidget ? treeWidget->updatesEnabled() : true;
    const bool oldSignalsBlocked = treeWidget ? treeWidget->signalsBlocked() : false;
    const bool oldUpdating = m_updatingGeometryTree;
    const bool oldLoading = m_loadingProject;
    const bool oldRestoring = m_restoringGeometryHistory;
    const bool oldFastDisplay = CadBody::fastDisplayMode();
    const bool oldRenderSuspended = GeometryDisplayManager::isRenderSuspended();

    if (treeWidget)
    {
        treeWidget->blockSignals(true);
        treeWidget->setUpdatesEnabled(false);
        treeWidget->setCurrentItem(m_geomNode);
    }
    m_updatingGeometryTree = true;
    m_loadingProject = true;
    m_restoringGeometryHistory = true;
    CadBody::setFastDisplayMode(true);
    GeometryDisplayManager::setRenderSuspended(true);

    auto applySnapshot = [&](const GeometryHistorySnapshot& candidate) -> bool
    {
        try
        {
            clearGeometryRuntimeState(false);
            while (m_geomNode->childCount() > 0)
            {
                ProjectNode* node = static_cast<ProjectNode*>(m_geomNode->child(0));
                m_geomNode->takeChild(0);
                deleteNodeTree(node);
            }
            m_geometryGroups.clear();
            m_nameCounters.clear();
            m_currentTreeSelectionUuid.clear();
            m_sourcePreviewUuids.clear();

            if (!loadGeometryItems(candidate.items, &candidate.shapes))
                return false;
            synchronizeAllSplitPartNodes();
            synchronizeAllStepImportPartNodes();
            refreshGeometryNodePresentation();

            QList<ProjectNode*> geomNodes;
            collectGeometryLeafNodes(m_geomNode, geomNodes);
            for (ProjectNode* node : geomNodes)
            {
                const QString uuid = geometryNodeUuid(node);
                if (uuid.isEmpty())
                    continue;
                const QVariant visibleData = node->data(0, kRoleGeometryVisible);
                GeometryDisplayManager::setVisible(
                    uuid, visibleData.isValid() ? visibleData.toBool() : true, false);
            }
            clearFeaturePreviewForGeometryVisibilityChange(QString());
            clearTreeSelectionHighlight();
            return true;
        }
        catch (const Standard_Failure& failure)
        {
            qWarning() << "Geometry history restore failed:"
                       << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
        }
        catch (const std::exception& error)
        {
            qWarning() << "Geometry history restore failed:" << error.what();
        }
        catch (...)
        {
            qWarning() << "Geometry history restore failed: unknown exception";
        }
        return false;
    };

    const bool ok = applySnapshot(snapshot);
    bool rollbackOk = true;
    if (!ok)
    {
        qWarning() << "Geometry history target restore failed; rolling back current state.";
        rollbackOk = applySnapshot(rollbackSnapshot);
        if (!rollbackOk)
            qCritical() << "Geometry history rollback also failed.";
    }

    CadBody::setFastDisplayMode(oldFastDisplay);
    m_loadingProject = oldLoading;
    m_updatingGeometryTree = oldUpdating;
    m_restoringGeometryHistory = oldRestoring;
    if (treeWidget)
    {
        treeWidget->blockSignals(oldSignalsBlocked);
        treeWidget->setUpdatesEnabled(oldUpdatesEnabled);
        treeWidget->viewport()->update();
    }
    GeometryDisplayManager::setRenderSuspended(oldRenderSuspended);

    GeometrySelectionVisuals::refreshHighlights(false);
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
        visual->render();
    return ok && rollbackOk;
}

void GeometryWorkbench::undoGeometryChange()
{
    if (m_geometryUndoHistory.isEmpty() || m_restoringGeometryHistory)
        return;

    const GeometryHistorySnapshot current = m_geometryHistoryCurrent;
    const GeometryHistorySnapshot target = m_geometryUndoHistory.takeLast();
    if (!restoreGeometryHistorySnapshot(target))
    {
        m_geometryUndoHistory.push_back(target);
        QMessageBox::warning(nullptr, tr("撤销失败"), tr("几何历史无法恢复。"));
        return;
    }

    m_geometryRedoHistory.push_back(current);
    if (m_geometryRedoHistory.size() > 20)
        m_geometryRedoHistory.remove(0);
    m_geometryHistoryCurrent = target;
}

void GeometryWorkbench::redoGeometryChange()
{
    if (m_geometryRedoHistory.isEmpty() || m_restoringGeometryHistory)
        return;

    const GeometryHistorySnapshot current = m_geometryHistoryCurrent;
    const GeometryHistorySnapshot target = m_geometryRedoHistory.takeLast();
    if (!restoreGeometryHistorySnapshot(target))
    {
        m_geometryRedoHistory.push_back(target);
        QMessageBox::warning(nullptr, tr("重做失败"), tr("几何历史无法恢复。"));
        return;
    }

    m_geometryUndoHistory.push_back(current);
    if (m_geometryUndoHistory.size() > 20)
        m_geometryUndoHistory.remove(0);
    m_geometryHistoryCurrent = target;
}

QJsonArray GeometryWorkbench::serializeGeometryItems(bool includeCachedBrep, const QString& changedUuid,
                                                     bool preserveCommittedState) const
{
    QJsonArray geomItems;
    if (!m_geomNode)
        return geomItems;

    QList<ProjectNode*> geomNodes;
    collectGeometryLeafNodes(m_geomNode, geomNodes);

    QSet<QString> existingUuids;
    existingUuids.reserve(geomNodes.size());
    for (ProjectNode* node : geomNodes)
    {
        const QString uuid = geometryNodeUuid(node);
        if (!uuid.isEmpty())
            existingUuids.insert(uuid);
    }

    // STEP 根导入节点包含完整装配，子节点又分别保存各零件 BRep。两份同时写入
    // 会让工程文件接近翻倍。存在 STEP 子节点时只保存子节点缓存；即使源文件
    // 后续丢失，独立实体仍可从各自 cachedBrep 恢复。
    QSet<QString> stepImportsWithParts;
    for (ProjectNode* node : geomNodes)
    {
        if (!node ||
            node->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("StepImportPart"))
        {
            continue;
        }
        const QStringList inputs = cachedGeometryInputUuids(node);
        if (!inputs.isEmpty())
            stepImportsWithParts.insert(inputs.front());
    }

    // Visibility/rename/delete history changes only metadata. Reuse committed
    // page state before serialization, especially hundreds of thousands of DXF points.
    QHash<QString, FeatureData> committedByUuid;
    if (preserveCommittedState)
        for (const auto& value : m_geometryHistoryCurrent.items)
        {
            const auto feature = FeatureData::fromJson(value.toObject());
            committedByUuid.insert(feature.uuid, feature);
        }

    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    for (ProjectNode* node : geomNodes)
    {
        if (!node)
            continue;
        BasePage* page = node->getPage();
        if (!page)
            continue;

        FeatureData feature;
        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        feature.uuid = nodeUuid;
        feature.title = stableNodeName(node);
        feature.type = page->getPageType();
        feature.module = node->data(0, ProjectNode::RoleModule).toString();
        feature.kind = node->data(0, ProjectNode::RoleKind).toString();
        // 工程文件保存模型树的逻辑显隐，而不是编辑/预览期间 Actor 的临时状态。
        // 草图编辑时普通 Actor 会暂时隐藏，因此直接读取运行时可见性会把
        // “已勾选”错误保存成隐藏，重开工程后就会出现复选框与显示不一致。
        const QVariant storedVisible = node->data(0, kRoleGeometryVisible);
        feature.visible = storedVisible.isValid()
                              ? storedVisible.toBool()
                              : GeometryDisplayManager::isVisible(nodeUuid);
        const auto committed = committedByUuid.constFind(nodeUuid);
        if (nodeUuid != changedUuid && committed != committedByUuid.cend())
        {
            feature.state = committed->state;
            feature.inputUuids = committed->inputUuids;
        }
        else
        {
            feature.state = page->saveJsonForPersistence();
            feature.inputUuids = FeatureData::collectInputUuids(feature.state, feature.uuid);
        }
        for (const QString& inputUuid : feature.inputUuids)
            if (!existingUuids.contains(inputUuid))
                feature.missingInputUuids.push_back(inputUuid);

        const bool skipDuplicatedStepRootCache =
            feature.kind == QStringLiteral("ImportGeometry") &&
            stepImportsWithParts.contains(nodeUuid);
        if (includeCachedBrep && manager && !skipDuplicatedStepRootCache)
        {
            if (GeomShapeObject* object = manager->getGeomShapeObject(nodeUuid))
            {
                TopoDS_Shape* shape = object->getShape();
                if (shape && !shape->IsNull())
                {
                    const auto saved = m_savedBrepAssets.constFind(nodeUuid);
                    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
                    const QFileInfo asset(saved != m_savedBrepAssets.cend()
                        ? QDir(projectDir).filePath(saved->relativePath) : QString());
                    if (saved != m_savedBrepAssets.cend() && shape->IsEqual(saved->shape)
                        && GeometryProjectStorage::safePath(saved->relativePath)
                        && asset.isFile() && !asset.isSymLink() && asset.size() == saved->bytes
                        && asset.lastModified().toMSecsSinceEpoch() == saved->modifiedMsecs) {
                        feature.cachedBrepAsset = saved->relativePath;
                        feature.cachedBrep.clear();
                    } else {
                        std::ostringstream stream;
                        BRepTools::Write(*shape, stream);
                        feature.cachedBrep = QString::fromStdString(stream.str());
                        feature.cachedBrepAsset.clear();
                    }
                }
                feature.faceGroups = object->faceGroups();
            }
        }
        geomItems.append(feature.toJson());
    }
    return geomItems;
}

bool GeometryWorkbench::saveProject()
{
    if (!m_geomNode)
        return true;
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPreProcessingProjectFileName);

    QJsonObject root;
    root["module"] = "PreProcessing";
    root["root_title"] = "几何";

    root["featureDataSchema"] = QStringLiteral("GeometryFeatureData.v2");
    QJsonArray items = serializeGeometryItems(true);
    if (!GeometryProjectStorage::externalize(projectDir, items)) {
        qWarning() << "Cannot persist geometry assets; previous project JSON retained";
        return false;
    }
    root["geom_items"] = items;
    root["presentation"] = GeometryDisplayManager::presentationJson();
    if (!writeJsonFile(filePath, root)) return false;
    QHash<QString, SavedBrepAsset> savedAssets;
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    for (const auto& value : items) {
        const auto item = value.toObject();
        const QString uuid = item.value("uuid").toString();
        const QString relative = item.value("cachedBrepAsset").toString();
        auto* object = manager ? manager->getGeomShapeObject(uuid) : nullptr;
        if (!object || !object->getShape() || !GeometryProjectStorage::safePath(relative)) continue;
        const QFileInfo asset(QDir(projectDir).filePath(relative));
        savedAssets[uuid] = {*object->getShape(), relative, asset.size(), asset.lastModified().toMSecsSinceEpoch()};
    }
    m_savedBrepAssets = std::move(savedAssets);
    return true;
}

bool GeometryWorkbench::loadProject()
{
    closeProject();

    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    if (!projectManagerService)
        return false;

    m_geomNode = new ProjectNode(tr("几何"));
    m_geomNode->setData(0, ProjectNode::RoleModule, "Geometry");
    m_geomNode->setData(0, ProjectNode::RoleKind, "GeometryRoot");
    removeGeometryContainerCheckBox(m_geomNode);
    projectManagerService->addTreeItem(m_geomNode);
    installGeometryTreeHooks();

    // 工程打开是批量恢复过程：暂停树刷新和 VTK Render，避免每个节点都触发一次重绘。
    // ScopeExit 保证即使 OCCT/VTK/页面恢复中抛出异常，也不会留下“树信号关闭”
    // 或“渲染永久暂停”的全局状态。
    QTreeWidget* treeWidget = m_geomNode ? m_geomNode->treeWidget() : nullptr;
    const bool oldUpdatesEnabled = treeWidget ? treeWidget->updatesEnabled() : true;
    const bool oldSignalsBlocked = treeWidget ? treeWidget->signalsBlocked() : false;
    const bool oldUpdating = m_updatingGeometryTree;
    const bool oldLoading = m_loadingProject;
    const bool oldFastDisplay = CadBody::fastDisplayMode();
    const bool oldRenderSuspended = GeometryDisplayManager::isRenderSuspended();

    if (treeWidget)
    {
        treeWidget->blockSignals(true);
        treeWidget->setUpdatesEnabled(false);
    }
    m_updatingGeometryTree = true;
    m_loadingProject = true;
    CadBody::setFastDisplayMode(true);
    GeometryDisplayManager::setRenderSuspended(true);

    bool loadTransactionActive = true;
    auto restoreLoadTransaction = [&]
    {
        if (!loadTransactionActive)
            return;
        loadTransactionActive = false;
        CadBody::setFastDisplayMode(oldFastDisplay);
        m_loadingProject = oldLoading;
        m_updatingGeometryTree = oldUpdating;
        if (treeWidget)
        {
            treeWidget->blockSignals(oldSignalsBlocked);
            treeWidget->setUpdatesEnabled(oldUpdatesEnabled);
        }
        GeometryDisplayManager::setRenderSuspended(oldRenderSuspended);
    };
    ScopeExit restoreState(restoreLoadTransaction);

    bool ok = false;
    try
    {
        ok = loadChildProject(m_geomNode, "geom_items");
        synchronizeAllSplitPartNodes();
        synchronizeAllStepImportPartNodes();

        refreshGeometryNodePresentation();
        QList<ProjectNode*> geomNodes;
        collectGeometryLeafNodes(m_geomNode, geomNodes);
        for (ProjectNode* node : geomNodes)
        {
            const QString uuid = geometryNodeUuid(node);
            if (uuid.isEmpty())
                continue;
            const QVariant visibleData = node->data(0, kRoleGeometryVisible);
            GeometryDisplayManager::setVisible(
                uuid, visibleData.isValid() ? visibleData.toBool() : true, false);
        }

        clearFeaturePreviewForGeometryVisibilityChange(QString());
        clearTreeSelectionHighlight();
    }
    catch (const Standard_Failure& failure)
    {
        qWarning() << "Geometry project load failed:"
                   << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
    }
    catch (const std::exception& error)
    {
        qWarning() << "Geometry project load failed:" << error.what();
    }
    catch (...)
    {
        qWarning() << "Geometry project load failed: unknown exception";
    }

    if (ok)
        resetGeometryHistory();

    // Commit the display transaction only after every Shape, Actor and visibility
    // state has been restored. Project scenes own their camera framing; keeping
    // the empty/previous project's camera can leave valid actors outside the view.
    restoreLoadTransaction();
    if (treeWidget)
        treeWidget->viewport()->update();
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        visual->showVisualizationView();
        visual->setGroupVisible(QStringLiteral("Geometry"), true, false);
        if (ok)
            visual->resetCamera(false);
        visual->render();
    }
    return ok;
}

bool GeometryWorkbench::loadChildProject(ProjectNode* rootNode, const QString& name)
{
    Q_UNUSED(rootNode);
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPreProcessingProjectFileName);
    QJsonObject root;
    if (!QFile::exists(filePath))
        return true;
    if (!readJsonFile(filePath, root))
    {
        QMessageBox::warning(nullptr, tr("打开工程"), tr("几何工程文件无法读取或内容已损坏。"));
        return false;
    }
    GeometryDisplayManager::loadPresentationJson(root.value("presentation").toObject());
    return loadGeometryItems(root.value(name).toArray());
}

bool GeometryWorkbench::loadGeometryItems(
    const QJsonArray& items,
    const QHash<QString, TopoDS_Shape>* shapeSnapshots)
{
    IGeometryService* service = geometryService();
    if (!service)
        return false;

    struct LoadedGeometryItem
    {
        FeatureData feature;
        ProjectNode* node = nullptr;
        bool restored = false;
        QString restoreError;
    };

    QVector<LoadedGeometryItem> loadedItems;
    QHash<QString, int> itemIndexByUuid;

    loadedItems.reserve(items.size());

    // 第一阶段：只创建完整模型树并保留原始参数，不立刻调用页面 loadJson()。
    // 页面加载会刷新来源下拉框；如果此时上游 Shape 还没有恢复，页面会把原始
    // UUID 当成“无效来源”清掉，随后布尔、拉伸和旋转就只能让用户重新点应用。
    for (const auto& v : items)
    {
        const auto obj = v.toObject();
        FeatureData feature = FeatureData::fromJson(obj);
        if (feature.cachedBrep.isEmpty() && !feature.cachedBrepAsset.isEmpty()) {
            QByteArray raw;
            const QString root = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
            if (!GeometryProjectStorage::read(root, feature.cachedBrepAsset, raw)) {
                qWarning() << "Cannot restore geometry asset" << feature.cachedBrepAsset;
                return false;
            }
            feature.cachedBrep = QString::fromUtf8(raw);
        }

        if (feature.uuid.isEmpty())
            feature.uuid = QUuid::createUuid().toString();

        const QString id = feature.uuid;
        // 在创建 GeomShapeObject 前先恢复逻辑可见性。快速加载模式据此判断：
        // 隐藏节点只恢复 OCC Shape，不立即生成 VTK Actor。
        GeometryDisplayManager::setVisible(id, feature.visible, false);

        const QString type = feature.type;
        const QString kind = feature.kind;
        const QJsonObject state = feature.state;
        const QString normalizedKind = GeometryFeatures::normalizeKind(type, kind);

        QString title = feature.title.trimmed();
        if (normalizedKind == QStringLiteral("Sketch2D"))
        {
            const int explicitNumber = workPlaneNumber(title);
            int count = m_nameCounters.value(normalizedKind, 0);
            if (explicitNumber > 0)
            {
                count = std::max(count, explicitNumber);
                m_nameCounters.insert(normalizedKind, count);
            }
            else
            {
                ++count;
                m_nameCounters.insert(normalizedKind, count);
                if (isGenericLoadedTitle(title))
                    title = tr("草图 %1").arg(count);
            }
        }

        if (title.isEmpty())
        {
            title = FeatureData::typeLabel(type, kind);
            if (title.isEmpty())
                title = tr("几何");
        }
        feature.title = title;

        ProjectNode* node = createGeometryNodeByType(type, kind, title, id);
        if (!node)
        {
            continue;
        }

        node->setData(0, kRoleGeometryInputUuids,
                      feature.inputUuids.isEmpty()
                          ? FeatureData::collectInputUuids(state, id)
                          : feature.inputUuids);
        node->setData(0, kRoleGeometryConsumedInputUuids,
                      GeometryFeatures::consumedInputUuidsForFinalModel(type, kind, state, id));

        LoadedGeometryItem loaded;
        loaded.feature = feature;
        loaded.node = node;

        bool restoredFromCache = false;
        if (type == PublicConstants::kPageGeomTerrainSurface)
            GeometryDisplayManager::setTerrainPresentation(id, state["showSurface"].toBool(true),
                                                           state["showContours"].toBool(true));
        if (shapeSnapshots)
        {
            const auto snapshotIt = shapeSnapshots->constFind(id);
            if (snapshotIt != shapeSnapshots->constEnd() && !snapshotIt.value().IsNull())
            {
                GeometryBuildUtils::registerShape(id, title, new TopoDS_Shape(snapshotIt.value()));
                restoredFromCache = hasRuntimeShape(id);
                if (!restoredFromCache)
                    loaded.restoreError = tr("历史几何恢复为显示对象失败。");
            }
        }
        if (!restoredFromCache)
        {
            TopoDS_Shape cachedShape;
            if (readCachedBrep(feature.cachedBrep, cachedShape))
            {
                GeometryBuildUtils::registerShape(id, title, new TopoDS_Shape(cachedShape));
                restoredFromCache = hasRuntimeShape(id);
                if (restoredFromCache)
                {
                    if (!feature.cachedBrepAsset.isEmpty()) {
                        const QString root = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
                        const QFileInfo asset(QDir(root).filePath(feature.cachedBrepAsset));
                        m_savedBrepAssets[id] = {cachedShape, feature.cachedBrepAsset, asset.size(), asset.lastModified().toMSecsSinceEpoch()};
                    }
                    if (auto* body = GeomManager::CadDocument::getInstance()->getGeomShapeObject(id))
                    {
                        if (!feature.faceGroups.isEmpty())
                        {
                            body->setFaceGroups(feature.faceGroups);
                        }
                        else if (!cachedShape.IsNull() && cachedShape.ShapeType() == TopAbs_SOLID)
                        {
                            TopTools_IndexedMapOfShape fMap;
                            TopExp::MapShapes(cachedShape, TopAbs_FACE, fMap);
                            if (fMap.Extent() == 6)
                            {
                                CadBody::FaceGroup topGroup{QStringLiteral("地表顶面组"), QStringLiteral("top"), {}};
                                CadBody::FaceGroup bottomGroup{QStringLiteral("基底底面组"), QStringLiteral("bottom"), {}};
                                CadBody::FaceGroup sidesGroup{QStringLiteral("四周侧面组"), QStringLiteral("sides"), {}};
                                for (int fi = 1; fi <= fMap.Extent(); ++fi)
                                {
                                    const TopoDS_Face& f = TopoDS::Face(fMap(fi));
                                    BRepAdaptor_Surface surf(f, Standard_True);
                                    gp_Pnt pMid;
                                    gp_Vec d1u, d1v;
                                    surf.D1(0.5 * (surf.FirstUParameter() + surf.LastUParameter()),
                                            0.5 * (surf.FirstVParameter() + surf.LastVParameter()),
                                            pMid, d1u, d1v);
                                    gp_Vec normal = d1u.Crossed(d1v);
                                    if (normal.Magnitude() > 1e-6)
                                    {
                                        normal.Normalize();
                                        if (f.Orientation() == TopAbs_REVERSED) normal.Reverse();
                                        if (normal.Z() > 0.2) topGroup.faceIndices.append(fi);
                                        else if (normal.Z() < -0.8) bottomGroup.faceIndices.append(fi);
                                        else sidesGroup.faceIndices.append(fi);
                                    }
                                    else sidesGroup.faceIndices.append(fi);
                                }
                                if (!topGroup.faceIndices.isEmpty() && !bottomGroup.faceIndices.isEmpty())
                                    body->setFaceGroups({topGroup, bottomGroup, sidesGroup});
                            }
                        }
                    }
                }
                if (!restoredFromCache)
                    loaded.restoreError = tr("已保存的几何结果恢复失败。");
            }
        }

        loaded.restored = restoredFromCache;
        itemIndexByUuid.insert(id, loadedItems.size());
        loadedItems.push_back(loaded);
    }

    // 第二阶段：按依赖关系多轮自动重建。工程中的节点顺序不再是硬约束，
    // 即使外部生成的 preprocessing.json 把布尔节点写在来源前面，也能等待
    // 来源恢复完成后再执行。cachedBrep 仍然优先使用。
    QSet<int> pending;
    for (int i = 0; i < loadedItems.size(); ++i)
    {
        if (!loadedItems[i].restored)
            pending.insert(i);
    }

    bool madeProgress = true;
    while (!pending.isEmpty() && madeProgress)
    {
        madeProgress = false;
        const QList<int> currentPending = pending.values();
        for (int index : currentPending)
        {
            LoadedGeometryItem& item = loadedItems[index];
            const QStringList inputs = item.feature.inputUuids.isEmpty()
                ? FeatureData::collectInputUuids(item.feature.state, item.feature.uuid)
                : item.feature.inputUuids;

            QStringList missingInputs;
            bool waitingForDependency = false;
            bool failedDependency = false;
            for (const QString& inputUuid : inputs)
            {
                if (inputUuid.isEmpty())
                    continue;

                const auto inputIt = itemIndexByUuid.constFind(inputUuid);
                if (inputIt == itemIndexByUuid.constEnd())
                {
                    missingInputs.push_back(inputUuid);
                    continue;
                }

                if (hasRuntimeShape(inputUuid))
                    continue;

                if (pending.contains(inputIt.value()))
                    waitingForDependency = true;
                else
                    failedDependency = true;
            }

            if (!missingInputs.isEmpty())
            {
                item.restoreError = tr("缺少来源对象：%1").arg(missingInputs.join(QStringLiteral(", ")));
                pending.remove(index);
                madeProgress = true;
                continue;
            }

            if (waitingForDependency)
                continue;

            if (failedDependency)
            {
                item.restoreError = tr("上游几何恢复失败。");
                pending.remove(index);
                madeProgress = true;
                continue;
            }

            QString rebuildMessage;
            const bool rebuilt = GeometryFeatures::rebuildFeature(item.feature, *service, &rebuildMessage) &&
                                 hasRuntimeShape(item.feature.uuid);
            if (rebuilt)
            {
                item.restored = true;
                item.restoreError.clear();
            }
            else
            {
                item.restoreError = rebuildMessage.isEmpty()
                    ? tr("几何自动恢复失败。")
                    : rebuildMessage;
                qWarning() << "Failed to restore geometry feature"
                           << item.feature.uuid << item.feature.title << item.restoreError;
            }

            pending.remove(index);
            madeProgress = true;
        }
    }

    // 无法推进的节点通常存在循环依赖，或其依赖链中有未能恢复的节点。
    for (int index : pending)
    {
        LoadedGeometryItem& item = loadedItems[index];
        item.restoreError = tr("存在循环依赖或上游对象尚未恢复。");
    }

    // 第三阶段：所有可恢复 Shape 都已存在后，再把原始 JSON 装入页面。
    // 这样来源下拉框、布尔输入列表和“已应用参数”会与工程文件保持一致。
    for (LoadedGeometryItem& item : loadedItems)
    {
        if (item.node)
        {
            if (BasePage* page = item.node->getPage())
                page->loadJson(item.feature.state);

            item.node->setData(0, kRoleGeometryInputUuids,
                               item.feature.inputUuids.isEmpty()
                                   ? FeatureData::collectInputUuids(item.feature.state,
                                                                    item.feature.uuid)
                                   : item.feature.inputUuids);
            item.node->setData(0, kRoleGeometryConsumedInputUuids,
                               GeometryFeatures::consumedInputUuidsForFinalModel(
                                   item.feature.type,
                                   item.feature.kind,
                                   item.feature.state,
                                   item.feature.uuid));
            item.node->setData(0, kRoleGeometryRestoreError, item.restoreError);
            applyNodeVisualState(item.node, item.feature.toJson());
        }
    }

    bool allRestored = true;
    for (const LoadedGeometryItem& item : loadedItems)
    {
        if (!item.restored || !hasRuntimeShape(item.feature.uuid))
        {
            allRestored = false;
            break;
        }
    }

    if (!m_loadingProject)
        refreshGeometryNodePresentation();
    // 打开普通工程时允许保留可恢复节点，并通过节点错误状态提示个别失败；
    // 撤销/重做必须是全量恢复，否则触发事务回滚。
    return m_restoringGeometryHistory ? allRestored : true;
}

ProjectNode* GeometryWorkbench::createGeometryNodeByType(const QString& type, const QString& kind,
                                                     const QString& title, const QString& uuid)
{
    if (!m_geomNode)
        return nullptr;

    BasePage* page = GeometryFeatures::createPage(type, kind);
    if (page)
        GeometryPanelUi::apply(page);
    QString nodeKind = GeometryFeatures::normalizeKind(type, kind);
    if (nodeKind.isEmpty())
        nodeKind = kind;

    if (!page)
        return nullptr;

    auto* node = new ProjectNode(title, uuid, page);
    clearGeometryNodeDecoration(node);
    page->setDisplayName(title);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, nodeKind);
    initializeGeometryNodeVisibility(node, false);
    node->setData(0, kRoleGeometryConsumed, false);
    node->setData(0, kRoleGeometryActiveResult, false);
    node->setData(0, kRoleGeometryInputUuids, QStringList());
    node->setData(0, kRoleGeometryConsumedInputUuids, QStringList());
    node->setData(0, kRoleGeometryDerivedCount, 0);
    node->setData(0, kRoleGeometryMissingInputCount, 0);
    node->setData(0, kRoleGeometryRestoreError, QString());
    node->setFlags(node->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    addGeometryNodeToTree(node);
    if (!m_loadingProject)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
        refreshGeometryNodePresentation();
    }
    return node;
}

void GeometryWorkbench::applyNodeVisualState(ProjectNode* node, const QJsonObject& itemState) const
{
    if (!node)
        return;

    clearGeometryNodeDecoration(node);

    const QString uuid = node->data(0, ProjectNode::RoleUuid).toString();
    const QString title = itemState.value("title").toString().trimmed();
    if (!title.isEmpty())
    {
        node->setName(title);
        node->setText(0, title);
        if (BasePage* page = node->getPage())
            page->setDisplayName(title);
        if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
        {
            if (GeomShapeObject* object = mgr->getGeomShapeObject(uuid))
                object->setName(title);
        }
    }

    const bool isVisible = itemState.value("visible").toBool(true);
    GeometryDisplayManager::setVisible(uuid, isVisible, !m_loadingProject);
    node->setData(0, kRoleGeometryVisible, isVisible);
    node->setData(0, ProjectNode::RoleDimmed, !isVisible);
    node->setCheckState(0, isVisible ? Qt::Checked : Qt::Unchecked);
}


void GeometryWorkbench::installGeometryTreeHooks()
{
    if (!m_geomNode || !m_geomNode->treeWidget())
        return;

    QTreeWidget* tree = m_geomNode->treeWidget();
    if (m_hookedGeometryTree == tree)
        return;

    m_hookedGeometryTree = tree;
    // 几何操作需要支持模型树多选：Ctrl/Shift 追加选择，
    // 右键菜单再根据当前选择数量提供布尔运算或单对象操作。
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    if (!m_loadingProject)
    {
        if (auto* current = tree->currentItem())
            onGeometryTreeCurrentItemChanged(current, nullptr);
    }

    // 不能用 lambda + Qt::UniqueConnection；Qt 对 functor/lambda 的 UniqueConnection 不可靠，
    // 会导致 itemChanged/currentItemChanged 实际没有连上，表现就是 checkbox 和树选中高亮都不生效。
    connect(tree, &QTreeWidget::currentItemChanged,
            this, &GeometryWorkbench::onGeometryTreeCurrentItemChanged,
            Qt::UniqueConnection);

    connect(tree, &QTreeWidget::itemChanged,
            this, &GeometryWorkbench::onGeometryTreeItemChanged,
            Qt::UniqueConnection);

}

void GeometryWorkbench::onGeometryTreeCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);

    m_sourcePreviewUuids.clear();
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    clearFeaturePreviewForGeometryVisibilityChange(QString());

    auto* node = dynamic_cast<ProjectNode*>(current);
    if (!node || node == m_geomNode ||
        node->data(0, ProjectNode::RoleModule).toString() != "Geometry" ||
        !node->getPage())
    {
        clearTreeSelectionHighlight();
        return;
    }

    // 草图编辑器本身已经使用统一的蓝色预览显示全部草图元素。
    // 如果这里再套一层几何树“当前对象”青色高亮，会让同一草图同时出现
    // 深蓝/亮青两种线色，尤其 DXF 中闭合轮廓和开放曲线混合时最明显。
    if (node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("Sketch2D"))
    {
        clearTreeSelectionHighlight();
        return;
    }

    setCurrentTreeSelection(geometryNodeUuid(node));
}

void GeometryWorkbench::onGeometryTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (column != 0)
        return;

    auto* node = dynamic_cast<ProjectNode*>(item);
    if (!node)
        return;

    if (node->data(0, Qt::DecorationRole).isValid())
    {
        // itemChanged 不区分具体角色。用信号阻断清理偶发写入的图标，
        // 避免清理 DecorationRole 时再次触发本槽形成递归。
        if (QTreeWidget* tree = node->treeWidget())
        {
            QSignalBlocker blocker(tree);
            clearGeometryNodeDecoration(node);
        }
        else
        {
            clearGeometryNodeDecoration(node);
        }
    }

    if (m_updatingGeometryTree)
        return;

    // itemChanged 也会被文字、提示和 DecorationRole 变化触发。只有复选框
    // 与已保存的可见状态不一致时，才真正执行显隐，避免无关刷新造成闪烁。
    const bool checkedVisible = node->checkState(0) == Qt::Checked;
    const QVariant storedVisible = node->data(0, kRoleGeometryVisible);
    if (storedVisible.isValid() && storedVisible.toBool() == checkedVisible)
        return;

    handleGeometryTreeVisibilityChange(node);
}

void GeometryWorkbench::handleGeometryTreeVisibilityChange(ProjectNode* node)
{
    if (m_updatingGeometryTree || !node || node == m_geomNode ||
        node->data(0, ProjectNode::RoleModule).toString() != "Geometry" || !node->getPage())
    {
        return;
    }

    const QString uuid = geometryNodeUuid(node);
    if (uuid.isEmpty())
        return;

    const bool visible = node->checkState(0) == Qt::Checked;

    clearFeaturePreviewForGeometryVisibilityChange(uuid);

    // checkbox 已经改变，但 kRoleGeometryVisible 此时还是旧值。
    // 必须先把逻辑状态写回节点，再调用 DisplayManager；这样即使
    // setNodeDimmed()/refreshTreePresentation() 再次触发 itemChanged，
    // onGeometryTreeItemChanged() 也会因为状态已经一致而直接返回。
    m_updatingGeometryTree = true;
    node->setData(0, kRoleGeometryVisible, visible);
    node->setData(0, ProjectNode::RoleDimmed, !visible);
    node->setToolTip(0, geometryNodeTooltip(node));
    m_updatingGeometryTree = false;

    // Visibility and selection are independent. Hiding a selected object keeps
    // the tree selection state, and showing it again restores the same highlight.
    GeometryDisplayManager::setVisible(uuid, visible, false);

    if (auto* sketch = dynamic_cast<Sketch2DDialog*>(node->getPage()))
        sketch->syncVisibilityFromTree(visible);

    GeometrySelectionVisuals::refreshHighlights(false);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
    commitGeometryHistory();
}

void GeometryWorkbench::clearFeaturePreviewForGeometryVisibilityChange(const QString& uuid)
{
    Q_UNUSED(uuid);

    // Whole-object source highlighting is a logical state and is visibility-aware.
    // Keep it while hiding/showing objects so the same input is highlighted again
    // when it becomes visible. Only transient sub-shape overlays are cleared here.
    GeometrySelectionVisuals::clearTreePickSelection();

    const QStringList geometryPickDomains = {
        QStringLiteral("GeometryExtrudeSource"),
        QStringLiteral("GeometryRevolveSource"),
        QStringLiteral("GeometrySweepInputs"),
        QStringLiteral("GeometryLoftProfiles"),
        QStringLiteral("GeometryCutExtrudeInputs"),
        QStringLiteral("GeometryTransformSource"),
        QStringLiteral("GeometryMirrorSource"),
        QStringLiteral("GeometryPatternSource"),
        QStringLiteral("GeometrySplitInputs"),
        QStringLiteral("GeometryFillet"),
        QStringLiteral("GeometryChamfer")
    };
    for (const QString& domain : geometryPickDomains)
        GeometryPickManager::instance().clearDomain(domain, QString(), false);

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis)
        return;

    const QList<QString> ids = vis->allIds();
    for (const QString& id : ids)
    {
        if (id.endsWith(QStringLiteral("_extrude_preview_face")) ||
            id.endsWith(QStringLiteral("_extrude_preview_edge")) ||
            id.endsWith(QStringLiteral("_sweep_preview_face")) ||
            id.endsWith(QStringLiteral("_sweep_preview_edge")) ||
            id.endsWith(QStringLiteral("_revolve_axis_preview")) ||
            id.contains(QStringLiteral("_feature_preview_")))
        {
            vis->removeProp(id, false);
        }
    }
}

void GeometryWorkbench::highlightGeometryNode(const QString& uuid, bool fit)
{
    setCurrentTreeSelection(uuid);

    if (fit && !uuid.isEmpty())
        fitNode(uuid);
}

void GeometryWorkbench::setCurrentTreeSelection(const QString& uuid)
{
    m_sourcePreviewUuids.clear();
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    m_currentTreeSelectionUuid.clear();
    GeometrySelectionVisuals::clearTreeSelectionHighlight(false);

    m_currentTreeSelectionUuid = uuid;

    // 树选中高亮：直接改当前 uuid 自己的 actor 颜色，不创建 overlay，不抽 OCC 子形状。
    if (!uuid.isEmpty() && GeometryDisplayManager::isVisible(uuid))
        GeometrySelectionVisuals::applyTreeSelectionHighlight(uuid);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

void GeometryWorkbench::clearTreeSelectionHighlight()
{
    m_currentTreeSelectionUuid.clear();
    GeometrySelectionVisuals::clearTreeSelectionHighlight(true);
}

void GeometryWorkbench::setSourcePreviewUuids(const QStringList& uuids)
{
    clearSourcePreviewHighlight();
    for (const QString& uuid : uuids)
    {
        if (uuid.isEmpty() || m_sourcePreviewUuids.contains(uuid))
            continue;
        m_sourcePreviewUuids.push_back(uuid);
        GeometrySelectionVisuals::applySourcePreviewHighlight(uuid);
    }

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

void GeometryWorkbench::clearSourcePreviewHighlight()
{
    m_sourcePreviewUuids.clear();
    GeometrySelectionVisuals::clearSourcePreviewHighlight(true);
}

void GeometryWorkbench::ensureGeometryRootGroups()
{
    // v6：模型树改成统一建模历史树，不再按“草图/实体”分组。
    // 这个函数保留为空实现，避免旧调用点或后续合并时产生接口变化。
}

ProjectNode* GeometryWorkbench::ensureGeometryGroup(const QString& groupKey, const QString& title)
{
    Q_UNUSED(groupKey);
    Q_UNUSED(title);
    // v6：不再创建“草图/实体”分组，所有几何节点直接挂在“几何”下，按创建顺序排列。
    return m_geomNode;
}

QString GeometryWorkbench::geometryGroupForKind(const QString& kind) const
{
    Q_UNUSED(kind);
    return QString();
}

void GeometryWorkbench::addGeometryNodeToTree(ProjectNode* node)
{
    if (!m_geomNode || !node)
        return;

    clearGeometryNodeDecoration(node);
    node->setData(0, Qt::UserRole, QVariant::fromValue(node));

    // v6：统一建模历史树。节点直接挂在“几何”下，顺序就是创建/恢复顺序。
    // 草图、拉伸、切除、圆角等不再分到不同文件夹里，用户可以按建模流程阅读。
    m_geomNode->addChild(node);
    node->setExpanded(false);
    m_geomNode->setExpanded(true);
    installGeometryTreeHooks();
}

void GeometryWorkbench::collectGeometryLeafNodes(ProjectNode* root, QList<ProjectNode*>& out) const
{
    if (!root)
        return;

    BasePage* page = root->getPage();
    const QString kind = root->data(0, ProjectNode::RoleKind).toString();
    // The project tree attaches DashboardPage to clicked containers. Having a
    // page does not make the geometry root/assembly group a model feature.
    const bool container = root == m_geomNode || root == m_gridNode ||
        kind == QStringLiteral("GeometryRoot") || kind == QStringLiteral("GeometryGroup") ||
        kind == QStringLiteral("StepAssemblyGroup") ||
        (page && page->getPageType() == QStringLiteral("DashboardPage"));
    if (page && !container)
    {
        out.push_back(root);
        return;
    }

    for (int i = 0; i < root->childCount(); ++i)
        collectGeometryLeafNodes(static_cast<ProjectNode*>(root->child(i)), out);
}

ProjectNode* GeometryWorkbench::findGeometryNodeByUuid(const QString& uuid) const
{
    if (uuid.isEmpty() || !m_geomNode)
        return nullptr;

    QList<ProjectNode*> stack;
    stack.push_back(m_geomNode);
    while (!stack.isEmpty())
    {
        ProjectNode* node = stack.takeLast();
        if (!node)
            continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        if (nodeUuid == uuid)
            return node;

        for (int i = 0; i < node->childCount(); ++i)
            stack.push_back(static_cast<ProjectNode*>(node->child(i)));
    }
    return nullptr;
}

QString GeometryWorkbench::geometryNodeUuid(const ProjectNode* node) const
{
    if (!node)
        return QString();

    QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
    if (nodeUuid.isEmpty())
        nodeUuid = node->getUuid();
    return nodeUuid;
}

QJsonObject GeometryWorkbench::geometryNodeState(ProjectNode* node) const
{
    if (!node)
        return {};

    if (BasePage* page = node->getPage())
        return page->saveJsonForPersistence();
    return {};
}

QStringList GeometryWorkbench::geometryInputUuids(const QJsonObject& state,
                                                  const QString& selfUuid) const
{
    return FeatureData::collectInputUuids(state, selfUuid);
}

QStringList GeometryWorkbench::cachedGeometryInputUuids(ProjectNode* node) const
{
    if (!node)
        return {};

    const QVariant cached = node->data(0, kRoleGeometryInputUuids);
    if (cached.isValid())
        return cached.toStringList();

    return geometryInputUuids(geometryNodeState(node), geometryNodeUuid(node));
}

void GeometryWorkbench::updateGeometryNodeDependencyCache(ProjectNode* node)
{
    if (!node)
        return;

    const QString selfUuid = geometryNodeUuid(node);
    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    const QJsonObject state = geometryNodeState(node);
    node->setData(0, kRoleGeometryInputUuids, geometryInputUuids(state, selfUuid));
    node->setData(0, kRoleGeometryConsumedInputUuids,
                  hasRuntimeShape(selfUuid)
                      ? GeometryFeatures::consumedInputUuidsForFinalModel(
                            QString(), kind, state, selfUuid)
                      : QStringList());
}

void GeometryWorkbench::removeGeometryLeafNode(ProjectNode* node)
{
    if (!node || node == m_geomNode)
        return;

    const QString uuid = geometryNodeUuid(node);
    if (node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("ImportGeometry"))
    {
        ImportGeometryBuilder::clearStepImportParts(uuid);
        ImportGeometryBuilder::clearStepImportOptions(uuid);
    }
    GeometryCleanup::removeExistingGeometry(uuid);
    GeometryDisplayManager::forgetVisibilityState(uuid);

    if (BasePage* page = node->getPage())
    {
        // 先让页面清理来源高亮、选择域和临时显示，再删除未应用节点。
        page->beforeUnload();
        page->deleteLater();
        node->setPage(nullptr);
    }

    if (auto* parent = static_cast<ProjectNode*>(node->parent()))
        parent->removeChild(node);
    else if (m_geomNode)
        m_geomNode->removeChild(node);
    delete node;
}

void GeometryWorkbench::showGeometryInputs(const QString& uuid)
{
    if (uuid.isEmpty())
        return;

    ProjectNode* node = findGeometryNodeByUuid(uuid);
    if (!node)
        return;

    const QStringList inputUuids = cachedGeometryInputUuids(node);
    if (inputUuids.isEmpty())
        return;

    // “显示来源对象”是明确的显示命令：打开来源对象可见性，并跳到第一个来源节点。
    // 不再创建蓝色/橙色预览线，避免和复选框状态打架。
    QString firstExistingInputUuid;
    for (const QString& inputUuid : inputUuids)
    {
        if (!findGeometryNodeByUuid(inputUuid))
            continue;
        GeometryDisplayManager::show(inputUuid, false);
        if (firstExistingInputUuid.isEmpty())
            firstExistingInputUuid = inputUuid;
    }

    if (firstExistingInputUuid.isEmpty())
    {
        QMessageBox::information(nullptr,
                                 tr("来源已删除"),
                                 tr("来源已删除。"));
        return;
    }

    refreshGeometryNodePresentation();

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        if (ProjectNode* inputNode = pm->findNodeByUuid(firstExistingInputUuid))
            pm->setCurrentItem(inputNode);
    }

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

bool GeometryWorkbench::isExtrudableProfileNode(const ProjectNode* node) const
{
    if (!node)
        return false;

    const QString uuid = geometryNodeUuid(node);
    if (uuid.isEmpty())
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    int faceCount = 0;
    int openEdgeCount = 0;
    FeatureBuilder::analyzeExtrudeProfile(uuid, &faceCount, nullptr, nullptr, &openEdgeCount);
    // 即使轮廓当前未闭合，也允许进入拉伸/旋转面板，让页面给出明确的
    // 闭合错误；不能因为来源无效就让菜单项悄悄消失。
    return faceCount > 0 || openEdgeCount > 0;
}

QString GeometryWorkbench::geometryNodeTypeLabel(const QString& kind) const
{
    return GeometryFeatures::labelForKind(kind);
}

QString GeometryWorkbench::geometryNodeInputSummary(
    ProjectNode* node, const QHash<QString, ProjectNode*>* nodesByUuid) const
{
    if (!node)
        return QString();

    const QStringList inputUuids = cachedGeometryInputUuids(node);
    if (inputUuids.isEmpty())
        return QString();

    QStringList names;
    for (const QString& inputUuid : inputUuids)
    {
        ProjectNode* inputNode = nodesByUuid
            ? nodesByUuid->value(inputUuid, nullptr)
            : findGeometryNodeByUuid(inputUuid);
        QString inputName = inputNode ? inputNode->getName() : tr("来源已删除");
        if (inputName.size() > 18)
            inputName = inputName.left(15) + QStringLiteral("...");
        names.push_back(inputName);
    }

    if (names.size() == 1)
        return names.front();
    if (names.size() == 2)
        return tr("%1 + %2").arg(names.at(0), names.at(1));
    return tr("%1 等 %2 个输入").arg(names.front()).arg(names.size());
}

QString GeometryWorkbench::geometryNodeDisplayText(ProjectNode* node) const
{
    if (!node)
        return QString();

    QString name = node->getName().isEmpty() ? node->text(0) : node->getName();
    name = name.simplified();

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    const QString typeLabel = geometryNodeTypeLabel(kind);

    // 模型树只显示用户真正关心的名字；类型、输入关系、显示状态放到 tooltip。
    if (name.startsWith(typeLabel + QStringLiteral(" | ")))
        name = name.mid(typeLabel.size() + 3).trimmed();

    // 树上只显示对象名，不附加类型/来源/状态。名字过长再截断。
    if (name.size() > 24)
        name = name.left(21) + QStringLiteral("...");

    return name;
}

QString GeometryWorkbench::geometryNodeTooltip(
    ProjectNode* node, const QHash<QString, ProjectNode*>* nodesByUuid) const
{
    if (!node)
        return QString();

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    QStringList lines;
    lines << geometryNodeTypeLabel(kind);

    const QString inputSummary = geometryNodeInputSummary(node, nodesByUuid);
    if (!inputSummary.isEmpty())
        lines << tr("来源：%1").arg(inputSummary);

    const int missingCount = node->data(0, kRoleGeometryMissingInputCount).toInt();
    if (missingCount > 0)
        lines << tr("缺少 %1 个来源").arg(missingCount);

    const QString restoreError = node->data(0, kRoleGeometryRestoreError).toString();
    if (!restoreError.isEmpty())
        lines << restoreError;

    return lines.join(QLatin1Char('\n'));
}

void GeometryWorkbench::clearGeometryInputReferenceNodes(ProjectNode* node)
{
    if (!node)
        return;

    for (int i = node->childCount() - 1; i >= 0; --i)
    {
        auto* child = static_cast<ProjectNode*>(node->child(i));
        if (!child || child->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("GeometryInputRef"))
            continue;

        node->removeChild(child);
        delete child;
    }
}

void GeometryWorkbench::syncGeometryInputReferenceNodes(ProjectNode* node)
{
    if (!node)
        return;

    clearGeometryInputReferenceNodes(node);
}

void GeometryWorkbench::refreshGeometryNodePresentation()
{
    if (!m_geomNode)
        return;

    installGeometryTreeHooks();

    // 刷新树时阻断 itemChanged；逻辑可见状态由模型树保存，并在必要时重新应用到 Actor。
    QTreeWidget* treeWidget = m_geomNode->treeWidget();
    const bool oldTreeSignalsBlocked = treeWidget ? treeWidget->blockSignals(true) : false;
    removeGeometryContainerCheckBox(m_geomNode);

    // 兼容旧版运行时树：如果根节点下还存在“草图/实体”分组，刷新时自动拍平。
    for (int i = m_geomNode->childCount() - 1; i >= 0; --i)
    {
        auto* child = static_cast<ProjectNode*>(m_geomNode->child(i));
        if (!child || child->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("GeometryGroup"))
            continue;

        m_geomNode->takeChild(i);
        while (child->childCount() > 0)
        {
            ProjectNode* leaf = static_cast<ProjectNode*>(child->takeChild(0));
            if (leaf)
                m_geomNode->addChild(leaf);
        }
        delete child;
    }
    m_geometryGroups.clear();

    QList<ProjectNode*> geomNodes;
    collectGeometryLeafNodes(m_geomNode, geomNodes);

    QSet<QString> allNodeUuids;
    QHash<QString, ProjectNode*> nodesByUuid;
    nodesByUuid.reserve(geomNodes.size());
    for (ProjectNode* node : geomNodes)
    {
        const QString uuid = geometryNodeUuid(node);
        if (uuid.isEmpty())
            continue;
        allNodeUuids.insert(uuid);
        if (!nodesByUuid.contains(uuid))
            nodesByUuid.insert(uuid, node);
    }

    QSet<QString> consumedUuids;
    QHash<QString, int> derivedCounts;
    QHash<QString, int> missingInputCounts;
    for (ProjectNode* node : geomNodes)
    {
        if (!node)
            continue;

        const QString selfUuid = geometryNodeUuid(node);
        const QStringList inputs = node->data(0, kRoleGeometryInputUuids).toStringList();
        const QStringList replacedInputs = hasRuntimeShape(selfUuid)
            ? node->data(0, kRoleGeometryConsumedInputUuids).toStringList()
            : QStringList();
        int missingCount = 0;
        for (const QString& inputUuid : inputs)
        {
            if (inputUuid.isEmpty())
                continue;

            // 来源是弱引用。只有派生结果实际存在且该输入确实被结果替代时，
            // 才从默认导出/网格集合中排除。扫掠路径和分割工具仍然保留。
            if (replacedInputs.contains(inputUuid))
                consumedUuids.insert(inputUuid);

            if (allNodeUuids.contains(inputUuid))
                derivedCounts[inputUuid] = derivedCounts.value(inputUuid) + 1;
            else
                ++missingCount;
        }
        if (!selfUuid.isEmpty())
            missingInputCounts.insert(selfUuid, missingCount);
    }

    QStringList activeUuids;
    if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
    {
        for (ProjectNode* node : geomNodes)
        {
            const QString uuid = geometryNodeUuid(node);
            if (uuid.isEmpty() || consumedUuids.contains(uuid) || activeUuids.contains(uuid))
                continue;
            if (mgr->getGeomShapeObject(uuid))
                activeUuids.push_back(uuid);
        }
    }

    bool visibilityNeedsRender = false;
    m_updatingGeometryTree = true;
    for (ProjectNode* node : geomNodes)
    {
        if (!node)
            continue;

        clearGeometryNodeDecoration(node);

        const QString uuid = geometryNodeUuid(node);
        const QString kind = node->data(0, ProjectNode::RoleKind).toString();
        const QVariant storedVisible = node->data(0, kRoleGeometryVisible);
        const bool hasShape = hasRuntimeShape(uuid);
        const bool visible = hasShape
            ? GeometryDisplayManager::isVisible(uuid)
            : (storedVisible.isValid() ? storedVisible.toBool() : false);

        // 切换出草图模式时，显示窗口可能临时隐藏草图 Actor，但模型树中的
        // 逻辑可见状态仍然为 true。刷新模型树时只在 Actor 状态确实不一致
        // 的情况下重新应用显隐，保证“勾选即显示”，同时避免无意义重绘。
        if (hasShape && !runtimeGeometryVisibilityMatches(uuid, visible))
        {
            GeometryDisplayManager::setVisible(uuid, visible, false);
            visibilityNeedsRender = true;
        }

        const bool consumed = consumedUuids.contains(uuid);
        const bool active = activeUuids.contains(uuid);
        if (hasShape)
            node->setData(0, kRoleGeometryRestoreError, QString());

        node->setFlags(node->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        node->setData(0, ProjectNode::RoleDimmed, !visible);
        node->setData(0, kRoleGeometryVisible, visible);
        node->setData(0, kRoleGeometryConsumed, consumed);
        node->setData(0, kRoleGeometryActiveResult, active);
        node->setData(0, kRoleGeometryDerivedCount, derivedCounts.value(uuid));
        node->setData(0, kRoleGeometryMissingInputCount, missingInputCounts.value(uuid));
        node->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        node->setText(0, geometryNodeDisplayText(node));
        node->setToolTip(0, geometryNodeTooltip(node, &nodesByUuid));
        node->setForeground(0, QBrush(QColor(15, 23, 42)));
        syncGeometryInputReferenceNodes(node);
    }
    m_updatingGeometryTree = false;

    if (treeWidget)
        treeWidget->blockSignals(oldTreeSignalsBlocked);

    if (visibilityNeedsRender)
    {
        GeometrySelectionVisuals::refreshHighlights(false);
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
}

bool GeometryWorkbench::discardUnappliedGeometryNode(const QString& uuid)
{
    if (uuid.isEmpty() || hasRuntimeShape(uuid) || !m_geomNode)
        return false;

    ProjectNode* node = findGeometryNodeByUuid(uuid);
    if (!node || node == m_geomNode)
        return false;

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(m_geomNode);
    removeGeometryLeafNode(node);
    clearTreeSelectionHighlight();
    refreshGeometryNodePresentation();
    // 取消未应用节点不是一项可撤销建模操作；直接让当前快照与界面重新对齐。
    m_geometryHistoryCurrent = captureGeometryHistorySnapshot(QString(), true);
    m_geometryRedoHistory.clear();
    return true;
}

void GeometryWorkbench::synchronizeSplitPartNodes(const QString& splitUuid)
{
    if (splitUuid.isEmpty() || !m_geomNode)
        return;

    ProjectNode* splitNode = findGeometryNodeByUuid(splitUuid);
    if (!splitNode || splitNode->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("Split"))
        return;

    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* splitObject = manager ? manager->getGeomShapeObject(splitUuid) : nullptr;
    if (!splitObject || !splitObject->getShape() || splitObject->getShape()->IsNull())
        return;

    const QVector<TopoDS_Shape> parts = ModifyBuilder::splitResultParts(*(splitObject->getShape()));
    if (parts.size() <= 1)
        return;

    QSet<int> suppressedPartIndices;
    if (auto* splitPage = dynamic_cast<SplitDialog*>(splitNode->getPage()))
        suppressedPartIndices = splitPage->suppressedPartIndices();

    QHash<int, ProjectNode*> existingByIndex;
    QList<ProjectNode*> allNodes;
    collectGeometryLeafNodes(m_geomNode, allNodes);
    for (ProjectNode* node : allNodes)
    {
        if (!node || node->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("SplitPart"))
            continue;
        const QJsonObject state = geometryNodeState(node);
        if (state.value(QStringLiteral("splitUuid")).toString() != splitUuid)
            continue;
        existingByIndex.insert(state.value(QStringLiteral("partIndex")).toInt(-1), node);
    }

    for (int index = 0; index < parts.size(); ++index)
    {
        ProjectNode* partNode = existingByIndex.take(index);
        const QString partUuid = splitUuid + QStringLiteral("_part_%1").arg(index + 1);
        const QString partTitle = tr("%1 - 块 %2")
                                      .arg(stableNodeName(splitNode))
                                      .arg(index + 1);

        if (suppressedPartIndices.contains(index))
        {
            if (partNode)
                removeGeometryLeafNode(partNode);
            continue;
        }

        // 早期保存的分块节点可能使用随机 UUID。若与确定性 UUID 不一致，
        // 必须重建节点，否则树节点和运行时 Shape 会指向两个不同标识。
        if (partNode && geometryNodeUuid(partNode) != partUuid)
        {
            removeGeometryLeafNode(partNode);
            partNode = nullptr;
        }
        bool created = false;
        if (!partNode)
        {
            partNode = createGeometryNodeByType(QStringLiteral("geom_split_part"),
                                                QStringLiteral("SplitPart"),
                                                partTitle, partUuid);
            created = partNode != nullptr;
        }
        if (!partNode)
            continue;

        if (auto* page = dynamic_cast<SplitPartDialog*>(partNode->getPage()))
            page->setSplitPart(splitUuid, index);
        QString effectiveTitle = stableNodeName(partNode).trimmed();
        if (created || effectiveTitle.isEmpty())
        {
            effectiveTitle = partTitle;
            partNode->setName(effectiveTitle);
            partNode->setText(0, effectiveTitle);
            if (BasePage* page = partNode->getPage())
                page->setDisplayName(effectiveTitle);
        }
        partNode->setData(0, ProjectNode::RoleKind, QStringLiteral("SplitPart"));
        partNode->setData(0, kRoleGeometryInputUuids, QStringList{splitUuid});
        const QVariant savedVisibility = partNode->data(0, kRoleGeometryVisible);
        const bool partVisible = created || !savedVisibility.isValid()
            ? true
            : savedVisibility.toBool();
        initializeGeometryNodeVisibility(partNode, partVisible);
        GeometryDisplayManager::setVisible(partUuid, partVisible, false);

        // 新格式工程已经单独保存了 SplitPart 的 BRep。加载阶段若运行时 Shape
        // 已恢复，就不要再从父 Split 拆分并重复做一次 OCC->VTK 转换。
        if (!m_loadingProject || !hasRuntimeShape(partUuid))
        {
            GeometryBuildUtils::registerShape(partUuid, effectiveTitle,
                                              new TopoDS_Shape(parts.at(index)));
        }
    }

    for (ProjectNode* staleNode : existingByIndex)
        removeGeometryLeafNode(staleNode);

    GeometryDisplayManager::setVisible(splitUuid, false, false);
    splitNode->setData(0, kRoleGeometryVisible, false);
    splitNode->setCheckState(0, Qt::Unchecked);
}

void GeometryWorkbench::synchronizeAllSplitPartNodes()
{
    if (!m_geomNode)
        return;
    QList<ProjectNode*> nodes;
    collectGeometryLeafNodes(m_geomNode, nodes);
    QStringList splitUuids;
    for (ProjectNode* node : nodes)
    {
        if (node && node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("Split"))
            splitUuids.push_back(geometryNodeUuid(node));
    }
    for (const QString& uuid : splitUuids)
        synchronizeSplitPartNodes(uuid);
}

void GeometryWorkbench::synchronizeStepImportPartNodes(const QString& importUuid)
{
    if (importUuid.isEmpty() || !m_geomNode)
        return;

    ProjectNode* importNode = findGeometryNodeByUuid(importUuid);
    if (!importNode ||
        importNode->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("ImportGeometry"))
    {
        return;
    }

    auto semanticKey = [](const QString& assemblyPath, const QString& originalName)
    {
        const QString path = assemblyPath.simplified().toCaseFolded();
        if (!path.isEmpty())
            return path;
        return originalName.simplified().toCaseFolded();
    };
    auto indexNameKey = [](int partIndex, const QString& originalName)
    {
        if (partIndex < 0)
            return QString();
        return QString::number(partIndex) + QStringLiteral("\x1F") +
               originalName.simplified().toCaseFolded();
    };

    QList<ProjectNode*> allNodes;
    collectGeometryLeafNodes(m_geomNode, allNodes);

    QHash<QString, QList<ProjectNode*>> existingByUuid;
    QHash<QString, QList<ProjectNode*>> existingBySourceKey;
    QHash<QString, QList<ProjectNode*>> existingBySemanticKey;
    QHash<QString, QList<ProjectNode*>> existingByIndexName;
    QSet<ProjectNode*> unmatchedNodes;
    QList<ProjectNode*> existingPartNodes;

    auto appendCandidate = [](QHash<QString, QList<ProjectNode*>>& map,
                              const QString& key,
                              ProjectNode* node)
    {
        if (!key.isEmpty() && node)
            map[key].push_back(node);
    };

    for (ProjectNode* node : allNodes)
    {
        if (!node ||
            node->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("StepImportPart"))
        {
            continue;
        }

        const QJsonObject state = geometryNodeState(node);
        if (state.value(QStringLiteral("importUuid")).toString() != importUuid)
            continue;

        unmatchedNodes.insert(node);
        existingPartNodes.push_back(node);
        appendCandidate(existingByUuid, geometryNodeUuid(node), node);
        appendCandidate(existingBySourceKey,
                        state.value(QStringLiteral("sourceKey")).toString(), node);
        appendCandidate(existingBySemanticKey,
                        semanticKey(state.value(QStringLiteral("assemblyPath")).toString(),
                                    state.value(QStringLiteral("originalName")).toString()),
                        node);
        appendCandidate(existingByIndexName,
                        indexNameKey(state.value(QStringLiteral("partIndex")).toInt(-1),
                                     state.value(QStringLiteral("originalName")).toString()),
                        node);

        // 即使源 STEP 暂时找不到，工程中的缓存 BRep 仍然可以恢复。
        // 此时没有 XDE 内存数据，但已保存的颜色仍应重新应用。
        ImportGeometryBuilder::applyStepColor(
            geometryNodeUuid(node), state.value(QStringLiteral("colorHex")).toString());
    }

    auto takeCandidate = [&unmatchedNodes](QHash<QString, QList<ProjectNode*>>& map,
                                           const QString& key) -> ProjectNode*
    {
        if (key.isEmpty())
            return nullptr;
        auto it = map.find(key);
        if (it == map.end())
            return nullptr;
        for (ProjectNode* node : it.value())
        {
            if (unmatchedNodes.remove(node))
                return node;
        }
        return nullptr;
    };

    const QJsonObject importState = geometryNodeState(importNode);
    const QString sourcePath = importState.value(QStringLiteral("path")).toString();
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    const bool isStepSource = suffix == QStringLiteral("step") || suffix == QStringLiteral("stp");
    const StepImportOptions importOptions =
        ImportGeometryBuilder::stepImportOptions(importUuid);

    auto assemblyRootTitle = [&]()
    {
        QString title = QFileInfo(sourcePath).completeBaseName().simplified();
        if (title.isEmpty())
            title = stableNodeName(importNode).simplified();
        if (title.isEmpty())
            title = tr("导入模型");
        return tr("STEP-%1").arg(title);
    };

    // 同一个导入节点若被改成 IGES/BREP/DXF，应清理原来的 STEP 分组和子对象。
    if (!isStepSource)
    {
        clearStepAssemblyGroups(m_geomNode, importUuid, existingPartNodes);
        for (ProjectNode* node : std::as_const(unmatchedNodes))
            removeGeometryLeafNode(node);
        return;
    }

    const QVector<StepImportPartInfo>& parts = ImportGeometryBuilder::stepImportParts(importUuid);
    const bool importNeedsSync = ImportGeometryBuilder::stepImportNeedsSynchronization(importUuid);
    if (parts.isEmpty())
    {
        // 源文件丢失或旧工程只剩 BRep 缓存时，仍按已保存的装配路径恢复树结构，
        // 但不删除缓存中的 STEP 实体节点。
        QTreeWidget* tree = m_geomNode->treeWidget();
        const bool oldBlocked = tree ? tree->blockSignals(true) : false;
        const bool oldUpdates = tree ? tree->updatesEnabled() : true;
        if (tree)
            tree->setUpdatesEnabled(false);

        clearStepAssemblyGroups(m_geomNode, importUuid, existingPartNodes);
        if (importOptions.groupByAssembly && !existingPartNodes.isEmpty())
        {
            ProjectNode* rootGroup = createStepAssemblyGroup(
                m_geomNode, assemblyRootTitle(), importUuid, QString());
            if (rootGroup)
                rootGroup->setExpanded(true);
            QHash<QString, ProjectNode*> groupsByPath;
            for (ProjectNode* partNode : existingPartNodes)
            {
                const QJsonObject state = geometryNodeState(partNode);
                moveStepPartIntoAssemblyTree(
                    partNode, rootGroup, importUuid,
                    state.value(QStringLiteral("assemblyPath")).toString(), groupsByPath);
            }
        }

        if (tree)
        {
            tree->blockSignals(oldBlocked);
            tree->setUpdatesEnabled(oldUpdates);
            tree->viewport()->update();
        }
        return;
    }

    QTreeWidget* treeWidget = m_geomNode->treeWidget();
    const bool oldSignalsBlocked = treeWidget ? treeWidget->signalsBlocked() : false;
    const bool oldUpdatesEnabled = treeWidget ? treeWidget->updatesEnabled() : true;
    const bool oldLoading = m_loadingProject;
    const bool oldRenderSuspended = GeometryDisplayManager::isRenderSuspended();

    if (treeWidget)
    {
        treeWidget->blockSignals(true);
        treeWidget->setUpdatesEnabled(false);
    }
    // createGeometryNodeByType() 在普通交互中会逐节点刷新。STEP 可能包含数百个实体，
    // 这里临时进入批量模式，最后由外层 refreshTreePresentation() 统一刷新一次。
    m_loadingProject = true;
    GeometryDisplayManager::setRenderSuspended(true);

    ScopeExit restoreBatchState([&, treeWidget]
    {
        m_loadingProject = oldLoading;
        if (treeWidget)
        {
            treeWidget->blockSignals(oldSignalsBlocked);
            treeWidget->setUpdatesEnabled(oldUpdatesEnabled);
            treeWidget->viewport()->update();
        }
        GeometryDisplayManager::setRenderSuspended(oldRenderSuspended);
    });

    clearStepAssemblyGroups(m_geomNode, importUuid, existingPartNodes);
    ProjectNode* assemblyRootGroup = nullptr;
    QHash<QString, ProjectNode*> assemblyGroupsByPath;
    if (importOptions.groupByAssembly)
    {
        assemblyRootGroup = createStepAssemblyGroup(
            m_geomNode, assemblyRootTitle(), importUuid, QString());
        if (assemblyRootGroup)
            assemblyRootGroup->setExpanded(true);
    }

    for (const StepImportPartInfo& part : parts)
    {
        if (!part.isValid())
            continue;

        ProjectNode* partNode = takeCandidate(existingByUuid, part.uuid);
        if (!partNode)
            partNode = takeCandidate(existingBySourceKey, part.sourceKey);
        if (!partNode)
            partNode = takeCandidate(existingBySemanticKey,
                                     semanticKey(part.assemblyPath, part.name));
        if (!partNode)
            partNode = takeCandidate(existingByIndexName,
                                     indexNameKey(part.partIndex, part.name));

        bool created = false;
        if (!partNode)
        {
            partNode = createGeometryNodeByType(QStringLiteral("geom_step_import_part"),
                                                QStringLiteral("StepImportPart"),
                                                part.name,
                                                part.uuid);
            created = partNode != nullptr;
        }
        if (!partNode)
            continue;

        const QJsonObject previousState = geometryNodeState(partNode);
        const QString previousOriginalName =
            previousState.value(QStringLiteral("originalName")).toString().simplified();

        if (auto* page = dynamic_cast<StepImportPartDialog*>(partNode->getPage()))
        {
            page->setStepPart(importUuid,
                              part.partIndex,
                              part.sourceKey,
                              part.name,
                              part.assemblyPath,
                              part.colorHex);
        }

        QString effectiveTitle = stableNodeName(partNode).trimmed();
        // 未手动重命名的节点跟随 STEP 中的新名称；用户自定义名称继续保留。
        if (created || effectiveTitle.isEmpty() ||
            (!previousOriginalName.isEmpty() && effectiveTitle == previousOriginalName))
        {
            effectiveTitle = part.name;
            partNode->setName(effectiveTitle);
            partNode->setText(0, effectiveTitle);
            if (BasePage* page = partNode->getPage())
                page->setDisplayName(effectiveTitle);
        }

        // 重新导入时优先复用旧节点 UUID，使圆角、布尔、材料等下游引用不失效。
        QString partUuid = geometryNodeUuid(partNode);
        if (partUuid.isEmpty())
            partUuid = part.uuid;

        partNode->setData(0, ProjectNode::RoleKind, QStringLiteral("StepImportPart"));
        partNode->setData(0, kRoleGeometryInputUuids, QStringList{importUuid});
        partNode->setData(0, kRoleGeometryConsumedInputUuids, QStringList{importUuid});

        const QVariant savedVisibility = partNode->data(0, kRoleGeometryVisible);
        const bool partVisible = created || !savedVisibility.isValid()
                                     ? part.visible
                                     : savedVisibility.toBool();
        initializeGeometryNodeVisibility(partNode, partVisible);
        GeometryDisplayManager::setVisible(partUuid, partVisible, false);

        // 只有首次创建、源 STEP 重新读取或运行时 Shape 缺失时才重新注册。
        // 旧逻辑在普通模型树刷新时会无条件重建每个 STEP 子实体的 VTK Actor，
        // 大型 STEP 会因此反复卡顿。
        if (created || importNeedsSync || !hasRuntimeShape(partUuid))
        {
            GeometryBuildUtils::registerShape(partUuid,
                                              effectiveTitle,
                                              new TopoDS_Shape(part.shape));
        }
        ImportGeometryBuilder::applyStepColor(partUuid, part.colorHex);
        if (assemblyRootGroup)
        {
            moveStepPartIntoAssemblyTree(partNode,
                                         assemblyRootGroup,
                                         importUuid,
                                         part.assemblyPath,
                                         assemblyGroupsByPath);
        }
        partNode->setData(0, kRoleGeometryRestoreError, QString());
    }

    // 重新导入后已不存在的实体节点需要移除；使用它们生成的下游结果仍按弱来源规则保留。
    for (ProjectNode* staleNode : std::as_const(unmatchedNodes))
        removeGeometryLeafNode(staleNode);

    // 根导入 Shape 只承担完整文件缓存和恢复，识别成功后显示独立 STEP 实体。
    GeometryDisplayManager::setVisible(importUuid, false, false);
    importNode->setData(0, kRoleGeometryVisible, false);
    importNode->setData(0, ProjectNode::RoleDimmed, true);
    importNode->setCheckState(0, Qt::Unchecked);
    ImportGeometryBuilder::markStepImportSynchronized(importUuid);
}

void GeometryWorkbench::synchronizeAllStepImportPartNodes()
{
    if (!m_geomNode)
        return;

    QList<ProjectNode*> nodes;
    collectGeometryLeafNodes(m_geomNode, nodes);
    QStringList importUuids;
    for (ProjectNode* node : nodes)
    {
        if (node &&
            node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("ImportGeometry"))
        {
            importUuids.push_back(geometryNodeUuid(node));
        }
    }

    for (const QString& uuid : importUuids)
        synchronizeStepImportPartNodes(uuid);
}

void GeometryWorkbench::refreshTreePresentation(const QString& changedUuid)
{
    // GeometryService 在每个 recreate* 成功后都会通知刷新。工程加载期间采用
    // 三阶段恢复，此时页面参数可能尚未装入；若提前刷新，会从空页面 saveJson()
    // 并覆盖节点缓存关系。加载完成后由 loadProject() 统一刷新一次。
    if (m_loadingProject)
        return;

    // registerObject()/setVisible() 的公共显示路径只会调用 refreshTreePresentation()，
    // 不一定能把刚刚 recreate 的 UUID 传回来。V55 只处理 changedUuid 非空的情况，
    // 因而第一次 Apply 时新 Shape 仍可能继承“空节点未勾选”的隐藏状态。
    //
    // 首次真正生成的节点有一个可靠特征：运行时 Shape 已存在，但当前提交历史中
    // 还没有这个 UUID。只对这种节点强制显示；已经提交过、后来被用户主动隐藏的
    // 节点仍然完全保留原来的可见性。
    QSet<QString> committedUuids;
    committedUuids.reserve(m_geometryHistoryCurrent.items.size());
    for (const QJsonValue& value : m_geometryHistoryCurrent.items)
    {
        const QString uuid = FeatureData::fromJson(value.toObject()).uuid;
        if (!uuid.isEmpty())
            committedUuids.insert(uuid);
    }

    auto makeFirstGeneratedNodeVisible = [this, &committedUuids](ProjectNode* node)
    {
        if (!node)
            return;

        const QString uuid = geometryNodeUuid(node);
        if (uuid.isEmpty() || committedUuids.contains(uuid) || !hasRuntimeShape(uuid))
            return;

        const bool oldUpdating = m_updatingGeometryTree;
        m_updatingGeometryTree = true;
        node->setData(0, kRoleGeometryVisible, true);
        node->setData(0, ProjectNode::RoleDimmed, false);
        node->setCheckState(0, Qt::Checked);
        m_updatingGeometryTree = oldUpdating;
        GeometryDisplayManager::show(uuid, false);
    };

    if (!changedUuid.isEmpty())
    {
        ProjectNode* changedNode = findGeometryNodeByUuid(changedUuid);
        if (changedNode)
        {
            updateGeometryNodeDependencyCache(changedNode);
            // “应用”意味着当前特征结果应立即可见。无论它是首次生成还是
            // 对一个曾被隐藏的节点重新计算，都同步树勾选与图形区显示。
            const QString uuid = geometryNodeUuid(changedNode);
            if (!uuid.isEmpty() && hasRuntimeShape(uuid))
            {
                const bool oldUpdating = m_updatingGeometryTree;
                m_updatingGeometryTree = true;
                changedNode->setData(0, kRoleGeometryVisible, true);
                changedNode->setData(0, ProjectNode::RoleDimmed, false);
                changedNode->setCheckState(0, Qt::Checked);
                m_updatingGeometryTree = oldUpdating;
                GeometryDisplayManager::show(uuid, false);
            }
        }
        synchronizeSplitPartNodes(changedUuid);
        synchronizeStepImportPartNodes(changedUuid);
    }
    else
    {
        // GeometryDisplayManager::registerObject() 通过 setVisible(renderNow=true)
        // 进入这里时没有 changedUuid。扫描一次几何树即可找到刚刚首次生成的节点；
        // 这个扫描只发生在一次完整显示刷新中，不会进入鼠标移动等高频路径。
        QList<ProjectNode*> nodes;
        collectGeometryLeafNodes(m_geomNode, nodes);
        for (ProjectNode* node : nodes)
            makeFirstGeneratedNodeVisible(node);
    }
    refreshGeometryNodePresentation();
    // Recreated geometry owns new VTK actors. Reapply the logical selection and
    // source-preview state so colors do not disappear after pressing Generate.
    GeometrySelectionVisuals::refreshHighlights(true);
    commitGeometryHistory(changedUuid);
}

void GeometryWorkbench::deleteNodeTree(ProjectNode* node, bool runBeforeUnload)
{
    if (!node)
        return;

    while (node->childCount() > 0)
    {
        ProjectNode* child = static_cast<ProjectNode*>(node->child(0));
        node->takeChild(0);
        deleteNodeTree(child, runBeforeUnload);
    }

    if (auto* page = node->getPage())
    {
        if (runBeforeUnload)
            page->beforeUnload();
        page->deleteLater();
        node->setPage(nullptr);
    }
    delete node;
}

void GeometryWorkbench::clearGeometryRuntimeState(bool renderNow)
{
    clearEntityHighlight();
    clearTreeSelectionHighlight();
    clearSourcePreviewHighlight();
    GeometryPickManager::instance().clearDomain(QString(), QString(), false);

    if (m_geomNode)
    {
        QList<ProjectNode*> nodes;
        collectGeometryLeafNodes(m_geomNode, nodes);
        for (ProjectNode* node : nodes)
        {
            if (node &&
                node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("ImportGeometry"))
            {
                const QString importUuid = geometryNodeUuid(node);
                ImportGeometryBuilder::clearStepImportParts(importUuid);
                ImportGeometryBuilder::clearStepImportOptions(importUuid);
            }
        }
    }

    const bool oldRenderSuspended = GeometryDisplayManager::isRenderSuspended();
    GeometryDisplayManager::setRenderSuspended(true);
    if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
    {
        const QStringList uuids = mgr->getShapMap().keys();
        for (const QString& uuid : uuids)
            GeometryCleanup::removeExistingGeometry(uuid, false);
    }
    GeometryDisplayManager::clearVisibilityStates();
    GeometryDisplayManager::setRenderSuspended(oldRenderSuspended);

    if (renderNow)
    {
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
}

QStringList GeometryWorkbench::activeGeometryUuids() const
{
    QStringList uuids;
    if (!m_geomNode)
        return uuids;

    QList<ProjectNode*> geomNodes;
    collectGeometryLeafNodes(m_geomNode, geomNodes);

    QSet<QString> consumedUuids;
    for (ProjectNode* node : geomNodes)
    {
        if (!node)
            continue;

        const QString consumerUuid = geometryNodeUuid(node);
        if (!hasRuntimeShape(consumerUuid))
            continue;

        QStringList replacedInputs =
            node->data(0, kRoleGeometryConsumedInputUuids).toStringList();
        if (!node->data(0, kRoleGeometryConsumedInputUuids).isValid())
        {
            const QString kind = node->data(0, ProjectNode::RoleKind).toString();
            replacedInputs = GeometryFeatures::consumedInputUuidsForFinalModel(
                QString(), kind, geometryNodeState(node), consumerUuid);
        }
        for (const QString& inputUuid : replacedInputs)
        {
            if (!inputUuid.isEmpty())
                consumedUuids.insert(inputUuid);
        }
    }

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    for (ProjectNode* node : geomNodes)
    {
        const QString uuid = geometryNodeUuid(node);
        if (uuid.isEmpty() || consumedUuids.contains(uuid) || uuids.contains(uuid))
            continue;
        if (!mgr || !mgr->getGeomShapeObject(uuid))
            continue;

        uuids.push_back(uuid);
    }

    return uuids;
}

ProjectNode* GeometryWorkbench::currentGeometryNode() const
{
    if (!m_currentTreeSelectionUuid.isEmpty())
    {
        if (ProjectNode* node = findGeometryNodeByUuid(m_currentTreeSelectionUuid))
            return node;
    }

    QTreeWidget* tree = m_geomNode ? m_geomNode->treeWidget() : nullptr;
    auto* node = tree ? dynamic_cast<ProjectNode*>(tree->currentItem()) : nullptr;
    if (!node || node == m_geomNode ||
        node->data(0, ProjectNode::RoleModule).toString() != QStringLiteral("Geometry") ||
        !node->getPage())
    {
        return nullptr;
    }
    return node;
}

QString GeometryWorkbench::currentGeometrySelectionUuid() const
{
    const QStringList uuids = currentGeometrySelectionUuids();
    return uuids.isEmpty() ? QString() : uuids.front();
}

QStringList GeometryWorkbench::currentGeometrySelectionUuids() const
{
    QStringList uuids;
    ProjectNode* current = currentGeometryNode();
    if (current)
    {
        const QString uuid = geometryNodeUuid(current);
        if (!uuid.isEmpty())
            uuids.push_back(uuid);
    }

    QTreeWidget* tree = m_geomNode ? m_geomNode->treeWidget() : nullptr;
    if (!tree)
        return uuids;

    for (QTreeWidgetItem* item : tree->selectedItems())
    {
        auto* node = dynamic_cast<ProjectNode*>(item);
        if (!node || node == m_geomNode || !node->getPage() ||
            node->data(0, ProjectNode::RoleModule).toString() != QStringLiteral("Geometry"))
        {
            continue;
        }
        const QString uuid = geometryNodeUuid(node);
        if (!uuid.isEmpty() && !uuids.contains(uuid))
            uuids.push_back(uuid);
    }
    return uuids;
}

QList<QPair<QString, QString>> GeometryWorkbench::geometryObjectChoices() const
{
    QList<QPair<QString, QString>> out;
    if (!m_geomNode)
        return out;

    QList<ProjectNode*> nodes;
    collectGeometryLeafNodes(m_geomNode, nodes);
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    for (ProjectNode* node : nodes)
    {
        const QString uuid = geometryNodeUuid(node);
        GeomShapeObject* object = manager ? manager->getGeomShapeObject(uuid) : nullptr;
        if (!object || !object->getShape() || object->getShape()->IsNull())
            continue;

        QString title = stableNodeName(node);
        if (title.isEmpty())
            title = object->getName();
        if (title.isEmpty())
            title = uuid;
        out.append(qMakePair(uuid, title));
    }
    return out;
}

QString GeometryWorkbench::selectedGeometryUuidForTools() const
{
    return currentGeometrySelectionUuid();
}

void GeometryWorkbench::focusGeometryForTools(const QString& uuid)
{
    if (uuid.isEmpty() || !hasRuntimeShape(uuid))
        return;

    GeometryDisplayManager::show(uuid, false);
    setCurrentTreeSelection(uuid);
    highlightGeometryNode(uuid, true);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        if (ProjectNode* node = pm->findNodeByUuid(uuid))
            pm->setCurrentItem(node);
    }
}

void GeometryWorkbench::openGeometryCheckTool()
{
    if (m_geometryCheckDialog)
    {
        QMetaObject::invokeMethod(m_geometryCheckDialog, "refreshObjects", Qt::QueuedConnection);
        m_geometryCheckDialog->show();
        m_geometryCheckDialog->raise();
        m_geometryCheckDialog->activateWindow();
        return;
    }

    auto* panel = new GeometryCheckDialog(this);
    panel->setAttribute(Qt::WA_DeleteOnClose, true);
    panel->setWindowFlag(Qt::Tool, true);
    panel->setWindowTitle(tr("几何检查与修复"));
    panel->resize(560, 640);
    m_geometryCheckDialog = panel;
    connect(panel, &QObject::destroyed, this,
            [this]
            {
                m_geometryCheckDialog = nullptr;
            });
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void GeometryWorkbench::openGeometryMeasureTool()
{
    if (m_geometryMeasureDialog)
    {
        QMetaObject::invokeMethod(m_geometryMeasureDialog, "refreshObjects", Qt::QueuedConnection);
        m_geometryMeasureDialog->show();
        m_geometryMeasureDialog->raise();
        m_geometryMeasureDialog->activateWindow();
        return;
    }

    auto* panel = new GeometryMeasureDialog(this);
    panel->setAttribute(Qt::WA_DeleteOnClose, true);
    panel->setWindowFlag(Qt::Tool, true);
    panel->setWindowTitle(tr("几何测量"));
    panel->resize(620, 620);
    m_geometryMeasureDialog = panel;
    connect(panel, &QObject::destroyed, this,
            [this]
            {
                m_geometryMeasureDialog = nullptr;
            });
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void GeometryWorkbench::closeProject()
{
    m_savedBrepAssets.clear();
    m_exportedGeometryShapes.clear();
    if (m_geometryCheckDialog)
        m_geometryCheckDialog->close();
    if (m_geometryMeasureDialog)
        m_geometryMeasureDialog->close();

    // 页面先结束预览、拾取和延时任务，再拆除其依赖的 CAD actor。
    // deleteNodeTree() 也调用 beforeUnload，钩子必须保持幂等。
    if (m_geomNode)
    {
        QList<ProjectNode*> nodes;
        collectGeometryLeafNodes(m_geomNode, nodes);
        for (ProjectNode* node : nodes)
        {
            if (node && node->getPage())
                node->getPage()->beforeUnload();
        }
    }
    if (m_gridNode)
    {
        QList<ProjectNode*> nodes;
        collectGeometryLeafNodes(m_gridNode, nodes);
        for (ProjectNode* node : nodes)
        {
            if (node && node->getPage())
                node->getPage()->beforeUnload();
        }
    }

    clearGeometryRuntimeState(false);

    while (m_geomNode && m_geomNode->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_geomNode->child(0));
        m_geomNode->takeChild(0);
        deleteNodeTree(node, false);
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

    if (m_geomNode)
    {
        delete m_geomNode;
        m_geomNode = nullptr;
    }
    m_geometryGroups.clear();
    m_currentTreeSelectionUuid.clear();
    m_sourcePreviewUuids.clear();

    if (m_gridNode)
    {
        delete m_gridNode;
        m_gridNode = nullptr;
    }

    m_geometryUndoHistory.clear();
    m_geometryRedoHistory.clear();
    m_geometryHistoryCurrent = GeometryHistorySnapshot{};

    m_nameCounters.clear();
    m_geometryGroups.clear();
}


// The normal "final model" exporter intentionally excludes sketches consumed
// by extrusion. That is correct for 3D solids, but incorrect for an explicitly
// approved DXF 2D region: the sketch face must be fed to the 2D mesher instead
// of the solid's complete skin. Visibility check boxes are not mesh scopes.
bool GeometryWorkbench::prepareMeshingGeometry(int dimension, QString& outputPath,
                                                QString& diagnostic)
{
    outputPath.clear();
    diagnostic.clear();
    const QString dataDir = MeshWorkingPaths::dataDir();
    if (!MeshWorkingPaths::prepare() || !QDir().mkpath(dataDir))
    {
        diagnostic = tr("无法创建网格几何目录：%1").arg(dataDir);
        return false;
    }
    if (dimension == 3)
    {
        const QString brep = QDir(dataDir).filePath(QStringLiteral("ExportedModel.brep"));
        generateStepFile();
        if (!QFileInfo::exists(brep) || QFileInfo(brep).size() <= 0) {
            diagnostic = tr("三维网格几何导出失败：没有生成有效的 BREP 文件。");
            return false;
        }
        outputPath = brep;
        diagnostic = tr("三维网格：使用原生 BREP 拓扑编号。");
        return true;
    }
    if (dimension != 2)
    {
        diagnostic = tr("不支持的网格维度：%1").arg(dimension);
        return false;
    }

    const QString brep = QDir(dataDir).filePath(QStringLiteral("ExportedMesh2D.brep"));
    const QString temporary = brep + QStringLiteral(".tmp");
    QFile::remove(brep);
    QFile::remove(temporary);
    QFile::remove(SurfaceDomainData::fileName(brep));
    try
    {
        auto* mgr = GeomManager::GeomManagerInstance::getInstance();
        if (!mgr || !m_geomNode)
        {
            diagnostic = tr("尚未创建可用于二维网格的几何模型。");
            return false;
        }
        QList<ProjectNode*> nodes;
        collectGeometryLeafNodes(m_geomNode, nodes);
        TopTools_IndexedMapOfShape explicitlyApprovedFaces;
        TopTools_IndexedMapOfShape ordinaryFaces;
        TopTools_IndexedMapOfShape ordinaryShells;
        TopTools_IndexedMapOfShape consumedSketchFaces;
        TopTools_IndexedMapOfShape solidSkinFaces;
        int regionSketches = 0;
        int terrainSurfaces = 0;
        TopTools_IndexedMapOfShape terrainDomainFaces;
        QVector<int> terrainFaceOwners;
        SurfaceDomainData::Definition domainData;
        int nextDomain=0,nextMaterialGroup=0;
        QStringList pendingTerrains;
        int skippedCurveObjects = 0;
        QSet<QString> active;
        for (const QString& uuid : activeGeometryUuids())
            active.insert(uuid);
        // Do not rely on the global final-model list for approved sketches:
        // Extrude consumes a profile and deliberately hides it from that list.
        for (ProjectNode* node : nodes)
        {
            if (!node)
                continue;
            const QString uuid = geometryNodeUuid(node);
            const QJsonObject state = geometryNodeState(node);
            const QString kind = node->data(0, ProjectNode::RoleKind).toString();
            const bool isApproved = state.value(QStringLiteral("planarFaceMode")).toBool(false);

            if (kind == QStringLiteral("TerrainSurface") &&
                !node->data(0, kRoleGeometryConsumed).toBool())
            {
                if (!state.value(QStringLiteral("generated")).toBool())
                {
                    pendingTerrains.append(node->text(0));
                    continue;
                }
                // Rendering actors and old BREP caches are not authoritative
                // modelling data. Reconstruct the approved terrain surface from
                // the saved inputs, just as approved sketch regions are rebuilt.
                const auto terrain = GeometryTerrain::TerrainSurfaceBuilder::build(
                    GeometryTerrain::TerrainSurfaceBuilder::sourcesFromState(state),
                    state.value(QStringLiteral("spacing")).toDouble(5),
                    GeometryTerrain::TerrainSurfaceBuilder::domainMode(state),
                    GeometryTerrain::TerrainSurfaceBuilder::gridOptions(state));
                if (!terrain.ok())
                {
                    diagnostic = tr("地形 %1 无法准备为网格面：%2").arg(node->text(0), terrain.error);
                    return false;
                }
                TopTools_IndexedMapOfShape faces, shells;
                TopExp::MapShapes(terrain.shape, TopAbs_FACE, faces);
                TopExp::MapShapes(terrain.shape, TopAbs_SHELL, shells);
                if (faces.IsEmpty() || shells.IsEmpty())
                {
                    diagnostic = tr("地形 %1 的构建结果不含完整的 CAD 地形面，请重新构建地形面。")
                                     .arg(node->text(0));
                    return false;
                }
                for (int i = 1; i <= faces.Extent(); ++i) ordinaryFaces.Add(faces(i));
                for (int i = 1; i <= shells.Extent(); ++i) ordinaryShells.Add(shells(i));
                const int materialGroup=++nextMaterialGroup;
                QMap<int,int> domainIds;
                for(const auto& domain:terrain.domains)
                {
                    const int id=++nextDomain;domainIds[domain.id]=id;
                    domainData.names[id]=node->text(0)+QStringLiteral(" / ")+domain.name;
                    domainData.materialGroups[id]=materialGroup;
                }
                for(int i=1;i<=faces.Extent();++i)
                {
                    terrainDomainFaces.Add(faces(i));
                    terrainFaceOwners.append(domainIds.value(terrain.faceDomains.value(i-1)));
                }
                ++terrainSurfaces;
                continue;
            }
            // A confirmed sketch region is sourced from its saved selected
            // documents, not solely from GeomManager's cached shape. Cache
            // restoration can leave a wire-only runtime object even though
            // the sketch's approval state and preview are already restored.
            // Never silently fall back to unrelated faces in this case.
            if (isApproved &&
                node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("Sketch2D"))
            {
                auto* sketch = dynamic_cast<Sketch2DDialog*>(node->getPage());
                if (!sketch)
                {
                    diagnostic = tr("已确认构面草图 %1 的编辑数据不可用。").arg(node->text(0));
                    return false;
                }
                TopoDS_Shape approvedShape;
                QString regionDiagnostic;
                if (!sketch->buildApprovedMeshingFaces(approvedShape, regionDiagnostic))
                {
                    diagnostic = tr("草图 %1 的二维构面无法恢复：%2")
                                     .arg(node->text(0), regionDiagnostic);
                    return false;
                }
                TopTools_IndexedMapOfShape faces;
                TopExp::MapShapes(approvedShape, TopAbs_FACE, faces);
                if (faces.Extent() == 0)
                {
                    diagnostic = tr("草图 %1 已记录构面，但重建结果不含 OCC 二维面。")
                                     .arg(node->text(0));
                    return false;
                }
                for (int i = 1; i <= faces.Extent(); ++i)
                    explicitlyApprovedFaces.Add(faces(i));
                ++regionSketches;
                continue;
            }

            auto* obj = mgr->getGeomShapeObject(uuid);
            if (!obj || !obj->getShape() || obj->getShape()->IsNull())
                continue;
            const TopoDS_Shape& shape = *obj->getShape();
            if (isApproved)
            {
                TopTools_IndexedMapOfShape faces;
                TopExp::MapShapes(shape, TopAbs_FACE, faces);
                for (int i = 1; i <= faces.Extent(); ++i)
                    explicitlyApprovedFaces.Add(faces(i));
                if (faces.Extent() > 0)
                    ++regionSketches;
                continue;
            }
            if (!active.contains(uuid))
            {
                // A normal completed sketch can also be a valid 2D mesh source,
                // even when a later extrusion consumed it in the final 3D model.
                if (node->data(0, ProjectNode::RoleKind).toString() ==
                    QStringLiteral("Sketch2D"))
                {
                    TopTools_IndexedMapOfShape faces;
                    TopExp::MapShapes(shape, TopAbs_FACE, faces);
                    for (int i = 1; i <= faces.Extent(); ++i)
                        consumedSketchFaces.Add(faces(i));
                }
                continue;
            }
            TopTools_IndexedMapOfShape solids;
            TopExp::MapShapes(shape, TopAbs_SOLID, solids);
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(shape, TopAbs_FACE, faces);
            if (solids.Extent() > 0)
            {
                for (int i = 1; i <= faces.Extent(); ++i)
                    solidSkinFaces.Add(faces(i));
            }
            else if (faces.Extent() > 0)
            {
                for (int i = 1; i <= faces.Extent(); ++i)
                    ordinaryFaces.Add(faces(i));
                // A terrain shell is one surface domain. Keep that container
                // in the BREP instead of flattening all its facets into regions.
                TopTools_IndexedMapOfShape shells;
                TopExp::MapShapes(shape, TopAbs_SHELL, shells);
                for (int i = 1; i <= shells.Extent(); ++i)
                    ordinaryShells.Add(shells(i));
            }
            else
                ++skippedCurveObjects;
        }

        const TopTools_IndexedMapOfShape* selectedFaces = nullptr;
        QString source;
        if (explicitlyApprovedFaces.Extent() > 0)
        {
            selectedFaces = &explicitlyApprovedFaces;
            source = tr("%1 个已确认构面草图").arg(regionSketches);
        }
        else if (ordinaryFaces.Extent() > 0)
        {
            selectedFaces = &ordinaryFaces;
            source = terrainSurfaces > 0
                ? tr("%1 个已构建地形面及独立二维面").arg(terrainSurfaces)
                : tr("已有二维面（未发现已确认的线网构面）");
        }
        else if (consumedSketchFaces.Extent() > 0)
        {
            selectedFaces = &consumedSketchFaces;
            source = tr("已被三维特征消耗的原始二维草图面");
        }
        else if (solidSkinFaces.Extent() > 0)
        {
            // Keep the existing "surface of a solid" workflow working.
            selectedFaces = &solidSkinFaces;
            source = tr("三维实体的外表面（未找到独立二维面）");
        }
        if (!selectedFaces)
        {
            diagnostic = pendingTerrains.isEmpty()
                ? tr("没有可用于二维表面网格的面。请先生成草图二维面或构建地形面。")
                : tr("地形 %1 尚未构建或输入已修改。请在等高线地形页面点击“构建地形面”，再划分二维表面网格。")
                      .arg(pendingTerrains.join(QStringLiteral("、")));
            return false;
        }
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        TopTools_IndexedMapOfShape shellFaces;
        if (selectedFaces == &ordinaryFaces)
        {
            for (int i = 1; i <= ordinaryShells.Extent(); ++i)
            {
                builder.Add(compound, ordinaryShells(i));
                TopExp::MapShapes(ordinaryShells(i), TopAbs_FACE, shellFaces);
            }
        }
        int badFaces = 0;
        for (int i = 1; i <= selectedFaces->Extent(); ++i)
        {
            const TopoDS_Shape& face = (*selectedFaces)(i);
            if (!BRepCheck_Analyzer(face, Standard_True).IsValid())
            {
                ++badFaces;
                continue;
            }
            if (!shellFaces.Contains(face)) builder.Add(compound, face);
        }
        if (badFaces > 0)
        {
            diagnostic = tr("有 %1 个无效几何面，请检查交叉、重叠或微小区域。")
                             .arg(badFaces);
            return false;
        }
        if (!BRepTools::Write(compound, QFile::encodeName(temporary).constData()) ||
            !QFileInfo::exists(temporary) || QFileInfo(temporary).size() <= 0 ||
            !QFile::rename(temporary, brep))
        {
            QFile::remove(temporary);
            diagnostic = tr("二维网格几何准备失败。");
            return false;
        }
        outputPath = brep;
        if(selectedFaces==&ordinaryFaces&&terrainSurfaces>0)
        {
            TopTools_IndexedMapOfShape exportedFaces;TopExp::MapShapes(compound,TopAbs_FACE,exportedFaces);
            QHash<int,int> defaultShellOwners;
            for(int i=1;i<=ordinaryShells.Extent();++i)
            {
                TopTools_IndexedMapOfShape faces;TopExp::MapShapes(ordinaryShells(i),TopAbs_FACE,faces);
                int id=0;
                for(int f=1;f<=faces.Extent();++f)if(!terrainDomainFaces.Contains(faces(f)))
                {
                    if(!id){id=++nextDomain;domainData.materialGroups[id]=++nextMaterialGroup;domainData.names[id]=tr("独立面域 %1").arg(id);}
                    defaultShellOwners[exportedFaces.FindIndex(faces(f))]=id;
                }
            }
            for(int i=1;i<=exportedFaces.Extent();++i)
            {
                const int terrainIndex=terrainDomainFaces.FindIndex(exportedFaces(i));
                int id=terrainIndex?terrainFaceOwners[terrainIndex-1]:defaultShellOwners.value(i);
                if(!id){id=++nextDomain;domainData.materialGroups[id]=++nextMaterialGroup;domainData.names[id]=tr("独立面域 %1").arg(id);}
                domainData.faceDomains.append(id);
            }
            if(!SurfaceDomainData::write(brep,domainData,diagnostic))
            {QFile::remove(brep);outputPath.clear();return false;}
        }
        diagnostic = tr("二维网格几何准备完成：%1，%2 个面。未选的草图线及无关实体未混入；另有 %3 个独立曲线对象未参与。采用 OCC BREP 传递面拓扑。")
                         .arg(source).arg(selectedFaces->Extent()).arg(skippedCurveObjects);
        return true;
    }
    catch (const Standard_Failure& e)
    {
        const char* msg = e.GetMessageString();
        diagnostic = tr("二维网格几何导出失败：%1")
                         .arg(msg ? QString::fromLocal8Bit(msg) : tr("OCC 异常"));
    }
    catch (const std::exception& e)
    {
        diagnostic = tr("二维网格几何导出失败：%1").arg(QString::fromLocal8Bit(e.what()));
    }
    QFile::remove(temporary);
    QFile::remove(brep);
    return false;
}

void GeometryWorkbench::generateStepFile()
{
    const QString dataDir = MeshWorkingPaths::dataDir();
    const QString stepPath = QDir(dataDir).filePath("ExportedModel.step");
    const QString stepTemporaryPath = stepPath + QStringLiteral(".tmp");
    const QString brepPath = QDir(dataDir).filePath("ExportedModel.brep");
    const QString brepTemporaryPath = brepPath + QStringLiteral(".tmp");

    QFile::remove(stepTemporaryPath);
    QFile::remove(brepTemporaryPath);

    try
    {
        if (!MeshWorkingPaths::prepare() || !QDir().mkpath(dataDir))
            return;

        GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
        if (!mgr)
            return;

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);

        bool hasShape = false;
        const QStringList uuids = activeGeometryUuids();

        // Export one occurrence of every final Solid.  Boolean/Split features may
        // expose both a result compound and child result nodes in the runtime tree;
        // adding both objects verbatim duplicates the same Solid in ExportedModel.brep.
        // The mesh UI, however, uses a unique TopExp Solid id space.  Flattening here
        // keeps Geometry preview, Sweep ids and the BREP consumed by SMESH identical.
        TopTools_IndexedMapOfShape uniqueSolids;
        QVector<TopoDS_Shape> nonSolidShapes;
        for (const QString& uuid : uuids)
        {
            GeomShapeObject* shapeObj = mgr->getGeomShapeObject(uuid);
            if (!shapeObj || !shapeObj->getShape() || shapeObj->getShape()->IsNull())
                continue;

            TopTools_IndexedMapOfShape localSolids;
            TopExp::MapShapes(*shapeObj->getShape(), TopAbs_SOLID, localSolids);
            if (localSolids.Extent() > 0)
            {
                for (int i = 1; i <= localSolids.Extent(); ++i)
                    uniqueSolids.Add(localSolids(i));
            }
            else
            {
                nonSolidShapes.push_back(*shapeObj->getShape());
            }
        }
        for (int i = 1; i <= uniqueSolids.Extent(); ++i)
        {
            builder.Add(compound, uniqueSolids(i));
            hasShape = true;
        }
        for (const TopoDS_Shape& shape : std::as_const(nonSolidShapes))
        {
            builder.Add(compound, shape);
            hasShape = true;
        }

        if (!hasShape) {
            QFile::remove(stepPath); QFile::remove(brepPath);
            m_exportedGeometryShapes.clear();
            return;
        }
        QVector<TopoDS_Shape> exportShapes;
        for (int i=1;i<=uniqueSolids.Extent();++i) exportShapes.append(uniqueSolids(i));
        exportShapes += nonSolidShapes;
        bool unchanged = exportShapes.size() == m_exportedGeometryShapes.size();
        for (qsizetype i=0;unchanged && i<exportShapes.size();++i)
            unchanged = exportShapes[i].IsEqual(m_exportedGeometryShapes[i]);
        const QFileInfo previous(brepPath);
        if (unchanged && previous.isFile() && previous.size() == m_exportedGeometryBytes
            && previous.lastModified().toMSecsSinceEpoch() == m_exportedGeometryModifiedMsecs
            && QFileInfo::exists(brepPath + QStringLiteral(".shape-index.json"))
            && QFileInfo::exists(stepPath)) return;
        QFile::remove(stepPath); QFile::remove(brepPath);
        QFile::remove(brepPath + QStringLiteral(".shape-index.json"));
        QFile::remove(brepPath + QStringLiteral(".finalized.brep"));
        QFile::remove(brepPath + QStringLiteral(".finalized.brep.topology-map.json"));
        m_exportedGeometryShapes.clear();

        // BREP 保留给需要直接使用 OCC 几何的网格功能；不做额外预检、修复或诊断快照。
        if (BRepTools::Write(compound, brepTemporaryPath.toLocal8Bit().constData()))
        {
            if (!QFile::rename(brepTemporaryPath, brepPath))
            {
                QFile::remove(brepTemporaryPath);
                qWarning() << "Failed to publish meshing BREP file:" << brepPath;
            }
        }
        else
        {
            QFile::remove(brepTemporaryPath);
            qWarning() << "Failed to generate meshing BREP file:" << brepPath;
        }

        if (!writeJsonFile(brepPath + QStringLiteral(".shape-index.json"),
                           GeometryEntityTools::meshingShapeIndexMap(uuids,compound))) {
            QFile::remove(brepPath);
            qWarning() << "Cannot publish explicit meshing topology indices";
            return;
        }
        const QFileInfo exported(brepPath);
        if (!exported.isFile() || exported.size() <= 0) return;
        m_exportedGeometryShapes = exportShapes;
        m_exportedGeometryBytes = exported.size();
        m_exportedGeometryModifiedMsecs = exported.lastModified().toMSecsSinceEpoch();

        // STEP remains available for exchange; meshing consumes the indexed BREP.
        STEPControl_Writer stepWriter;
        IFSelect_ReturnStatus transferStatus =
            stepWriter.Transfer(compound, STEPControl_ManifoldSolidBrep);
        if (transferStatus != IFSelect_RetDone)
            transferStatus = stepWriter.Transfer(compound, STEPControl_AsIs);
        if (transferStatus != IFSelect_RetDone)
            return;

        if (stepWriter.Write(stepTemporaryPath.toLocal8Bit().constData()) != IFSelect_RetDone)
        {
            QFile::remove(stepTemporaryPath);
            qWarning() << "Failed to generate solver STEP file:" << stepPath;
            return;
        }

        if (!QFile::rename(stepTemporaryPath, stepPath))
        {
            QFile::remove(stepTemporaryPath);
            qWarning() << "Failed to publish solver STEP file:" << stepPath;
        }
    }
    catch (const Standard_Failure& failure)
    {
        QFile::remove(stepTemporaryPath);
        QFile::remove(brepTemporaryPath);
        qWarning() << "Failed to generate geometry exchange files:"
                   << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
    }
    catch (const std::exception& error)
    {
        QFile::remove(stepTemporaryPath);
        QFile::remove(brepTemporaryPath);
        qWarning() << "Failed to generate geometry exchange files:" << error.what();
    }
}

bool GeometryWorkbench::writeJsonFile(const QString& filePath, const QJsonObject& obj)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool GeometryWorkbench::readJsonFile(const QString& filePath, QJsonObject& out)
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

void GeometryWorkbench::initRibbonActionConn()
{
    auto& em = EventManager::instance();
    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId, this,
                           [this, fn](const QVariantMap&)
                           {
                               fn();
                           });
    };

    reg(PublicConstants::kActionPreprocessImportGeometry,
        [this]
        {
            newGeometryWidget("ImportGeometry", PublicConstants::kPagePreprocessImportGeometry, QObject::tr("导入几何"));
        });
    reg(PublicConstants::kActionPreprocessExportGeometry,
        [this]
        {
            exportGeom();
        });

    reg(PublicConstants::kActionPreprocessBox,
        [this]
        {
            newGeometryWidget("Box", PublicConstants::kPagePreprocessBox, QObject::tr("长方体"));
        });
    reg(PublicConstants::kActionPreprocessSphere,
        [this]
        {
            newGeometryWidget("Sphere", PublicConstants::kPagePreprocessSphere, QObject::tr("球体"));
        });
    reg(PublicConstants::kActionPreprocessCone,
        [this]
        {
            newGeometryWidget("Cone", PublicConstants::kPagePreprocessCone, QObject::tr("圆锥体"));
        });
    reg(PublicConstants::kActionPreprocessCylinder,
        [this]
        {
            newGeometryWidget("Cylinder", PublicConstants::kPagePreprocessCylinder, QObject::tr("圆柱体"));
        });
    reg(PublicConstants::kActionPreprocessTorus,
        [this]
        {
            newGeometryWidget("Torus", PublicConstants::kPagePreprocessTorus, QObject::tr("圆环体"));
        });
    reg(PublicConstants::kActionPreprocessGravityDam,
        [this]
        {
            newGeometryWidget("GravityDam", PublicConstants::kPagePreprocessGravityDam,
                              QObject::tr("重力坝"));
        });
    reg(PublicConstants::kActionPreprocessEarthRockfillDam,
        [this]
        {
            newGeometryWidget("EarthRockfillDam",
                              PublicConstants::kPagePreprocessEarthRockfillDam,
                              QObject::tr("土石坝"));
        });
    reg(PublicConstants::kActionPreprocessSlopeTemplate,
        [this]
        {
            newGeometryWidget("SlopeTemplate", PublicConstants::kPagePreprocessSlopeTemplate,
                              QObject::tr("边坡"));
        });
    reg(PublicConstants::kActionPreprocessCavernTemplate,
        [this]
        {
            newGeometryWidget("CavernTemplate", PublicConstants::kPagePreprocessCavernTemplate,
                              QObject::tr("洞室"));
        });


    em.registerHandler(PublicConstants::kActionGeomSketch2D, this,
                       [this](const QVariantMap& data)
                       {
                           newGeometryWidget("Sketch2D", PublicConstants::kPageGeomSketch2D,
                                             QObject::tr("二维草图"),
                                             data.value(QStringLiteral("initialSketchTool")).toString());
                       });
    reg(PublicConstants::kActionGeomTerrainSurface, [this]
        { newGeometryWidget("TerrainSurface", PublicConstants::kPageGeomTerrainSurface, tr("等高线地形")); });

    reg(PublicConstants::kActionGeomCreatePlanarCircle,
        [this]
        {
            newGeometryWidget("Circle", PublicConstants::kPagePreprocessCircle, QObject::tr("圆"));
        });
    reg(PublicConstants::kActionGeomCreateEllipse,
        [this]
        {
            newGeometryWidget("Ellipse", PublicConstants::kPagePreprocessEllipse, QObject::tr("椭圆"));
        });
    reg(PublicConstants::kActionGeomCreatePlanarPolygon,
        [this]
        {
            newGeometryWidget("Polygon", PublicConstants::kPagePreprocessPolygon, QObject::tr("多边形"));
        });
    reg(PublicConstants::kActionGeomCreatePlanarRect,
        [this]
        {
            newGeometryWidget("Rectangle", PublicConstants::kPagePreprocessRectangle, QObject::tr("矩形"));
        });

    reg(PublicConstants::kActionGeomAddExtrude,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            newExtrudeWidget(node && isExtrudableProfileNode(node) ? geometryNodeUuid(node) : QString());
        });
    reg(PublicConstants::kActionGeomAddRevolve,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            newRevolveWidget(node && isExtrudableProfileNode(node) ? geometryNodeUuid(node) : QString());
        });
    reg(PublicConstants::kActionGeomAddSweep,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            const QString uuid = node ? geometryNodeUuid(node) : QString();
            if (node && isExtrudableProfileNode(node))
                newSweepWidget(uuid, QString());
            else
                newSweepWidget(QString(), uuid);
        });
    reg(PublicConstants::kActionGeomAddLoft,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            newLoftWidget(node && isExtrudableProfileNode(node) ? geometryNodeUuid(node) : QString());
        });
    reg(PublicConstants::kActionGeomAddCutExtrude,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            const QString uuid = node ? geometryNodeUuid(node) : QString();
            if (node && isExtrudableProfileNode(node))
                newCutExtrudeWidget(uuid, QString());
            else
                newCutExtrudeWidget(QString(), uuid);
        });

    reg(PublicConstants::kActionGeomAddPattern,
        [this]
        {
            newPatternWidget(currentGeometrySelectionUuid());
        });

    reg(PublicConstants::kActionGeomAddTransform,
        [this]
        {
            newTransformWidget(currentGeometrySelectionUuid());
        });
    reg(PublicConstants::kActionGeomAddMirror,
        [this]
        {
            newMirrorWidget(currentGeometrySelectionUuid());
        });
    reg(PublicConstants::kActionGeomAddFillet,
        [this]
        {
            newFilletWidget(currentGeometrySelectionUuid());
        });
    reg(PublicConstants::kActionGeomAddChamfer,
        [this]
        {
            newChamferWidget(currentGeometrySelectionUuid());
        });
    reg(PublicConstants::kActionGeomAddSplit,
        [this]
        {
            newSplitWidget(currentGeometrySelectionUuid(), QString());
        });

    reg(PublicConstants::kActionGeomFuse,
        [this]
        {
            newGeometryWidget("BooleanFuse", PublicConstants::kPagePostProcessOccBoolean, QObject::tr("并集"));
        });
    reg(PublicConstants::kActionGeomUnion,
        [this]
        {
            newGeometryWidget("BooleanFuse", PublicConstants::kPagePostProcessOccBoolean, QObject::tr("并集"));
        });
    reg(PublicConstants::kActionGeomCut,
        [this]
        {
            newGeometryWidget("BooleanCut", PublicConstants::kPagePostProcessOccBoolean, QObject::tr("差集"));
        });
    reg(PublicConstants::kActionGeomCommon,
        [this]
        {
            newGeometryWidget("BooleanCommon", PublicConstants::kPagePostProcessOccBoolean, QObject::tr("交集"));
        });

    reg(PublicConstants::kActionGeomUndo,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            if (node && node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("Sketch2D"))
                return;
            undoGeometryChange();
        });
    reg(PublicConstants::kActionGeomRedo,
        [this]
        {
            ProjectNode* node = currentGeometryNode();
            if (node && node->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("Sketch2D"))
                return;
            redoGeometryChange();
        });

    reg(PublicConstants::kActionGeomCheckRepair,
        [this]
        {
            openGeometryCheckTool();
        });
    reg(PublicConstants::kActionGeomMeasure,
        [this]
        {
            openGeometryMeasureTool();
        });
}

void GeometryWorkbench::newGeometryWidget(const QString& kind, const QString& pageType,
                                          const QString& defaultTitle,
                                          const QString& initialSketchTool)
{
    if (!m_geomNode)
    {
        QMessageBox::information(nullptr, tr("尚未打开工程"),
                                 tr("请先新建或打开工程，再创建几何对象。"));
        return;
    }

    const QString nodeKind = GeometryFeatures::normalizeKind(pageType, kind);
    if (nodeKind == QStringLiteral("ImportGeometry"))
    {
        importGeometryFile();
        return;
    }

    BasePage* w = GeometryFeatures::createPage(pageType, kind);
    if (w)
        GeometryPanelUi::apply(w);
    if (!w)
        return;

    if (auto* sketch = dynamic_cast<Sketch2DDialog*>(w))
    {
        if (initialSketchTool == QStringLiteral("point"))
            sketch->setInitialCommand(GeometrySketch::Command::Point);
        else if (initialSketchTool == QStringLiteral("line"))
            sketch->setInitialCommand(GeometrySketch::Command::Polyline);
    }

    // 布尔页面自动带入当前模型树对象。用户只需再选择另一个对象，
    // 不必重新在长列表中寻找第一个输入。
    if (nodeKind == QStringLiteral("BooleanFuse") ||
        nodeKind == QStringLiteral("BooleanCut") ||
        nodeKind == QStringLiteral("BooleanCommon"))
    {
        const QStringList selectedUuids = currentGeometrySelectionUuids();
        if (!selectedUuids.isEmpty())
        {
            QJsonArray selected;
            for (const QString& selectedUuid : selectedUuids)
                selected.append(selectedUuid);
            QJsonObject initialState;
            initialState[QStringLiteral("selectedKeys")] = selected;
            initialState[QStringLiteral("operationKeys")] = selected;
            w->loadJson(initialState);
        }
    }

    QString uuid = w->getUuid();
    QString displayName;
    if (nodeKind == "Sketch2D")
    {
        const int count = m_nameCounters.value(nodeKind, 0) + 1;
        m_nameCounters.insert(nodeKind, count);
        displayName = tr("草图 %1").arg(count);
    }
    else
    {
        displayName = makeUniqueName(defaultTitle, nodeKind.isEmpty() ? kind : nodeKind);
    }
    ProjectNode* node = new ProjectNode(displayName, uuid, w);
    clearGeometryNodeDecoration(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, nodeKind.isEmpty() ? kind : nodeKind);
    node->setFlags(node->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    initializeGeometryNodeVisibility(node, false);
    node->setData(0, kRoleGeometryInputUuids, QStringList());
    node->setData(0, kRoleGeometryConsumedInputUuids, QStringList());
    node->setData(0, kRoleGeometryDerivedCount, 0);
    node->setData(0, kRoleGeometryMissingInputCount, 0);
    addGeometryNodeToTree(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
    refreshGeometryNodePresentation();
    // 新节点保持未勾选时不会再依赖 itemChanged 顺带触发重绘。
    // 页面切换（尤其退出草图模式）后必须显式刷新显示窗口。
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();

    // 参数化节点在真正拥有 Shape 之前只是一个编辑占位节点。树插入/样式刷新若
    // 曾经把“未勾选”同步进 DisplayManager，会让第一次 registerObject() 继承 hidden。
    // 交互式新建节点的 UUID 是全新的，因此这里清掉空节点阶段的显示缓存最安全；
    // 工程加载走 createGeometryNodeByType()，不会经过这里，所以不会丢失保存的显隐。
    if (nodeKind != QStringLiteral("Sketch2D") && !hasRuntimeShape(uuid))
        GeometryDisplayManager::forgetVisibilityState(uuid);

    // 草图节点本身就是一个立即创建的编辑文档；其他参数化节点要等首次“应用”
    // 后再作为一次完整历史记录提交，避免撤销一次只回到空节点。
    if (nodeKind == QStringLiteral("Sketch2D"))
        commitGeometryHistory(uuid);
}

ProjectNode* GeometryWorkbench::createOperationNode(BasePage* page,
                                                       const QString& kind,
                                                       const QString& defaultTitle,
                                                       bool selectNode)
{
    if (!m_geomNode || !page)
    {
        delete page;
        return nullptr;
    }

    const QString uuid = page->getUuid();
    const QString displayName = makeUniqueName(defaultTitle, kind);
    auto* node = new ProjectNode(displayName, uuid, page);
    clearGeometryNodeDecoration(node);
    page->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind, kind);
    node->setFlags(node->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    initializeGeometryNodeVisibility(node, false);
    node->setData(0, kRoleGeometryInputUuids, QStringList());
    node->setData(0, kRoleGeometryConsumedInputUuids, QStringList());
    node->setData(0, kRoleGeometryDerivedCount, 0);
    node->setData(0, kRoleGeometryMissingInputCount, 0);
    addGeometryNodeToTree(node);

    if (selectNode)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
    refreshGeometryNodePresentation();
    // 操作节点尚未生成结果时保持未勾选，但不能因此跳过页面切换后的重绘。
    // 这样来源节点只要仍为勾选状态，就会继续在视图中显示。
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();

    // 与 newGeometryWidget() 相同：操作节点尚未生成 Shape 时不应留下逻辑 hidden
    // 缓存，否则拉伸/旋转/扫掠等第一次 Apply 注册出来的 actor 会直接不可见。
    if (!hasRuntimeShape(uuid))
        GeometryDisplayManager::forgetVisibilityState(uuid);

    return node;
}

void GeometryWorkbench::newExtrudeWidget(const QString& profileUuid)
{
    auto* page = dynamic_cast<ExtrudeDialog*>(createPageByType(PublicConstants::kPageGeomExtrude, tr("拉伸")));
    if (!page)
        return;
    if (!profileUuid.isEmpty())
        page->setInitialProfile(profileUuid);
    createOperationNode(page, QStringLiteral("Extrude"), tr("拉伸"));
}

void GeometryWorkbench::newRevolveWidget(const QString& sourceUuid)
{
    auto* page = dynamic_cast<RevolveDialog*>(createPageByType(PublicConstants::kPageGeomRevolve, tr("旋转生成")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    createOperationNode(page, QStringLiteral("Revolve"), tr("旋转生成"));
}

void GeometryWorkbench::newSweepWidget(const QString& profileUuid, const QString& pathUuid)
{
    auto* page = dynamic_cast<SweepDialog*>(createPageByType(PublicConstants::kPageGeomSweep, tr("扫掠")));
    if (!page)
        return;
    if (!profileUuid.isEmpty())
        page->setInitialProfile(profileUuid);
    if (!pathUuid.isEmpty())
        page->setInitialPath(pathUuid);
    createOperationNode(page, QStringLiteral("Sweep"), tr("扫掠"));
}

void GeometryWorkbench::newLoftWidget(const QString& initialProfileUuid)
{
    auto* page = dynamic_cast<LoftDialog*>(createPageByType(PublicConstants::kPageGeomLoft, tr("放样")));
    if (!page)
        return;
    if (!initialProfileUuid.isEmpty())
        page->addInitialProfile(initialProfileUuid);
    createOperationNode(page, QStringLiteral("Loft"), tr("放样"));
}

void GeometryWorkbench::newCutExtrudeWidget(const QString& profileUuid, const QString& targetUuid)
{
    auto* page = dynamic_cast<CutExtrudeDialog*>(createPageByType(QStringLiteral("cut_extrude"), tr("拉伸切除")));
    if (!page)
        return;
    if (!profileUuid.isEmpty())
        page->setInitialProfile(profileUuid);
    if (!targetUuid.isEmpty())
        page->setInitialTarget(targetUuid);
    createOperationNode(page, QStringLiteral("CutExtrude"), tr("拉伸切除"));
}

void GeometryWorkbench::newPatternWidget(const QString& sourceUuid)
{
    auto* page = dynamic_cast<PatternDialog*>(createPageByType(PublicConstants::kPageGeomPattern, tr("阵列")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    createOperationNode(page, QStringLiteral("Pattern"), tr("阵列"));
}

void GeometryWorkbench::newTransformWidget(const QString& sourceUuid)
{
    auto* page = dynamic_cast<TransformDialog*>(createPageByType(PublicConstants::kPageGeomTransform, tr("对象变换")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    createOperationNode(page, QStringLiteral("Transform"), tr("对象变换"));
}

void GeometryWorkbench::newMirrorWidget(const QString& sourceUuid)
{
    auto* page = dynamic_cast<MirrorDialog*>(createPageByType(PublicConstants::kPageGeomMirror, tr("镜像")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    createOperationNode(page, QStringLiteral("Mirror"), tr("镜像"));
}

void GeometryWorkbench::newFilletWidget(const QString& sourceUuid)
{
    auto* page = dynamic_cast<FilletDialog*>(createPageByType(PublicConstants::kPageGeomFillet, tr("圆角")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    createOperationNode(page, QStringLiteral("Fillet"), tr("圆角"));
}

void GeometryWorkbench::newChamferWidget(const QString& sourceUuid)
{
    auto* page = dynamic_cast<ChamferDialog*>(createPageByType(PublicConstants::kPageGeomChamfer, tr("倒角")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    createOperationNode(page, QStringLiteral("Chamfer"), tr("倒角"));
}

void GeometryWorkbench::newSplitWidget(const QString& sourceUuid, const QString& toolUuid)
{
    auto* page = dynamic_cast<SplitDialog*>(createPageByType(PublicConstants::kPageGeomSplit, tr("分割")));
    if (!page)
        return;
    if (!sourceUuid.isEmpty())
        page->setInitialSource(sourceUuid);
    if (!toolUuid.isEmpty())
        page->setInitialTool(toolUuid);
    createOperationNode(page, QStringLiteral("Split"), tr("分割"));
}

void GeometryWorkbench::importGeometryFile()
{
    if (!m_geomNode)
        return;

    const QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("导入几何"),
        QDir::homePath(),
        tr("几何文件 (*.stp *.step *.igs *.iges *.brep *.dxf);;所有文件 (*)"));
    if (filePath.isEmpty())
        return;

    auto* page = dynamic_cast<GeometryImportWidget*>(
        createPageByType(PublicConstants::kPagePreprocessImportGeometry, tr("导入几何")));
    if (!page)
    {
        QMessageBox::warning(nullptr, tr("导入失败"), tr("无法创建导入任务。"));
        return;
    }

    const StepImportOptions options;
    QJsonObject state;
    state[QStringLiteral("path")] = filePath;
    state[QStringLiteral("stepSplitMode")] = static_cast<int>(options.splitMode);
    state[QStringLiteral("stepPreserveColors")] = options.preserveColors;
    state[QStringLiteral("stepIncludeHidden")] = options.includeHidden;
    state[QStringLiteral("stepGroupByAssembly")] = options.groupByAssembly;
    page->loadJson(state);

    ProjectNode* previousNode = currentGeometryNode();
    ProjectNode* node = createOperationNode(page, QStringLiteral("ImportGeometry"), tr("导入几何"), false);
    if (!node)
        return;

    const QString uuid = geometryNodeUuid(node);
    ImportGeometryBuilder::setStepImportOptions(uuid, options);

    IGeometryService* service = geometryService();
    if (!service)
    {
        removeGeometryLeafNode(node);
        QMessageBox::warning(nullptr, tr("导入失败"), tr("几何服务当前不可用。"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString errorMessage;
    const bool ok = service->recreateImportGeometry(uuid,
                                                     node->getName().isEmpty() ? tr("导入几何") : node->getName(),
                                                     filePath,
                                                     errorMessage);
    QApplication::restoreOverrideCursor();

    if (!ok)
    {
        removeGeometryLeafNode(node);
        QMessageBox::warning(nullptr,
                             tr("导入失败"),
                             errorMessage.isEmpty() ? tr("无法读取所选几何文件。") : errorMessage);
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(previousNode ? previousNode : m_geomNode);
        return;
    }

    // 导入是一个即时命令：选择文件后直接生成模型树，不先创建空白参数页。
    // 导入节点仍保留内部页面用于工程保存/重新加载，但不强迫用户停留在该页面。
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(previousNode ? previousNode : m_geomNode);

    if (!errorMessage.isEmpty())
        QMessageBox::information(nullptr, tr("导入完成"), errorMessage);
}

bool GeometryWorkbench::exportGeom()
{
    QString filePath =
        QFileDialog::getSaveFileName(nullptr, tr("导出几何"), QDir::homePath(),
                                     tr("几何文件 (*.step *.stp *.iges *.igs *.brep)"));
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
        filePath += QStringLiteral(".step");
        extension = ".step";
    }

    bool success = false;
    if (extension == ".step" || extension == ".stp")
    {
        success = writeMultipleShapesStepFile(filePath);
    }
    else if (extension == ".iges" || extension == ".igs")
    {
        success = writeMultipleShapesIgesFile(filePath);
    }
    else if (extension == ".brep")
    {
        success = writeMultipleShapesBrepFile(filePath);
    }
    return success;
}

bool GeometryWorkbench::writeMultipleShapesStepFile(const QString& filePath)
{
    try
    {
        GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
        if (!mgr)
            return false;

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);

        bool hasShape = false;
        const QStringList uuids = activeGeometryUuids();
        for (const QString& uuid : uuids)
        {
            GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
            if (!obj || !obj->getShape() || obj->getShape()->IsNull())
                continue;
            builder.Add(compound, *obj->getShape());
            hasShape = true;
        }
        if (!hasShape)
            return false;

        STEPControl_Writer writer;
        IFSelect_ReturnStatus status = writer.Transfer(compound, STEPControl_AsIs);
        if (status != IFSelect_RetDone)
            return false;

        status = writer.Write(filePath.toUtf8().constData());
        return status == IFSelect_RetDone;
    }
    catch (const Standard_Failure& failure)
    {
        qWarning() << "STEP export failed:"
                   << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
        return false;
    }
    catch (const std::exception& error)
    {
        qWarning() << "STEP export failed:" << error.what();
        return false;
    }
}

bool GeometryWorkbench::writeMultipleShapesIgesFile(const QString& filePath)
{
    try
    {
        GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
        if (!mgr)
            return false;

        IGESControl_Writer writer;
        bool hasShape = false;
        const QStringList uuids = activeGeometryUuids();
        for (const QString& uuid : uuids)
        {
            GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
            if (!obj || !obj->getShape() || obj->getShape()->IsNull())
                continue;

            writer.AddShape(*obj->getShape());
            hasShape = true;
        }
        if (!hasShape)
            return false;

        return writer.Write(filePath.toUtf8().constData());
    }
    catch (const Standard_Failure& failure)
    {
        qWarning() << "IGES export failed:"
                   << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
        return false;
    }
    catch (const std::exception& error)
    {
        qWarning() << "IGES export failed:" << error.what();
        return false;
    }
}

bool GeometryWorkbench::writeMultipleShapesBrepFile(const QString& filePath)
{
    try
    {
        GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
        if (!mgr)
            return false;

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);

        bool hasShape = false;
        const QStringList uuids = activeGeometryUuids();
        for (const QString& uuid : uuids)
        {
            GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
            if (!obj || !obj->getShape() || obj->getShape()->IsNull())
                continue;

            builder.Add(compound, *obj->getShape());
            hasShape = true;
        }
        if (!hasShape)
            return false;

        return BRepTools::Write(compound, filePath.toUtf8().constData());
    }
    catch (const Standard_Failure& failure)
    {
        qWarning() << "BREP export failed:"
                   << (failure.GetMessageString() ? failure.GetMessageString() : "OCCT exception");
        return false;
    }
    catch (const std::exception& error)
    {
        qWarning() << "BREP export failed:" << error.what();
        return false;
    }
}

QString GeometryWorkbench::makeUniqueName(const QString& baseName, const QString& typeKey)
{
    QString key = typeKey.isEmpty() ? baseName : typeKey;
    int count = m_nameCounters.value(key, 0) + 1;
    m_nameCounters.insert(key, count);
    return QString("%1(%2)").arg(baseName).arg(count);
}


QStringList GeometryWorkbench::listEntityIds(int dim)
{
    return GeometryEntityTools::listEntityIds(activeGeometryUuids(), dim);
}

bool GeometryWorkbench::entityCenter(int dim, int id, QVector3D& center)
{
    return GeometryEntityTools::entityCenter(activeGeometryUuids(), dim, id, center);
}

QStringList GeometryWorkbench::entitySubshapeIds(int parentDim, int parentId, int childDim)
{
    QStringList ids;
    if (parentDim <= childDim || childDim < 1 || parentDim > 3) return ids;
    TopoDS_Shape parent;
    const auto uuids = activeGeometryUuids();
    if (!GeometryEntityTools::entityShape(uuids, parentDim, parentId, parent)) return ids;
    TopTools_IndexedMapOfShape children;
    TopExp::MapShapes(parent, childDim == 1 ? TopAbs_EDGE : TopAbs_FACE, children);
    for (int i=1; i<=children.Extent(); ++i) {
        const int id = GeometryEntityTools::globalEntityIdForShape(uuids, childDim, children(i));
        if (id > 0) ids.push_back(QString::number(id));
    }
    return ids;
}

bool GeometryWorkbench::entityMeasure(int dim, int id, double& measure)
{
    return GeometryEntityTools::entityMeasure(activeGeometryUuids(), dim, id, measure);
}

bool GeometryWorkbench::highlightEntity(int dim, int id)
{
    return GeometryEntityTools::highlightEntity(activeGeometryUuids(), dim, id);
}

bool GeometryWorkbench::highlightEntities(int dim, const QList<int>& ids, bool hoverPreview)
{
    return GeometryEntityTools::highlightEntities(activeGeometryUuids(), dim, ids, hoverPreview);
}

void GeometryWorkbench::clearEntityHoverHighlight()
{
    GeometryEntityTools::clearEntityHoverHighlight();
}

void GeometryWorkbench::clearEntityHighlight()
{
    GeometryEntityTools::clearEntityHighlight();
}

bool GeometryWorkbench::beginExternalEntityPick(QObject* receiver, int dim)
{
    if (!receiver || dim < 1 || dim > 3)
        return false;

    // Edge/Face use the normal scene-wide topology picker. For a Volume we let
    // the user click any visible face, then resolve that OCC face to its owning
    // solid. This is both faster and more reliable than introducing another
    // duplicate solid-picking presentation, and it also works when one STEP
    // object contains multiple solids.
    const GeometryPickMode mode = dim == 1 ? GeometryPickMode::Edge : GeometryPickMode::Face;
    QPointer<QObject> target(receiver);

    auto notifyPick = [this, target, dim](const QString& uuid, int localIndex)
    {
        if (!target) return;

        const int globalId = dim == 3
            ? GeometryEntityTools::globalSolidIdForFace(activeGeometryUuids(), uuid, localIndex)
            : GeometryEntityTools::globalEntityId(activeGeometryUuids(), uuid, dim, localIndex);
        if (globalId <= 0) return;

        target->setProperty("_geometryExternalPickDim", dim);
        target->setProperty("_geometryExternalPickId", globalId);
        QCoreApplication::postEvent(
            target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
    };

    auto previewPick = [this, target, dim](const QString& uuid, int localIndex)
    {
        if (!target) return;

        const int globalId = dim == 3
            ? GeometryEntityTools::globalSolidIdForFace(activeGeometryUuids(), uuid, localIndex)
            : GeometryEntityTools::globalEntityId(activeGeometryUuids(), uuid, dim, localIndex);
        if (globalId > 0)
            GeometryEntityTools::highlightEntities(activeGeometryUuids(), dim, QList<int>{globalId}, true);
    };

    auto clearPreview = []()
    {
        GeometryEntityTools::clearEntityHoverHighlight();
    };

    QString activeText;
    if (dim == 3)
        activeText = tr("在图形区选择一个体；单击该体任意可见外表面确认");
    else if (dim == 2)
        activeText = tr("在图形区选择一个面；单击确认");
    else
        activeText = tr("在图形区选择一条边；单击确认");

    return GeometryViewportPickController::startTopologyScene(
        receiver, nullptr, mode, []() { return QString(); },
        std::move(notifyPick), QString(), {}, {}, activeText,
        std::move(previewPick), std::move(clearPreview));
}

QVariantList GeometryWorkbench::externalSelectedSolidIds() const
{
    QVariantList out;
    const QStringList allUuids = activeGeometryUuids();
    if (allUuids.isEmpty())
        return out;

    QStringList selectedUuids;
    QSet<QString> activeSet;
    for (const QString& uuid : allUuids) activeSet.insert(uuid);
    QSet<QString> selectedSet;
    QTreeWidget* tree = m_geomNode ? m_geomNode->treeWidget() : nullptr;
    if (tree)
    {
        const QList<QTreeWidgetItem*> selectedItems = tree->selectedItems();
        for (QTreeWidgetItem* root : selectedItems)
        {
            QList<QTreeWidgetItem*> stack;
            stack.push_back(root);
            while (!stack.isEmpty())
            {
                QTreeWidgetItem* item = stack.takeLast();
                auto* node = dynamic_cast<ProjectNode*>(item);
                if (node)
                {
                    const QString uuid = geometryNodeUuid(node);
                    if (!uuid.isEmpty() && activeSet.contains(uuid) && hasRuntimeShape(uuid))
                        selectedSet.insert(uuid);
                }
                for (int i = 0; item && i < item->childCount(); ++i)
                    stack.push_back(item->child(i));
            }
        }
    }

    // 兼容只有 currentItem、没有多选状态的旧树交互。
    if (selectedSet.isEmpty())
    {
        const QStringList fallback = currentGeometrySelectionUuids();
        for (const QString& uuid : fallback)
            if (activeSet.contains(uuid) && hasRuntimeShape(uuid))
                selectedSet.insert(uuid);
    }

    for (const QString& uuid : allUuids)
        if (selectedSet.contains(uuid)) selectedUuids.push_back(uuid);

    const QList<int> ids = GeometryEntityTools::globalEntityIdsForGeometryUuids(
        allUuids, selectedUuids, 3);
    for (int id : ids) out.push_back(id);
    return out;
}

QVariantList GeometryWorkbench::externalSolidEntries() const
{
    QVariantList out;
    const QStringList allUuids = activeGeometryUuids();
    if (allUuids.isEmpty())
        return out;

    QSet<QString> selectedSet;
    const QStringList selectedUuids = currentGeometrySelectionUuids();
    for (const QString& uuid : selectedUuids) selectedSet.insert(uuid);

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return out;

    // 大装配优化：全局 Solid UI ID 映射只构建一次。
    // 旧代码对每个 uuid 都调用 globalEntityIdsForGeometryUuids()，而该函数每次
    // 都重新遍历整个装配建立 globalIndexed；上百个 STEP 零件会出现明显 O(N^2) 卡顿。
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    bool hasShape = false;
    for (const QString& uuid : allUuids)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;
        builder.Add(compound, *obj->getShape());
        hasShape = true;
    }
    if (!hasShape)
        return out;

    TopTools_IndexedMapOfShape globalSolids;
    TopExp::MapShapes(compound, TopAbs_SOLID, globalSolids);
    if (globalSolids.Extent() <= 0)
        return out;

    out.reserve(globalSolids.Extent());
    for (const QString& uuid : allUuids)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;

        TopTools_IndexedMapOfShape localSolids;
        TopExp::MapShapes(*obj->getShape(), TopAbs_SOLID, localSolids);
        if (localSolids.Extent() <= 0)
            continue;

        QString name = uuid;
        if (ProjectNode* node = findGeometryNodeByUuid(uuid))
        {
            const QString n = stableNodeName(node).trimmed();
            if (!n.isEmpty()) name = n;
        }
        const bool visible = GeometryDisplayManager::isVisible(uuid);

        for (int i = 1; i <= localSolids.Extent(); ++i)
        {
            int globalId = globalSolids.FindIndex(localSolids(i));
            if (globalId <= 0)
            {
                for (int g = 1; g <= globalSolids.Extent(); ++g)
                {
                    if (globalSolids(g).IsSame(localSolids(i)))
                    {
                        globalId = g;
                        break;
                    }
                }
            }
            if (globalId <= 0)
                continue;

            QVariantMap e;
            e.insert(QStringLiteral("id"), globalId);
            e.insert(QStringLiteral("uuid"), uuid);
            e.insert(QStringLiteral("name"), localSolids.Extent() == 1
                ? name : tr("%1 / Solid %2").arg(name).arg(i));
            e.insert(QStringLiteral("visible"), visible);
            e.insert(QStringLiteral("selected"), selectedSet.contains(uuid));
            out.push_back(e);
        }
    }
    return out;
}

bool GeometryWorkbench::beginExternalSolidFacePick(QObject* receiver, int volumeUiId)
{
    if (!receiver || volumeUiId <= 0)
        return false;

    GeometryViewportPickController::stop(receiver, true);
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);

    const QStringList uuids = activeGeometryUuids();
    TopoDS_Shape solid;
    if (!GeometryEntityTools::entityShape(uuids, 3, volumeUiId, solid) || solid.IsNull())
        return false;

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(solid, TopAbs_FACE, faces);
    if (faces.Extent() <= 0)
        return false;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return false;

    // 大装配体中 Source/Target 只能从当前已选 Volume 自己的边界面中拾取。
    // 临时隐藏其它 CAD，避免误点相邻/重合零件；退出拾取后完整恢复。
    beginExternalPartitionGeometryIsolation();

    QVector<QPair<QString, vtkActor*>> pickActors;
    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const TopoDS_Shape& face = faces(i);
        const int globalFaceId = GeometryEntityTools::globalEntityIdForShape(uuids, 2, face);
        if (globalFaceId <= 0)
            continue;

        vtkSmartPointer<vtkActor> actor = makePartitionRegionPreviewActor(face, false);
        if (!actor)
            continue;
        actor->GetProperty()->SetOpacity(0.24);

        const QString propId = QStringLiteral("mesh.sweep.pick.volume.%1.face.%2")
            .arg(volumeUiId).arg(globalFaceId);
        visual->addProp(propId, actor, QStringLiteral("网格"),
                        tr("Volume %1 / Face %2（临时选择）").arg(volumeUiId).arg(globalFaceId),
                        false, false);
        m_externalPartitionRegionPropIds << propId;
        pickActors.push_back(qMakePair(QString::number(globalFaceId), actor.GetPointer()));
    }

    if (pickActors.isEmpty())
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    QPointer<QObject> target(receiver);
    const bool started = GeometryViewportPickController::startExternalActors(
        receiver, nullptr, pickActors,
        [target](const QString& key)
        {
            bool ok = false;
            const int faceId = key.toInt(&ok);
            if (!target || !ok || faceId <= 0)
                return;
            target->setProperty("_geometryExternalPickDim", 2);
            target->setProperty("_geometryExternalPickId", faceId);
            target->setProperty("_geometryExternalPickIsPartitionFace", false);
            target->setProperty("_geometryExternalPickRegionOrdinal", -1);
            QCoreApplication::postEvent(
                target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
        },
        tr("当前只显示 Volume %1 的边界面；请选择 Source/Target 面").arg(volumeUiId));

    if (!started)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    visual->render();
    return true;
}

bool GeometryWorkbench::ensureExternalPartitionRegions(const QVariantList& planes,
                                                       bool forceRebuild,
                                                       QString* detail)
{
    const QStringList geometryUuids = activeGeometryUuids();
    if (!forceRebuild && !m_externalPartitionRegionShapes.isEmpty() &&
        m_externalPartitionRegionPlanes == planes &&
        m_externalPartitionRegionGeometryUuids == geometryUuids)
    {
        if (detail)
            *detail = tr("复用 %1 个临时 OCC Region").arg(m_externalPartitionRegionShapes.size());
        return true;
    }

    QString localDetail;
    QVector<int> ownerVolumeIds;
    QVector<TopoDS_Shape> regions =
        GeometryEntityTools::splitTemporaryPartitionRegions(geometryUuids, planes, &localDetail, &ownerVolumeIds);
    if (regions.isEmpty())
    {
        if (detail) *detail = localDetail;
        m_externalPartitionRegionShapes.clear();
        m_externalPartitionRegionOwnerVolumeIds.clear();
        m_externalPartitionRegionPlanes.clear();
        m_externalPartitionRegionGeometryUuids.clear();
        return false;
    }

    m_externalPartitionRegionShapes = std::move(regions);
    m_externalPartitionRegionOwnerVolumeIds = std::move(ownerVolumeIds);
    m_externalPartitionRegionPlanes = planes;
    m_externalPartitionRegionGeometryUuids = geometryUuids;
    if (detail) *detail = localDetail;
    return true;
}

void GeometryWorkbench::beginExternalPartitionGeometryIsolation()
{
    if (m_externalPartitionGeometryIsolated)
        return;

    m_externalPartitionSavedGeometryVisibility.clear();
    const QStringList uuids = activeGeometryUuids();
    if (uuids.isEmpty())
        return;

    GeometryDisplayManager::setRenderSuspended(true);
    for (const QString& uuid : uuids)
    {
        if (uuid.isEmpty())
            continue;
        const bool visible = GeometryDisplayManager::isVisible(uuid);
        m_externalPartitionSavedGeometryVisibility.insert(uuid, visible);
        if (visible)
            GeometryDisplayManager::setVisible(uuid, false, false);
    }
    GeometryDisplayManager::setRenderSuspended(false);
    m_externalPartitionGeometryIsolated = !m_externalPartitionSavedGeometryVisibility.isEmpty();
}

void GeometryWorkbench::restoreExternalPartitionGeometryIsolation(bool renderNow)
{
    if (!m_externalPartitionGeometryIsolated)
        return;

    GeometryDisplayManager::setRenderSuspended(true);
    for (auto it = m_externalPartitionSavedGeometryVisibility.cbegin();
         it != m_externalPartitionSavedGeometryVisibility.cend(); ++it)
    {
        GeometryDisplayManager::setVisible(it.key(), it.value(), false);
    }
    GeometryDisplayManager::setRenderSuspended(false);

    m_externalPartitionSavedGeometryVisibility.clear();
    m_externalPartitionGeometryIsolated = false;

    if (renderNow)
    {
        if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
            visual->render();
    }
}

bool GeometryWorkbench::beginExternalPartitionRegionPick(QObject* receiver,
                                                         const QVariantList& planes)
{
    if (!receiver)
        return false;

    // End any previous CAD/Region pick owned by this receiver and rebuild the
    // temporary child solids from the current manual split rules. These actors are
    // presentation-only and are never added to the project geometry tree.
    GeometryViewportPickController::stop(receiver, true);
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);

    QString detail;
    // 新的一次 Region 点选显式重建一次，保证 CAD 刚刚修改过时不会复用旧形状。
    // 同一轮后续的列表高亮则复用这批 Shape，不重复跑 BOPAlgo_Splitter。
    if (!ensureExternalPartitionRegions(planes, true, &detail))
        return false;
    const QVector<TopoDS_Shape>& regions = m_externalPartitionRegionShapes;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return false;

    // Mesh-only Region 与原 CAD 外表面大量重合。进入 Region 选择时临时隐藏
    // 原 CAD，只显示真实分块副本；否则内部 Region 永远会被原实体挡住。
    beginExternalPartitionGeometryIsolation();

    QVector<QPair<QString, vtkActor*>> pickActors;
    for (int i = 0; i < regions.size(); ++i)
    {
        vtkSmartPointer<vtkActor> actor = makePartitionRegionPreviewActor(regions[i], false);
        if (!actor)
            continue;

        const QString propId = QStringLiteral("mesh.partition.pick.region.%1").arg(i + 1);
        visual->addProp(propId, actor, QStringLiteral("网格"),
                        tr("分块 Region %1（临时选择）").arg(i + 1), false, false);
        m_externalPartitionRegionPropIds << propId;
        pickActors.push_back(qMakePair(QString::number(i + 1), actor.GetPointer()));
    }

    if (pickActors.isEmpty())
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    QPointer<QObject> target(receiver);
    const bool started = GeometryViewportPickController::startExternalActors(
        receiver, nullptr, pickActors,
        [this, target](const QString& key)
        {
            bool ok = false;
            const int ordinal = key.toInt(&ok);
            if (!target || !ok || ordinal <= 0)
                return;

            target->setProperty("_geometryExternalPickDim", 3);
            target->setProperty("_geometryExternalPickId", ordinal);
            target->setProperty("_geometryExternalPickIsPartitionRegion", true);
            QCoreApplication::postEvent(
                target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
        },
        tr("请直接单击一个 Mesh-only 分块 Region；橙色悬停表示将要选择的分块"));

    if (!started)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    visual->render();
    return true;
}

bool GeometryWorkbench::highlightExternalPartitionRegion(const QVariantList& planes,
                                                          int regionOrdinal)
{
    clearExternalPartitionRegionPreviewImpl(false);
    if (regionOrdinal <= 0)
        return false;

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    const QVector<TopoDS_Shape>& regions = m_externalPartitionRegionShapes;
    if (regionOrdinal > regions.size())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        restoreExternalPartitionGeometryIsolation(false);
        return false;
    }

    beginExternalPartitionGeometryIsolation();

    vtkSmartPointer<vtkActor> actor =
        makePartitionRegionPreviewActor(regions[regionOrdinal - 1], true);
    if (!actor)
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    actor->PickableOff();

    const QString propId = QStringLiteral("mesh.partition.selected.region");
    visual->addProp(propId, actor, QStringLiteral("网格"),
                    tr("已选择分块 Region %1").arg(regionOrdinal), false, false);
    m_externalPartitionRegionPropIds << propId;
    visual->render();
    return true;
}

bool GeometryWorkbench::showExternalPartitionCutters(const QVariantList& planes)
{
    // Cutter-only preview is intentionally cheap: do not run OCC splitting while
    // the user drags/edits the coordinate. Keep the original CAD visible and draw
    // a large translucent plane over the target solid.
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);
    if (planes.isEmpty())
    {
        if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
            visual->render();
        return true;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return false;
    const QStringList uuids = activeGeometryUuids();
    int shown = 0;
    for (int i = 0; i < planes.size(); ++i)
    {
        const QVariantMap plane = planes[i].toMap();
        const int targetId = plane.value(QStringLiteral("targetVolumeUiId"), -1).toInt();
        TopoDS_Shape targetShape;
        if (targetId > 0)
            GeometryEntityTools::entityShape(uuids, 3, targetId, targetShape);
        vtkSmartPointer<vtkActor> actor = makePartitionCutPlanePreviewActor(plane, targetShape);
        if (!actor) continue;
        const QString id = QStringLiteral("mesh.partition.cutter.%1").arg(i + 1);
        visual->addProp(id, actor, QStringLiteral("网格"), tr("扫掠切面 %1").arg(i + 1), false, false);
        m_externalPartitionRegionPropIds << id;
        ++shown;
    }
    visual->render();
    return shown > 0;
}

bool GeometryWorkbench::showExternalPartitionRegions(const QVariantList& planes,
                                                          const QVariantList& visibleRegionOrdinals,
                                                          int selectedRegionOrdinal)
{
    clearExternalPartitionRegionPreviewImpl(false);

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        restoreExternalPartitionGeometryIsolation(false);
        return false;
    }

    QSet<int> visibleOrdinals;
    for (const QVariant& value : visibleRegionOrdinals)
    {
        bool ok = false;
        const int ordinal = value.toInt(&ok);
        if (ok && ordinal > 0 && ordinal <= m_externalPartitionRegionShapes.size())
            visibleOrdinals.insert(ordinal);
    }

    // 全部隐藏不保留一个“空的分块模式”，直接恢复 CAD，避免用户误以为模型丢失。
    if (visibleOrdinals.isEmpty())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return true;
    }

    // Normal preview keeps the original CAD visible as spatial context.  Only
    // active picking temporarily isolates CAD; otherwise the old behaviour made
    // the model look washed-out and the user could not tell where the cutter was.
    restoreExternalPartitionGeometryIsolation(false);

    // Draw enabled cutters together with the region outlines.
    const QStringList uuids = activeGeometryUuids();
    for (int pi = 0; pi < planes.size(); ++pi)
    {
        const QVariantMap plane = planes[pi].toMap();
        const int targetId = plane.value(QStringLiteral("targetVolumeUiId"), -1).toInt();
        TopoDS_Shape targetShape;
        if (targetId > 0) GeometryEntityTools::entityShape(uuids, 3, targetId, targetShape);
        vtkSmartPointer<vtkActor> cutter = makePartitionCutPlanePreviewActor(plane, targetShape);
        if (!cutter) continue;
        const QString cid = QStringLiteral("mesh.partition.visible.cutter.%1").arg(pi + 1);
        visual->addProp(cid, cutter, QStringLiteral("网格"), tr("扫掠切面 %1").arg(pi + 1), false, false);
        m_externalPartitionRegionPropIds << cid;
    }

    for (int ordinal : std::as_const(visibleOrdinals))
    {
        const bool selected = ordinal == selectedRegionOrdinal;
        vtkSmartPointer<vtkActor> actor =
            makePartitionRegionPreviewActor(m_externalPartitionRegionShapes[ordinal - 1], selected);
        if (!actor)
            continue;
        actor->PickableOff();
        if (!selected)
            actor->GetProperty()->SetOpacity(0.14);

        const QString propId = QStringLiteral("mesh.partition.visible.region.%1").arg(ordinal);
        visual->addProp(propId, actor, QStringLiteral("网格"),
                        tr("分块 Region %1").arg(ordinal), false, false);
        m_externalPartitionRegionPropIds << propId;
    }

    visual->render();
    return true;
}

int GeometryWorkbench::externalPartitionRegionCount(const QVariantList& planes)
{
    QString detail;
    // This is the explicit "apply/check partition" boundary used by the mesh UI.
    // Rebuild here so editing a CAD feature under the same project UUID cannot
    // leave stale temporary Regions in the SMESH workflow. Subsequent face/Region
    // queries reuse this freshly built OCC result.
    if (!ensureExternalPartitionRegions(planes, true, &detail))
        return 0;
    return m_externalPartitionRegionShapes.size();
}

QVariantMap GeometryWorkbench::externalPartitionRegionInfo(const QVariantList& planes,
                                                            int regionOrdinal)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    if (regionOrdinal <= 0)
        return out;

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail) ||
        regionOrdinal > m_externalPartitionRegionShapes.size())
        return out;

    const int ownerVolumeId = regionOrdinal <= m_externalPartitionRegionOwnerVolumeIds.size()
        ? m_externalPartitionRegionOwnerVolumeIds[regionOrdinal - 1] : 0;

    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("regionOrdinal"), regionOrdinal);
    out.insert(QStringLiteral("ownerVolumeId"), ownerVolumeId);
    return out;
}

bool GeometryWorkbench::beginExternalPartitionFaceScenePick(QObject* receiver,
                                                              const QVariantList& planes,
                                                              bool externalOnly,
                                                              const QString& selectedRefs)
{
    if (!receiver || planes.isEmpty())
        return false;

    GeometryViewportPickController::stop(receiver, true);
    clearExternalPartitionRegionPreviewImpl(false);
    // Direct sizing / boundary-layer selection keeps the user's normal CAD view.
    // Region ownership is an internal identifier only; users should not have to
    // isolate/select a Region before they can click a visible face.
    restoreExternalPartitionGeometryIsolation(false);

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
        return false;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return false;

    QVector<QPair<QString, TopoDS_Shape>> selectedFaceShapes;
    for (const QString& raw : selectedRefs.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString ref = raw.trimmed();
        const QStringList p = ref.split(QLatin1Char(':'));
        bool okR = false, okF = false;
        const int r = p.value(0).toInt(&okR), f = p.value(1).toInt(&okF);
        if (!okR || !okF || r <= 0 || r > m_externalPartitionRegionShapes.size()) continue;
        const QVector<PartitionFaceView> selectedFaces = sortedPartitionFaces(m_externalPartitionRegionShapes[r - 1]);
        if (f <= 0 || f > selectedFaces.size()) continue;
        selectedFaceShapes.push_back(qMakePair(ref, selectedFaces[f - 1].shape));
    }

    TopTools_IndexedMapOfShape seenFaces;
    QVector<QPair<QString, vtkActor*>> pickActors;
    for (int r = 0; r < m_externalPartitionRegionShapes.size(); ++r)
    {
        const QVector<PartitionFaceView> faces = sortedPartitionFaces(m_externalPartitionRegionShapes[r]);
        for (int f = 0; f < faces.size(); ++f)
        {
            const TopoDS_Shape& face = faces[f].shape;
            if (face.IsNull() || seenFaces.Contains(face))
                continue;

            int ownerCount = 0;
            for (const TopoDS_Shape& regionShape : std::as_const(m_externalPartitionRegionShapes))
            {
                TopTools_IndexedMapOfShape regionFaces;
                TopExp::MapShapes(regionShape, TopAbs_FACE, regionFaces);
                if (regionFaces.Contains(face)) ++ownerCount;
            }
            if (externalOnly && ownerCount != 1)
                continue;

            seenFaces.Add(face);
            vtkSmartPointer<vtkActor> actor = makePartitionRegionPreviewActor(face, false);
            if (!actor) continue;
            // Candidate faces should not repaint the whole model cyan.  They stay
            // almost invisible and become orange only while hovered by the picker.
            actor->GetProperty()->EdgeVisibilityOff();
            actor->GetProperty()->SetColor(0.62, 0.66, 0.70);
            actor->GetProperty()->SetOpacity(0.012);

            const int regionOrdinal = r + 1;
            const int faceOrdinal = f + 1;
            QString key = QStringLiteral("%1:%2").arg(regionOrdinal).arg(faceOrdinal);
            bool selectedMatch = false;
            for (const auto& selectedFace : std::as_const(selectedFaceShapes))
            {
                if (!selectedFace.second.IsNull() && selectedFace.second.IsSame(face))
                {
                    key = selectedFace.first;
                    selectedMatch = true;
                    break;
                }
            }
            if (selectedMatch)
            {
                actor->GetProperty()->SetColor(1.0, 0.50, 0.06);
                actor->GetProperty()->SetOpacity(0.30);
            }
            const QString propId = QStringLiteral("mesh.partition.scene.face.%1.%2")
                .arg(regionOrdinal).arg(faceOrdinal);
            visual->addProp(propId, actor, QStringLiteral("网格"),
                            tr("分块 %1 / 面 %2（直接选择）").arg(regionOrdinal).arg(faceOrdinal),
                            false, false);
            m_externalPartitionRegionPropIds << propId;
            pickActors.push_back(qMakePair(key, actor.GetPointer()));
        }
    }

    if (pickActors.isEmpty())
    {
        clearExternalPartitionRegionPreviewImpl(false);
        return false;
    }

    QPointer<QObject> target(receiver);
    const bool started = GeometryViewportPickController::startExternalActors(
        receiver, nullptr, pickActors,
        [target](const QString& key)
        {
            const QStringList p = key.split(QLatin1Char(':'));
            bool okR = false, okF = false;
            const int regionOrdinal = p.value(0).toInt(&okR);
            const int faceOrdinal = p.value(1).toInt(&okF);
            if (!target || !okR || !okF || regionOrdinal <= 0 || faceOrdinal <= 0)
                return;
            target->setProperty("_geometryExternalPickDim", 2);
            target->setProperty("_geometryExternalPickId", faceOrdinal);
            target->setProperty("_geometryExternalPickIsPartitionRegion", false);
            target->setProperty("_geometryExternalPickIsPartitionFace", true);
            target->setProperty("_geometryExternalPickRegionOrdinal", regionOrdinal);
            target->setProperty("_geometryExternalPickAppend",
                                GeometryViewportPickController::appendSelectionRequested());
            QCoreApplication::postEvent(
                target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
        },
        externalOnly
            ? tr("直接单击外壁面；Ctrl+单击可连续选择")
            : tr("直接单击分块后的面；Ctrl+单击可连续选择"));

    if (!started)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        return false;
    }
    // addProp(resetCam=false): deliberately keep the user's current zoom/view.
    visual->render();
    return true;
}

bool GeometryWorkbench::showExternalPartitionFaceRefs(const QVariantList& planes,
                                                        const QString& refs)
{
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);
    if (planes.isEmpty() || refs.trimmed().isEmpty())
    {
        if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
            visual->render();
        return true;
    }

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
        return false;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return false;

    int shown = 0;
    QSet<QString> seen;
    for (const QString& raw : refs.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString ref = raw.trimmed();
        if (ref.isEmpty() || seen.contains(ref)) continue;
        seen.insert(ref);
        const QStringList p = ref.split(QLatin1Char(':'));
        bool okR = false, okF = false;
        const int r = p.value(0).toInt(&okR), f = p.value(1).toInt(&okF);
        if (!okR || !okF || r <= 0 || r > m_externalPartitionRegionShapes.size()) continue;
        const QVector<PartitionFaceView> faces = sortedPartitionFaces(m_externalPartitionRegionShapes[r - 1]);
        if (f <= 0 || f > faces.size()) continue;
        vtkSmartPointer<vtkActor> actor = makePartitionRegionPreviewActor(faces[f - 1].shape, true);
        if (!actor) continue;
        actor->PickableOff();
        actor->GetProperty()->EdgeVisibilityOff();
        // A stored face selection is a locator, not a paint layer.  Keep enough
        // tint to recognise it while preserving the underlying CAD shape.
        actor->GetProperty()->SetOpacity(0.24);
        const QString id = QStringLiteral("mesh.partition.selected.face.ref.%1.%2").arg(r).arg(f);
        visual->addProp(id, actor, QStringLiteral("网格"),
                        tr("已选分块 %1 / 面 %2").arg(r).arg(f), false, false);
        m_externalPartitionRegionPropIds << id;
        ++shown;
    }
    visual->render();
    return shown > 0;
}

bool GeometryWorkbench::beginExternalPartitionFacePick(QObject* receiver,
                                                       const QVariantList& planes,
                                                       int regionOrdinal)
{
    return beginExternalPartitionSweepFacePick(receiver, planes, regionOrdinal, 0, false);
}

bool GeometryWorkbench::beginExternalPartitionSweepFacePick(QObject* receiver,
                                                              const QVariantList& planes,
                                                              int regionOrdinal,
                                                              int fixedFaceOrdinal,
                                                              bool selectingSource)
{
    if (!receiver || regionOrdinal <= 0)
        return false;

    GeometryViewportPickController::stop(receiver, true);
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    if (regionOrdinal > m_externalPartitionRegionShapes.size())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    const QVector<PartitionFaceView> faces =
        sortedPartitionFaces(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    if (faces.isEmpty())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        restoreExternalPartitionGeometryIsolation(false);
        return false;
    }

    // 分块后的 Face ID 是 Region 内局部编号。进入选面时彻底隔离其它几何，
    // 并保留已经选好的另一端面，让用户不需要记“面 1 / 面 8”。
    beginExternalPartitionGeometryIsolation();

    QVector<QPair<QString, vtkActor*>> pickActors;
    for (int i = 0; i < faces.size(); ++i)
    {
        const int faceOrdinal = i + 1;
        const bool fixed = fixedFaceOrdinal > 0 && faceOrdinal == fixedFaceOrdinal;
        vtkSmartPointer<vtkActor> actor =
            makePartitionRegionPreviewActor(faces[i].shape, fixed);
        if (!actor)
            continue;

        const QString propId = QStringLiteral("mesh.partition.pick.region.%1.face.%2")
            .arg(regionOrdinal).arg(faceOrdinal);
        if (fixed)
        {
            actor->PickableOff();
            actor->GetProperty()->SetOpacity(0.88);
            visual->addProp(propId, actor, QStringLiteral("网格"),
                            selectingSource ? tr("已选目标面") : tr("已选起始面"),
                            false, false);
        }
        else
        {
            // 候选面保持足够可见，但不要盖住模型空间关系；悬停由 PickController 高亮。
            actor->GetProperty()->SetOpacity(0.24);
            visual->addProp(propId, actor, QStringLiteral("网格"),
                            tr("当前分块候选面"), false, false);
            pickActors.push_back(qMakePair(QString::number(faceOrdinal), actor.GetPointer()));
        }
        m_externalPartitionRegionPropIds << propId;
    }

    if (pickActors.isEmpty())
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    QPointer<QObject> target(receiver);
    const bool started = GeometryViewportPickController::startExternalActors(
        receiver, nullptr, pickActors,
        [target, regionOrdinal, faces](const QString& key)
        {
            bool ok = false;
            const int faceOrdinal = key.toInt(&ok);
            if (!target || !ok || faceOrdinal <= 0 || faceOrdinal > faces.size())
                return;

            const PartitionFaceView& f = faces[faceOrdinal - 1];
            target->setProperty("_geometryExternalPickDim", 2);
            target->setProperty("_geometryExternalPickId", faceOrdinal);
            target->setProperty("_geometryExternalPickIsPartitionFace", true);
            target->setProperty("_geometryExternalPickRegionOrdinal", regionOrdinal);
            target->setProperty("_geometryExternalPartitionFaceCenter",
                                QVariant::fromValue(QVector3D(float(f.cx), float(f.cy), float(f.cz))));
            QCoreApplication::postEvent(
                target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
        },
        selectingSource
            ? tr("只显示当前分块：单击起始面；已选目标面保持橙色")
            : tr("只显示当前分块：单击目标面；已选起始面保持橙色"));

    if (!started)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    visual->render();
    return true;
}

bool GeometryWorkbench::showExternalPartitionSweepSelection(const QVariantList& planes,
                                                              int regionOrdinal,
                                                              int sourceFaceOrdinal,
                                                              int targetFaceOrdinal)
{
    clearExternalPartitionRegionPreviewImpl(false);
    if (regionOrdinal <= 0)
        return false;

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail) ||
        regionOrdinal > m_externalPartitionRegionShapes.size())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    const QVector<PartitionFaceView> faces =
        sortedPartitionFaces(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual || faces.isEmpty())
    {
        restoreExternalPartitionGeometryIsolation(false);
        return false;
    }

    beginExternalPartitionGeometryIsolation();

    // 当前 Region 作为淡色上下文持续显示。
    vtkSmartPointer<vtkActor> contextActor =
        makePartitionRegionPreviewActor(m_externalPartitionRegionShapes[regionOrdinal - 1], false);
    if (contextActor)
    {
        contextActor->PickableOff();
        contextActor->GetProperty()->SetOpacity(0.18);
        const QString id = QStringLiteral("mesh.partition.sweep.context");
        visual->addProp(id, contextActor, QStringLiteral("网格"),
                        tr("当前扫掠分块 %1").arg(regionOrdinal), false, false);
        m_externalPartitionRegionPropIds << id;
    }

    auto addFace = [&](int faceOrdinal, bool source)
    {
        if (faceOrdinal <= 0 || faceOrdinal > faces.size())
            return;
        vtkSmartPointer<vtkActor> actor =
            makePartitionRegionPreviewActor(faces[faceOrdinal - 1].shape, true);
        if (!actor)
            return;
        actor->PickableOff();
        actor->GetProperty()->SetOpacity(0.88);
        if (!source)
            actor->GetProperty()->SetColor(0.20, 0.72, 0.38);
        const QString id = source
            ? QStringLiteral("mesh.partition.sweep.source")
            : QStringLiteral("mesh.partition.sweep.target");
        visual->addProp(id, actor, QStringLiteral("网格"),
                        source ? tr("扫掠起始面") : tr("扫掠目标面"), false, false);
        m_externalPartitionRegionPropIds << id;
    };

    addFace(sourceFaceOrdinal, true);
    if (targetFaceOrdinal != sourceFaceOrdinal)
        addFace(targetFaceOrdinal, false);

    visual->render();
    return true;
}

bool GeometryWorkbench::highlightExternalPartitionFace(const QVariantList& planes,
                                                        int regionOrdinal,
                                                        int faceOrdinal)
{
    clearExternalPartitionRegionPreviewImpl(false);
    if (regionOrdinal <= 0 || faceOrdinal <= 0)
        return false;

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    if (regionOrdinal > m_externalPartitionRegionShapes.size())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    const QVector<PartitionFaceView> faces =
        sortedPartitionFaces(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    if (faceOrdinal > faces.size())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        restoreExternalPartitionGeometryIsolation(false);
        return false;
    }

    beginExternalPartitionGeometryIsolation();

    // 先用低透明度显示整个当前 Region 作为空间上下文，再用橙色强调已选面。
    vtkSmartPointer<vtkActor> contextActor =
        makePartitionRegionPreviewActor(m_externalPartitionRegionShapes[regionOrdinal - 1], false);
    if (contextActor)
    {
        contextActor->PickableOff();
        contextActor->GetProperty()->SetOpacity(0.16);
        const QString contextId = QStringLiteral("mesh.partition.selected.face.context");
        visual->addProp(contextId, contextActor, QStringLiteral("网格"),
                        tr("Region %1（面选择上下文）").arg(regionOrdinal), false, false);
        m_externalPartitionRegionPropIds << contextId;
    }

    vtkSmartPointer<vtkActor> actor =
        makePartitionRegionPreviewActor(faces[faceOrdinal - 1].shape, true);
    if (!actor)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    actor->PickableOff();
    actor->GetProperty()->SetOpacity(0.82);

    const QString propId = QStringLiteral("mesh.partition.selected.face");
    visual->addProp(propId, actor, QStringLiteral("网格"),
                    tr("Region %1 / Face %2").arg(regionOrdinal).arg(faceOrdinal),
                    false, false);
    m_externalPartitionRegionPropIds << propId;
    visual->render();
    return true;
}

QVariantMap GeometryWorkbench::externalPartitionFaceInfo(const QVariantList& planes,
                                                          int regionOrdinal,
                                                          int faceOrdinal)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    if (regionOrdinal <= 0 || faceOrdinal <= 0)
        return out;

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
        return out;
    if (regionOrdinal > m_externalPartitionRegionShapes.size())
        return out;
    const QVector<PartitionFaceView> faces =
        sortedPartitionFaces(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    if (faceOrdinal > faces.size())
        return out;

    const PartitionFaceView& f = faces[faceOrdinal - 1];
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(f.shape, TopAbs_EDGE, edgeMap);

    int ownerCount = 0;
    for (const TopoDS_Shape& regionShape : std::as_const(m_externalPartitionRegionShapes))
    {
        TopTools_IndexedMapOfShape regionFaces;
        TopExp::MapShapes(regionShape, TopAbs_FACE, regionFaces);
        if (regionFaces.Contains(f.shape)) ++ownerCount;
    }
    const int ownerVolumeId =
        regionOrdinal <= m_externalPartitionRegionOwnerVolumeIds.size()
        ? m_externalPartitionRegionOwnerVolumeIds[regionOrdinal - 1] : 0;

    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("regionOrdinal"), regionOrdinal);
    out.insert(QStringLiteral("faceOrdinal"), faceOrdinal);
    out.insert(QStringLiteral("ownerVolumeId"), ownerVolumeId);
    out.insert(QStringLiteral("isExternal"), ownerCount == 1);
    out.insert(QStringLiteral("ownerCount"), ownerCount);
    out.insert(QStringLiteral("center"), QVariant::fromValue(QVector3D(float(f.cx), float(f.cy), float(f.cz))));
    out.insert(QStringLiteral("area"), f.area);
    out.insert(QStringLiteral("edgeCount"), edgeMap.Extent());
    return out;
}


bool GeometryWorkbench::beginExternalPartitionEdgeScenePick(QObject* receiver,
                                                              const QVariantList& planes,
                                                              const QString& selectedRefs)
{
    if (!receiver || planes.isEmpty()) return false;
    GeometryViewportPickController::stop(receiver, true);
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
        return false;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return false;

    QVector<QPair<QString, TopoDS_Shape>> selectedEdgeShapes;
    for (const QString& raw : selectedRefs.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString ref = raw.trimmed();
        const QStringList p = ref.split(QLatin1Char(':'));
        bool okR = false, okE = false;
        const int r = p.value(0).toInt(&okR), e = p.value(1).toInt(&okE);
        if (!okR || !okE || r <= 0 || r > m_externalPartitionRegionShapes.size()) continue;
        const QVector<PartitionEdgeView> selectedEdges = sortedPartitionEdges(m_externalPartitionRegionShapes[r - 1]);
        if (e <= 0 || e > selectedEdges.size()) continue;
        selectedEdgeShapes.push_back(qMakePair(ref, selectedEdges[e - 1].shape));
    }

    TopTools_IndexedMapOfShape seenEdges;
    QVector<QPair<QString, vtkActor*>> pickActors;
    for (int r = 0; r < m_externalPartitionRegionShapes.size(); ++r)
    {
        const QVector<PartitionEdgeView> edges = sortedPartitionEdges(m_externalPartitionRegionShapes[r]);
        for (int e = 0; e < edges.size(); ++e)
        {
            const TopoDS_Shape& edge = edges[e].shape;
            if (edge.IsNull() || seenEdges.Contains(edge)) continue;
            seenEdges.Add(edge);
            vtkSmartPointer<vtkActor> actor = makePartitionEdgePreviewActor(edge, false);
            if (!actor) continue;
            // Keep candidates visually quiet.  Hover is handled by the common
            // picker and promotes only the edge under the cursor to orange.
            actor->GetProperty()->SetColor(0.16, 0.18, 0.20);
            actor->GetProperty()->SetLineWidth(1.0);
            actor->GetProperty()->SetOpacity(0.045);
            actor->GetProperty()->SetRenderLinesAsTubes(0);

            const int regionOrdinal = r + 1;
            const int edgeOrdinal = e + 1;
            QString key = QStringLiteral("%1:%2").arg(regionOrdinal).arg(edgeOrdinal);
            bool selectedMatch = false;
            for (const auto& selectedEdge : std::as_const(selectedEdgeShapes))
            {
                if (!selectedEdge.second.IsNull() && selectedEdge.second.IsSame(edge))
                {
                    key = selectedEdge.first;
                    selectedMatch = true;
                    break;
                }
            }
            if (selectedMatch)
            {
                actor->GetProperty()->SetColor(1.0, 0.46, 0.04);
                actor->GetProperty()->SetLineWidth(3.2);
                actor->GetProperty()->SetOpacity(1.0);
                actor->GetProperty()->SetRenderLinesAsTubes(0);
            }
            const QString propId = QStringLiteral("mesh.partition.scene.edge.%1.%2")
                .arg(regionOrdinal).arg(edgeOrdinal);
            visual->addProp(propId, actor, QStringLiteral("网格"),
                            tr("分块 %1 / 边 %2（直接选择）").arg(regionOrdinal).arg(edgeOrdinal),
                            false, false);
            m_externalPartitionRegionPropIds << propId;
            pickActors.push_back(qMakePair(key, actor.GetPointer()));
        }
    }

    if (pickActors.isEmpty())
    {
        clearExternalPartitionRegionPreviewImpl(false);
        return false;
    }

    QPointer<QObject> target(receiver);
    const bool started = GeometryViewportPickController::startExternalActors(
        receiver, nullptr, pickActors,
        [target](const QString& key)
        {
            const QStringList p = key.split(QLatin1Char(':'));
            bool okR = false, okE = false;
            const int regionOrdinal = p.value(0).toInt(&okR);
            const int edgeOrdinal = p.value(1).toInt(&okE);
            if (!target || !okR || !okE || regionOrdinal <= 0 || edgeOrdinal <= 0)
                return;
            target->setProperty("_geometryExternalPickDim", 1);
            target->setProperty("_geometryExternalPickId", edgeOrdinal);
            target->setProperty("_geometryExternalPickIsPartitionRegion", false);
            target->setProperty("_geometryExternalPickIsPartitionEdge", true);
            target->setProperty("_geometryExternalPickRegionOrdinal", regionOrdinal);
            target->setProperty("_geometryExternalPickAppend",
                                GeometryViewportPickController::appendSelectionRequested());
            QCoreApplication::postEvent(
                target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
        },
        tr("直接单击分块后的边；Ctrl+单击可连续选择"));
    if (!started)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        return false;
    }
    visual->render();
    return true;
}

bool GeometryWorkbench::showExternalPartitionEdgeRefs(const QVariantList& planes,
                                                        const QString& refs)
{
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);
    if (planes.isEmpty() || refs.trimmed().isEmpty())
    {
        if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
            visual->render();
        return true;
    }

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail))
        return false;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return false;

    int shown = 0;
    QSet<QString> seen;
    for (const QString& raw : refs.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString ref = raw.trimmed();
        if (ref.isEmpty() || seen.contains(ref)) continue;
        seen.insert(ref);
        const QStringList p = ref.split(QLatin1Char(':'));
        bool okR = false, okE = false;
        const int r = p.value(0).toInt(&okR), e = p.value(1).toInt(&okE);
        if (!okR || !okE || r <= 0 || r > m_externalPartitionRegionShapes.size()) continue;
        const QVector<PartitionEdgeView> edges = sortedPartitionEdges(m_externalPartitionRegionShapes[r - 1]);
        if (e <= 0 || e > edges.size()) continue;
        vtkSmartPointer<vtkActor> actor = makePartitionEdgePreviewActor(edges[e - 1].shape, true);
        if (!actor) continue;
        actor->PickableOff();
        const QString id = QStringLiteral("mesh.partition.selected.edge.ref.%1.%2").arg(r).arg(e);
        visual->addProp(id, actor, QStringLiteral("网格"),
                        tr("已选分块 %1 / 边 %2").arg(r).arg(e), false, false);
        m_externalPartitionRegionPropIds << id;
        ++shown;
    }
    visual->render();
    return shown > 0;
}

bool GeometryWorkbench::beginExternalPartitionEdgePick(QObject* receiver,
                                                         const QVariantList& planes,
                                                         int regionOrdinal)
{
    if (!receiver || regionOrdinal <= 0) return false;
    GeometryViewportPickController::stop(receiver, true);
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(false);

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail) ||
        regionOrdinal > m_externalPartitionRegionShapes.size())
        return false;

    const QVector<PartitionEdgeView> edges =
        sortedPartitionEdges(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual || edges.isEmpty()) return false;

    beginExternalPartitionGeometryIsolation();

    vtkSmartPointer<vtkActor> context =
        makePartitionRegionPreviewActor(m_externalPartitionRegionShapes[regionOrdinal - 1], false);
    if (context)
    {
        context->PickableOff();
        context->GetProperty()->SetOpacity(0.26);
        const QString id = QStringLiteral("mesh.partition.edge.context");
        visual->addProp(id, context, QStringLiteral("网格"),
                        tr("分块 %1（边选择上下文）").arg(regionOrdinal), false, false);
        m_externalPartitionRegionPropIds << id;
    }

    QVector<QPair<QString, vtkActor*>> pickActors;
    for (int i = 0; i < edges.size(); ++i)
    {
        const int edgeOrdinal = i + 1;
        vtkSmartPointer<vtkActor> actor = makePartitionEdgePreviewActor(edges[i].shape, false);
        if (!actor) continue;
        const QString propId = QStringLiteral("mesh.partition.pick.region.%1.edge.%2")
            .arg(regionOrdinal).arg(edgeOrdinal);
        visual->addProp(propId, actor, QStringLiteral("网格"),
                        tr("分块 %1 / 边 %2").arg(regionOrdinal).arg(edgeOrdinal), false, false);
        m_externalPartitionRegionPropIds << propId;
        pickActors.push_back(qMakePair(QString::number(edgeOrdinal), actor.GetPointer()));
    }
    if (pickActors.isEmpty())
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }

    QPointer<QObject> target(receiver);
    const bool started = GeometryViewportPickController::startExternalActors(
        receiver, nullptr, pickActors,
        [target, regionOrdinal](const QString& key)
        {
            bool ok = false;
            const int edgeOrdinal = key.toInt(&ok);
            if (!target || !ok || edgeOrdinal <= 0) return;
            target->setProperty("_geometryExternalPickDim", 1);
            target->setProperty("_geometryExternalPickId", edgeOrdinal);
            target->setProperty("_geometryExternalPickIsPartitionEdge", true);
            target->setProperty("_geometryExternalPickRegionOrdinal", regionOrdinal);
            QCoreApplication::postEvent(
                target.data(), new QEvent(static_cast<QEvent::Type>(QEvent::User + 421)));
        },
        tr("只显示当前分块：请直接单击要设置分段数的边"));
    if (!started)
    {
        clearExternalPartitionRegionPreviewImpl(false);
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    visual->render();
    return true;
}

bool GeometryWorkbench::highlightExternalPartitionEdge(const QVariantList& planes,
                                                         int regionOrdinal,
                                                         int edgeOrdinal)
{
    clearExternalPartitionRegionPreviewImpl(false);
    if (regionOrdinal <= 0 || edgeOrdinal <= 0) return false;
    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail) ||
        regionOrdinal > m_externalPartitionRegionShapes.size())
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    const QVector<PartitionEdgeView> edges =
        sortedPartitionEdges(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    if (edgeOrdinal > edges.size()) return false;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return false;
    beginExternalPartitionGeometryIsolation();

    vtkSmartPointer<vtkActor> context =
        makePartitionRegionPreviewActor(m_externalPartitionRegionShapes[regionOrdinal - 1], false);
    if (context)
    {
        context->PickableOff();
        context->GetProperty()->SetOpacity(0.28);
        const QString id = QStringLiteral("mesh.partition.selected.edge.context");
        visual->addProp(id, context, QStringLiteral("网格"),
                        tr("分块 %1（边选择上下文）").arg(regionOrdinal), false, false);
        m_externalPartitionRegionPropIds << id;
    }
    vtkSmartPointer<vtkActor> actor = makePartitionEdgePreviewActor(edges[edgeOrdinal - 1].shape, true);
    if (!actor)
    {
        restoreExternalPartitionGeometryIsolation(true);
        return false;
    }
    actor->PickableOff();
    const QString id = QStringLiteral("mesh.partition.selected.edge");
    visual->addProp(id, actor, QStringLiteral("网格"),
                    tr("分块 %1 / 边 %2").arg(regionOrdinal).arg(edgeOrdinal), false, false);
    m_externalPartitionRegionPropIds << id;
    visual->render();
    return true;
}

QVariantMap GeometryWorkbench::externalPartitionEdgeInfo(const QVariantList& planes,
                                                           int regionOrdinal,
                                                           int edgeOrdinal)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    if (regionOrdinal <= 0 || edgeOrdinal <= 0) return out;
    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail) ||
        regionOrdinal > m_externalPartitionRegionShapes.size())
        return out;
    const QVector<PartitionEdgeView> edges =
        sortedPartitionEdges(m_externalPartitionRegionShapes[regionOrdinal - 1]);
    if (edgeOrdinal > edges.size()) return out;
    const PartitionEdgeView& e = edges[edgeOrdinal - 1];
    int ownerCount = 0;
    for (const TopoDS_Shape& regionShape : std::as_const(m_externalPartitionRegionShapes))
    {
        TopTools_IndexedMapOfShape regionEdges;
        TopExp::MapShapes(regionShape, TopAbs_EDGE, regionEdges);
        if (regionEdges.Contains(e.shape)) ++ownerCount;
    }
    const int ownerVolumeId =
        regionOrdinal <= m_externalPartitionRegionOwnerVolumeIds.size()
        ? m_externalPartitionRegionOwnerVolumeIds[regionOrdinal - 1] : 0;
    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("regionOrdinal"), regionOrdinal);
    out.insert(QStringLiteral("edgeOrdinal"), edgeOrdinal);
    out.insert(QStringLiteral("ownerVolumeId"), ownerVolumeId);
    out.insert(QStringLiteral("ownerCount"), ownerCount);
    out.insert(QStringLiteral("center"), QVariant::fromValue(
        QVector3D(float(e.cx), float(e.cy), float(e.cz))));
    out.insert(QStringLiteral("length"), e.length);
    return out;
}

void GeometryWorkbench::clearExternalPartitionRegionPreviewImpl(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        m_externalPartitionRegionPropIds.clear();
        return;
    }

    for (const QString& id : std::as_const(m_externalPartitionRegionPropIds))
        visual->removeProp(id, false);
    m_externalPartitionRegionPropIds.clear();
    if (renderNow)
        visual->render();
}

void GeometryWorkbench::clearExternalPartitionRegionPreview()
{
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(true);
}

void GeometryWorkbench::stopExternalEntityPick(QObject* receiver)
{
    if (receiver)
        GeometryViewportPickController::stop(receiver, true);
    GeometryEntityTools::clearEntityHoverHighlight();
    clearExternalPartitionRegionPreviewImpl(false);
    restoreExternalPartitionGeometryIsolation(true);
}

int GeometryWorkbench::externalFaceDominantAxis(int globalFaceId) const
{
    QVector3D n;
    if (!GeometryEntityTools::entityNormal(activeGeometryUuids(), 2, globalFaceId, n))
        return -1;

    const float ax = std::abs(n.x());
    const float ay = std::abs(n.y());
    const float az = std::abs(n.z());
    if (ax >= ay && ax >= az) return 0;
    if (ay >= ax && ay >= az) return 1;
    return 2;
}

QVariantMap GeometryWorkbench::externalFacePlane(int globalFaceId) const
{
    QVariantMap out;
    QVector3D origin;
    QVector3D normal;
    const bool ok = GeometryEntityTools::entityPlane(
        activeGeometryUuids(), 2, globalFaceId, origin, normal);
    out.insert(QStringLiteral("valid"), ok);
    if (ok)
    {
        out.insert(QStringLiteral("origin"), QVariant::fromValue(origin));
        out.insert(QStringLiteral("normal"), QVariant::fromValue(normal));
    }
    return out;
}

QVariantMap GeometryWorkbench::externalEdgeLine(int globalEdgeId) const
{
    QVariantMap out;
    QVector3D origin;
    QVector3D direction;
    const bool ok = GeometryEntityTools::entityLine(
        activeGeometryUuids(), 1, globalEdgeId, origin, direction);
    out.insert(QStringLiteral("valid"), ok);
    if (ok)
    {
        out.insert(QStringLiteral("origin"), QVariant::fromValue(origin));
        out.insert(QStringLiteral("direction"), QVariant::fromValue(direction));
    }
    return out;
}

QVariantMap GeometryWorkbench::externalEdgeBlockingInfo(int globalEdgeId) const
{
    return GeometryEntityTools::edgeBlockingInfo(activeGeometryUuids(), globalEdgeId);
}

QVariantMap GeometryWorkbench::externalEdgeSweepFeaturePlane(int globalEdgeId) const
{
    return GeometryEntityTools::edgeSweepFeaturePlane(activeGeometryUuids(), globalEdgeId);
}

QVariantMap GeometryWorkbench::externalDetectTranslationalPaveSweep() const
{
    return GeometryEntityTools::detectTranslationalPaveSweep(activeGeometryUuids());
}

QVariantMap GeometryWorkbench::externalAnalyzeSweepVolume(int volumeUiId) const
{
    return GeometryEntityTools::analyzeTranslationalSweepVolume(activeGeometryUuids(), volumeUiId);
}


QVariantList GeometryWorkbench::externalAnalyzeSweepVolumes(const QVariantList& volumeUiIds) const
{
    QList<int> ids;
    ids.reserve(volumeUiIds.size());
    QSet<int> seen;
    for (const QVariant& v : volumeUiIds)
    {
        bool ok = false;
        const int id = v.toInt(&ok);
        if (ok && id > 0 && !seen.contains(id))
        {
            seen.insert(id);
            ids.push_back(id);
        }
    }
    return GeometryEntityTools::analyzeTranslationalSweepVolumes(activeGeometryUuids(), ids);
}

QVariantMap GeometryWorkbench::externalAnalyzePartitionSweepRegion(const QVariantList& planes,
                                                                    int regionOrdinal)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("sweepable"), false);
    out.insert(QStringLiteral("candidateCount"), 0);
    if (regionOrdinal <= 0)
    {
        out.insert(QStringLiteral("message"), tr("没有选择 Mesh-only Region。"));
        return out;
    }

    QString detail;
    if (!ensureExternalPartitionRegions(planes, false, &detail) ||
        regionOrdinal > m_externalPartitionRegionShapes.size())
    {
        out.insert(QStringLiteral("message"), detail.isEmpty()
            ? tr("当前分块尚未生成有效 Region。") : detail);
        return out;
    }

    const TopoDS_Shape& region = m_externalPartitionRegionShapes[regionOrdinal - 1];
    QVariantMap local = GeometryEntityTools::analyzeTranslationalSweepShape(region);
    if (!local.value(QStringLiteral("valid")).toBool())
        return local;

    TopTools_IndexedMapOfShape topExpFaces;
    TopExp::MapShapes(region, TopAbs_FACE, topExpFaces);
    const QVector<PartitionFaceView> ordered = sortedPartitionFaces(region);
    auto stableOrdinal = [&](int localIndex) -> int
    {
        if (localIndex <= 0 || localIndex > topExpFaces.Extent()) return -1;
        const TopoDS_Shape& f = topExpFaces(localIndex);
        for (int i = 0; i < ordered.size(); ++i)
            if (ordered[i].shape.IsSame(f)) return i + 1;
        return -1;
    };

    QVariantList mapped;
    for (const QVariant& v : local.value(QStringLiteral("candidates")).toList())
    {
        QVariantMap c = v.toMap();
        const int source = stableOrdinal(c.value(QStringLiteral("sourceLocalFaceIndex")).toInt());
        const int target = stableOrdinal(c.value(QStringLiteral("targetLocalFaceIndex")).toInt());
        if (source <= 0 || target <= 0) continue;
        c.insert(QStringLiteral("source"), source);
        c.insert(QStringLiteral("target"), target);
        const QVector3D dir = c.value(QStringLiteral("direction")).value<QVector3D>();
        c.insert(QStringLiteral("label"),
            tr("R%1/F%2 → R%1/F%3；方向=(%4,%5,%6)，距离=%7")
                .arg(regionOrdinal).arg(source).arg(target)
                .arg(dir.x(), 0, 'g', 4).arg(dir.y(), 0, 'g', 4).arg(dir.z(), 0, 'g', 4)
                .arg(c.value(QStringLiteral("distance")).toDouble(), 0, 'g', 8));
        mapped.push_back(c);
    }

    out = local;
    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("regionOrdinal"), regionOrdinal);
    out.insert(QStringLiteral("candidates"), mapped);
    out.insert(QStringLiteral("candidateCount"), mapped.size());
    out.insert(QStringLiteral("sweepable"), !mapped.isEmpty());
    out.insert(QStringLiteral("ambiguous"), mapped.size() > 1);
    if (mapped.isEmpty())
        out.insert(QStringLiteral("message"), tr("Region %1 没有找到可精确平移对应的 Source/Target；可手动指定或继续分块。") .arg(regionOrdinal));
    else if (mapped.size() == 1)
        out.insert(QStringLiteral("message"), tr("Region %1：Source/Target、扫掠方向和距离已自动确定。") .arg(regionOrdinal));
    else
        out.insert(QStringLiteral("message"), tr("Region %1：检测到 %2 组合法 Sweep 方向，请选择候选。")
            .arg(regionOrdinal).arg(mapped.size()));
    return out;
}

QVariantMap GeometryWorkbench::externalEvaluateHexBlocking(const QVariantList& planes) const
{
    return GeometryEntityTools::evaluateHexBlockingPlanes(activeGeometryUuids(), planes);
}

QVariantMap GeometryWorkbench::externalEvaluateHexBlockingSurfaces(const QVariantList& surfaces) const
{
    return GeometryEntityTools::evaluateHexBlockingSurfaces(activeGeometryUuids(), surfaces);
}

QVariantMap GeometryWorkbench::externalExportPolycubeSurfaceObj(const QString& objPath,
                                                                 const QString& faceMapPath) const
{
    return GeometryEntityTools::exportPolycubeSurfaceObj(activeGeometryUuids(), objPath, faceMapPath);
}
