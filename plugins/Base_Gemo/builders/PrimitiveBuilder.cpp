#include "PrimitiveBuilder.h"

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
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
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

bool PrimitiveBuilder::recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z)
{
    if (length <= 0 || width <= 0 || height <= 0)
        return false;
    gp_Pnt position(x, y, z);
    BRepPrimAPI_MakeBox boxMaker(length, width, height);
    TopoDS_Shape boxShape = boxMaker.Shape();
    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), position);
    BRepBuilderAPI_Transform transformer(boxShape, transform);
    boxShape = transformer.Shape();
    registerShape(uuid, name, new TopoDS_Shape(boxShape));
    return true;
}

bool PrimitiveBuilder::recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z)
{
    if (radius <= 0) return false;
    gp_Pnt pos(x, y, z);
    BRepPrimAPI_MakeSphere sphereMaker(pos, radius);
    registerShape(uuid, name, new TopoDS_Shape(sphereMaker.Shape()));
    return true;
}

bool PrimitiveBuilder::recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z)
{
    if (bottomRadius < 0 || topRadius < 0 || height <= 0) return false;
    gp_Pnt baseCenter(x, y, z);
    BRepPrimAPI_MakeCone coneMaker(bottomRadius, topRadius, height);
    TopoDS_Shape coneShape = coneMaker.Shape();
    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), baseCenter);
    BRepBuilderAPI_Transform transformer(coneShape, transform);
    coneShape = transformer.Shape();
    registerShape(uuid, name, new TopoDS_Shape(coneShape));
    return true;
}

bool PrimitiveBuilder::recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z)
{
    if (radius <= 0 || height <= 0) return false;
    gp_Pnt baseCenter(x, y, z);
    BRepPrimAPI_MakeCylinder cylinderMaker(radius, height);
    TopoDS_Shape cylinderShape = cylinderMaker.Shape();
    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), baseCenter);
    BRepBuilderAPI_Transform transformer(cylinderShape, transform);
    cylinderShape = transformer.Shape();
    registerShape(uuid, name, new TopoDS_Shape(cylinderShape));
    return true;
}

bool PrimitiveBuilder::recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z)
{
    if (majorRadius <= 0 || minorRadius <= 0 || minorRadius >= majorRadius) return false;
    gp_Pnt center(x, y, z);
    BRepPrimAPI_MakeTorus torusMaker(majorRadius, minorRadius);
    TopoDS_Shape torusShape = torusMaker.Shape();
    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), center);
    BRepBuilderAPI_Transform transformer(torusShape, transform);
    torusShape = transformer.Shape();
    registerShape(uuid, name, new TopoDS_Shape(torusShape));
    return true;
}
