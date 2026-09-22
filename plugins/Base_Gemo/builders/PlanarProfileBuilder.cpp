#include "PlanarProfileBuilder.h"

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

bool PlanarProfileBuilder::recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType)
{
    if (radius <= 0.0) return false;
    gp_Pnt center(x, y, z);
    gp_Ax2 axis(center, getPlaneNormal(planeType));
    Handle(Geom_Circle) gc = new Geom_Circle(axis, radius);
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(gc);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
    TopoDS_Shape circleShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
    registerShape(uuid, name, new TopoDS_Shape(circleShape));
    return true;
}

bool PlanarProfileBuilder::recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType)
{
    if (majorRadius <= 0.0 || minorRadius <= 0.0 || minorRadius > majorRadius) return false;
    gp_Pnt center(x, y, z);
    gp_Ax2 axis(center, getPlaneNormal(planeType));
    try {
        gp_Elips el(axis, majorRadius, minorRadius);
        Handle(Geom_Ellipse) ge = new Geom_Ellipse(el);
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(ge);
        TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
        TopoDS_Shape ellipseShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
        registerShape(uuid, name, new TopoDS_Shape(ellipseShape));
        return true;
    } catch (...) {
        return false;
    }
}

bool PlanarProfileBuilder::recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType)
{
    if (length <= 0.0 || width <= 0.0) return false;
    gp_Pnt center(x, y, z);
    gp_Ax2 axis(center, getPlaneNormal(planeType));
    const gp_Vec ux(axis.XDirection());
    const gp_Vec uy(axis.YDirection());
    const double hl = 0.5 * length;
    const double hw = 0.5 * width;
    const gp_Pnt p1 = center.Translated(ux * hl + uy * hw);
    const gp_Pnt p2 = center.Translated(ux * hl - uy * hw);
    const gp_Pnt p3 = center.Translated(-ux * hl - uy * hw);
    const gp_Pnt p4 = center.Translated(-ux * hl + uy * hw);
    
    try {
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(p1);
        poly.Add(p2);
        poly.Add(p3);
        poly.Add(p4);
        poly.Close();
        
        TopoDS_Wire wire = poly.Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
        TopoDS_Shape rectShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
        registerShape(uuid, name, new TopoDS_Shape(rectShape));
        return true;
    } catch (...) {
        return false;
    }
}

bool PlanarProfileBuilder::recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType)
{
    if (radius <= 0.0 || sides < 3 || sides > 100) return false;
    gp_Pnt center(x, y, z);
    gp_Ax2 axis(center, getPlaneNormal(planeType));
    const gp_Vec ux(axis.XDirection());
    const gp_Vec uy(axis.YDirection());
    const double pi = 3.14159265358979323846;
    const double rotRad = rotateDeg * pi / 180.0;
    
    try {
        BRepBuilderAPI_MakePolygon poly;
        for (int i = 0; i < sides; ++i)
        {
            const double theta = rotRad + (2.0 * pi * i) / static_cast<double>(sides);
            const double c = std::cos(theta);
            const double s2 = std::sin(theta);
            gp_Pnt pt = center.Translated(ux * (radius * c) + uy * (radius * s2));
            poly.Add(pt);
        }
        poly.Close();
        
        TopoDS_Wire wire = poly.Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
        TopoDS_Shape polyShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
        registerShape(uuid, name, new TopoDS_Shape(polyShape));
        return true;
    } catch (...) {
        return false;
    }
}

