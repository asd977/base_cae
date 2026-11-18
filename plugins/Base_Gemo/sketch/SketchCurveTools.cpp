#include "SketchCurveTools.h"
#include "SketchShapeBuilder.h"
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <GeomAPI.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pln.hxx>
#include <gp.hxx>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>

namespace GeometrySketch
{
namespace
{
constexpr double pi=3.14159265358979323846,eps=1e-8;
Point2D point2(const gp_Pnt& p){return {p.X(),p.Y()};}
double dist(const Point2D& a,const Point2D& b){return std::hypot(a.u-b.u,a.v-b.v);}
std::vector<TopoDS_Edge> edgesOf(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Edge> edges;
    if(shape.IsNull())return edges;
    if(shape.ShapeType()==TopAbs_WIRE)
        for(BRepTools_WireExplorer it(TopoDS::Wire(shape));it.More();it.Next())edges.push_back(it.Current());
    else for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next())edges.push_back(TopoDS::Edge(it.Current()));
    return edges;
}
TopoDS_Shape wireOf(const std::vector<TopoDS_Edge>& edges)
{
    BRepBuilderAPI_MakeWire wire;for(const auto& e:edges)wire.Add(e);
    return wire.IsDone()?TopoDS_Shape(wire.Wire()):TopoDS_Shape();
}
Handle(Geom2d_Curve) curve2d(const TopoDS_Edge& edge,bool trimmed=true)
{
    double f,l;auto curve=BRep_Tool::Curve(edge,f,l);auto result=GeomAPI::To2d(curve,gp_Pln(gp::XOY()));
    return trimmed?Handle(Geom2d_Curve)(new Geom2d_TrimmedCurve(result,f,l)):result;
}
TopoDS_Edge trimmed(const TopoDS_Edge& edge,double f,double l)
{
    double a,b;const auto curve=BRep_Tool::Curve(edge,a,b);
    TopoDS_Edge result=BRepBuilderAPI_MakeEdge(curve,std::min(f,l),std::max(f,l)).Edge();
    if(f>l)result.Reverse();return result;
}
QString failure(const Standard_Failure& ex){return QString::fromLocal8Bit(ex.GetMessageString()?ex.GetMessageString():"CAD curve error");}
}
TopoDS_Shape SketchCurveTools::shape(const SketchDocument& doc)
{PlaneSettings settings;settings.outputMode=OutputMode::Curve;const auto r=SketchShapeBuilder::build(doc,settings);return r.ok?r.shape:TopoDS_Shape();}
SketchDocument SketchCurveTools::document(const TopoDS_Shape& input,bool construction)
{
    SketchDocument doc;const auto edges=edgesOf(input);if(edges.empty())return doc;
    if(edges.size()==1)
    {
        BRepAdaptor_Curve curve(edges.front());double f=curve.FirstParameter(),l=curve.LastParameter();if(edges.front().Orientation()==TopAbs_REVERSED)std::swap(f,l);
        if(curve.GetType()==GeomAbs_Line)doc.setProfile({point2(curve.Value(f)),point2(curve.Value(l))},false,true);
        else if(curve.GetType()==GeomAbs_Circle)
        {
            if(std::abs(l-f)>=2*pi-1e-7)doc.setCircle(curve.Circle().Location().X(),curve.Circle().Location().Y(),curve.Circle().Radius());
            else doc.setArc({point2(curve.Value(f)),point2(curve.Value((f+l)/2)),point2(curve.Value(l))},true);
        }
    }
    if(!doc.isRenderable())
    {
        bool analytic=true;std::vector<SketchDocument::ProfileSegment> segments;std::vector<Point2D> keys;
        for(const auto& edge:edges)
        {
            BRepAdaptor_Curve curve(edge);double f=curve.FirstParameter(),l=curve.LastParameter();
            if(edge.Orientation()==TopAbs_REVERSED)std::swap(f,l);
            if(curve.GetType()!=GeomAbs_Line&&curve.GetType()!=GeomAbs_Circle){analytic=false;break;}
            SketchDocument::ProfileSegment segment;segment.start=point2(curve.Value(f));segment.end=point2(curve.Value(l));
            if(curve.GetType()==GeomAbs_Circle){segment.kind=SketchDocument::ProfileSegmentKind::ArcThrough;segment.control=point2(curve.Value((f+l)/2));}
            if(keys.empty())keys.push_back(segment.start);keys.push_back(segment.end);segments.push_back(segment);
        }
        if(analytic&&!segments.empty())
        {
            doc.setProfileSegments(keys,segments,dist(keys.front(),keys.back())<eps,true);
            doc.setConstruction(construction);return doc;
        }
        const auto wire=wireOf(edges);if(wire.IsNull())return doc;
        std::ostringstream stream;BRepTools::Write(wire,stream);
        std::vector<Point2D> samples;
        for(const auto& edge:edges)
        {
            BRepAdaptor_Curve curve(edge);double f=curve.FirstParameter(),l=curve.LastParameter();if(edge.Orientation()==TopAbs_REVERSED)std::swap(f,l);
            for(int i=0;i<=32;++i) {const auto p=point2(curve.Value(f+(l-f)*i/32));if(samples.empty()||dist(samples.back(),p)>eps)samples.push_back(p);}
        }
        doc.setNativeCurve(samples,QString::fromStdString(stream.str()),samples.size()>2&&dist(samples.front(),samples.back())<eps);
    }
    doc.setFinished(true);doc.setConstruction(construction);return doc;
}
SketchDocument SketchCurveTools::translated(const SketchDocument& source,double du,double dv)
{
    gp_Trsf transform;transform.SetTranslation(gp_Vec(du,dv,0));return document(BRepBuilderAPI_Transform(shape(source),transform,true).Shape(),source.isConstruction());
}
SketchDocument SketchCurveTools::projected(const TopoDS_Shape& source)
{
    std::vector<TopoDS_Edge> projected;
    for(const auto& edge:edgesOf(source))
    {
        double f,l;auto curve=BRep_Tool::Curve(edge,f,l);BRepAdaptor_Curve adaptor(edge);
        TopoDS_Edge part;
        if(adaptor.GetType()==GeomAbs_Line)
        {
            auto a=curve->Value(f),b=curve->Value(l);a.SetZ(0);b.SetZ(0);if(a.Distance(b)<=eps)continue;
            part=BRepBuilderAPI_MakeEdge(a,b).Edge();
        }
        else if(adaptor.GetType()==GeomAbs_Circle&&std::abs(adaptor.Circle().Axis().Direction().Z())>1-1e-10)
        {
            gp_Trsf transform;transform.SetTranslation(gp_Vec(0,0,-adaptor.Circle().Location().Z()));
            part=TopoDS::Edge(BRepBuilderAPI_Transform(edge,transform,true).Shape());
            // Transform already preserves the edge orientation.
            projected.push_back(part);continue;
        }
        else
        {
            auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,f,l));
            for(int i=1;i<=spline->NbPoles();++i){auto p=spline->Pole(i);p.SetZ(0);spline->SetPole(i,p);}
            part=BRepBuilderAPI_MakeEdge(spline).Edge();
        }
        if(edge.Orientation()==TopAbs_REVERSED)part.Reverse();projected.push_back(part);
    }
    return document(wireOf(projected));
}
bool SketchCurveTools::bounds(const SketchDocument& doc,double& minU,double& maxU,double& minV,double& maxV)
{
    try {const auto s=shape(doc);if(s.IsNull())return false;Bnd_Box box;BRepBndLib::AddOptimal(s,box,false,false);if(box.IsVoid())return false;double z1,z2;box.Get(minU,minV,z1,maxU,maxV,z2);return true;}
    catch(const Standard_Failure&){return false;}
}
bool SketchCurveTools::intersectsBox(const SketchDocument& doc,double minU,double maxU,double minV,double maxV)
{
    try
    {
        const auto s=shape(doc);if(s.IsNull())return false;
        BRepBuilderAPI_MakePolygon polygon;polygon.Add(gp_Pnt(minU,minV,0));polygon.Add(gp_Pnt(maxU,minV,0));polygon.Add(gp_Pnt(maxU,maxV,0));polygon.Add(gp_Pnt(minU,maxV,0));polygon.Close();
        BRepBuilderAPI_MakeFace face(polygon.Wire());if(!face.IsDone())return false;
        BRepExtrema_DistShapeShape distance(s,face.Face());return distance.IsDone()&&distance.Value()<=eps;
    }
    catch(const Standard_Failure&){return false;}
}
double SketchCurveTools::distance(const SketchDocument& doc,const Point2D& p)
{
    try {const auto s=shape(doc);if(s.IsNull())return std::numeric_limits<double>::max();BRepExtrema_DistShapeShape d(s,BRepBuilderAPI_MakeVertex(gp_Pnt(p.u,p.v,0)).Vertex());return d.IsDone()?d.Value():std::numeric_limits<double>::max();}
    catch(const Standard_Failure&){return std::numeric_limits<double>::max();}
}
bool SketchCurveTools::splitAtNodes(const std::vector<SketchDocument>& documents, double tolerance,
                                  std::vector<std::vector<SketchDocument>>& pieces, QString& error,
                                  const std::vector<SketchDocument>& referenceDocuments)
{
    pieces.clear(); error.clear();
    const double tol = std::max(1.0e-9, tolerance);
    struct EdgeData
    {
        TopoDS_Edge edge;
        Handle(Geom_Curve) curve;
        double first, last, minX, maxX, minY, maxY;
        size_t owner;
        std::vector<double> cuts;
    };
    try
    {
        std::vector<EdgeData> edges;
        std::vector<Point2D> references;
        pieces.resize(documents.size());
        for (size_t i = 0; i < documents.size() + referenceDocuments.size(); ++i)
        {
            const auto& doc = i < documents.size() ? documents[i] : referenceDocuments[i-documents.size()];
            if (doc.kind() == SketchDocument::Kind::PointsOnly)
            { references.insert(references.end(), doc.points().begin(), doc.points().end()); continue; }
            for (const auto& edge : edgesOf(shape(doc)))
            {
                EdgeData data; data.edge = edge; data.owner = i;
                data.curve = BRep_Tool::Curve(edge, data.first, data.last);
                if (data.curve.IsNull()) continue;
                Bnd_Box box; BRepBndLib::AddOptimal(edge, box, false, false);
                double z1, z2; box.Get(data.minX, data.minY, z1, data.maxX, data.maxY, z2);
                data.cuts = {data.first, data.last};
                references.push_back(point2(data.curve->Value(data.first)));
                references.push_back(point2(data.curve->Value(data.last)));
                edges.push_back(data);
            }
        }
        const auto offer = [tol](EdgeData& edge, const gp_Pnt& point) {
            if (point.X() < edge.minX-tol || point.X() > edge.maxX+tol ||
                point.Y() < edge.minY-tol || point.Y() > edge.maxY+tol) return;
            GeomAPI_ProjectPointOnCurve projection(point, edge.curve, edge.first, edge.last);
            if (!projection.NbPoints() || projection.LowerDistance() > tol) return;
            for (int i = 1; i <= projection.NbPoints(); ++i)
            {
                if (projection.Distance(i) > tol) continue;
                const double parameter = projection.Parameter(i);
                const auto p = edge.curve->Value(parameter);
                bool duplicate = false;
                for (double cut : edge.cuts)
                    if (std::abs(parameter-cut) < 1.0e-13 ||
                        (p.Distance(edge.curve->Value(cut)) <= tol &&
                         GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(edge.edge), std::min(parameter,cut), std::max(parameter,cut)) <= tol))
                    { duplicate = true; break; }
                if (!duplicate) edge.cuts.push_back(parameter);
            }
        };
        std::sort(references.begin(), references.end(), [](const Point2D& a, const Point2D& b) { return a.u < b.u; });
        for (auto& edge : edges)
        {
            if (edge.owner >= documents.size()) continue;
            auto first = std::lower_bound(references.begin(), references.end(), edge.minX-tol,
                                         [](const Point2D& p, double x) { return p.u < x; });
            for (auto it = first; it != references.end() && it->u <= edge.maxX+tol; ++it)
                offer(edge, gp_Pnt(it->u, it->v, 0));
        }
        for (auto& edge : edges)
        {
            if (edge.owner >= documents.size()) continue;
            BRepAdaptor_Curve adaptor(edge.edge);
            if (adaptor.GetType() == GeomAbs_Line || adaptor.GetType() == GeomAbs_Circle) continue;
            Geom2dAPI_InterCurveCurve self(curve2d(edge.edge), tol);
            for (int k=1;k<=self.NbPoints();++k)
            {
                const auto p=self.Point(k);offer(edge,gp_Pnt(p.X(),p.Y(),0));
            }
        }
        std::vector<size_t> order(edges.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return edges[a].minX < edges[b].minX; });
        for (size_t a = 0; a < order.size(); ++a)
            for (size_t b = a+1; b < order.size(); ++b)
            {
                auto& x = edges[order[a]]; auto& y = edges[order[b]];
                if (y.minX > x.maxX+tol) break;
                if (x.owner >= documents.size() && y.owner >= documents.size()) continue;
                if (y.minY > x.maxY+tol || y.maxY < x.minY-tol) continue;
                Geom2dAPI_InterCurveCurve intersections(curve2d(x.edge), curve2d(y.edge), tol);
                for (int k = 1; k <= intersections.NbPoints(); ++k)
                {
                    const auto p = intersections.Point(k);
                    const gp_Pnt point(p.X(), p.Y(), 0);
                    if (x.owner < documents.size()) offer(x, point);
                    if (y.owner < documents.size()) offer(y, point);
                }
                // Coincident/partially overlapping curves have already received
                // each other's endpoints; preserve their ownership and geometry.
            }
        for (auto& edge : edges)
        {
            if (edge.owner >= documents.size()) continue;
            std::sort(edge.cuts.begin(), edge.cuts.end());
            if (edge.edge.Orientation() == TopAbs_REVERSED) std::reverse(edge.cuts.begin(), edge.cuts.end());
            for (size_t i = 1; i < edge.cuts.size(); ++i)
            {
                if (std::abs(edge.cuts[i]-edge.cuts[i-1]) < 1.0e-14) continue;
                auto part = document(wireOf({trimmed(edge.edge, edge.cuts[i-1], edge.cuts[i])}),
                                     documents[edge.owner].isConstruction());
                if (!part.isRenderable()) { pieces.clear(); error = QStringLiteral("分割结果无效，原线条已保留。"); return false; }
                pieces[edge.owner].push_back(part);
            }
        }
        for (size_t i = 0; i < documents.size(); ++i)
            if (pieces[i].empty()) pieces[i].push_back(documents[i]);
        return true;
    }
    catch (const Standard_Failure& ex) { pieces.clear(); error = failure(ex); return false; }
}

bool SketchCurveTools::explode(const SketchDocument& doc,std::vector<SketchDocument>& pieces,QString& error)
{
    try {for(const auto& edge:edgesOf(shape(doc)))pieces.push_back(document(wireOf({edge}),doc.isConstruction()));
        if(pieces.size()<2){error=QStringLiteral("该对象只有一段曲线，无需分解；如需断开可使用“打断于点”。");pieces.clear();return false;}return true;}
    catch(const Standard_Failure& ex){pieces.clear();error=failure(ex);return false;}
}
bool SketchCurveTools::split(const SketchDocument& doc,const Point2D& click,double tolerance,std::vector<SketchDocument>& pieces,QString& error)
{
    pieces.clear();
    try
    {
        const auto edges=edgesOf(shape(doc));int selected=-1;double parameter=0,best=tolerance;
        for(int i=0;i<int(edges.size());++i)
        {
            double f,l;auto curve=BRep_Tool::Curve(edges[i],f,l);GeomAPI_ProjectPointOnCurve p(gp_Pnt(click.u,click.v,0),curve,f,l);
            if(p.NbPoints()>0&&p.LowerDistance()<best){best=p.LowerDistance();parameter=p.LowerDistanceParameter();selected=i;}
        }
        if(selected<0){error=QStringLiteral("请在线条需要断开的位置单击。");return false;}
        std::vector<TopoDS_Edge> left,right;
        BRepAdaptor_Curve c(edges[selected]);double f=c.FirstParameter(),l=c.LastParameter();
        if(edges[selected].Orientation()==TopAbs_REVERSED)std::swap(f,l);
        if(edges.size()==1&&c.GetType()==GeomAbs_Circle&&std::abs(l-f)>=2*pi-1e-7)
        {left={trimmed(edges[selected],parameter,parameter+pi)};right={trimmed(edges[selected],parameter+pi,parameter+2*pi)};}
        else
        {
            left.insert(left.end(),edges.begin(),edges.begin()+selected);right.insert(right.end(),edges.begin()+selected+1,edges.end());
            if(c.Value(f).Distance(c.Value(parameter))>eps)left.push_back(trimmed(edges[selected],f,parameter));
            if(c.Value(l).Distance(c.Value(parameter))>eps)right.insert(right.begin(),trimmed(edges[selected],parameter,l));
        }
        if(left.empty()||right.empty()){error=QStringLiteral("请在线段内部选择打断位置，不能使用开放曲线的端点。");return false;}
        for(const auto& group:{left,right}) {auto part=document(wireOf(group),doc.isConstruction());if(!part.isRenderable()){error=QStringLiteral("打断结果无效。");pieces.clear();return false;}pieces.push_back(part);}
        return true;
    }
    catch(const Standard_Failure& ex){error=failure(ex);return false;}
}
bool SketchCurveTools::extend(const SketchDocument& doc,const SketchDocument& boundary,const Point2D& click,SketchDocument& output,QString& message)
{
    if(doc.isClosed() || doc.kind()==SketchDocument::Kind::Circle)
    {message=QStringLiteral("闭合曲线无需延伸；可先打断于点或分解。");return false;}
    try
    {
        const auto source=edgesOf(shape(doc)),targets=edgesOf(shape(boundary));if(source.empty()||targets.empty())return false;
        double best=std::numeric_limits<double>::max();std::vector<TopoDS_Edge> result;bool tangent=false;
        for(int end=0;end<2;++end)
        {
            const auto edge=end==0?source.front():source.back();BRepAdaptor_Curve adaptor(edge);
            double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);bool reversed=edge.Orientation()==TopAbs_REVERSED;
            double start=reversed?last:first,finish=reversed?first:last,at=end==0?start:finish;
            gp_Pnt p;gp_Vec derivative;curve->D1(at,p,derivative);
            const double orientation=(reversed?-1.0:1.0)*(end==0?-1.0:1.0);derivative.Multiply(orientation);
            if(derivative.Magnitude()<eps)continue;
            const bool circle=adaptor.GetType()==GeomAbs_Circle;
            if(circle&&last-first>=2*pi-1e-7)continue;
            Handle(Geom2d_Curve) extended;
            if(circle)extended=GeomAPI::To2d(curve,gp_Pln(gp::XOY()));
            else extended=new Geom2d_Line(gp_Pnt2d(p.X(),p.Y()),gp_Dir2d(derivative.X(),derivative.Y()));
            for(const auto& target:targets)
            {
                Geom2dAPI_InterCurveCurve intersections(extended,curve2d(target),eps);
                for(int i=1;i<=intersections.NbPoints();++i)
                {
                    const auto xy=intersections.Point(i);const gp_Pnt q(xy.X(),xy.Y(),0);
                    double amount=0,newStart=start,newFinish=finish;TopoDS_Edge changed;
                    if(circle)
                    {
                        GeomAPI_ProjectPointOnCurve projection(q,curve,0,2*pi);if(!projection.NbPoints())continue;
                        double parameter=projection.LowerDistanceParameter();
                        // Unwrap in the orientation of the endpoint being extended.
                        if(orientation>0){while(parameter<=at+1e-9)parameter+=2*pi;}
                        else {while(parameter>=at-1e-9)parameter-=2*pi;}
                        amount=std::abs(parameter-at)*adaptor.Circle().Radius();
                        if(std::abs(parameter-(end==0?finish:start))>=2*pi-1e-8)continue;
                        if(end==0)newStart=parameter;else newFinish=parameter;
                        changed=trimmed(edge,newStart,newFinish);
                    }
                    else
                    {
                        gp_Vec delta(p,q);amount=delta.Dot(derivative.Normalized());
                        if(amount<=eps||delta.Crossed(derivative.Normalized()).Magnitude()>1e-6)continue;
                    }
                    const double score=distance(boundary,click)+amount;
                    if(score>=best)continue;
                    auto candidate=source;const bool isLine=adaptor.GetType()==GeomAbs_Line;
                    if(circle){if(end==0)candidate.front()=changed;else candidate.back()=changed;}
                    else if(isLine){auto straight=BRepBuilderAPI_MakeEdge(end==0?q:curve->Value(start),end==0?curve->Value(finish):q).Edge();if(end==0)candidate.front()=straight;else candidate.back()=straight;}
                    else {auto line=BRepBuilderAPI_MakeEdge(end==0?q:p,end==0?p:q).Edge();if(end==0)candidate.insert(candidate.begin(),line);else candidate.push_back(line);}
                    best=score;result=candidate;tangent=!circle&&!isLine;
                }
            }
        }
        if(result.empty()){message=QStringLiteral("没有找到位于真实目标边界上的延伸交点。");return false;}
        output=document(wireOf(result),doc.isConstruction());
        message=tangent?QStringLiteral("已沿曲线端点切线延伸到目标边界，原曲线保持不变。"):QStringLiteral("已沿原直线或圆弧延伸到目标边界。");
        return output.isRenderable();
    }
    catch(const Standard_Failure& ex){message=failure(ex);return false;}
}
CurveSnap SketchCurveTools::snap(const std::vector<SketchDocument>& documents,const std::vector<Point2D>& references,const Point2D& cursor,double tolerance)
{
    CurveSnap result;double featureDistance=tolerance,curveDistance=tolerance;Point2D curvePoint;bool onCurve=false;
    const auto offer=[&](const Point2D& p,int kind){const double d=dist(p,cursor);if(d<=featureDistance){featureDistance=d;result={true,p,kind};}};
    for(const auto& p:references)offer(p,1);
    std::vector<TopoDS_Edge> nearby;
    try
    {
        for(const auto& doc:documents)
        {
            if(doc.kind()==SketchDocument::Kind::Circle&&!doc.points().empty())offer(doc.points().front(),1);
            // 几何量很大的 DXF 中，鼠标每移动一次不能为每一条远处的
            // 直线/多段线/样条都重新构造 OCC Shape。先使用文档顶点或
            // 曲线的采样点做廉价外包框过滤，近处才调用精确 OCC 投影。
            // 圆/圆弧不以少量控制点框过滤，避免漏掉弧线凸出的部分。
            const auto kind = doc.kind();
            if (kind == SketchDocument::Kind::NativeCurve ||
                kind == SketchDocument::Kind::Curve ||
                (kind == SketchDocument::Kind::Profile && !doc.hasProfileSegments()))
            {
                // Profile 的显示采样点可能与构建几何用的顶点不同。
                const auto& vertices = kind == SketchDocument::Kind::Profile
                                           ? doc.points() : doc.displayPoints();
                if (!vertices.empty())
                {
                    double minU=vertices.front().u,maxU=minU,minV=vertices.front().v,maxV=minV;
                    for (const auto& vertex : vertices)
                    {
                        minU=std::min(minU,vertex.u);maxU=std::max(maxU,vertex.u);
                        minV=std::min(minV,vertex.v);maxV=std::max(maxV,vertex.v);
                    }
                    // 采样曲线近似外包框留小余量，真正位置仍由 OCC 精确投影。
                    const double margin = tolerance + 0.02 * std::hypot(maxU-minU,maxV-minV);
                    if (cursor.u < minU-margin || cursor.u > maxU+margin ||
                        cursor.v < minV-margin || cursor.v > maxV+margin)
                        continue;
                }
            }
            // 普通 DXF LINE/LWPOLYLINE 在草图中就是离散直线，
            // 投影到直线段的结果是精确的，不需要每次鼠标移动重建 OCC。
            if (kind == SketchDocument::Kind::Profile && !doc.hasProfileSegments())
            {
                const auto& vertices = doc.points();
                const size_t segmentCount = doc.isClosed() ? vertices.size() : vertices.size()-1;
                for (size_t k=0; k<segmentCount; ++k)
                {
                    const auto& a = vertices[k];
                    const auto& b = vertices[(k+1)%vertices.size()];
                    const double du=b.u-a.u, dv=b.v-a.v;
                    const double lengthSquared=du*du+dv*dv;
                    if (lengthSquared <= eps*eps) continue;
                    const double t=std::clamp(((cursor.u-a.u)*du+(cursor.v-a.v)*dv)/lengthSquared,0.0,1.0);
                    const Point2D closest{a.u+t*du,a.v+t*dv};
                    const double d=dist(cursor,closest);
                    if (d>tolerance) continue;
                    offer(a,1);offer(b,1);
                    offer({(a.u+b.u)/2,(a.v+b.v)/2},3);
                    if (d<curveDistance) {curveDistance=d;curvePoint=closest;onCurve=true;}
                    nearby.push_back(BRepBuilderAPI_MakeEdge(gp_Pnt(a.u,a.v,0),gp_Pnt(b.u,b.v,0)).Edge());
                }
                continue;
            }
            double x1,x2,y1,y2;if(!bounds(doc,x1,x2,y1,y2)||cursor.u<x1-tolerance||cursor.u>x2+tolerance||cursor.v<y1-tolerance||cursor.v>y2+tolerance)continue;
            for(const auto& edge:edgesOf(shape(doc)))
            {
                double f,l;auto curve=BRep_Tool::Curve(edge,f,l);GeomAPI_ProjectPointOnCurve p(gp_Pnt(cursor.u,cursor.v,0),curve,f,l);
                if(p.NbPoints()==0||p.LowerDistance()>tolerance)continue;
                nearby.push_back(edge);offer(point2(curve->Value(f)),1);offer(point2(curve->Value(l)),1);
                BRepAdaptor_Curve adaptor(edge);const double length=GCPnts_AbscissaPoint::Length(adaptor,f,l);
                GCPnts_AbscissaPoint halfway(adaptor,length/2,f);if(halfway.IsDone())offer(point2(curve->Value(halfway.Parameter())),3);
                if(p.LowerDistance()<curveDistance){curveDistance=p.LowerDistance();curvePoint=point2(p.NearestPoint());onCurve=true;}
            }
        }
        // Nearby edges are already spatially filtered. Dense intersections must
        // still snap to the real crossing instead of disabling intersection snap.
        if (nearby.size() >= 2)
        {
            for(size_t i=0;i<nearby.size();++i)for(size_t j=i+1;j<nearby.size();++j)
            {
                Geom2dAPI_InterCurveCurve intersections(curve2d(nearby[i]),curve2d(nearby[j]),eps);
                for(int k=1;k<=intersections.NbPoints();++k){const auto p=intersections.Point(k);offer({p.X(),p.Y()},4);}
            }
        }
    }
    catch(const Standard_Failure&) { /* A bad source curve cannot move the cursor to an unverified point. */ }
    if(!result.hit&&onCurve)result={true,curvePoint,5};return result;
}
}
