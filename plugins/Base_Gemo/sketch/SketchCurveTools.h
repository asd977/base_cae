#pragma once
#include "SketchDocument.h"
#include <TopoDS_Shape.hxx>
#include <QString>
namespace GeometrySketch
{
struct CurveSnap {bool hit=false;Point2D point;int kind=0;};
class SketchCurveTools
{
public:
    static TopoDS_Shape shape(const SketchDocument& document);
    static SketchDocument document(const TopoDS_Shape& shape,bool construction=false);
    static SketchDocument projected(const TopoDS_Shape& shape);
    static SketchDocument translated(const SketchDocument& document,double du,double dv);
    static bool bounds(const SketchDocument& document,double& minU,double& maxU,double& minV,double& maxV);
    static bool intersectsBox(const SketchDocument& document,double minU,double maxU,double minV,double maxV);
    static double distance(const SketchDocument& document,const Point2D& point);
    // Each source object keeps its own output group, including point-only objects.
    static bool splitAtNodes(const std::vector<SketchDocument>& documents, double tolerance,
                             std::vector<std::vector<SketchDocument>>& pieces, QString& error,
                             const std::vector<SketchDocument>& referenceDocuments = {});
    static bool explode(const SketchDocument& document,std::vector<SketchDocument>& pieces,QString& error);
    static bool split(const SketchDocument& document,const Point2D& clicked,double tolerance,std::vector<SketchDocument>& pieces,QString& error);
    static bool extend(const SketchDocument& document,const SketchDocument& boundary,const Point2D& clicked,SketchDocument& result,QString& message);
    static CurveSnap snap(const std::vector<SketchDocument>& documents,const std::vector<Point2D>& referencePoints,const Point2D& cursor,double tolerance);
};
}
