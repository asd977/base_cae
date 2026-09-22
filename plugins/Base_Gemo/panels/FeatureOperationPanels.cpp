#include "FeatureOperationPanels.h"
#include "../workbench/GeometryWorkbench.h"

#include <CustomComboBox.h>
#include <GeomManagerInstance.h>
#include <GeometryDisplayManager.h>
#include <GeometryPickManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <QPushButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QListWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QJsonArray>
#include <QJsonValue>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QVariant>

#include "ui_ChamferDialog.h"
#include "ui_FilletDialog.h"
#include "ui_MirrorDialog.h"
#include "ui_RevolveDialog.h"
#include "ui_SplitDialog.h"
#include "ui_TransformDialog.h"
#include "ui_SweepDialog.h"
#include "ui_LoftDialog.h"
#include "ui_CutExtrudeDialog.h"
#include "ui_SplitPartDialog.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <gp_Trsf.hxx>
#include <Standard_Failure.hxx>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCellPicker.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkDataArray.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>
#include <functional>

#include <GeometryServiceAccess.h>
#include "FeatureBuilder.h"
#include "GeometryBuildUtils.h"
#include "ModifyBuilder.h"
#include "OccToVtkConverter.h"
#include <GeometryDisplayStyle.h>
#include "../workbench/GeometrySelectionVisuals.h"
#include "../workbench/GeometryViewportPickController.h"

namespace
{
bool discardUnappliedGeometryNode(const QString& uuid)
{
    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    auto* workbench = dynamic_cast<GeometryWorkbench*>(pre);
    return workbench && workbench->discardUnappliedGeometryNode(uuid);
}

const QString kFilletPickDomain = QStringLiteral("GeometryFillet");
const QString kChamferPickDomain = QStringLiteral("GeometryChamfer");
const QString kRevolvePickDomain = QStringLiteral("GeometryRevolveSource");
const QString kSweepPickDomain = QStringLiteral("GeometrySweepInputs");
const QString kLoftPickDomain = QStringLiteral("GeometryLoftProfiles");
const QString kCutExtrudePickDomain = QStringLiteral("GeometryCutExtrudeInputs");
const QString kTransformPickDomain = QStringLiteral("GeometryTransformSource");
const QString kMirrorPickDomain = QStringLiteral("GeometryMirrorSource");
const QString kSplitPickDomain = QStringLiteral("GeometrySplitInputs");
const int kDirectionProfileNormal = -1;

QString sourceDisplayName(const QString& uuid, GeomShapeObject* obj)
{
    const QString baseName = (!obj || obj->getName().isEmpty()) ? uuid : obj->getName();
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node)
        return baseName;

    if (node->data(0, ProjectNode::RoleDimmed).toBool())
        return QObject::tr("%1（已隐藏输入）").arg(baseName);
    return baseName;
}

QString currentComboText(QComboBox* combo)
{
    return (combo && combo->currentIndex() >= 0) ? combo->currentText() : QObject::tr("未选择");
}

bool revolveSourceFace(const QString& uuid, int faceIndex, TopoDS_Face* faceOut = nullptr)
{
    if (uuid.isEmpty() || faceIndex <= 0)
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(*obj->getShape(), TopAbs_FACE, faces);
    if (faceIndex > faces.Extent())
        return false;

    const TopoDS_Face face = TopoDS::Face(faces(faceIndex));
    if (face.IsNull())
        return false;

    // A Revolve section is expected to be planar.  Do this validation after the
    // actual face has been picked rather than rejecting the whole source object
    // simply because it owns more than one face.
    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane)
        return false;

    if (faceOut)
        *faceOut = face;
    return true;
}

QString revolveFaceText(const QString& uuid, int faceIndex)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    return QObject::tr("%1 / 面 %2").arg(sourceDisplayName(uuid, obj)).arg(faceIndex);
}

QStringList visibleShapeUuids(const QString& selfUuid)
{
    QStringList uuids;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return uuids;

    const auto map = mgr->getShapMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        const QString uuid = it.key();
        GeomShapeObject* obj = it.value();
        if (uuid.isEmpty() || uuid == selfUuid || !obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;
        if (!GeometryDisplayManager::isVisible(uuid))
            continue;
        uuids.push_back(uuid);
    }
    return uuids;
}

QJsonArray stringListToJsonArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& value : values)
        arr.append(value);
    return arr;
}

QStringList jsonArrayToStringList(const QJsonArray& arr)
{
    QStringList values;
    for (const auto& value : arr)
    {
        const QString text = value.toString();
        if (!text.isEmpty() && !values.contains(text))
            values.push_back(text);
    }
    return values;
}

QString directionText(int index)
{
    if (index == kDirectionProfileNormal)
        return QObject::tr("沿轮廓法向");
    switch (index)
    {
    case 0: return QObject::tr("+X 全局");
    case 1: return QObject::tr("-X 全局");
    case 2: return QObject::tr("+Y 全局");
    case 3: return QObject::tr("-Y 全局");
    case 5: return QObject::tr("-Z 全局");
    case 4:
    default: return QObject::tr("+Z 全局");
    }
}

void fillDirectionCombo(QComboBox* combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->addItem(directionText(kDirectionProfileNormal), kDirectionProfileNormal);
    combo->addItem(directionText(4), 4);
    combo->addItem(directionText(5), 5);
    combo->addItem(directionText(0), 0);
    combo->addItem(directionText(1), 1);
    combo->addItem(directionText(2), 2);
    combo->addItem(directionText(3), 3);
}

int resolvedExtrudeDirection(const QString& profileUuid, int direction, bool reverse)
{
    if (direction == kDirectionProfileNormal)
    {
        int suggestedDirection = 4;
        FeatureBuilder::analyzeExtrudeProfile(profileUuid, nullptr, &suggestedDirection, nullptr);
        direction = suggestedDirection;
    }

    if (!reverse)
        return direction;

    switch (direction)
    {
    case 0: return 1;
    case 1: return 0;
    case 2: return 3;
    case 3: return 2;
    case 4: return 5;
    case 5: return 4;
    default: return direction;
    }
}

enum class ShapeComboFilter
{
    Any,
    ExtrudableProfile,
    RevolvableProfile,
    SweepProfile,
    Section,
    Solid,
    Path,
    EdgeSource,
    PlanarFace
};

bool hasPlanarFace(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return false;

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next())
    {
        const TopoDS_Face face = TopoDS::Face(exp.Current());
        if (face.IsNull())
            continue;

        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane)
            return true;
    }
    return false;
}

bool principalMirrorPlaneFromFace(const QString& sourceUuid,
                                  int faceIndex,
                                  int& planeType,
                                  double& offset)
{
    planeType = 0;
    offset = 0.0;
    if (sourceUuid.isEmpty() || faceIndex <= 0)
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(sourceUuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(*obj->getShape(), TopAbs_FACE, faces);
    if (faceIndex > faces.Extent())
        return false;

    const TopoDS_Face face = TopoDS::Face(faces(faceIndex));
    if (face.IsNull())
        return false;

    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane)
        return false;

    const gp_Pln plane = surface.Plane();
    const gp_Dir normal = plane.Axis().Direction();
    const gp_Pnt location = plane.Location();
    constexpr double kPrincipalTolerance = 1.0 - 1.0e-6;

    if (std::abs(normal.Z()) >= kPrincipalTolerance)
    {
        planeType = 0; // XY
        offset = location.Z();
        return true;
    }
    if (std::abs(normal.Y()) >= kPrincipalTolerance)
    {
        planeType = 1; // XZ
        offset = location.Y();
        return true;
    }
    if (std::abs(normal.X()) >= kPrincipalTolerance)
    {
        planeType = 2; // YZ
        offset = location.X();
        return true;
    }
    return false;
}

bool revolveAxisFromEdge(const QString& sourceUuid,
                         int edgeIndex,
                         QVector3D& origin,
                         QVector3D& direction)
{
    origin = QVector3D();
    direction = QVector3D();
    if (sourceUuid.isEmpty() || edgeIndex <= 0)
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(sourceUuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(*obj->getShape(), TopAbs_EDGE, edges);
    if (edgeIndex > edges.Extent())
        return false;

    const TopoDS_Edge edge = TopoDS::Edge(edges(edgeIndex));
    if (edge.IsNull())
        return false;

    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Line)
        return false;

    const gp_Lin line = curve.Line();
    const gp_Pnt location = line.Location();
    const gp_Dir dir = line.Direction();
    origin = QVector3D(float(location.X()), float(location.Y()), float(location.Z()));
    direction = QVector3D(float(dir.X()), float(dir.Y()), float(dir.Z())).normalized();
    return direction.lengthSquared() > 1.0e-16f;
}

bool shapeMatchesFilter(const QString& uuid, GeomShapeObject* obj, ShapeComboFilter filter)
{
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    if (filter == ShapeComboFilter::Any)
        return true;

    const TopoDS_Shape& shape = *(obj->getShape());

    // 不要为了判断一个候选类型，就预先 MapShapes(SOLID/FACE/WIRE/EDGE) 四遍。
    // 对大型 STEP，这种“打开属性页时的类型判断”本身就会形成明显停顿。
    // 每种 filter 只做自己真正需要的检查；EdgeSource/Solid 使用 Explorer
    // 找到第一个目标子形状即返回。
    switch (filter)
    {
    case ShapeComboFilter::ExtrudableProfile:
    case ShapeComboFilter::RevolvableProfile:
    {
        // Imported assemblies are dominated by solids. Reject them before invoking
        // the more expensive profile analyzer, otherwise opening a feature page can
        // scan every face/wire of hundreds of STEP parts just to fill one combo.
        TopExp_Explorer solidExp(shape, TopAbs_SOLID);
        if (solidExp.More())
            return false;
        int profileFaceCount = 0;
        return FeatureBuilder::analyzeExtrudeProfile(uuid, &profileFaceCount, nullptr, nullptr) &&
               profileFaceCount > 0;
    }
    case ShapeComboFilter::SweepProfile:
    {
        TopExp_Explorer solidExp(shape, TopAbs_SOLID);
        if (solidExp.More())
            return false;
        return FeatureBuilder::analyzeSweepProfile(uuid, nullptr);
    }
    case ShapeComboFilter::Section:
    {
        TopExp_Explorer solidExp(shape, TopAbs_SOLID);
        if (solidExp.More())
            return false;
        return FeatureBuilder::analyzeLoftSection(uuid, nullptr);
    }
    case ShapeComboFilter::Solid:
    {
        TopExp_Explorer exp(shape, TopAbs_SOLID);
        return exp.More();
    }
    case ShapeComboFilter::Path:
    {
        TopExp_Explorer faceExp(shape, TopAbs_FACE);
        if (faceExp.More())
            return false;
        return FeatureBuilder::analyzeSweepPath(uuid, nullptr);
    }
    case ShapeComboFilter::EdgeSource:
    {
        TopExp_Explorer exp(shape, TopAbs_EDGE);
        return exp.More();
    }
    case ShapeComboFilter::PlanarFace:
    {
        TopExp_Explorer solidExp(shape, TopAbs_SOLID);
        return !solidExp.More() && hasPlanarFace(shape);
    }
    case ShapeComboFilter::Any:
    default:
        return true;
    }
}


constexpr int kShapeComboItemLimit = 160;
constexpr int kShapeComboScanLimit = 640;
constexpr vtkIdType kHeavyLivePreviewCellLimit = 80000;
constexpr int kHeavyPreviewDebounceMs = 140;

bool hasAnyVisibleCadEdgeSource(const QString& excludedUuid = QString())
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return false;

    const auto map = mgr->getShapMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        const QString& uuid = it.key();
        GeomShapeObject* obj = it.value();
        if (uuid.isEmpty() || uuid == excludedUuid || !obj ||
            !GeometryDisplayManager::isVisible(uuid))
        {
            continue;
        }

        vtkActor* edgeActor = obj->getEdgeActor();
        auto* mapper = edgeActor
            ? vtkPolyDataMapper::SafeDownCast(edgeActor->GetMapper())
            : nullptr;
        vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
        if (poly && poly->GetNumberOfLines() > 0 && poly->GetCellData() &&
            poly->GetCellData()->GetArray("OCC_EdgeId"))
        {
            return true;
        }
    }
    return false;
}

bool setComboCurrentUuid(QComboBox* combo, const QString& uuid)
{
    if (!combo || uuid.isEmpty())
        return false;

    int index = combo->findData(uuid);
    if (index < 0)
    {
        auto* mgr = GeomManager::GeomManagerInstance::getInstance();
        GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            return false;
        combo->insertItem(0, sourceDisplayName(uuid, obj), uuid);
        index = 0;
    }
    combo->setCurrentIndex(index);
    return true;
}

vtkIdType displayCellCount(const QString& uuid)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj)
        return 0;

    auto countActor = [](vtkActor* actor) -> vtkIdType
    {
        auto* mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
        return poly ? poly->GetNumberOfCells() : 0;
    };

    const vtkIdType surfaceCells = countActor(obj->getSurfActor());
    if (surfaceCells > 0)
        return surfaceCells;
    return countActor(obj->getEdgeActor());
}

bool allowHeavyLivePreview(const QStringList& sourceUuids)
{
    vtkIdType total = 0;
    for (const QString& uuid : sourceUuids)
    {
        if (uuid.isEmpty())
            continue;
        total += displayCellCount(uuid);
        if (total > kHeavyLivePreviewCellLimit)
            return false;
    }
    return true;
}

void cancelDeferredPreview(QObject* owner)
{
    if (!owner)
        return;
    const qulonglong revision = owner->property("_geometryPreviewRevision").toULongLong() + 1ULL;
    owner->setProperty("_geometryPreviewRevision", QVariant::fromValue(revision));
}

void scheduleDeferredPreview(QObject* owner, const std::function<void()>& callback)
{
    if (!owner || !callback)
        return;

    const qulonglong revision = owner->property("_geometryPreviewRevision").toULongLong() + 1ULL;
    owner->setProperty("_geometryPreviewRevision", QVariant::fromValue(revision));
    QPointer<QObject> guard(owner);
    QTimer::singleShot(kHeavyPreviewDebounceMs, owner, [guard, revision, callback]()
    {
        if (!guard || guard->property("_geometryPreviewRevision").toULongLong() != revision)
            return;
        callback();
    });
}

void stopGeometryObjectPick(QObject* owner, bool resetButton = true)
{
    GeometryViewportPickController::stop(owner, resetButton);
}

bool startGeometryObjectPick(QObject* owner,
                             QPushButton* button,
                             ShapeComboFilter filter,
                             const QString& selfUuid,
                             const QStringList& excludedUuids,
                             const std::function<void(const QString&)>& onPicked,
                             const QString& activeText = QString())
{
    return GeometryViewportPickController::startObject(
        owner, button, selfUuid, excludedUuids,
        [filter](const QString& uuid, GeomShapeObject* obj)
        {
            return shapeMatchesFilter(uuid, obj, filter);
        },
        onPicked, activeText);
}

void configurePickButton(QPushButton* button)
{
    GeometryViewportPickController::configureButton(button);
}

QString emptyFilterMessage(ShapeComboFilter filter)
{
    switch (filter)
    {
    case ShapeComboFilter::ExtrudableProfile:
        return QObject::tr("没有找到可用的闭合草图/轮廓。");
    case ShapeComboFilter::RevolvableProfile:
        return QObject::tr("没有可旋转截面。");
    case ShapeComboFilter::SweepProfile:
        return QObject::tr("没有可用扫掠截面。");
    case ShapeComboFilter::Section:
        return QObject::tr("没有可用放样截面。");
    case ShapeComboFilter::Solid:
        return QObject::tr("没有找到三维实体。");
    case ShapeComboFilter::Path:
        return QObject::tr("没有可用路径。");
    case ShapeComboFilter::PlanarFace:
        return QObject::tr("没有可用目标平面。");
    case ShapeComboFilter::EdgeSource:
        return QObject::tr("没有找到带边的几何对象。");
    case ShapeComboFilter::Any:
    default:
        return QObject::tr("没有找到可用几何对象。");
    }
}

GeometryPickItem wholeShapePickItem(const QString& uuid)
{
    if (uuid.isEmpty())
        return {};

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return {};

    auto pickFor = [&](GeometryPickMode mode, TopAbs_ShapeEnum type) -> GeometryPickItem
    {
        // Whole-object highlighting only needs to know whether a subshape type exists.
        // Do not build a complete indexed map of a large STEP object just to return index 1.
        TopExp_Explorer exp(*obj->getShape(), type);
        return exp.More() ? GeometryPickItem{uuid, mode, 1} : GeometryPickItem{};
    };

    GeometryPickItem item = pickFor(GeometryPickMode::Solid, TopAbs_SOLID);
    if (item.isValid())
        return item;

    item = pickFor(GeometryPickMode::Face, TopAbs_FACE);
    if (item.isValid())
        return item;

    return pickFor(GeometryPickMode::Edge, TopAbs_EDGE);
}

bool boundingBoxCenter(const QString& uuid, double& x, double& y, double& z)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    Bnd_Box box;
    BRepBndLib::Add(*(obj->getShape()), box);
    if (box.IsVoid())
        return false;

    double xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    x = 0.5 * (xmin + xmax);
    y = 0.5 * (ymin + ymax);
    z = 0.5 * (zmin + zmax);
    return true;
}

bool geometryCentroid(const TopoDS_Shape& shape, double& x, double& y, double& z)
{
    if (shape.IsNull())
        return false;

    try
    {
        GProp_GProps properties;
        // Only existence is needed to select the property formula.  Explorer stops
        // at the first match; building full indexed maps here made Transform pivot
        // initialization unexpectedly expensive on imported STEP parts.
        TopExp_Explorer solidExp(shape, TopAbs_SOLID);
        if (solidExp.More())
            BRepGProp::VolumeProperties(shape, properties);
        else
        {
            TopExp_Explorer faceExp(shape, TopAbs_FACE);
            if (faceExp.More())
                BRepGProp::SurfaceProperties(shape, properties);
            else
                BRepGProp::LinearProperties(shape, properties);
        }
        if (std::abs(properties.Mass()) <= Precision::Confusion())
            return false;
        const gp_Pnt center = properties.CentreOfMass();
        x = center.X();
        y = center.Y();
        z = center.Z();
        return true;
    }
    catch (...)
    {
        return false;
    }
}
double boundingBoxDiagonalOrDefault(const QString& uuid, double fallback = 80.0)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return fallback;

    Bnd_Box box;
    BRepBndLib::Add(*(obj->getShape()), box);
    if (box.IsVoid())
        return fallback;

    double xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = xmax - xmin;
    const double dy = ymax - ymin;
    const double dz = zmax - zmin;
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::max(fallback, diag * 1.25);
}

QString revolveAxisPreviewPropId(const QString& ownerUuid)
{
    return ownerUuid + QStringLiteral("_revolve_axis_preview");
}

QString featurePreviewFaceId(const QString& ownerUuid, const QString& tag)
{
    return ownerUuid + QStringLiteral("_feature_preview_") + tag + QStringLiteral("_face");
}

QString featurePreviewEdgeId(const QString& ownerUuid, const QString& tag)
{
    return ownerUuid + QStringLiteral("_feature_preview_") + tag + QStringLiteral("_edge");
}

QString mirrorPlanePreviewId(const QString& ownerUuid)
{
    return ownerUuid + QStringLiteral("_feature_preview_mirror_plane");
}

bool geometryObjectExists(const QString& uuid)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    return obj && obj->getShape() && !obj->getShape()->IsNull();
}

bool sourceShapeCopy(const QString& uuid, TopoDS_Shape& shape)
{
    shape.Nullify();
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;
    shape = *obj->getShape();
    return !shape.IsNull();
}

void clearFeatureShapePreview(const QString& ownerUuid, const QString& tag, bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(featurePreviewFaceId(ownerUuid, tag), false);
        vis->removeProp(featurePreviewEdgeId(ownerUuid, tag), renderNow);
    }
}

bool showFeatureShapePreview(const QString& ownerUuid, const QString& tag,
                             const TopoDS_Shape& shape, bool renderNow = true)
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || shape.IsNull())
        return false;

    clearFeatureShapePreview(ownerUuid, tag, false);
    // deviation 是包围盒相对系数（默认 0.5），不是绝对长度公差。
    // 预览略粗于正式显示，避免复杂特征在参数拖动时生成过量三角形。
    const OCC::DisplayPolyData display =
        OCC::BuildDisplayPolyData(shape, 0.8, 35.0, true, true);

    bool added = false;
    if (display.surface && display.surface->GetNumberOfCells() > 0)
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(display.surface);
        mapper->ScalarVisibilityOff();
        GeometryDisplayStyle::configurePreviewSurfaceMapper(mapper);

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        GeometryDisplayStyle::applyPreviewSurface(actor->GetProperty(), 0.30);
        const QString id = featurePreviewFaceId(ownerUuid, tag);
        vis->addProp(id, actor, QStringLiteral("Geometry"), id, false, false);
        added = true;
    }

    if (display.edges && display.edges->GetNumberOfCells() > 0)
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(display.edges);
        mapper->ScalarVisibilityOff();
        GeometryDisplayStyle::configureCoincidentEdgeMapper(mapper, -16.0);

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        GeometryDisplayStyle::applyPreviewEdge(actor->GetProperty(), 1.35);
        const QString id = featurePreviewEdgeId(ownerUuid, tag);
        vis->addProp(id, actor, QStringLiteral("Geometry"), id, false, false);
        added = true;
    }

    if (renderNow)
        vis->render();
    return added;
}

bool transformedPreviewShape(const TopoDS_Shape& source,
                             double dx, double dy, double dz,
                             double rxDeg, double ryDeg, double rzDeg,
                             int pivotMode,
                             double pivotX, double pivotY, double pivotZ,
                             int orderVersion,
                             TopoDS_Shape& result)
{
    result.Nullify();
    if (source.IsNull())
        return false;

    try
    {
        gp_Pnt pivot(0.0, 0.0, 0.0);
        if (pivotMode == 1)
        {
            double cx = 0.0, cy = 0.0, cz = 0.0;
            if (geometryCentroid(source, cx, cy, cz))
                pivot = gp_Pnt(cx, cy, cz);
        }
        else if (pivotMode == 2)
        {
            pivot = gp_Pnt(pivotX, pivotY, pivotZ);
        }

        auto apply = [](const TopoDS_Shape& input, const gp_Trsf& trsf,
                        TopoDS_Shape& output) -> bool
        {
            BRepBuilderAPI_Transform operation(input, trsf, Standard_True);
            operation.Build();
            if (!operation.IsDone() || operation.Shape().IsNull())
                return false;
            output = operation.Shape();
            return true;
        };

        result = source;
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(dx, dy, dz));

        const struct RotationDef
        {
            gp_Dir axis;
            double angleDeg;
        } rotations[] = {
            {gp_Dir(1.0, 0.0, 0.0), rxDeg},
            {gp_Dir(0.0, 1.0, 0.0), ryDeg},
            {gp_Dir(0.0, 0.0, 1.0), rzDeg}
        };
        constexpr double kPi = 3.14159265358979323846;

        if (orderVersion < 2)
        {
            if (!apply(result, translation, result))
                return false;
            for (const RotationDef& rotation : rotations)
            {
                if (std::abs(rotation.angleDeg) <= 1.0e-12)
                    continue;
                gp_Trsf trsf;
                trsf.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), rotation.axis),
                                 rotation.angleDeg * kPi / 180.0);
                if (!apply(result, trsf, result))
                    return false;
            }
        }
        else
        {
            for (const RotationDef& rotation : rotations)
            {
                if (std::abs(rotation.angleDeg) <= 1.0e-12)
                    continue;
                gp_Trsf trsf;
                trsf.SetRotation(gp_Ax1(pivot, rotation.axis),
                                 rotation.angleDeg * kPi / 180.0);
                if (!apply(result, trsf, result))
                    return false;
            }
            if (!apply(result, translation, result))
                return false;
        }
        return !result.IsNull();
    }
    catch (const Standard_Failure&)
    {
        result.Nullify();
        return false;
    }
}

bool mirroredPreviewShape(const TopoDS_Shape& source, int planeType, double offset,
                          TopoDS_Shape& result)
{
    result.Nullify();
    if (source.IsNull())
        return false;

    try
    {
        gp_Pnt origin(0.0, 0.0, 0.0);
        gp_Dir normal(0.0, 0.0, 1.0);
        if (planeType == 0)
        {
            origin.SetZ(offset);
            normal = gp_Dir(0.0, 0.0, 1.0);
        }
        else if (planeType == 1)
        {
            origin.SetY(offset);
            normal = gp_Dir(0.0, 1.0, 0.0);
        }
        else
        {
            origin.SetX(offset);
            normal = gp_Dir(1.0, 0.0, 0.0);
        }

        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2(origin, normal));
        BRepBuilderAPI_Transform transformer(source, mirror, Standard_True);
        transformer.Build();
        if (!transformer.IsDone() || transformer.Shape().IsNull())
            return false;
        result = transformer.Shape();
        return true;
    }
    catch (const Standard_Failure&)
    {
        result.Nullify();
        return false;
    }
}

void showMirrorPlanePreview(const QString& ownerUuid, const QString& sourceUuid,
                            int planeType, double offset, bool renderNow)
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || !vis->renderer())
        return;

    vis->removeProp(mirrorPlanePreviewId(ownerUuid), false);

    double cx = 0.0, cy = 0.0, cz = 0.0;
    boundingBoxCenter(sourceUuid, cx, cy, cz);
    const double half = 0.55 * boundingBoxDiagonalOrDefault(sourceUuid, 40.0);

    vtkNew<vtkPoints> points;
    if (planeType == 0)
    {
        points->InsertNextPoint(cx - half, cy - half, offset);
        points->InsertNextPoint(cx + half, cy - half, offset);
        points->InsertNextPoint(cx + half, cy + half, offset);
        points->InsertNextPoint(cx - half, cy + half, offset);
    }
    else if (planeType == 1)
    {
        points->InsertNextPoint(cx - half, offset, cz - half);
        points->InsertNextPoint(cx + half, offset, cz - half);
        points->InsertNextPoint(cx + half, offset, cz + half);
        points->InsertNextPoint(cx - half, offset, cz + half);
    }
    else
    {
        points->InsertNextPoint(offset, cy - half, cz - half);
        points->InsertNextPoint(offset, cy + half, cz - half);
        points->InsertNextPoint(offset, cy + half, cz + half);
        points->InsertNextPoint(offset, cy - half, cz + half);
    }

    vtkNew<vtkCellArray> lines;
    for (vtkIdType i = 0; i < 4; ++i)
    {
        lines->InsertNextCell(2);
        lines->InsertCellPoint(i);
        lines->InsertCellPoint((i + 1) % 4);
    }

    vtkNew<vtkPolyData> poly;
    poly->SetPoints(points);
    poly->SetLines(lines);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(poly);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    GeometryDisplayStyle::applyReferenceGuide(actor->GetProperty(), 1.45, 0.80);

    const QString id = mirrorPlanePreviewId(ownerUuid);
    vis->addProp(id, actor, QStringLiteral("Geometry"), id, false, false);
    if (renderNow)
        vis->render();
}

void renderGeometryView()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

void syncWholeShapeHighlight(const QStringList& uuids)
{
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    for (const QString& uuid : uuids)
    {
        if (uuid.isEmpty() || !GeometryDisplayManager::isVisible(uuid))
            continue;
        GeometrySelectionVisuals::applySourcePreviewHighlight(uuid);
    }
    renderGeometryView();
}

void syncSingleShapeHighlight(const QString& domain, const QString& ownerUuid, const QString& uuid)
{
    // Whole-object input highlight now mutates the existing actor only. It does
    // not create a blue overlay and it never makes a hidden object visible.
    GeometryPickManager::instance().clearSelection(domain, ownerUuid);
    syncWholeShapeHighlight(uuid.isEmpty() ? QStringList{} : QStringList{uuid});
}

void syncMultiShapeHighlight(const QString& domain, const QString& ownerUuid, const QStringList& uuids)
{
    GeometryPickManager::instance().clearSelection(domain, ownerUuid);
    syncWholeShapeHighlight(uuids);
}

void fillShapeComboFiltered(QComboBox* combo,
                            QString& selectedUuid,
                            const QString& selfUuid,
                            ShapeComboFilter filter)
{
    if (!combo)
        return;

    const QString oldUuid = combo->currentData().toString().isEmpty()
                                ? selectedUuid
                                : combo->currentData().toString();
    combo->clear();
    combo->setMaxVisibleItems(18);

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return;

    // CAD 属性页不应该因为装配里有几千个对象就创建几千个 QComboBox item。
    // 始终优先保留当前引用，其余只列出有限数量；未列出的对象仍可由模型树
    // 预选或“选择择”直接写回 combo。
    auto appendIfValid = [&](const QString& uuid, GeomShapeObject* obj) -> bool
    {
        if (uuid.isEmpty() || uuid == selfUuid || !obj || !obj->getShape() ||
            obj->getShape()->IsNull() || !shapeMatchesFilter(uuid, obj, filter))
        {
            return false;
        }
        combo->addItem(sourceDisplayName(uuid, obj), uuid);
        return true;
    };

    const auto map = mgr->getShapMap();
    if (!oldUuid.isEmpty())
        appendIfValid(oldUuid, mgr->getGeomShapeObject(oldUuid));

    bool truncated = false;
    int inspected = 0;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (it.key() == oldUuid)
            continue;

        // Candidate combos are only a convenience cache.  Direct viewport selection
        // is the primary CAD workflow, so never scan an entire multi-thousand-part
        // assembly merely because a rare profile/path candidate sits at the end.
        if (combo->count() >= kShapeComboItemLimit || inspected >= kShapeComboScanLimit)
        {
            truncated = true;
            break;
        }
        ++inspected;
        appendIfValid(it.key(), it.value());
    }

    const int index = combo->findData(oldUuid);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
    else if (!oldUuid.isEmpty() && !mgr->getGeomShapeObject(oldUuid))
    {
        combo->insertItem(0, QObject::tr("来源已删除（请重新选择）"), oldUuid);
        combo->setCurrentIndex(0);
    }
    else if (combo->count() > 0)
    {
        combo->setCurrentIndex(0);
    }

    combo->setToolTip(truncated
                          ? QObject::tr("大型模型不会在列表中显示全部对象，请直接在模型中选择。")
                          : QString());
}

void fillShapeCombo(QComboBox* combo, QString& selectedUuid, const QString& selfUuid)
{
    fillShapeComboFiltered(combo, selectedUuid, selfUuid, ShapeComboFilter::Any);
}

void setStatusText(QLabel* label, const QString& text, bool ok)
{
    if (!label)
        return;

    const QString trimmed = text.trimmed();
    label->setText(trimmed);
    label->setVisible(!trimmed.isEmpty());
    if (!trimmed.isEmpty())
    {
        label->setStyleSheet(ok ? QStringLiteral("color:#167a36;")
                                : QStringLiteral("color:#a33a00;"));
    }
}

bool shapeHasEdges(const QString& uuid)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return false;

    GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    // 这里只判断“是否存在边”，不要为了打开圆角/倒角面板就 MapShapes 全模型。
    // 对大型 STEP，TopExp_Explorer 找到第一条边即可返回。
    TopExp_Explorer edgeExp(*(obj->getShape()), TopAbs_EDGE);
    return edgeExp.More();
}

QSet<int> parseLegacySelectionText(const QString& text)
{
    QSet<int> result;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& rawPart : parts)
    {
        const QString part = rawPart.trimmed();
        if (part.contains('-'))
        {
            const QStringList range = part.split('-', Qt::SkipEmptyParts);
            if (range.size() != 2)
                continue;

            bool okA = false;
            bool okB = false;
            int a = range[0].trimmed().toInt(&okA);
            int b = range[1].trimmed().toInt(&okB);
            if (!okA || !okB)
                continue;
            if (a > b)
                std::swap(a, b);
            for (int i = a; i <= b; ++i)
                result.insert(i);
            continue;
        }

        bool ok = false;
        const int index = part.toInt(&ok);
        if (ok)
            result.insert(index);
    }
    return result;
}

bool sourceShapeForEdges(const QString& sourceUuid, TopoDS_Shape& shape)
{
    return sourceShapeCopy(sourceUuid, shape);
}

QSet<int> resolvedEdgeIndices(const QString& sourceUuid, const QString& selection, bool* resolvedOk = nullptr)
{
    if (resolvedOk)
        *resolvedOk = false;
    if (selection.trimmed().isEmpty())
    {
        if (resolvedOk)
            *resolvedOk = true;
        return {};
    }

    TopoDS_Shape shape;
    if (!sourceShapeForEdges(sourceUuid, shape))
        return parseLegacySelectionText(selection);

    bool ok = false;
    QSet<int> result = GeometryBuildUtils::resolveStableEdgeSelection(shape, selection, ok);
    if (!ok && !selection.startsWith(QStringLiteral("edge-v2:")) &&
        !selection.startsWith(QStringLiteral("edge-v3:")))
    {
        result = parseLegacySelectionText(selection);
        ok = !result.isEmpty();
    }
    if (resolvedOk)
        *resolvedOk = ok;
    return result;
}

QString stableEdgeSelection(const QString& sourceUuid, const QSet<int>& selected)
{
    if (selected.isEmpty())
        return QString();

    TopoDS_Shape shape;
    if (!sourceShapeForEdges(sourceUuid, shape))
    {
        QList<int> values = selected.values();
        std::sort(values.begin(), values.end());
        QStringList text;
        for (int value : values)
            text.push_back(QString::number(value));
        return text.join(',');
    }
    return GeometryBuildUtils::encodeStableEdgeSelection(shape, selected);
}

QString edgeSelectionDisplayText(const QString& sourceUuid, const QString& selection)
{
    const QSet<int> selected = resolvedEdgeIndices(sourceUuid, selection);
    QList<int> values = selected.values();
    std::sort(values.begin(), values.end());
    QStringList text;
    for (int value : values)
        text.push_back(QString::number(value));
    return text.join(',');
}

QVector<GeometryPickItem> edgePickItems(const QString& sourceUuid, const QString& edgeSelection)
{
    QVector<GeometryPickItem> items;
    const QSet<int> edges = resolvedEdgeIndices(sourceUuid, edgeSelection);
    QList<int> values = edges.values();
    std::sort(values.begin(), values.end());
    for (int edge : values)
        items.push_back({sourceUuid, GeometryPickMode::Edge, edge});
    return items;
}

void clearGeometryPick(const QString& domain, const QString& ownerUuid)
{
    auto& pick = GeometryPickManager::instance();
    pick.clearPreview(domain, ownerUuid);
    pick.clearSelection(domain, ownerUuid);
    GeometrySelectionVisuals::clearSourcePreviewHighlight(true);
}
}

RevolveDialog::RevolveDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomRevolve)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::RevolveDialog;
    ui->setupUi(this);
    m_sourceCombo = ui->sourceCombo;
    m_axisCombo = ui->axisCombo;
    m_axisCombo->addItems({tr("全局 X 轴"), tr("全局 Y 轴"), tr("全局 Z 轴"), tr("自定义/所选直线")});
    m_axisCombo->setCurrentIndex(0);
    m_angleSpin = ui->angleSpin;
    m_oxSpin = ui->oxSpin;
    m_oySpin = ui->oySpin;
    m_ozSpin = ui->ozSpin;
    m_axisDxSpin = ui->axisDxSpin;
    m_axisDySpin = ui->axisDySpin;
    m_axisDzSpin = ui->axisDzSpin;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    m_previewCheck = ui->previewCheck;
    setStatusText(m_statusLabel, QString(), true);

    connect(m_applyButton, &QPushButton::clicked, this, &RevolveDialog::onApplyClicked);
    connect(m_previewCheck, &QCheckBox::toggled, this, [this](bool)
    {
        cancelDeferredPreview(this);
        updatePreview();
    });

    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        m_sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
        m_sourceFaceIndex = 0;
        syncSourceHighlight();
        updatePreview();
        if (!m_sourceUuid.isEmpty())
            QTimer::singleShot(0, this, [this] { startAutomaticAxisPick(); });
    });

    connect(m_axisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index)
    {
        if (index >= 0 && index <= 2)
        {
            const QSignalBlocker bx(m_axisDxSpin);
            const QSignalBlocker by(m_axisDySpin);
            const QSignalBlocker bz(m_axisDzSpin);
            m_axisDxSpin->setValue(index == 0 ? 1.0 : 0.0);
            m_axisDySpin->setValue(index == 1 ? 1.0 : 0.0);
            m_axisDzSpin->setValue(index == 2 ? 1.0 : 0.0);
        }
        updateAxisPreview();
        scheduleDeferredPreview(this, [this] { updatePreview(); });
    });

    const auto directionEdited = [this](double)
    {
        if (m_axisCombo && m_axisCombo->currentIndex() != 3)
        {
            const QSignalBlocker blocker(m_axisCombo);
            m_axisCombo->setCurrentIndex(3);
        }
        updateAxisPreview();
        scheduleDeferredPreview(this, [this] { updatePreview(); });
    };
    connect(m_axisDxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, directionEdited);
    connect(m_axisDySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, directionEdited);
    connect(m_axisDzSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, directionEdited);

    const auto update = [this]
    {
        updateAxisPreview();
        scheduleDeferredPreview(this, [this] { updatePreview(); });
    };
    connect(m_angleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [update](double) { update(); });
    connect(m_oxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [update](double) { update(); });
    connect(m_oySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [update](double) { update(); });
    connect(m_ozSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [update](double) { update(); });
    delete ui;
}

void RevolveDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty() && geometryObjectExists(getUuid()))
        m_committedState = saveJson();
    updatePreview();
    QTimer::singleShot(0, this, [this]
    {
        if (!m_sourceUuid.isEmpty())
            startAutomaticAxisPick();
        else
            startAutomaticSourcePick();
    });
}

void RevolveDialog::beforeUnload()
{
    cancelDeferredPreview(this);
    GeometryViewportPickController::stop(this, true);
    GeometryPickManager::instance().clearPreview(kRevolvePickDomain, getUuid());
    clearGeometryPick(kRevolvePickDomain, getUuid());
    clearPreview(true);
}

RevolveParameters RevolveDialog::parameters() const
{
    RevolveParameters p;
    p.angleDeg = m_angleSpin ? m_angleSpin->value() : 360.0;
    p.axisOrigin = QVector3D(float(m_oxSpin ? m_oxSpin->value() : 0.0),
                            float(m_oySpin ? m_oySpin->value() : 0.0),
                            float(m_ozSpin ? m_ozSpin->value() : 0.0));
    p.axisDirection = QVector3D(float(m_axisDxSpin ? m_axisDxSpin->value() : 1.0),
                               float(m_axisDySpin ? m_axisDySpin->value() : 0.0),
                               float(m_axisDzSpin ? m_axisDzSpin->value() : 0.0));
    if (!m_sourceUuid.isEmpty() && m_sourceFaceIndex > 0)
        p.sourceFace = CadFaceRef{m_sourceUuid, m_sourceFaceIndex};
    return p;
}

QJsonObject RevolveDialog::saveJson()
{
    const RevolveParameters p = parameters();
    return {{"revolveSchema", 3},
            {"sourceUuid", m_sourceUuid},
            {"sourceFaceIndex", m_sourceFaceIndex},
            {"axisPreset", m_axisCombo ? m_axisCombo->currentIndex() : 3},
            {"axis", m_axisCombo && m_axisCombo->currentIndex() <= 2 ? m_axisCombo->currentIndex() : 2},
            {"angle", p.angleDeg},
            {"ox", p.axisOrigin.x()}, {"oy", p.axisOrigin.y()}, {"oz", p.axisOrigin.z()},
            {"axisDx", p.axisDirection.x()},
            {"axisDy", p.axisDirection.y()},
            {"axisDz", p.axisDirection.z()}};
}

void RevolveDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_sourceUuid = obj.value("sourceUuid").toString();
    m_sourceFaceIndex = obj.value("sourceFaceIndex").toInt(0);

    const QSignalBlocker axisBlocker(m_axisCombo);
    const QSignalBlocker angleBlocker(m_angleSpin);
    const QSignalBlocker oxBlocker(m_oxSpin);
    const QSignalBlocker oyBlocker(m_oySpin);
    const QSignalBlocker ozBlocker(m_ozSpin);
    const QSignalBlocker dxBlocker(m_axisDxSpin);
    const QSignalBlocker dyBlocker(m_axisDySpin);
    const QSignalBlocker dzBlocker(m_axisDzSpin);

    if (m_angleSpin)
        m_angleSpin->setValue(obj.value("angle").toDouble(360.0));
    if (m_oxSpin) m_oxSpin->setValue(obj.value("ox").toDouble(0.0));
    if (m_oySpin) m_oySpin->setValue(obj.value("oy").toDouble(0.0));
    if (m_ozSpin) m_ozSpin->setValue(obj.value("oz").toDouble(0.0));

    int preset = 0;
    QVector3D direction;
    if (obj.value("revolveSchema").toInt(0) >= 2 || obj.contains("axisDx"))
    {
        preset = obj.value("axisPreset").toInt(3);
        direction = QVector3D(float(obj.value("axisDx").toDouble(1.0)),
                              float(obj.value("axisDy").toDouble(0.0)),
                              float(obj.value("axisDz").toDouble(0.0)));
    }
    else
    {
        preset = std::clamp(obj.value("axis").toInt(2), 0, 2);
        direction = preset == 0 ? QVector3D(1, 0, 0)
                  : preset == 1 ? QVector3D(0, 1, 0)
                                : QVector3D(0, 0, 1);
    }
    if (m_axisCombo)
        m_axisCombo->setCurrentIndex(std::clamp(preset, 0, 3));
    if (m_axisDxSpin) m_axisDxSpin->setValue(direction.x());
    if (m_axisDySpin) m_axisDySpin->setValue(direction.y());
    if (m_axisDzSpin) m_axisDzSpin->setValue(direction.z());

    refreshSources();
    m_committedState = saveJson();
    clearFeatureShapePreview(getUuid(), QStringLiteral("revolve"), true);
}

void RevolveDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_sourceFaceIndex = 0;
    m_committedState = QJsonObject();
    refreshSources();
}

void RevolveDialog::refreshSources()
{
    const QString requestedSource = m_sourceUuid;
    const int requestedFace = m_sourceFaceIndex;

    const QSignalBlocker sourceBlocker(m_sourceCombo);
    fillShapeComboFiltered(m_sourceCombo, m_sourceUuid, getUuid(), ShapeComboFilter::RevolvableProfile);

    bool valid = false;
    if (requestedFace > 0 && revolveSourceFace(requestedSource, requestedFace))
    {
        // A face selected from a solid intentionally does not pass the
        // RevolvableProfile whole-object filter.  Keep it as one temporary combo
        // entry so the property page still exposes the exact persisted source.
        const QString faceText = revolveFaceText(requestedSource, requestedFace);
        int index = m_sourceCombo ? m_sourceCombo->findData(requestedSource) : -1;
        if (m_sourceCombo && (index < 0 || m_sourceCombo->itemText(index) != faceText))
        {
            m_sourceCombo->addItem(faceText, requestedSource);
            index = m_sourceCombo->count() - 1;
        }
        if (m_sourceCombo && index >= 0)
            m_sourceCombo->setCurrentIndex(index);
        m_sourceUuid = requestedSource;
        m_sourceFaceIndex = requestedFace;
        valid = true;
    }
    else
    {
        m_sourceFaceIndex = 0;
        if (m_sourceCombo && requestedSource.isEmpty())
            m_sourceCombo->setCurrentIndex(-1);
        m_sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
        valid = !m_sourceUuid.isEmpty();
    }

    syncSourceHighlight();
    if (m_applyButton)
        m_applyButton->setEnabled(valid && parameters().axisDirection.lengthSquared() > 1.0e-16f);

    if (!valid)
    {
        clearPreview(true);
        updateStatus(requestedSource.isEmpty()
                         ? tr("请直接在模型中选择一个平面截面，或从列表选择草图/轮廓。")
                         : tr("旋转截面来源已删除或不可用。"),
                     false);
        return;
    }

    updateStatus(QString(), true);
    updatePreview();
}

void RevolveDialog::syncSourceHighlight()
{
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    GeometryPickManager::instance().clearSelection(kRevolvePickDomain, getUuid());

    if (!m_sourceUuid.isEmpty() && m_sourceFaceIndex > 0 &&
        revolveSourceFace(m_sourceUuid, m_sourceFaceIndex))
    {
        GeometryPickManager::instance().setSelection(
            kRevolvePickDomain, getUuid(),
            {{m_sourceUuid, GeometryPickMode::Face, m_sourceFaceIndex}});
        renderGeometryView();
        return;
    }

    syncWholeShapeHighlight(m_sourceUuid.isEmpty() ? QStringList{} : QStringList{m_sourceUuid});
}

bool RevolveDialog::setSourceFromFace(const QString& uuid, int faceIndex)
{
    if (!revolveSourceFace(uuid, faceIndex))
    {
        updateStatus(tr("请选择一个平面几何面作为旋转截面。"), false);
        return false;
    }

    GeometrySelectionVisuals::clearTreeSelectionHighlight(false);
    m_sourceUuid = uuid;
    m_sourceFaceIndex = faceIndex;

    if (m_sourceCombo)
    {
        const QSignalBlocker blocker(m_sourceCombo);
        int index = m_sourceCombo->findData(uuid);
        if (index < 0 || m_sourceCombo->itemText(index) != revolveFaceText(uuid, faceIndex))
        {
            // Do not repurpose a normal sketch entry for a solid-face selection.
            // Add a compact transient entry that carries the same object UUID while
            // the exact face index remains in m_sourceFaceIndex / saved state.
            m_sourceCombo->addItem(revolveFaceText(uuid, faceIndex), uuid);
            index = m_sourceCombo->count() - 1;
        }
        m_sourceCombo->setCurrentIndex(index);
    }

    syncSourceHighlight();
    updateStatus(tr("截面面已选择，请直接单击一条直线边作为旋转轴。"), true);
    updatePreview();
    QTimer::singleShot(0, this, [this] { startAutomaticAxisPick(); });
    return true;
}

void RevolveDialog::startAutomaticSourcePick()
{
    const bool started = GeometryViewportPickController::startTopologyScene(
        this, nullptr, GeometryPickMode::Face,
        []() -> QString { return QString(); },
        [this](const QString& uuid, int faceIndex)
        {
            setSourceFromFace(uuid, faceIndex);
        },
        getUuid(), {}, {}, tr("直接选择旋转截面"),
        [this](const QString& uuid, int faceIndex)
        {
            if (!uuid.isEmpty() && faceIndex > 0 && revolveSourceFace(uuid, faceIndex))
            {
                GeometryPickManager::instance().setPreview(
                    kRevolvePickDomain, getUuid(),
                    {uuid, GeometryPickMode::Face, faceIndex});
            }
            else
            {
                GeometryPickManager::instance().clearPreview(kRevolvePickDomain, getUuid());
            }
        },
        [this]()
        {
            GeometryPickManager::instance().clearPreview(kRevolvePickDomain, getUuid());
        });

    if (!started)
        updateStatus(tr("当前没有可直接选择的几何面；也可以从列表选择草图/轮廓。"), false);
}

bool RevolveDialog::setAxisFromEdge(const QString& uuid, int edgeIndex)
{
    QVector3D origin;
    QVector3D direction;
    if (!revolveAxisFromEdge(uuid, edgeIndex, origin, direction))
    {
        updateStatus(tr("请选择一条直线边作为旋转轴。"), false);
        return false;
    }

    const QSignalBlocker axisBlocker(m_axisCombo);
    const QSignalBlocker oxBlocker(m_oxSpin);
    const QSignalBlocker oyBlocker(m_oySpin);
    const QSignalBlocker ozBlocker(m_ozSpin);
    const QSignalBlocker dxBlocker(m_axisDxSpin);
    const QSignalBlocker dyBlocker(m_axisDySpin);
    const QSignalBlocker dzBlocker(m_axisDzSpin);
    m_axisCombo->setCurrentIndex(3);
    m_oxSpin->setValue(origin.x());
    m_oySpin->setValue(origin.y());
    m_ozSpin->setValue(origin.z());
    m_axisDxSpin->setValue(direction.x());
    m_axisDySpin->setValue(direction.y());
    m_axisDzSpin->setValue(direction.z());

    // Keep the selected axis edge visually independent from the generic
    // whole-object selection produced by the same viewport click.
    GeometrySelectionVisuals::clearTreeSelectionHighlight(false);
    if (m_sourceFaceIndex > 0 && revolveSourceFace(m_sourceUuid, m_sourceFaceIndex))
    {
        GeometryPickManager::instance().setSelection(
            kRevolvePickDomain, getUuid(),
            {{m_sourceUuid, GeometryPickMode::Face, m_sourceFaceIndex},
             {uuid, GeometryPickMode::Edge, edgeIndex}});
    }
    else
    {
        GeometryPickManager::instance().setSelection(kRevolvePickDomain, getUuid(),
            {{uuid, GeometryPickMode::Edge, edgeIndex}});
    }
    GeometryPickManager::instance().clearPreview(kRevolvePickDomain, getUuid());
    updateAxisPreview();
    scheduleDeferredPreview(this, [this] { updatePreview(); });
    updateStatus(tr("已使用所选直线边作为旋转轴；可继续单击其他直线边切换。"), true);

    // Axis selection is a persistent CAD command: choosing one edge must not end
    // the interaction.  Re-arm the scene-wide edge picker on the next Qt turn so
    // any refresh/preview work triggered by this pick cannot leave the page in a
    // non-picking state.  Deferring is important because this function itself is
    // called from the current pick session callback.
    QTimer::singleShot(0, this, [this]
    {
        startAutomaticAxisPick();
    });
    return true;
}

void RevolveDialog::startAutomaticAxisPick()
{
    const QString sourceUuid = m_sourceUuid;
    if (sourceUuid.isEmpty())
    {
        startAutomaticSourcePick();
        return;
    }

    const bool started = GeometryViewportPickController::startTopologyScene(
        this, nullptr, GeometryPickMode::Edge,
        []() -> QString { return QString(); },
        [this](const QString& uuid, int edgeIndex)
        {
            setAxisFromEdge(uuid, edgeIndex);
        },
        getUuid(), {}, {}, tr("直接选择旋转轴"),
        [this](const QString& uuid, int edgeIndex)
        {
            if (!uuid.isEmpty() && edgeIndex > 0)
                GeometryPickManager::instance().setPreview(
                    kRevolvePickDomain, getUuid(), {uuid, GeometryPickMode::Edge, edgeIndex});
        },
        [this]()
        {
            GeometryPickManager::instance().clearPreview(kRevolvePickDomain, getUuid());
        });

    if (!started)
        updateStatus(tr("当前模型中没有可选择的直线边；也可以直接输入旋转轴方向和经过点。"), true);
}

void RevolveDialog::onApplyClicked()
{
    const QString sourceUuid = m_sourceUuid;
    if (sourceUuid.isEmpty())
    {
        updateStatus(tr("请选择闭合截面。"), false);
        return;
    }

    const RevolveParameters p = parameters();
    if (p.axisDirection.lengthSquared() <= 1.0e-16f)
    {
        updateStatus(tr("旋转轴方向不能为零。"), false);
        return;
    }
    if (std::abs(p.angleDeg) <= 1.0e-12)
    {
        updateStatus(tr("旋转角度不能为 0。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("当前无法执行旋转，请重新打开此功能后再试。"), false);
        return;
    }

    const QJsonObject previousCommittedState = m_committedState;
    m_committedState = saveJson();
    if (service->recreateRevolve(m_uuid, getDisplayName(), sourceUuid, p))
    {
        m_sourceUuid = sourceUuid;
        clearFeatureShapePreview(getUuid(), QStringLiteral("revolve"), false);
        updateAxisPreview();
        updateStatus(tr("旋转已应用。"), true);
    }
    else
    {
        m_committedState = previousCommittedState;
        updateStatus(tr("生成失败，请检查截面、旋转轴与角度。"), false);
    }
}

void RevolveDialog::clearAxisPreview(bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->removeProp(revolveAxisPreviewPropId(getUuid()), renderNow);
}

void RevolveDialog::clearPreview(bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(revolveAxisPreviewPropId(getUuid()), false);
        vis->removeProp(featurePreviewFaceId(getUuid(), QStringLiteral("revolve")), false);
        vis->removeProp(featurePreviewEdgeId(getUuid(), QStringLiteral("revolve")), renderNow);
    }
}

void RevolveDialog::updatePreview()
{
    const QString sourceUuid = m_sourceUuid;
    const RevolveParameters p = parameters();
    if (sourceUuid.isEmpty() || p.axisDirection.lengthSquared() <= 1.0e-16f)
    {
        clearPreview(true);
        if (m_applyButton)
            m_applyButton->setEnabled(false);
        return;
    }
    if (m_applyButton)
        m_applyButton->setEnabled(true);

    updateAxisPreview();

    // 旋转轴属于选择反馈，始终显示；旋转后的结果实体由用户显式控制。
    // 默认关闭，避免结果覆盖原几何后影响继续选择截面/旋转轴。
    if (!m_previewCheck || !m_previewCheck->isChecked())
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("revolve"), true);
        return;
    }

    if (geometryObjectExists(getUuid()) && !m_committedState.isEmpty() && saveJson() == m_committedState)
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("revolve"), true);
        return;
    }

    if (!allowHeavyLivePreview({sourceUuid}))
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("revolve"), true);
        updateStatus(tr("大型几何已关闭实时实体预览；参数可继续编辑，点击“应用”后生成。"), true);
        return;
    }

    TopoDS_Shape preview;
    QString message;
    if (!FeatureBuilder::buildRevolveShape(sourceUuid, p, preview, &message))
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("revolve"), true);
        updateStatus(message.isEmpty() ? tr("当前参数无法生成旋转预览。") : message, false);
        return;
    }

    showFeatureShapePreview(getUuid(), QStringLiteral("revolve"), preview, true);
    updateStatus(QString(), true);
}

void RevolveDialog::updateAxisPreview()
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || !vis->renderer())
        return;

    const QString sourceUuid = m_sourceUuid;
    if (sourceUuid.isEmpty())
    {
        clearAxisPreview(true);
        return;
    }

    const RevolveParameters p = parameters();
    if (p.axisDirection.lengthSquared() <= 1.0e-16f)
    {
        clearAxisPreview(true);
        return;
    }

    const QVector3D d = p.axisDirection.normalized();
    const double ox = p.axisOrigin.x();
    const double oy = p.axisOrigin.y();
    const double oz = p.axisOrigin.z();
    const double length = boundingBoxDiagonalOrDefault(sourceUuid);

    vtkNew<vtkPoints> points;
    points->InsertNextPoint(ox - d.x() * length, oy - d.y() * length, oz - d.z() * length);
    points->InsertNextPoint(ox + d.x() * length, oy + d.y() * length, oz + d.z() * length);

    vtkNew<vtkCellArray> lines;
    lines->InsertNextCell(2);
    lines->InsertCellPoint(0);
    lines->InsertCellPoint(1);

    vtkNew<vtkPolyData> poly;
    poly->SetPoints(points);
    poly->SetLines(lines);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(poly);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    GeometryDisplayStyle::applyReferenceGuide(actor->GetProperty(), 1.8, 0.92);

    const QString propId = revolveAxisPreviewPropId(getUuid());
    vis->removeProp(propId, false);
    vis->addProp(propId, actor, QStringLiteral("Geometry"), propId, false, false);
}

void RevolveDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}


SweepDialog::SweepDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomSweep)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::SweepDialog;
    ui->setupUi(this);
    m_profileCombo = ui->profileCombo;
    m_pathCombo = ui->pathCombo;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    auto* pickProfileButton = ui->pickProfileButton;
    auto* pickPathButton = ui->pickPathButton;
    configurePickButton(pickProfileButton);
    configurePickButton(pickPathButton);
    setStatusText(m_statusLabel, QString(), true);

    connect(m_applyButton, &QPushButton::clicked, this, &SweepDialog::onApplyClicked);
    connect(pickProfileButton, &QPushButton::clicked, this, [this, pickProfileButton]
    {
        startGeometryObjectPick(this, pickProfileButton, ShapeComboFilter::SweepProfile,
                                getUuid(), {}, [this](const QString& uuid)
                                {
                                    m_profileUuid = uuid;
                                    refreshSources();
                                }, tr("请在模型中选择扫掠截面"));
    });
    connect(pickPathButton, &QPushButton::clicked, this, [this, pickPathButton]
    {
        const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
        startGeometryObjectPick(this, pickPathButton, ShapeComboFilter::Path,
                                getUuid(), {profileUuid}, [this](const QString& uuid)
                                {
                                    m_pathUuid = uuid;
                                    refreshSources();
                                }, tr("请在模型中选择扫掠路径"));
    });
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        m_profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
        refreshSources();
    });
    connect(m_pathCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        m_pathUuid = m_pathCombo ? m_pathCombo->currentData().toString() : QString();
        syncHighlight();
        updateUiState();
        updatePreview();
    });
    delete ui;
}

void SweepDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
}

void SweepDialog::beforeUnload()
{
    stopGeometryObjectPick(this);
    clearGeometryPick(kSweepPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject SweepDialog::saveJson()
{
    return {{"profileUuid", m_profileCombo ? m_profileCombo->currentData().toString() : m_profileUuid},
            {"pathUuid", m_pathCombo ? m_pathCombo->currentData().toString() : m_pathUuid}};
}

void SweepDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_profileUuid = obj.value("profileUuid").toString();
    m_pathUuid = obj.value("pathUuid").toString();
    refreshSources();
    m_committedState = saveJson();
    clearPreview(true);
}

void SweepDialog::setInitialProfile(const QString& uuid)
{
    m_profileUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void SweepDialog::setInitialPath(const QString& uuid)
{
    m_pathUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void SweepDialog::refreshSources()
{
    const QSignalBlocker profileBlocker(m_profileCombo);
    const QSignalBlocker pathBlocker(m_pathCombo);

    // 截面允许一个面、一条线框或单边，路径只允许单条连续且不分叉的线框。
    fillShapeComboFiltered(m_profileCombo, m_profileUuid, getUuid(), ShapeComboFilter::SweepProfile);
    fillShapeComboFiltered(m_pathCombo, m_pathUuid, getUuid(), ShapeComboFilter::Path);

    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    if (m_pathCombo && !profileUuid.isEmpty())
    {
        for (int i = m_pathCombo->count() - 1; i >= 0; --i)
        {
            if (m_pathCombo->itemData(i).toString() == profileUuid)
                m_pathCombo->removeItem(i);
        }
        const int pathIndex = m_pathCombo->findData(m_pathUuid);
        if (pathIndex >= 0)
            m_pathCombo->setCurrentIndex(pathIndex);
        else if (m_pathCombo->count() > 0)
            m_pathCombo->setCurrentIndex(0);
    }

    m_profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    m_pathUuid = m_pathCombo ? m_pathCombo->currentData().toString() : QString();
    syncHighlight();
    updateUiState();
    updatePreview();
}

void SweepDialog::syncHighlight()
{
    QStringList ids;
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    const QString pathUuid = m_pathCombo ? m_pathCombo->currentData().toString() : QString();
    if (!profileUuid.isEmpty()) ids << profileUuid;
    if (!pathUuid.isEmpty() && pathUuid != profileUuid) ids << pathUuid;
    syncMultiShapeHighlight(kSweepPickDomain, getUuid(), ids);
}

void SweepDialog::updateUiState()
{
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    const QString pathUuid = m_pathCombo ? m_pathCombo->currentData().toString() : QString();
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* profileObj = mgr ? mgr->getGeomShapeObject(profileUuid) : nullptr;
    GeomShapeObject* pathObj = mgr ? mgr->getGeomShapeObject(pathUuid) : nullptr;
    const bool profileOk = shapeMatchesFilter(profileUuid, profileObj, ShapeComboFilter::SweepProfile);
    const bool pathOk = shapeMatchesFilter(pathUuid, pathObj, ShapeComboFilter::Path);
    const bool ready = profileOk && pathOk && profileUuid != pathUuid;
    if (m_applyButton)
        m_applyButton->setEnabled(ready);
    updateStatus(QString(), true);
}

void SweepDialog::updatePreview()
{
    // 扫掠可能触发较重的 OCC 计算；这里只清除旧预览，
    // 保留来源高亮，最终结果仅在用户点击“应用”时生成。
    clearPreview(false);
}

void SweepDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("sweep"), renderNow);
}

void SweepDialog::onApplyClicked()
{
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    const QString pathUuid = m_pathCombo ? m_pathCombo->currentData().toString() : QString();
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* profileObj = mgr ? mgr->getGeomShapeObject(profileUuid) : nullptr;
    GeomShapeObject* pathObj = mgr ? mgr->getGeomShapeObject(pathUuid) : nullptr;
    if (profileUuid == pathUuid ||
        !shapeMatchesFilter(profileUuid, profileObj, ShapeComboFilter::SweepProfile) ||
        !shapeMatchesFilter(pathUuid, pathObj, ShapeComboFilter::Path))
    {
        updateStatus(tr("请选择有效的截面和连续路径。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateSweep(m_uuid, getDisplayName(), profileUuid, pathUuid))
    {
        m_profileUuid = profileUuid;
        m_pathUuid = pathUuid;
        m_committedState = saveJson();
        clearPreview(true);
        syncHighlight();
        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败：路径或截面无效。"), false);
    }
}

void SweepDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    syncHighlight();
    updateStatus(QString(), true);
}

void SweepDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}

LoftDialog::LoftDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomLoft)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::LoftDialog;
    ui->setupUi(this);
    m_profileCombo = ui->profileCombo;
    m_profileList = ui->profileList;
    m_solidCheck = ui->solidCheck;
    m_ruledCheck = ui->ruledCheck;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    auto* addBtn = ui->addButton;
    auto* pickBtn = ui->pickButton;
    auto* removeBtn = ui->removeButton;
    auto* clearBtn = ui->clearButton;
    configurePickButton(pickBtn);
    setStatusText(m_statusLabel, QString(), true);

    connect(m_applyButton, &QPushButton::clicked, this, &LoftDialog::onApplyClicked);
    connect(addBtn, &QPushButton::clicked, this, &LoftDialog::addCurrentProfile);
    connect(pickBtn, &QPushButton::clicked, this, [this, pickBtn]
    {
        startGeometryObjectPick(this, pickBtn, ShapeComboFilter::Section, getUuid(),
                                m_profileUuids, [this](const QString& uuid)
                                {
                                    if (!m_profileUuids.contains(uuid))
                                        m_profileUuids.push_back(uuid);
                                    refreshSelectedList();
                                    syncHighlight();
                                    updatePreview();
                                }, tr("请在模型中选择要加入的截面"));
    });
    connect(removeBtn, &QPushButton::clicked, this, &LoftDialog::removeSelectedProfile);
    connect(clearBtn, &QPushButton::clicked, this, &LoftDialog::clearProfiles);
    connect(m_profileList, &QListWidget::itemSelectionChanged, this, &LoftDialog::syncHighlight);
    connect(m_solidCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    connect(m_ruledCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    delete ui;
}

void LoftDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
    updatePreview();
}

void LoftDialog::beforeUnload()
{
    stopGeometryObjectPick(this);
    clearGeometryPick(kLoftPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject LoftDialog::saveJson()
{
    QJsonArray profiles;
    for (const QString& uuid : m_profileUuids)
        profiles.append(uuid);
    return {{"profileUuids", profiles},
            {"makeSolid", m_solidCheck ? m_solidCheck->isChecked() : true},
            {"ruled", m_ruledCheck ? m_ruledCheck->isChecked() : false}};
}

void LoftDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_profileUuids.clear();
    const QJsonArray arr = obj.value("profileUuids").toArray();
    for (const QJsonValue& value : arr)
    {
        const QString uuid = value.toString();
        if (!uuid.isEmpty())
            m_profileUuids << uuid;
    }
    if (m_solidCheck)
        m_solidCheck->setChecked(obj.value("makeSolid").toBool(true));
    if (m_ruledCheck)
        m_ruledCheck->setChecked(obj.value("ruled").toBool(false));
    refreshSources();
    m_committedState = saveJson();
    clearPreview(true);
}

void LoftDialog::addInitialProfile(const QString& uuid)
{
    if (uuid.isEmpty())
        return;
    if (!m_profileUuids.contains(uuid))
        m_profileUuids << uuid;
    m_committedState = QJsonObject();
    refreshSources();
    refreshSelectedList();
    syncHighlight();
    updatePreview();
}

void LoftDialog::refreshSources()
{
    QString dummy;
    fillShapeComboFiltered(m_profileCombo, dummy, getUuid(), ShapeComboFilter::Section);
    refreshSelectedList();
    syncHighlight();
    updateStatus(QString(), true);
}

void LoftDialog::addCurrentProfile()
{
    const QString uuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    if (uuid.isEmpty())
    {
        updateStatus(tr("未选择截面。"), false);
        return;
    }
    if (m_profileUuids.contains(uuid))
    {
        updateStatus(tr("截面已存在。"), false);
        return;
    }
    m_profileUuids << uuid;
    refreshSelectedList();
    syncHighlight();
    updateStatus(QString(), true);
    updatePreview();
}

void LoftDialog::removeSelectedProfile()
{
    const int row = m_profileList ? m_profileList->currentRow() : -1;
    if (row < 0 || row >= m_profileUuids.size())
    {
        updateStatus(tr("未选择截面。"), false);
        return;
    }
    m_profileUuids.removeAt(row);
    refreshSelectedList();
    syncHighlight();
    updatePreview();
}

void LoftDialog::clearProfiles()
{
    m_profileUuids.clear();
    refreshSelectedList();
    syncHighlight();
    updateStatus(QString(), true);
    updatePreview();
}

void LoftDialog::refreshSelectedList()
{
    if (!m_profileList)
        return;
    m_profileList->clear();
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    for (int i = 0; i < m_profileUuids.size(); ++i)
    {
        const QString uuid = m_profileUuids.at(i);
        GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
        m_profileList->addItem(tr("%1. %2").arg(i + 1).arg(sourceDisplayName(uuid, obj)));
    }
}

void LoftDialog::syncHighlight()
{
    syncMultiShapeHighlight(kLoftPickDomain, getUuid(), m_profileUuids);
}

void LoftDialog::updatePreview()
{
    if (m_applyButton)
        m_applyButton->setEnabled(m_profileUuids.size() >= 2);
    clearPreview(false);
    updateStatus(QString(), true);
}

void LoftDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("loft"), renderNow);
}

void LoftDialog::onApplyClicked()
{
    if (m_profileUuids.size() < 2)
    {
        updateStatus(tr("至少两个截面。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateLoft(m_uuid, getDisplayName(), m_profileUuids,
                              m_solidCheck ? m_solidCheck->isChecked() : true,
                              m_ruledCheck ? m_ruledCheck->isChecked() : false))
    {
        m_committedState = saveJson();
        clearPreview(true);
        syncHighlight();
        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败：截面不兼容。"), false);
    }
}

void LoftDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    syncHighlight();
    updateStatus(QString(), true);
}

void LoftDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}

CutExtrudeDialog::CutExtrudeDialog(QWidget* parent)
    : BasePage(QStringLiteral("cut_extrude"))
{
    Q_UNUSED(parent);
    auto* ui = new Ui::CutExtrudeDialog;
    ui->setupUi(this);
    m_targetCombo = ui->targetCombo;
    m_profileCombo = ui->profileCombo;
    m_directionCombo = ui->directionCombo;
    m_depthSpin = ui->depthSpin;
    m_reverseCheck = ui->reverseCheck;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    auto* pickTargetButton = ui->pickTargetButton;
    auto* pickProfileButton = ui->pickProfileButton;
    fillDirectionCombo(m_directionCombo);
    configurePickButton(pickTargetButton);
    configurePickButton(pickProfileButton);
    setStatusText(m_statusLabel, QString(), true);

    connect(m_applyButton, &QPushButton::clicked, this, &CutExtrudeDialog::onApplyClicked);
    connect(pickTargetButton, &QPushButton::clicked, this, [this, pickTargetButton]
    {
        const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
        startGeometryObjectPick(this, pickTargetButton, ShapeComboFilter::Solid, getUuid(),
                                {profileUuid}, [this](const QString& uuid)
                                {
                                    m_targetUuid = uuid;
                                    refreshSources();
                                }, tr("请在模型中选择目标实体"));
    });
    connect(pickProfileButton, &QPushButton::clicked, this, [this, pickProfileButton]
    {
        const QString targetUuid = m_targetCombo ? m_targetCombo->currentData().toString() : QString();
        startGeometryObjectPick(this, pickProfileButton, ShapeComboFilter::ExtrudableProfile,
                                getUuid(), {targetUuid}, [this](const QString& uuid)
                                {
                                    m_profileUuid = uuid;
                                    refreshSources();
                                }, tr("请在模型中选择切除轮廓"));
    });
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        syncHighlight();
        updatePreview();
    });
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        syncHighlight();
        updatePreview();
    });
    connect(m_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updatePreview(); });
    connect(m_depthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updatePreview(); });
    connect(m_reverseCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });
    delete ui;
}

void CutExtrudeDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
    updatePreview();
}

void CutExtrudeDialog::beforeUnload()
{
    stopGeometryObjectPick(this);
    clearGeometryPick(kCutExtrudePickDomain, getUuid());
    clearPreview(true);
}

QJsonObject CutExtrudeDialog::saveJson()
{
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : m_profileUuid;
    const int selectedDirection = m_directionCombo ? m_directionCombo->currentData().toInt() : 4;
    const bool reverse = m_reverseCheck && m_reverseCheck->isChecked();

    return {{"targetUuid", m_targetCombo ? m_targetCombo->currentData().toString() : m_targetUuid},
            {"profileUuid", profileUuid},
            {"depth", m_depthSpin ? m_depthSpin->value() : 10.0},
            {"direction", selectedDirection},
            {"reverse", reverse}};
}

void CutExtrudeDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_targetUuid = obj.value("targetUuid").toString();
    m_profileUuid = obj.value("profileUuid").toString();
    if (m_depthSpin)
        m_depthSpin->setValue(obj.value("depth").toDouble(10.0));
    if (m_directionCombo)
    {
        const int index = m_directionCombo->findData(obj.value("direction").toInt(4));
        if (index >= 0)
            m_directionCombo->setCurrentIndex(index);
    }
    if (m_reverseCheck)
        m_reverseCheck->setChecked(obj.value("reverse").toBool(false));
    refreshSources();
    m_committedState = saveJson();
    clearPreview(true);
}

void CutExtrudeDialog::setInitialProfile(const QString& uuid)
{
    m_profileUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void CutExtrudeDialog::setInitialTarget(const QString& uuid)
{
    m_targetUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void CutExtrudeDialog::refreshSources()
{
    fillShapeComboFiltered(m_targetCombo, m_targetUuid, getUuid(), ShapeComboFilter::Solid);
    fillShapeComboFiltered(m_profileCombo, m_profileUuid, getUuid(), ShapeComboFilter::ExtrudableProfile);
    syncHighlight();
    const QString targetUuid = m_targetCombo ? m_targetCombo->currentData().toString() : QString();
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    const bool ok = !targetUuid.isEmpty() && !profileUuid.isEmpty() && targetUuid != profileUuid;
    if (m_applyButton)
        m_applyButton->setEnabled(ok);
    updateStatus(ok ? QString() : tr("缺少目标或轮廓。"), ok);
    updatePreview();
}

void CutExtrudeDialog::syncHighlight()
{
    QStringList ids;
    const QString targetUuid = m_targetCombo ? m_targetCombo->currentData().toString() : QString();
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    if (!targetUuid.isEmpty()) ids << targetUuid;
    if (!profileUuid.isEmpty() && profileUuid != targetUuid) ids << profileUuid;
    syncMultiShapeHighlight(kCutExtrudePickDomain, getUuid(), ids);
}

void CutExtrudeDialog::updatePreview()
{
    const QString targetUuid = m_targetCombo ? m_targetCombo->currentData().toString() : QString();
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    if (m_applyButton)
        m_applyButton->setEnabled(!targetUuid.isEmpty() && !profileUuid.isEmpty() && targetUuid != profileUuid);
    clearPreview(false);
    updateStatus(QString(), true);
}

void CutExtrudeDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("cut_extrude"), renderNow);
}

void CutExtrudeDialog::onApplyClicked()
{
    const QString targetUuid = m_targetCombo ? m_targetCombo->currentData().toString() : QString();
    const QString profileUuid = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    if (targetUuid.isEmpty() || profileUuid.isEmpty())
    {
        updateStatus(tr("未选择目标或轮廓。"), false);
        return;
    }

    const int selectedDirection = m_directionCombo ? m_directionCombo->currentData().toInt() : 4;
    const int direction = resolvedExtrudeDirection(profileUuid, selectedDirection,
                                                    m_reverseCheck && m_reverseCheck->isChecked());

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateCutExtrude(m_uuid, getDisplayName(), targetUuid, profileUuid,
                                    m_depthSpin ? m_depthSpin->value() : 10.0, direction))
    {
        m_targetUuid = targetUuid;
        m_profileUuid = profileUuid;
        m_committedState = saveJson();
        clearPreview(true);
        syncHighlight();
        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败：轮廓或相交无效。"), false);
    }
}

void CutExtrudeDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    syncHighlight();
    updateStatus(QString(), true);
}

void CutExtrudeDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}

TransformDialog::TransformDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomTransform)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::TransformDialog;
    ui->setupUi(this);
    m_sourceCombo = ui->sourceCombo;
    m_dxSpin = ui->dxSpin;
    m_dySpin = ui->dySpin;
    m_dzSpin = ui->dzSpin;
    m_rxSpin = ui->rxSpin;
    m_rySpin = ui->rySpin;
    m_rzSpin = ui->rzSpin;
    m_pivotModeCombo = ui->pivotModeCombo;
    m_pivotXSpin = ui->pivotXSpin;
    m_pivotYSpin = ui->pivotYSpin;
    m_pivotZSpin = ui->pivotZSpin;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    m_previewCheck = ui->previewCheck;
    setStatusText(m_statusLabel, QString(), true);
    m_pivotModeCombo->addItem(tr("全局原点"), 0);
    m_pivotModeCombo->addItem(tr("几何质心"), 1);
    m_pivotModeCombo->addItem(tr("自定义中心"), 2);
    m_pivotModeCombo->setCurrentIndex(1);
    m_pivotModeCombo->setToolTip(tr("先围绕此中心旋转，再按全局 X/Y/Z 位移。"));


    m_applyButton->setText(tr("应用"));
    auto* pickSourceButton = ui->pickSourceButton;
    configurePickButton(pickSourceButton);

    connect(m_applyButton, &QPushButton::clicked, this, &TransformDialog::onApplyClicked);
    connect(m_previewCheck, &QCheckBox::toggled, this, [this](bool)
    {
        cancelDeferredPreview(this);
        updatePreview();
    });
    connect(pickSourceButton, &QPushButton::clicked, this, [this, pickSourceButton]
    {
        // 重新选来源时先移除结果虚影，保证原几何清晰可见。
        clearPreview(true);
        const bool started =
            startGeometryObjectPick(this, pickSourceButton, ShapeComboFilter::Any, getUuid(), {},
                                    [this](const QString& uuid)
                                    {
                                        m_sourceUuid = uuid;
                                        setComboCurrentUuid(m_sourceCombo, uuid);
                                    }, tr("请在模型中选择要变换的对象"));
        if (!started)
            updatePreview();
    });
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        if (!m_loadingState)
            m_orderVersion = 2;
        syncSingleShapeHighlight(kTransformPickDomain, getUuid(),
                                 m_sourceCombo ? m_sourceCombo->currentData().toString() : QString());
        updatePreview();
    });

    auto update = [this]
    {
        if (m_loadingState)
            return;
        m_orderVersion = 2;
        scheduleDeferredPreview(this, [this] { updatePreview(); });
    };
    QDoubleSpinBox* spins[] = {m_dxSpin, m_dySpin, m_dzSpin, m_rxSpin, m_rySpin, m_rzSpin,
                               m_pivotXSpin, m_pivotYSpin, m_pivotZSpin};
    for (QDoubleSpinBox* spin : spins)
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [update](double) { update(); });
    connect(m_pivotModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (!m_loadingState)
                    m_orderVersion = 2;
                const bool custom = m_pivotModeCombo && m_pivotModeCombo->currentData().toInt() == 2;
                m_pivotXSpin->setEnabled(custom);
                m_pivotYSpin->setEnabled(custom);
                m_pivotZSpin->setEnabled(custom);
                if (!m_loadingState)
                    scheduleDeferredPreview(this, [this] { updatePreview(); });
            });
    m_pivotXSpin->setEnabled(false);
    m_pivotYSpin->setEnabled(false);
    m_pivotZSpin->setEnabled(false);
    delete ui;
}

void TransformDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
    updatePreview();
}

void TransformDialog::beforeUnload()
{
    cancelDeferredPreview(this);
    stopGeometryObjectPick(this);
    clearGeometryPick(kTransformPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject TransformDialog::saveJson()
{
    return {{"sourceUuid", m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid},
            {"dx", m_dxSpin->value()},
            {"dy", m_dySpin->value()},
            {"dz", m_dzSpin->value()},
            {"rx", m_rxSpin->value()},
            {"ry", m_rySpin->value()},
            {"rz", m_rzSpin->value()},
            {"pivotMode", m_pivotModeCombo ? m_pivotModeCombo->currentData().toInt() : 1},
            {"pivotX", m_pivotXSpin->value()},
            {"pivotY", m_pivotYSpin->value()},
            {"pivotZ", m_pivotZSpin->value()},
            {"orderVersion", m_orderVersion}};
}

void TransformDialog::loadJson(const QJsonObject& obj)
{
    m_loadingState = true;
    m_committedState = QJsonObject();
    m_orderVersion = obj.contains("orderVersion") ? obj.value("orderVersion").toInt(2) : 1;
    m_sourceUuid = obj.value("sourceUuid").toString();
    m_dxSpin->setValue(obj.value("dx").toDouble(10.0));
    m_dySpin->setValue(obj.value("dy").toDouble(0.0));
    m_dzSpin->setValue(obj.value("dz").toDouble(0.0));
    m_rxSpin->setValue(obj.value("rx").toDouble(0.0));
    m_rySpin->setValue(obj.value("ry").toDouble(0.0));
    m_rzSpin->setValue(obj.value("rz").toDouble(0.0));
    const int pivotMode = obj.contains("pivotMode") ? obj.value("pivotMode").toInt(1) : 0;
    const int pivotIndex = m_pivotModeCombo ? m_pivotModeCombo->findData(pivotMode) : -1;
    if (m_pivotModeCombo && pivotIndex >= 0)
        m_pivotModeCombo->setCurrentIndex(pivotIndex);
    m_pivotXSpin->setValue(obj.value("pivotX").toDouble(0.0));
    m_pivotYSpin->setValue(obj.value("pivotY").toDouble(0.0));
    m_pivotZSpin->setValue(obj.value("pivotZ").toDouble(0.0));
    const bool customPivot = pivotMode == 2;
    m_pivotXSpin->setEnabled(customPivot);
    m_pivotYSpin->setEnabled(customPivot);
    m_pivotZSpin->setEnabled(customPivot);
    refreshSources();
    m_loadingState = false;
    m_committedState = saveJson();
    clearPreview(true);
}

void TransformDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void TransformDialog::refreshSources()
{
    fillShapeCombo(m_sourceCombo, m_sourceUuid, getUuid());
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    syncSingleShapeHighlight(kTransformPickDomain, getUuid(), sourceUuid);
    if (m_applyButton)
        m_applyButton->setEnabled(!sourceUuid.isEmpty());
    if (sourceUuid.isEmpty())
    {
        clearPreview(true);
        updateStatus(QString(), true);
        return;
    }
    updateStatus(QString(), true);
    updatePreview();
}

void TransformDialog::updatePreview()
{
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    TopoDS_Shape source;
    if (!sourceShapeCopy(sourceUuid, source))
    {
        clearPreview(true);
        if (m_applyButton)
            m_applyButton->setEnabled(false);
        updateStatus(sourceUuid.isEmpty() ? QString() : tr("来源已删除。"), false);
        return;
    }

    if (geometryObjectExists(getUuid()) && !m_committedState.isEmpty() && saveJson() == m_committedState)
    {
        clearPreview(true);
        updateStatus(QString(), true);
        return;
    }

    const double dx = m_dxSpin ? m_dxSpin->value() : 0.0;
    const double dy = m_dySpin ? m_dySpin->value() : 0.0;
    const double dz = m_dzSpin ? m_dzSpin->value() : 0.0;
    const double rx = m_rxSpin ? m_rxSpin->value() : 0.0;
    const double ry = m_rySpin ? m_rySpin->value() : 0.0;
    const double rz = m_rzSpin ? m_rzSpin->value() : 0.0;
    const bool identity = std::abs(dx) <= 1.0e-12 && std::abs(dy) <= 1.0e-12 &&
                          std::abs(dz) <= 1.0e-12 && std::abs(rx) <= 1.0e-12 &&
                          std::abs(ry) <= 1.0e-12 && std::abs(rz) <= 1.0e-12;
    if (m_applyButton)
        m_applyButton->setEnabled(!identity);
    if (identity)
    {
        clearPreview(true);
        updateStatus(tr("请输入位移或旋转量。"), false);
        return;
    }

    if (!m_previewCheck || !m_previewCheck->isChecked())
    {
        clearPreview(true);
        updateStatus(QString(), true);
        return;
    }

    if (!allowHeavyLivePreview({sourceUuid}))
    {
        clearPreview(true);
        updateStatus(tr("大型几何仅保留参数编辑与来源高亮；点击“应用”后执行实际变换。"), true);
        return;
    }

    const int pivotMode = m_pivotModeCombo ? m_pivotModeCombo->currentData().toInt() : 1;
    const double pivotX = m_pivotXSpin ? m_pivotXSpin->value() : 0.0;
    const double pivotY = m_pivotYSpin ? m_pivotYSpin->value() : 0.0;
    const double pivotZ = m_pivotZSpin ? m_pivotZSpin->value() : 0.0;
    TopoDS_Shape preview;
    if (!transformedPreviewShape(source, dx, dy, dz, rx, ry, rz,
                                 pivotMode, pivotX, pivotY, pivotZ, m_orderVersion, preview))
    {
        clearPreview(true);
        updateStatus(tr("无法预览。"), false);
        return;
    }

    showFeatureShapePreview(getUuid(), QStringLiteral("transform"), preview, true);
    updateStatus(QString(), true);
}

void TransformDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("transform"), renderNow);
}

void TransformDialog::onApplyClicked()
{
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    if (sourceUuid.isEmpty())
    {
        updateStatus(tr("请选择源对象。"), false);
        return;
    }

    const bool identity = std::abs(m_dxSpin->value()) <= 1.0e-12 &&
                          std::abs(m_dySpin->value()) <= 1.0e-12 &&
                          std::abs(m_dzSpin->value()) <= 1.0e-12 &&
                          std::abs(m_rxSpin->value()) <= 1.0e-12 &&
                          std::abs(m_rySpin->value()) <= 1.0e-12 &&
                          std::abs(m_rzSpin->value()) <= 1.0e-12;
    if (identity)
    {
        updateStatus(tr("请输入位移或旋转量。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateTransform(m_uuid, getDisplayName(), sourceUuid,
                                   m_dxSpin->value(), m_dySpin->value(), m_dzSpin->value(),
                                   m_rxSpin->value(), m_rySpin->value(), m_rzSpin->value(),
                                   m_pivotModeCombo ? m_pivotModeCombo->currentData().toInt() : 1,
                                   m_pivotXSpin->value(), m_pivotYSpin->value(), m_pivotZSpin->value(), m_orderVersion))
    {
        m_sourceUuid = sourceUuid;
        m_committedState = saveJson();
        clearPreview(true);
        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败。"), false);
    }
}

void TransformDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    syncSingleShapeHighlight(kTransformPickDomain, getUuid(),
                             m_sourceCombo ? m_sourceCombo->currentData().toString() : QString());
    updateStatus(QString(), true);
}

void TransformDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}

MirrorDialog::MirrorDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomMirror)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::MirrorDialog;
    ui->setupUi(this);
    m_sourceCombo = ui->sourceCombo;
    m_planeCombo = ui->planeCombo;
    m_planeCombo->addItems({tr("XY 平面"), tr("XZ 平面"), tr("YZ 平面")});
    m_offsetSpin = ui->offsetSpin;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    m_previewCheck = ui->previewCheck;
    setStatusText(m_statusLabel, QString(), true);

    m_applyButton->setText(tr("应用"));
    auto* pickSourceButton = ui->pickSourceButton;
    configurePickButton(pickSourceButton);
    m_pickPlaneFaceButton = ui->pickPlaneFaceButton;
    configurePickButton(m_pickPlaneFaceButton);

    connect(m_applyButton, &QPushButton::clicked, this, &MirrorDialog::onApplyClicked);
    connect(m_previewCheck, &QCheckBox::toggled, this, [this](bool)
    {
        cancelDeferredPreview(this);
        updatePreview();
    });
    connect(pickSourceButton, &QPushButton::clicked, this, [this, pickSourceButton]
    {
        clearPreview(true);
        const bool started =
            startGeometryObjectPick(this, pickSourceButton, ShapeComboFilter::Any, getUuid(), {},
                                    [this](const QString& uuid)
                                    {
                                        m_sourceUuid = uuid;
                                        setComboCurrentUuid(m_sourceCombo, uuid);
                                    }, tr("请在模型中选择要镜像的对象"));
        if (!started)
            updatePreview();
    });
    connect(m_pickPlaneFaceButton, &QPushButton::clicked, this, [this]
    {
        // 基准面选择需要直接看见原几何；临时移除镜像结果和旧参考框。
        clearPreview(true);
        const QString sourceUuid =
            m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
        if (sourceUuid.isEmpty())
        {
            updateStatus(tr("请先选择要镜像的对象。"), false);
            return;
        }

        const bool started = GeometryViewportPickController::startTopologyScene(
            this, m_pickPlaneFaceButton, GeometryPickMode::Face,
            []() -> QString { return QString(); },
            [this](const QString& faceUuid, int faceIndex)
            {
                int planeType = 0;
                double offset = 0.0;
                if (!principalMirrorPlaneFromFace(faceUuid, faceIndex, planeType, offset))
                {
                    Q_UNUSED(faceIndex);
                    updateStatus(tr("请选择与 XY、XZ 或 YZ 平行的平面。"), false);
                    return;
                }

                const QSignalBlocker planeBlocker(m_planeCombo);
                const QSignalBlocker offsetBlocker(m_offsetSpin);
                if (m_planeCombo)
                    m_planeCombo->setCurrentIndex(planeType);
                if (m_offsetSpin)
                    m_offsetSpin->setValue(offset);

                GeometryPickManager::instance().clearPreview(kMirrorPickDomain, getUuid());
                GeometryViewportPickController::stop(this, true);
                updatePreview();
                updateStatus(tr("已设置镜像基准面。"), true);
            },
            getUuid(), {}, {},
            tr("请选择一个平面"),
            [this](const QString& uuid, int faceIndex)
            {
                if (!uuid.isEmpty() && faceIndex > 0)
                    GeometryPickManager::instance().setPreview(
                        kMirrorPickDomain, getUuid(),
                        {uuid, GeometryPickMode::Face, faceIndex});
            },
            [this]()
            {
                GeometryPickManager::instance().clearPreview(kMirrorPickDomain, getUuid());
            });

        if (!started)
        {
            updatePreview();
            updateStatus(tr("当前无法从模型中选择平面，请重新进入此功能。"), false);
        }
    });

    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        syncSingleShapeHighlight(kMirrorPickDomain, getUuid(),
                                 m_sourceCombo ? m_sourceCombo->currentData().toString() : QString());
        updatePreview();
    });
    connect(m_planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { scheduleDeferredPreview(this, [this] { updatePreview(); }); });
    connect(m_offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { scheduleDeferredPreview(this, [this] { updatePreview(); }); });
    delete ui;
}

void MirrorDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
    updatePreview();
}

void MirrorDialog::beforeUnload()
{
    cancelDeferredPreview(this);
    stopGeometryObjectPick(this);
    clearGeometryPick(kMirrorPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject MirrorDialog::saveJson()
{
    return {{"sourceUuid", m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid},
            {"plane", m_planeCombo ? m_planeCombo->currentIndex() : 0},
            {"offset", m_offsetSpin ? m_offsetSpin->value() : 0.0}};
}

void MirrorDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_sourceUuid = obj.value("sourceUuid").toString();
    const QSignalBlocker planeBlocker(m_planeCombo);
    const QSignalBlocker offsetBlocker(m_offsetSpin);
    if (m_planeCombo)
        m_planeCombo->setCurrentIndex(obj.value("plane").toInt(0));
    if (m_offsetSpin)
        m_offsetSpin->setValue(obj.value("offset").toDouble(0.0));
    refreshSources();
    m_committedState = saveJson();
    clearPreview(true);
}

void MirrorDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void MirrorDialog::refreshSources()
{
    fillShapeCombo(m_sourceCombo, m_sourceUuid, getUuid());
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    syncSingleShapeHighlight(kMirrorPickDomain, getUuid(), sourceUuid);
    if (m_applyButton)
        m_applyButton->setEnabled(!sourceUuid.isEmpty());
    if (sourceUuid.isEmpty())
    {
        clearPreview(true);
        updateStatus(QString(), true);
        return;
    }
    updateStatus(QString(), true);
    updatePreview();
}

void MirrorDialog::updatePreview()
{
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    TopoDS_Shape source;
    if (!sourceShapeCopy(sourceUuid, source))
    {
        clearPreview(true);
        if (m_applyButton)
            m_applyButton->setEnabled(false);
        updateStatus(sourceUuid.isEmpty() ? QString() : tr("来源已删除。"), false);
        return;
    }

    const int plane = m_planeCombo ? m_planeCombo->currentIndex() : 0;
    const double offset = m_offsetSpin ? m_offsetSpin->value() : 0.0;
    showMirrorPlanePreview(getUuid(), sourceUuid, plane, offset, false);

    // 基准面参考框属于操作反馈，始终保留；镜像后的实体由复选框控制。
    if (!m_previewCheck || !m_previewCheck->isChecked())
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("mirror"), true);
        updateStatus(QString(), true);
        return;
    }

    if (geometryObjectExists(getUuid()) && !m_committedState.isEmpty() && saveJson() == m_committedState)
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("mirror"), true);
        updateStatus(QString(), true);
        return;
    }

    if (!allowHeavyLivePreview({sourceUuid}))
    {
        clearFeatureShapePreview(getUuid(), QStringLiteral("mirror"), true);
        updateStatus(tr("大型几何仅显示镜像基准面；点击“应用”后生成镜像结果。"), true);
        return;
    }

    TopoDS_Shape preview;
    if (!mirroredPreviewShape(source, plane, offset, preview))
    {
        clearPreview(true);
        updateStatus(tr("无法预览。"), false);
        return;
    }

    showFeatureShapePreview(getUuid(), QStringLiteral("mirror"), preview, true);
    updateStatus(QString(), true);
}

void MirrorDialog::clearPreview(bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(featurePreviewFaceId(getUuid(), QStringLiteral("mirror")), false);
        vis->removeProp(featurePreviewEdgeId(getUuid(), QStringLiteral("mirror")), false);
        vis->removeProp(mirrorPlanePreviewId(getUuid()), renderNow);
    }
}

void MirrorDialog::onApplyClicked()
{
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    if (sourceUuid.isEmpty())
    {
        updateStatus(tr("请选择源对象。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateMirror(m_uuid, getDisplayName(), sourceUuid,
                                m_planeCombo->currentIndex(), m_offsetSpin->value()))
    {
        m_sourceUuid = sourceUuid;
        m_committedState = saveJson();
        clearPreview(true);
        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败。"), false);
    }
}

void MirrorDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    syncSingleShapeHighlight(kMirrorPickDomain, getUuid(),
                             m_sourceCombo ? m_sourceCombo->currentData().toString() : QString());
    updateStatus(QString(), true);
}

void MirrorDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}


FilletDialog::FilletDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomFillet)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::FilletDialog;
    ui->setupUi(this);

    m_sourceCombo = ui->sourceCombo;
    m_radiusSpin = ui->radiusSpin;
    m_selectedEdgesList = ui->selectedEdgesList;
    m_pickEdgeButton = ui->pickEdgeButton;
    m_removeEdgeButton = ui->removeEdgeButton;
    m_selectedEdgesLabel = ui->selectedEdgesLabel;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;

    setStatusText(m_statusLabel, QString(), true);
    m_applyButton->setText(tr("应用"));

    if (m_selectedEdgesList)
        m_selectedEdgesList->setSelectionMode(QAbstractItemView::ExtendedSelection);


    connect(m_applyButton, &QPushButton::clicked, this, &FilletDialog::onApplyClicked);

    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();

        if (!m_selectionSourceUuid.isEmpty() && m_selectionSourceUuid != sourceUuid)
        {
            m_edgeSelection.clear();
            m_edgeSelectionDirty = false;
            m_selectedEdgeIndices.clear();
        }

        m_selectionSourceUuid = sourceUuid;
        reloadStoredEdgeSelection();

        const bool canPick = hasAnyVisibleCadEdgeSource(getUuid());
        if (m_pickEdgeButton)
        {
            m_pickEdgeButton->setEnabled(canPick);

        }

        updatePreview();
    });

    connect(m_radiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updatePreview(); });

    connect(m_pickEdgeButton, &QPushButton::clicked, this,
            [this] { setViewportPickingEnabled(true); });

    connect(m_removeEdgeButton, &QPushButton::clicked,
            this, &FilletDialog::removeSelectedEdges);

    connect(ui->clearEdgesButton, &QPushButton::clicked, this, [this]()
    {
        m_selectedEdgeIndices.clear();
        m_edgeSelection.clear();
        m_edgeSelectionDirty = false;
        updateSelectedEdgesUi();
        GeometryPickManager::instance().clearPreview(kFilletPickDomain, getUuid());
        syncEdgeHighlight();
        updatePreview();
    });

    if (m_selectedEdgesList)
    {
        connect(m_selectedEdgesList, &QListWidget::itemSelectionChanged, this, [this]()
        {
            if (m_removeEdgeButton)
                m_removeEdgeButton->setEnabled(!m_selectedEdgesList->selectedItems().isEmpty());

            const QList<QListWidgetItem*> items = m_selectedEdgesList->selectedItems();
            if (items.size() == 1)
            {
                const int edge = items.front()->data(Qt::UserRole).toInt();
                const QString sourceUuid =
                    m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
                if (!sourceUuid.isEmpty() && edge > 0)
                {
                    GeometryPickManager::instance().setPreview(
                        kFilletPickDomain, getUuid(),
                        {sourceUuid, GeometryPickMode::Edge, edge});
                    return;
                }
            }
            GeometryPickManager::instance().clearPreview(kFilletPickDomain, getUuid());
        });
    }

    delete ui;
}

FilletDialog::~FilletDialog()
{
    stopGeometryObjectPick(this);
    stopViewportPicking();
}

void FilletDialog::beforeLoad()
{
    refreshSources();

    if (m_committedState.isEmpty())
        m_committedState = saveJson();

    updatePreview();
}

void FilletDialog::beforeUnload()
{
    // Page navigation only suspends the viewport presentation.  The selected CAD
    // references are feature parameters and must survive leaving/re-entering the
    // property page.  Convert the cheap interactive indices to the stable edge-v3
    // form once here (not on every click), then clear only transient visuals.
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
    if (m_edgeSelectionDirty && !sourceUuid.isEmpty() &&
        sourceUuid == m_selectionSourceUuid)
    {
        m_edgeSelection = m_selectedEdgeIndices.isEmpty()
            ? QString()
            : stableEdgeSelection(sourceUuid, m_selectedEdgeIndices);
        m_edgeSelectionDirty = false;
    }
    m_sourceUuid = sourceUuid;

    stopGeometryObjectPick(this);
    stopViewportPicking();
    clearGeometryPick(kFilletPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject FilletDialog::saveJson()
{
    return {{"sourceUuid", m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid},
            {"radius", m_radiusSpin->value()},
            {"edgeSelection", selectedEdges()}};
}

void FilletDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_sourceUuid = obj.value("sourceUuid").toString();
    m_selectionSourceUuid = m_sourceUuid;
    m_edgeSelection = obj.value("edgeSelection").toString();
    m_edgeSelectionDirty = false;
    m_selectedEdgeIndices.clear();

    m_radiusSpin->setValue(obj.value("radius").toDouble(0.2));
    refreshSources();

    m_committedState = saveJson();
    clearPreview(true);
}

void FilletDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_selectionSourceUuid = uuid;
    m_edgeSelection.clear();
    m_edgeSelectionDirty = false;
    m_selectedEdgeIndices.clear();
    m_committedState = QJsonObject();
    refreshSources();
}

void FilletDialog::refreshSources()
{
    fillShapeComboFiltered(m_sourceCombo, m_sourceUuid, getUuid(), ShapeComboFilter::EdgeSource);

    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    if (m_selectionSourceUuid.isEmpty())
        m_selectionSourceUuid = sourceUuid;

    reloadStoredEdgeSelection();

    const bool canPick = hasAnyVisibleCadEdgeSource(getUuid());
    if (m_pickEdgeButton)
        m_pickEdgeButton->setEnabled(canPick);

    updateStatus(canPick ? QString()
                         : tr("当前模型中没有可选择的边。"),
                 canPick);
}

QString FilletDialog::selectedEdges() const
{
    if (m_selectedEdgeIndices.isEmpty())
        return QString();

    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
    if (sourceUuid.isEmpty() || sourceUuid != m_selectionSourceUuid)
        return QString();

    // 交互点击时只维护轻量的 TopExp edge index 集合，不立即生成 edge-v3。
    // edge-v3 编码需要 MapShapesAndAncestors + 几何描述，放到真正保存/应用时再做，
    // 避免大型 STEP 每点一条边都重新扫描全模型。
    if (!m_edgeSelectionDirty)
        return m_edgeSelection;
    return stableEdgeSelection(sourceUuid, m_selectedEdgeIndices);
}

void FilletDialog::reloadStoredEdgeSelection()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;

    // A dirty set is the live, not-yet-serialized draft.  Do not destroy it just
    // because refreshSources() ran while the same page/source is still active.
    if (m_edgeSelectionDirty && !sourceUuid.isEmpty() &&
        sourceUuid == m_selectionSourceUuid)
    {
        updateSelectedEdgesUi();
        syncEdgeHighlight();
        return;
    }

    m_selectedEdgeIndices.clear();

    if (sourceUuid.isEmpty())
    {
        m_selectionSourceUuid.clear();
        m_edgeSelection.clear();
        m_edgeSelectionDirty = false;
        updateSelectedEdgesUi();
        syncEdgeHighlight();
        return;
    }

    if (m_selectionSourceUuid != sourceUuid)
    {
        m_selectionSourceUuid = sourceUuid;
        m_edgeSelection.clear();
        m_edgeSelectionDirty = false;
    }

    if (!m_edgeSelection.trimmed().isEmpty())
    {
        bool resolvedOk = false;
        m_selectedEdgeIndices =
            resolvedEdgeIndices(sourceUuid, m_edgeSelection, &resolvedOk);

        if (!resolvedOk)
        {
            m_selectedEdgeIndices.clear();
            m_edgeSelection.clear();
            m_edgeSelectionDirty = false;
            updateStatus(tr("原来的边已无法定位，请重新选择。"), false);
        }
        else
        {
            // 旧的 "1,2,3" 形式只在这里解析一次，并立即升级为稳定选择描述。
            m_edgeSelection = stableEdgeSelection(sourceUuid, m_selectedEdgeIndices);
            m_edgeSelectionDirty = false;
        }
    }

    updateSelectedEdgesUi();
    syncEdgeHighlight();
}

void FilletDialog::removeSelectedEdges()
{
    if (!m_selectedEdgesList)
        return;

    QSet<int> next = m_selectedEdgeIndices;
    for (QListWidgetItem* item : m_selectedEdgesList->selectedItems())
    {
        if (item)
            next.remove(item->data(Qt::UserRole).toInt());
    }

    m_selectedEdgeIndices = next;

    m_edgeSelectionDirty = true;

    GeometryPickManager::instance().clearPreview(kFilletPickDomain, getUuid());
    updateSelectedEdgesUi();
    syncEdgeHighlight();
    updatePreview();
}

void FilletDialog::updateSelectedEdgesUi()
{
    QList<int> values = m_selectedEdgeIndices.values();
    std::sort(values.begin(), values.end());

    if (m_selectedEdgesList)
    {
        const QSignalBlocker blocker(m_selectedEdgesList);
        m_selectedEdgesList->clear();
        for (int edge : values)
        {
            auto* item = new QListWidgetItem(tr("边 %1").arg(edge), m_selectedEdgesList);
            item->setData(Qt::UserRole, edge);
        }
    }

    if (m_selectedEdgesLabel)
    {
        m_selectedEdgesLabel->setText(
            values.isEmpty()
                ? tr("已选择 0 条边")
                : tr("已选择 %1 条边").arg(values.size()));
    }

    if (m_removeEdgeButton)
        m_removeEdgeButton->setEnabled(false);
}

void FilletDialog::syncEdgeHighlight()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();

    QVector<GeometryPickItem> items;
    QList<int> values = m_selectedEdgeIndices.values();
    std::sort(values.begin(), values.end());
    items.reserve(values.size());
    for (int edge : values)
        items.push_back({sourceUuid, GeometryPickMode::Edge, edge});

    if (sourceUuid.isEmpty() || items.isEmpty())
        GeometryPickManager::instance().clearSelection(kFilletPickDomain, getUuid());
    else
        GeometryPickManager::instance().setSelection(kFilletPickDomain, getUuid(), items);
}

void FilletDialog::updatePreview()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();

    const bool valid = !sourceUuid.isEmpty() &&
                       !m_selectedEdgeIndices.isEmpty() &&
                       m_radiusSpin &&
                       m_radiusSpin->value() > GeometryBuildUtils::kEps;

    if (m_applyButton)
        m_applyButton->setEnabled(valid);

    // 圆角/倒角属于可能很重的 OCC 运算。参数编辑阶段只保持选边高亮，
    // 不在每次 spinBox valueChanged 时自动 Build，避免大型导入件输入参数时卡顿。
    clearPreview(false);
}

void FilletDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("fillet"), renderNow);
}

void FilletDialog::setViewportPickingEnabled(bool enabled)
{
    if (!enabled)
    {
        stopViewportPicking();
        return;
    }

    if (!hasAnyVisibleCadEdgeSource(getUuid()))
    {
        updateStatus(tr("当前模型中没有可选择的边。"), false);
        return;
    }

    const bool started = GeometryViewportPickController::startTopologyScene(
        this, m_pickEdgeButton, GeometryPickMode::Edge,
        [this]() -> QString
        {
            // No selected edge => true global viewport selection.  The combo box is
            // only a display/helper control and does not constrain hover/picking.
            // Once the first edge is accepted, OCC fillet requires subsequent edges
            // to come from the same source shape, so lock to that source.
            return m_selectedEdgeIndices.isEmpty() ? QString() : m_selectionSourceUuid;
        },
        [this](const QString& uuid, int edgeIndex)
        {
            if (uuid.isEmpty() || edgeIndex <= 0)
                return;

            if (m_selectedEdgeIndices.isEmpty())
            {
                // First viewport edge chooses the feature source automatically.
                m_sourceUuid = uuid;
                m_selectionSourceUuid = uuid;
                m_edgeSelection.clear();
                m_edgeSelectionDirty = false;
                setComboCurrentUuid(m_sourceCombo, uuid);
            }
            else if (uuid != m_selectionSourceUuid)
            {
                updateStatus(tr("圆角一次只能选择同一对象上的边；请先清空当前选边再切换对象。"),
                             false);
                return;
            }

            togglePickedEdge(edgeIndex);
        },
        getUuid(), {},
        [](const QString&, GeomShapeObject* obj)
        {
            if (!obj)
                return false;
            vtkActor* actor = obj->getEdgeActor();
            auto* mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
            vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
            return poly && poly->GetNumberOfLines() > 0 && poly->GetCellData() &&
                   poly->GetCellData()->GetArray("OCC_EdgeId");
        },
        tr("正在选择边"),
        [this](const QString& uuid, int edgeIndex)
        {
            if (!uuid.isEmpty() && edgeIndex > 0)
            {
                GeometryPickManager::instance().setPreview(
                    kFilletPickDomain, getUuid(),
                    {uuid, GeometryPickMode::Edge, edgeIndex});
            }
        },
        [this]()
        {
            GeometryPickManager::instance().clearPreview(kFilletPickDomain, getUuid());
        });

    if (!started)
    {
        updateStatus(tr("当前无法从模型中选择边，请重新进入此功能。"), false);
        return;
    }

    updateStatus(tr("请在模型上依次选择需要圆角的边，完成后点击“应用”。"), true);
}

void FilletDialog::stopViewportPicking()
{
    GeometryViewportPickController::stop(this, true);
}

void FilletDialog::togglePickedEdge(int edgeIndex)
{
    if (edgeIndex <= 0)
        return;

    if (m_selectedEdgeIndices.contains(edgeIndex))
        m_selectedEdgeIndices.remove(edgeIndex);
    else
        m_selectedEdgeIndices.insert(edgeIndex);

    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
    m_selectionSourceUuid = sourceUuid;
    m_edgeSelectionDirty = true;

    GeometryPickManager::instance().clearPreview(kFilletPickDomain, getUuid());
    updateSelectedEdgesUi();
    syncEdgeHighlight();
    updatePreview();
    updateStatus(QString(), true);
}

void FilletDialog::onApplyClicked()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    const QString edges = selectedEdges();

    if (sourceUuid.isEmpty())
    {
        updateStatus(tr("未选择源对象。"), false);
        return;
    }
    if (edges.isEmpty())
    {
        updateStatus(tr("请先选择至少一条边。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateFillet(
            m_uuid, getDisplayName(), sourceUuid, m_radiusSpin->value(), edges))
    {
        m_sourceUuid = sourceUuid;
        m_selectionSourceUuid = sourceUuid;
        m_edgeSelection = edges;
        m_edgeSelectionDirty = false;
        m_committedState = saveJson();
        clearPreview(true);
        clearGeometryPick(kFilletPickDomain, getUuid());

        // 应用后源对象通常会进入 consumed/hidden 状态。继续保持“选边模式”会让
        // 用户误以为还能在结果体上追加原始边，因此应用成功后明确退出拾取。

        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败：请减小半径，或检查所选边是否适合圆角。"), false);
    }
}

void FilletDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;

    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);

    clearPreview(true);
    syncEdgeHighlight();
    updateStatus(QString(), true);
}

void FilletDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}

ChamferDialog::ChamferDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomChamfer)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::ChamferDialog;
    ui->setupUi(this);

    m_sourceCombo = ui->sourceCombo;
    m_distanceSpin = ui->distanceSpin;
    m_selectedEdgesList = ui->selectedEdgesList;
    m_pickEdgeButton = ui->pickEdgeButton;
    m_removeEdgeButton = ui->removeEdgeButton;
    m_selectedEdgesLabel = ui->selectedEdgesLabel;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;

    setStatusText(m_statusLabel, QString(), true);
    m_applyButton->setText(tr("应用"));

    if (m_selectedEdgesList)
        m_selectedEdgesList->setSelectionMode(QAbstractItemView::ExtendedSelection);


    connect(m_applyButton, &QPushButton::clicked, this, &ChamferDialog::onApplyClicked);

    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();

        if (!m_selectionSourceUuid.isEmpty() && m_selectionSourceUuid != sourceUuid)
        {
            m_edgeSelection.clear();
            m_edgeSelectionDirty = false;
            m_selectedEdgeIndices.clear();
        }

        m_selectionSourceUuid = sourceUuid;
        reloadStoredEdgeSelection();

        const bool canPick = hasAnyVisibleCadEdgeSource(getUuid());
        if (m_pickEdgeButton)
        {
            m_pickEdgeButton->setEnabled(canPick);

        }

        updatePreview();
    });

    connect(m_distanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updatePreview(); });

    connect(m_pickEdgeButton, &QPushButton::clicked, this,
            [this] { setViewportPickingEnabled(true); });

    connect(m_removeEdgeButton, &QPushButton::clicked,
            this, &ChamferDialog::removeSelectedEdges);

    connect(ui->clearEdgesButton, &QPushButton::clicked, this, [this]()
    {
        m_selectedEdgeIndices.clear();
        m_edgeSelection.clear();
        m_edgeSelectionDirty = false;
        updateSelectedEdgesUi();
        GeometryPickManager::instance().clearPreview(kChamferPickDomain, getUuid());
        syncEdgeHighlight();
        updatePreview();
    });

    if (m_selectedEdgesList)
    {
        connect(m_selectedEdgesList, &QListWidget::itemSelectionChanged, this, [this]()
        {
            if (m_removeEdgeButton)
                m_removeEdgeButton->setEnabled(!m_selectedEdgesList->selectedItems().isEmpty());

            const QList<QListWidgetItem*> items = m_selectedEdgesList->selectedItems();
            if (items.size() == 1)
            {
                const int edge = items.front()->data(Qt::UserRole).toInt();
                const QString sourceUuid =
                    m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
                if (!sourceUuid.isEmpty() && edge > 0)
                {
                    GeometryPickManager::instance().setPreview(
                        kChamferPickDomain, getUuid(),
                        {sourceUuid, GeometryPickMode::Edge, edge});
                    return;
                }
            }
            GeometryPickManager::instance().clearPreview(kChamferPickDomain, getUuid());
        });
    }

    delete ui;
}

ChamferDialog::~ChamferDialog()
{
    stopGeometryObjectPick(this);
    stopViewportPicking();
}

void ChamferDialog::beforeLoad()
{
    refreshSources();

    if (m_committedState.isEmpty())
        m_committedState = saveJson();

    updatePreview();
}

void ChamferDialog::beforeUnload()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
    if (m_edgeSelectionDirty && !sourceUuid.isEmpty() &&
        sourceUuid == m_selectionSourceUuid)
    {
        m_edgeSelection = m_selectedEdgeIndices.isEmpty()
            ? QString()
            : stableEdgeSelection(sourceUuid, m_selectedEdgeIndices);
        m_edgeSelectionDirty = false;
    }
    m_sourceUuid = sourceUuid;

    stopGeometryObjectPick(this);
    stopViewportPicking();
    clearGeometryPick(kChamferPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject ChamferDialog::saveJson()
{
    return {{"sourceUuid", m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid},
            {"distance", m_distanceSpin->value()},
            {"edgeSelection", selectedEdges()}};
}

void ChamferDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_sourceUuid = obj.value("sourceUuid").toString();
    m_selectionSourceUuid = m_sourceUuid;
    m_edgeSelection = obj.value("edgeSelection").toString();
    m_edgeSelectionDirty = false;
    m_selectedEdgeIndices.clear();

    m_distanceSpin->setValue(obj.value("distance").toDouble(0.2));
    refreshSources();

    m_committedState = saveJson();
    clearPreview(true);
}

void ChamferDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_selectionSourceUuid = uuid;
    m_edgeSelection.clear();
    m_edgeSelectionDirty = false;
    m_selectedEdgeIndices.clear();
    m_committedState = QJsonObject();
    refreshSources();
}

void ChamferDialog::refreshSources()
{
    fillShapeComboFiltered(m_sourceCombo, m_sourceUuid, getUuid(), ShapeComboFilter::EdgeSource);

    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    if (m_selectionSourceUuid.isEmpty())
        m_selectionSourceUuid = sourceUuid;

    reloadStoredEdgeSelection();

    const bool canPick = hasAnyVisibleCadEdgeSource(getUuid());
    if (m_pickEdgeButton)
        m_pickEdgeButton->setEnabled(canPick);

    updateStatus(canPick ? QString()
                         : tr("当前模型中没有可选择的边。"),
                 canPick);
}

QString ChamferDialog::selectedEdges() const
{
    if (m_selectedEdgeIndices.isEmpty())
        return QString();

    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
    if (sourceUuid.isEmpty() || sourceUuid != m_selectionSourceUuid)
        return QString();

    if (!m_edgeSelectionDirty)
        return m_edgeSelection;
    return stableEdgeSelection(sourceUuid, m_selectedEdgeIndices);
}

void ChamferDialog::reloadStoredEdgeSelection()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;

    // A dirty set is the live, not-yet-serialized draft.  Do not destroy it just
    // because refreshSources() ran while the same page/source is still active.
    if (m_edgeSelectionDirty && !sourceUuid.isEmpty() &&
        sourceUuid == m_selectionSourceUuid)
    {
        updateSelectedEdgesUi();
        syncEdgeHighlight();
        return;
    }

    m_selectedEdgeIndices.clear();

    if (sourceUuid.isEmpty())
    {
        m_selectionSourceUuid.clear();
        m_edgeSelection.clear();
        m_edgeSelectionDirty = false;
        updateSelectedEdgesUi();
        syncEdgeHighlight();
        return;
    }

    if (m_selectionSourceUuid != sourceUuid)
    {
        m_selectionSourceUuid = sourceUuid;
        m_edgeSelection.clear();
        m_edgeSelectionDirty = false;
    }

    if (!m_edgeSelection.trimmed().isEmpty())
    {
        bool resolvedOk = false;
        m_selectedEdgeIndices =
            resolvedEdgeIndices(sourceUuid, m_edgeSelection, &resolvedOk);

        if (!resolvedOk)
        {
            m_selectedEdgeIndices.clear();
            m_edgeSelection.clear();
            m_edgeSelectionDirty = false;
            updateStatus(tr("原来的边已无法定位，请重新选择。"), false);
        }
        else
        {
            m_edgeSelection = stableEdgeSelection(sourceUuid, m_selectedEdgeIndices);
            m_edgeSelectionDirty = false;
        }
    }

    updateSelectedEdgesUi();
    syncEdgeHighlight();
}

void ChamferDialog::removeSelectedEdges()
{
    if (!m_selectedEdgesList)
        return;

    QSet<int> next = m_selectedEdgeIndices;
    for (QListWidgetItem* item : m_selectedEdgesList->selectedItems())
    {
        if (item)
            next.remove(item->data(Qt::UserRole).toInt());
    }

    m_selectedEdgeIndices = next;

    m_edgeSelectionDirty = true;

    GeometryPickManager::instance().clearPreview(kChamferPickDomain, getUuid());
    updateSelectedEdgesUi();
    syncEdgeHighlight();
    updatePreview();
}

void ChamferDialog::updateSelectedEdgesUi()
{
    QList<int> values = m_selectedEdgeIndices.values();
    std::sort(values.begin(), values.end());

    if (m_selectedEdgesList)
    {
        const QSignalBlocker blocker(m_selectedEdgesList);
        m_selectedEdgesList->clear();
        for (int edge : values)
        {
            auto* item = new QListWidgetItem(tr("边 %1").arg(edge), m_selectedEdgesList);
            item->setData(Qt::UserRole, edge);
        }
    }

    if (m_selectedEdgesLabel)
    {
        m_selectedEdgesLabel->setText(
            values.isEmpty()
                ? tr("已选择 0 条边")
                : tr("已选择 %1 条边").arg(values.size()));
    }

    if (m_removeEdgeButton)
        m_removeEdgeButton->setEnabled(false);
}

void ChamferDialog::syncEdgeHighlight()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();

    QVector<GeometryPickItem> items;
    QList<int> values = m_selectedEdgeIndices.values();
    std::sort(values.begin(), values.end());
    items.reserve(values.size());
    for (int edge : values)
        items.push_back({sourceUuid, GeometryPickMode::Edge, edge});

    if (sourceUuid.isEmpty() || items.isEmpty())
        GeometryPickManager::instance().clearSelection(kChamferPickDomain, getUuid());
    else
        GeometryPickManager::instance().setSelection(kChamferPickDomain, getUuid(), items);
}

void ChamferDialog::updatePreview()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();

    const bool valid = !sourceUuid.isEmpty() &&
                       !m_selectedEdgeIndices.isEmpty() &&
                       m_distanceSpin &&
                       m_distanceSpin->value() > GeometryBuildUtils::kEps;

    if (m_applyButton)
        m_applyButton->setEnabled(valid);

    clearPreview(false);
}

void ChamferDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("chamfer"), renderNow);
}

void ChamferDialog::setViewportPickingEnabled(bool enabled)
{
    if (!enabled)
    {
        stopViewportPicking();
        return;
    }

    if (!hasAnyVisibleCadEdgeSource(getUuid()))
    {
        updateStatus(tr("当前模型中没有可选择的边。"), false);
        return;
    }

    const bool started = GeometryViewportPickController::startTopologyScene(
        this, m_pickEdgeButton, GeometryPickMode::Edge,
        [this]() -> QString
        {
            return m_selectedEdgeIndices.isEmpty() ? QString() : m_selectionSourceUuid;
        },
        [this](const QString& uuid, int edgeIndex)
        {
            if (uuid.isEmpty() || edgeIndex <= 0)
                return;

            if (m_selectedEdgeIndices.isEmpty())
            {
                m_sourceUuid = uuid;
                m_selectionSourceUuid = uuid;
                m_edgeSelection.clear();
                m_edgeSelectionDirty = false;
                setComboCurrentUuid(m_sourceCombo, uuid);
            }
            else if (uuid != m_selectionSourceUuid)
            {
                updateStatus(tr("倒角一次只能选择同一对象上的边；请先清空当前选边再切换对象。"),
                             false);
                return;
            }

            togglePickedEdge(edgeIndex);
        },
        getUuid(), {},
        [](const QString&, GeomShapeObject* obj)
        {
            if (!obj)
                return false;
            vtkActor* actor = obj->getEdgeActor();
            auto* mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
            vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
            return poly && poly->GetNumberOfLines() > 0 && poly->GetCellData() &&
                   poly->GetCellData()->GetArray("OCC_EdgeId");
        },
        tr("正在选择边"),
        [this](const QString& uuid, int edgeIndex)
        {
            if (!uuid.isEmpty() && edgeIndex > 0)
            {
                GeometryPickManager::instance().setPreview(
                    kChamferPickDomain, getUuid(),
                    {uuid, GeometryPickMode::Edge, edgeIndex});
            }
        },
        [this]()
        {
            GeometryPickManager::instance().clearPreview(kChamferPickDomain, getUuid());
        });

    if (!started)
    {
        updateStatus(tr("当前无法从模型中选择边，请重新进入此功能。"), false);
        return;
    }

    updateStatus(tr("请在模型上依次选择需要倒角的边，完成后点击“应用”。"), true);
}

void ChamferDialog::stopViewportPicking()
{
    GeometryViewportPickController::stop(this, true);
}

void ChamferDialog::togglePickedEdge(int edgeIndex)
{
    if (edgeIndex <= 0)
        return;

    if (m_selectedEdgeIndices.contains(edgeIndex))
        m_selectedEdgeIndices.remove(edgeIndex);
    else
        m_selectedEdgeIndices.insert(edgeIndex);

    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid;
    m_selectionSourceUuid = sourceUuid;
    m_edgeSelectionDirty = true;

    GeometryPickManager::instance().clearPreview(kChamferPickDomain, getUuid());
    updateSelectedEdgesUi();
    syncEdgeHighlight();
    updatePreview();
    updateStatus(QString(), true);
}

void ChamferDialog::onApplyClicked()
{
    const QString sourceUuid =
        m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    const QString edges = selectedEdges();

    if (sourceUuid.isEmpty())
    {
        updateStatus(tr("未选择源对象。"), false);
        return;
    }
    if (edges.isEmpty())
    {
        updateStatus(tr("请先选择至少一条边。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    if (service->recreateChamfer(
            m_uuid, getDisplayName(), sourceUuid, m_distanceSpin->value(), edges))
    {
        m_sourceUuid = sourceUuid;
        m_selectionSourceUuid = sourceUuid;
        m_edgeSelection = edges;
        m_edgeSelectionDirty = false;
        m_committedState = saveJson();
        clearPreview(true);
        clearGeometryPick(kChamferPickDomain, getUuid());

        // 应用后源对象通常会进入 consumed/hidden 状态。继续保持“选边模式”会让
        // 用户误以为还能在结果体上追加原始边，因此应用成功后明确退出拾取。

        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败：请减小距离，或检查所选边是否适合倒角。"), false);
    }
}

void ChamferDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;

    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);

    clearPreview(true);
    syncEdgeHighlight();
    updateStatus(QString(), true);
}

void ChamferDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}

SplitDialog::SplitDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomSplit)
{
    Q_UNUSED(parent);
    auto* ui = new Ui::SplitDialog;
    ui->setupUi(this);
    m_sourceCombo = ui->sourceCombo;
    m_toolCombo = ui->toolCombo;
    m_statusLabel = ui->statusLabel;
    m_applyButton = ui->applyButton;
    setStatusText(m_statusLabel, QString(), true);
    m_applyButton->setText(tr("应用"));
    auto* pickSourceButton = ui->pickSourceButton;
    auto* pickToolButton = ui->pickToolButton;
    configurePickButton(pickSourceButton);
    configurePickButton(pickToolButton);

    connect(m_applyButton, &QPushButton::clicked, this, &SplitDialog::onApplyClicked);
    connect(pickSourceButton, &QPushButton::clicked, this, [this, pickSourceButton]
    {
        const QString toolUuid = m_toolCombo ? m_toolCombo->currentData().toString() : QString();
        startGeometryObjectPick(this, pickSourceButton, ShapeComboFilter::Any, getUuid(),
                                {toolUuid}, [this](const QString& uuid)
                                {
                                    m_sourceUuid = uuid;
                                    refreshSources();
                                }, tr("请在模型中选择被分割对象"));
    });
    connect(pickToolButton, &QPushButton::clicked, this, [this, pickToolButton]
    {
        const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
        startGeometryObjectPick(this, pickToolButton, ShapeComboFilter::Any, getUuid(),
                                {sourceUuid}, [this](const QString& uuid)
                                {
                                    m_toolUuid = uuid;
                                    refreshSources();
                                }, tr("请在模型中选择分割工具"));
    });
    const auto syncInputs = [this]()
    {
        const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
        const QString toolUuid = m_toolCombo ? m_toolCombo->currentData().toString() : QString();
        syncMultiShapeHighlight(kSplitPickDomain, getUuid(), {sourceUuid, toolUuid});
        const bool ready = !sourceUuid.isEmpty() && !toolUuid.isEmpty() && sourceUuid != toolUuid;
        if (m_applyButton)
            m_applyButton->setEnabled(ready);
        updateStatus(ready ? QString() : tr("请选择两个不同对象。"), ready);
        updatePreview();
    };
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [syncInputs](int) { syncInputs(); });
    connect(m_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [syncInputs](int) { syncInputs(); });
    delete ui;
}

void SplitDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
    updatePreview();
}

void SplitDialog::beforeUnload()
{
    stopGeometryObjectPick(this);
    clearGeometryPick(kSplitPickDomain, getUuid());
    clearPreview(true);
}

QJsonObject SplitDialog::saveJson()
{
    QJsonArray suppressed;
    QList<int> sorted = m_suppressedPartIndices.values();
    std::sort(sorted.begin(), sorted.end());
    for (int index : sorted)
        suppressed.append(index);
    return {{"sourceUuid", m_sourceCombo ? m_sourceCombo->currentData().toString() : m_sourceUuid},
            {"toolUuid", m_toolCombo ? m_toolCombo->currentData().toString() : m_toolUuid},
            {"suppressedPartIndices", suppressed}};
}

void SplitDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_sourceUuid = obj.value("sourceUuid").toString();
    m_toolUuid = obj.value("toolUuid").toString();
    m_suppressedPartIndices.clear();
    for (const QJsonValue& value : obj.value("suppressedPartIndices").toArray())
    {
        const int index = value.toInt(-1);
        if (index >= 0)
            m_suppressedPartIndices.insert(index);
    }
    refreshSources();
    m_committedState = saveJson();
    clearPreview(true);
}

void SplitDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void SplitDialog::setInitialTool(const QString& uuid)
{
    m_toolUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void SplitDialog::setPartSuppressed(int partIndex, bool suppressed)
{
    if (partIndex < 0)
        return;
    if (suppressed)
        m_suppressedPartIndices.insert(partIndex);
    else
        m_suppressedPartIndices.remove(partIndex);
    m_committedState = saveJson();
}

QSet<int> SplitDialog::suppressedPartIndices() const
{
    return m_suppressedPartIndices;
}

void SplitDialog::refreshSources()
{
    fillShapeCombo(m_sourceCombo, m_sourceUuid, getUuid());
    fillShapeCombo(m_toolCombo, m_toolUuid, getUuid());

    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    if (m_toolCombo && m_toolCombo->currentData().toString() == sourceUuid && m_toolCombo->count() > 1)
    {
        const int next = (m_toolCombo->currentIndex() + 1) % m_toolCombo->count();
        m_toolCombo->setCurrentIndex(next);
    }
    const QString toolUuid = m_toolCombo ? m_toolCombo->currentData().toString() : QString();
    syncMultiShapeHighlight(kSplitPickDomain, getUuid(), {sourceUuid, toolUuid});
    const bool ready = !sourceUuid.isEmpty() && !toolUuid.isEmpty() && sourceUuid != toolUuid;
    if (m_applyButton)
        m_applyButton->setEnabled(ready);
    updateStatus(ready ? QString() : tr("请选择两个不同对象。"), ready);
    updatePreview();
}

void SplitDialog::updatePreview()
{
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    const QString toolUuid = m_toolCombo ? m_toolCombo->currentData().toString() : QString();
    if (m_applyButton)
        m_applyButton->setEnabled(!sourceUuid.isEmpty() && !toolUuid.isEmpty() && sourceUuid != toolUuid);
    clearPreview(false);
    updateStatus(QString(), true);
}

void SplitDialog::clearPreview(bool renderNow)
{
    clearFeatureShapePreview(getUuid(), QStringLiteral("split"), renderNow);
}

void SplitDialog::onApplyClicked()
{
    const QString sourceUuid = m_sourceCombo ? m_sourceCombo->currentData().toString() : QString();
    const QString toolUuid = m_toolCombo ? m_toolCombo->currentData().toString() : QString();
    if (sourceUuid.isEmpty() || toolUuid.isEmpty() || sourceUuid == toolUuid)
    {
        updateStatus(tr("请选择两个不同对象。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    const QSet<int> previousSuppressed = m_suppressedPartIndices;
    // 重新应用分割意味着重新生成全部分块；旧结果中手动删除的块不沿用到新拓扑。
    m_suppressedPartIndices.clear();
    if (service->recreateSplit(m_uuid, getDisplayName(), sourceUuid, toolUuid))
    {
        m_sourceUuid = sourceUuid;
        m_toolUuid = toolUuid;
        m_committedState = saveJson();
        clearPreview(true);
        updateStatus(QString(), true);
    }
    else
    {
        m_suppressedPartIndices = previousSuppressed;
        updateStatus(tr("生成失败：两个对象没有形成有效分块。"), false);
    }
}

void SplitDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    updateStatus(QString(), true);
}

void SplitDialog::updateStatus(const QString& text, bool ok)
{
    setStatusText(m_statusLabel, text, ok);
}


SplitPartDialog::SplitPartDialog(QWidget* parent)
    : BasePage(QStringLiteral("geom_split_part"))
{
    Q_UNUSED(parent);
    auto* ui = new Ui::SplitPartDialog;
    ui->setupUi(this);
    m_infoLabel = ui->infoLabel;
    refreshText();
    delete ui;
}

void SplitPartDialog::beforeLoad()
{
    refreshText();
}

void SplitPartDialog::beforeUnload()
{
}

QJsonObject SplitPartDialog::saveJson()
{
    return {{QStringLiteral("splitUuid"), m_splitUuid},
            {QStringLiteral("partIndex"), m_partIndex}};
}

void SplitPartDialog::loadJson(const QJsonObject& obj)
{
    m_splitUuid = obj.value(QStringLiteral("splitUuid")).toString();
    m_partIndex = obj.value(QStringLiteral("partIndex")).toInt(-1);
    refreshText();
}

void SplitPartDialog::setSplitPart(const QString& splitUuid, int partIndex)
{
    m_splitUuid = splitUuid;
    m_partIndex = partIndex;
    refreshText();
}

void SplitPartDialog::refreshText()
{
    if (!m_infoLabel)
        return;
    m_infoLabel->setText(m_partIndex >= 0
        ? tr("该对象是分割结果中的第 %1 个独立分块，可单独显示、变换、布尔运算、指定材料和划分网格。")
              .arg(m_partIndex + 1)
        : tr("分割结果分块。"));
}
