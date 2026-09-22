#include "TransformBuilder.h"

#include "GeometryBuildUtils.h"

#include "CadDocument.h"
#include "CadBody.h"
#include <GeometryDisplayManager.h>
#include <GeometryCleanup.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <BRepTools.hxx>
#include <Precision.hxx>
#include <gp_Circ.hxx>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>

#include <QFile>
#include <QList>
#include <QSet>
#include <QTextStream>
#include <QVector>
#include <QPointF>
#include <QObject>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace GeometryBuildUtils;

namespace
{
constexpr double kPatternPi = 3.14159265358979323846;

bool shapeCentroid(const TopoDS_Shape& shape, gp_Pnt& center)
{
    if (shape.IsNull())
        return false;
    try
    {
        GProp_GProps properties;
        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        if (solids.Extent() > 0)
            BRepGProp::VolumeProperties(shape, properties);
        else
        {
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(shape, TopAbs_FACE, faces);
            if (faces.Extent() > 0)
                BRepGProp::SurfaceProperties(shape, properties);
            else
                BRepGProp::LinearProperties(shape, properties);
        }
        if (std::abs(properties.Mass()) <= Precision::Confusion())
            return false;
        center = properties.CentreOfMass();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

class ScopedRenderSuspender
{
public:
    ScopedRenderSuspender()
        : m_wasSuspended(GeometryDisplayManager::isRenderSuspended())
    {
        if (!m_wasSuspended)
            GeometryDisplayManager::setRenderSuspended(true);
    }

    ~ScopedRenderSuspender()
    {
        if (!m_wasSuspended)
            GeometryDisplayManager::setRenderSuspended(false);
    }

private:
    bool m_wasSuspended = false;
};

}

bool TransformBuilder::recreatePattern(const QString& uuid, const QString& name,
                                       const QString& sourceUuid,
                                       const PatternDirection& direction1,
                                       const PatternDirection& direction2,
                                       const PatternDirection& direction3)
{
    if (sourceUuid.isEmpty())
        return false;

    PatternDirection directions[3] = {direction1, direction2, direction3};
    qint64 totalCount = 1;
    for (PatternDirection& direction : directions)
    {
        direction.count = std::max(1, direction.count);
        totalCount *= direction.count;
        if (direction.count > 1 && direction.step.lengthSquared() <= float(kEps * kEps))
            return false;
    }
    if (totalCount < 2 || totalCount > 200000)
        return false;

    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    if (!sourceObj)
        return false;

    const TopoDS_Shape sourceShape = *(sourceObj->getShape());
    if (sourceShape.IsNull())
        return false;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    for (int i1 = 0; i1 < directions[0].count; ++i1)
    {
        for (int i2 = 0; i2 < directions[1].count; ++i2)
        {
            for (int i3 = 0; i3 < directions[2].count; ++i3)
            {
                const QVector3D translation =
                    directions[0].step * float(i1) +
                    directions[1].step * float(i2) +
                    directions[2].step * float(i3);

                TopoDS_Shape item;
                if (translation.lengthSquared() <= float(kEps * kEps))
                {
                    item = sourceShape;
                }
                else
                {
                    gp_Trsf transform;
                    transform.SetTranslation(gp_Vec(double(translation.x()),
                                                     double(translation.y()),
                                                     double(translation.z())));

                    // The pattern is a rigid translation. Reusing the underlying TShape
                    // keeps large arrays substantially lighter than deep-copying every item.
                    BRepBuilderAPI_Transform transformer(sourceShape, transform, Standard_False);
                    transformer.Build();
                    if (!transformer.IsDone())
                        return false;
                    item = transformer.Shape();
                }

                if (!item.IsNull())
                    builder.Add(compound, item);
            }
        }
    }

    if (compound.IsNull())
        return false;

    ScopedRenderSuspender suspendRender;
    registerShape(uuid, name, new TopoDS_Shape(compound));
    hideSourceGeometry(sourceUuid);
    return true;
}

bool TransformBuilder::recreatePattern(const QString& uuid, const QString& name,
                                       const QString& sourceUuid,
                                       int countX, int countY, int countZ,
                                       double dx, double dy, double dz)
{
    return recreatePattern(uuid, name, sourceUuid,
                           PatternDirection{countX, QVector3D(float(dx), 0.0f, 0.0f)},
                           PatternDirection{countY, QVector3D(0.0f, float(dy), 0.0f)},
                           PatternDirection{countZ, QVector3D(0.0f, 0.0f, float(dz))});
}


bool TransformBuilder::recreatePolarPattern(const QString& uuid, const QString& name,
                                            const QString& sourceUuid,
                                            const PolarPatternParameters& parameters)
{
    if (sourceUuid.isEmpty() || parameters.count < 2 || parameters.count > 200000)
        return false;
    if (!std::isfinite(parameters.totalAngleDeg) ||
        std::abs(parameters.totalAngleDeg) <= kEps ||
        std::abs(parameters.totalAngleDeg) > 360.0 + 1.0e-7)
    {
        return false;
    }

    const QVector3D axisVector = parameters.axisDirection;
    if (axisVector.lengthSquared() <= float(kEps * kEps))
        return false;

    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    if (!sourceObj)
        return false;

    const TopoDS_Shape sourceShape = *(sourceObj->getShape());
    if (sourceShape.IsNull())
        return false;

    const gp_Pnt origin(double(parameters.axisOrigin.x()),
                        double(parameters.axisOrigin.y()),
                        double(parameters.axisOrigin.z()));
    const gp_Dir direction(double(axisVector.x()),
                           double(axisVector.y()),
                           double(axisVector.z()));
    const gp_Ax1 axis(origin, direction);

    const double absoluteAngle = std::abs(parameters.totalAngleDeg);
    const bool fullCircle = std::abs(absoluteAngle - 360.0) <= 1.0e-7;
    const double intervalCount = fullCircle
                                     ? double(parameters.count)
                                     : double(parameters.count - 1);
    const double stepAngleRad = parameters.totalAngleDeg * kPatternPi / 180.0 / intervalCount;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    for (int index = 0; index < parameters.count; ++index)
    {
        TopoDS_Shape item;
        if (index == 0)
        {
            item = sourceShape;
        }
        else
        {
            gp_Trsf transform;
            transform.SetRotation(axis, stepAngleRad * double(index));
            BRepBuilderAPI_Transform transformer(sourceShape, transform, Standard_False);
            transformer.Build();
            if (!transformer.IsDone() || transformer.Shape().IsNull())
                return false;
            item = transformer.Shape();
        }

        if (!item.IsNull())
            builder.Add(compound, item);
    }

    if (compound.IsNull())
        return false;

    ScopedRenderSuspender suspendRender;
    registerShape(uuid, name, new TopoDS_Shape(compound));
    hideSourceGeometry(sourceUuid);
    return true;
}

bool TransformBuilder::recreateTransform(const QString& uuid, const QString& name,
                                         const QString& sourceUuid,
                                         double dx, double dy, double dz,
                                         double rxDeg, double ryDeg, double rzDeg,
                                         int pivotMode,
                                         double pivotX, double pivotY, double pivotZ,
                                         int orderVersion)
{
    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    if (!sourceObj)
        return false;

    const TopoDS_Shape source = *(sourceObj->getShape());
    gp_Pnt pivot(0.0, 0.0, 0.0);
    if (pivotMode == 1)
    {
        // 使用真实几何质心，而不是包围盒中心。不对称模型旋转时更符合用户预期。
        if (!shapeCentroid(source, pivot))
            pivot = gp_Pnt(0.0, 0.0, 0.0);
    }
    else if (pivotMode == 2)
    {
        pivot = gp_Pnt(pivotX, pivotY, pivotZ);
    }

    auto applyTransform = [](const TopoDS_Shape& input,
                             const gp_Trsf& transform,
                             TopoDS_Shape& output) -> bool
    {
        BRepBuilderAPI_Transform operation(input, transform, Standard_True);
        operation.Build();
        if (!operation.IsDone() || operation.Shape().IsNull())
            return false;
        output = operation.Shape();
        return true;
    };

    auto applyRotations = [&](const TopoDS_Shape& input, TopoDS_Shape& output) -> bool
    {
        output = input;
        const struct RotationDef
        {
            gp_Dir axis;
            double angle;
        } rotations[] = {
            {gp_Dir(1.0, 0.0, 0.0), rxDeg},
            {gp_Dir(0.0, 1.0, 0.0), ryDeg},
            {gp_Dir(0.0, 0.0, 1.0), rzDeg}
        };

        constexpr double kPi = 3.14159265358979323846;
        for (const RotationDef& rotationDef : rotations)
        {
            if (std::abs(rotationDef.angle) <= kEps)
                continue;
            gp_Trsf rotation;
            rotation.SetRotation(gp_Ax1(pivot, rotationDef.axis),
                                 rotationDef.angle * kPi / 180.0);
            if (!applyTransform(output, rotation, output))
                return false;
        }
        return true;
    };

    TopoDS_Shape result = source;
    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(dx, dy, dz));

    // 旧工程没有 orderVersion，继续使用“先平移再绕全局原点旋转”，保证重开结果不变。
    // 新建/重新应用的节点使用 v2：先围绕指定中心旋转，再做全局位移。
    if (orderVersion < 2)
    {
        if (!applyTransform(result, translation, result))
            return false;
        // 旧版本固定绕全局原点，忽略新增中心参数。
        const struct LegacyRotation
        {
            gp_Dir axis;
            double angle;
        } rotations[] = {
            {gp_Dir(1.0, 0.0, 0.0), rxDeg},
            {gp_Dir(0.0, 1.0, 0.0), ryDeg},
            {gp_Dir(0.0, 0.0, 1.0), rzDeg}
        };
        constexpr double kPi = 3.14159265358979323846;
        for (const LegacyRotation& item : rotations)
        {
            if (std::abs(item.angle) <= kEps)
                continue;
            gp_Trsf rotation;
            rotation.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), item.axis),
                                 item.angle * kPi / 180.0);
            if (!applyTransform(result, rotation, result))
                return false;
        }
    }
    else
    {
        if (!applyRotations(result, result))
            return false;
        if (!applyTransform(result, translation, result))
            return false;
    }

    if (result.IsNull())
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(sourceUuid);
    return true;
}

bool TransformBuilder::recreateMirror(const QString& uuid, const QString& name, const QString& sourceUuid, int planeType, double offset)
{
    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    if (!sourceObj)
        return false;

    gp_Pnt origin(0.0, 0.0, 0.0);
    if (planeType == 0)
        origin.SetZ(offset);
    else if (planeType == 1)
        origin.SetY(offset);
    else
        origin.SetX(offset);

    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(origin, getPlaneNormal(planeType)));
    BRepBuilderAPI_Transform transformer(*(sourceObj->getShape()), mirror, true);
    transformer.Build();
    if (!transformer.IsDone())
        return false;

    TopoDS_Shape result = transformer.Shape();
    if (result.IsNull())
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(sourceUuid);
    return true;
}
