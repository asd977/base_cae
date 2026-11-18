#include "SketchPlanarRegionBuilder.h"
#include "SketchShapeBuilder.h"

#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace GeometrySketch
{
namespace
{
struct Segment { Point2D a, b; };
struct Vertex { Point2D p; std::vector<int> leaving; };
struct MeshEdge { int a, b; TopoDS_Edge cadEdge; bool live = true; };
struct HalfEdge { int from, to, edge, twin, next = -1; bool visited = false; };
struct Loop { std::vector<int> half; std::vector<int> nodes; double area = 0.0; };

double distance(const Point2D& a, const Point2D& b) { return std::hypot(a.u-b.u, a.v-b.v); }
double cross(const Point2D& a, const Point2D& b) { return a.u*b.v-a.v*b.u; }
Point2D sub(const Point2D& a, const Point2D& b) { return {a.u-b.u, a.v-b.v}; }
Point2D at(const Segment& s, double t) { return {s.a.u + (s.b.u-s.a.u)*t, s.a.v + (s.b.v-s.a.v)*t}; }
double segmentParameter(const Segment& s, const Point2D& p)
{
    const double du=s.b.u-s.a.u, dv=s.b.v-s.a.v;
    const double len2=du*du+dv*dv;
    return len2>0.0 ? ((p.u-s.a.u)*du+(p.v-s.a.v)*dv)/len2 : 0.0;
}
bool onSegment(const Segment& s, const Point2D& p, double tol)
{
    const double t=segmentParameter(s,p);
    return t>=-1.0e-8 && t<=1.0+1.0e-8 && distance(at(s,std::clamp(t,0.0,1.0)),p)<=tol;
}

// Winding/ray test; points on the boundary are accepted for nested-hole lookup.
bool pointInPolygon(const Point2D& p, const Loop& loop, const std::vector<Vertex>& vertices, double eps)
{
    bool inside=false;
    for (std::size_t i=0, n=loop.nodes.size(); i<n; ++i)
    {
        const Point2D a=vertices[loop.nodes[i]].p;
        const Point2D b=vertices[loop.nodes[(i+1)%n]].p;
        if (onSegment({a,b},p,eps)) return true;
        if ((a.v>p.v)!=(b.v>p.v))
        {
            const double u=a.u+(p.v-a.v)*(b.u-a.u)/(b.v-a.v);
            if (u>p.u) inside=!inside;
        }
    }
    return inside;
}

// A point just to the left of the directed loop edge belongs to the face
// on this edge's left. For a clockwise ring this is OUTSIDE its enclosed island,
// which is exactly the point needed to identify the containing region.
Point2D leftSample(const Loop& loop, const std::vector<Vertex>& vertices, double step)
{
    for (std::size_t i=0, n=loop.nodes.size(); i<n; ++i)
    {
        const Point2D a=vertices[loop.nodes[i]].p, b=vertices[loop.nodes[(i+1)%n]].p;
        const double len=distance(a,b);
        if (len>step*10.0)
            return {(a.u+b.u)/2.0-(b.v-a.v)/len*step,
                    (a.v+b.v)/2.0+(b.u-a.u)/len*step};
    }
    return vertices[loop.nodes.front()].p;
}

double signedArea(const Loop& loop, const std::vector<Vertex>& vertices)
{
    double area=0.0;
    for (std::size_t i=0, n=loop.nodes.size(); i<n; ++i)
    {
        const Point2D a=vertices[loop.nodes[i]].p;
        const Point2D b=vertices[loop.nodes[(i+1)%n]].p;
        area+=cross(a,b);
    }
    return 0.5*area;
}

bool isLinear(const SketchDocument& d)
{
    if (d.kind()!=SketchDocument::Kind::Profile) return false;
    for (const auto& s:d.profileSegments())
        if (s.kind!=SketchDocument::ProfileSegmentKind::Line) return false;
    return true;
}

void appendLinearSegments(const SketchDocument& d, std::vector<Segment>& segments)
{
    if (d.hasProfileSegments())
    {
        for (const auto& s:d.profileSegments()) segments.push_back({s.start,s.end});
    }
    else
    {
        const auto& points=d.points();
        for (std::size_t i=1;i<points.size();++i)
            segments.push_back({points[i-1],points[i]});
        if (d.isClosed() && points.size()>2)
            segments.push_back({points.back(),points.front()});
    }
}

// Connected OCC edges are used twice by neighboring faces, with opposite
// orientation. This is important for a conforming interface mesh.
bool makeWire(const Loop& loop, const std::vector<HalfEdge>& halves,
              const std::vector<MeshEdge>& edges, TopoDS_Wire& wire)
{
    BRepBuilderAPI_MakeWire maker;
    for (int h:loop.half)
    {
        const HalfEdge& he=halves[h];
        TopoDS_Edge edge=edges[he.edge].cadEdge;
        if (he.from!=edges[he.edge].a) edge.Reverse();
        maker.Add(edge);
        if (!maker.IsDone()) return false;
    }
    if (!maker.IsDone() || maker.Wire().IsNull()) return false;
    wire=maker.Wire();
    return true;
}
} // namespace

PlanarRegionReport SketchPlanarRegionBuilder::build(const std::vector<SketchDocument>& docs,
                                                    const PlaneSettings& settings)
{
    PlanarRegionReport report;
    report.sourceObjects=static_cast<int>(docs.size());
    try
    {
        std::vector<Segment> raw;
        std::vector<TopoDS_Shape> standaloneFaces;
        PlaneSettings faceSettings=settings;
        faceSettings.outputMode=OutputMode::Entity;
        for (const SketchDocument& d:docs)
        {
            if (!d.isRenderable() || d.isConstruction()) continue;
            if (isLinear(d))
            {
                appendLinearSegments(d,raw);
            }
            else if (d.kind()==SketchDocument::Kind::Circle ||
                     ((d.kind()==SketchDocument::Kind::Profile || d.kind()==SketchDocument::Kind::NativeCurve) && d.isClosed()))
            {
                // Keep true analytic arcs/circles, never replace them with display polylines.
                const ShapeBuildResult result=SketchShapeBuilder::build(d,faceSettings);
                if (result.ok && !result.shape.IsNull())
                {
                    bool found=false;
                    for (TopExp_Explorer it(result.shape,TopAbs_FACE);it.More();it.Next())
                    {
                        standaloneFaces.push_back(it.Current()); found=true;
                    }
                    if (!found) ++report.ignoredOpenCurves;
                }
                else ++report.invalidFaces;
            }
            else ++report.ignoredOpenCurves;
        }
        report.sourceSegments=static_cast<int>(raw.size());
        // Do not silently combine unpartitioned curved regions with line regions:
        // doing so could create overlapping non-conforming surfaces.
        if (!standaloneFaces.empty() && !raw.empty())
        {
            report.error=QStringLiteral("所选对象同时包含圆弧/圆面和直线线网。请分别选择闭合曲线或直线线网构面，避免重叠面。");
            return report;
        }
        if (raw.empty())
        {
            if (standaloneFaces.empty())
                report.error=QStringLiteral("选中对象未形成可建面的闭合曲线或直线线网。");
            else if (standaloneFaces.size()>1)
                report.error=QStringLiteral("所选闭合曲线有多个面，尚未检查相交与包含关系；请逐个选择构面。");
            else
            {
                report.shape=standaloneFaces.front();report.faces=1;
            }
            return report;
        }
        if (raw.size()>4000)
        {
            report.error=QStringLiteral("候选线段超过 4000 条。请先按区域选择边界/分界线，再分批构面。");
            return report;
        }

        double xmin=std::numeric_limits<double>::max(), ymin=xmin;
        double xmax=-xmin, ymax=-xmin;
        for (const Segment& s:raw)
            for (const Point2D p:{s.a,s.b})
            {xmin=std::min(xmin,p.u);ymin=std::min(ymin,p.v);xmax=std::max(xmax,p.u);ymax=std::max(ymax,p.v);}
        const double scale=std::hypot(xmax-xmin,ymax-ymin);
        const double tol=std::max(1.0e-8,scale*1.0e-9);
        const double areaTol=tol*tol*100.0;
        std::vector<Segment> seg;
        for (const Segment& s:raw)
            if (distance(s.a,s.b)>tol) seg.push_back(s);
        std::vector<std::vector<double>> cuts(seg.size());
        for (auto& v:cuts) {v.push_back(0.0);v.push_back(1.0);}
        // Noding: crosses, T-junctions and collinear overlaps are all split at
        // true geometric intersection coordinates, NOT visually guessed endpoints.
        for (std::size_t i=0;i<seg.size();++i)
            for (std::size_t j=i+1;j<seg.size();++j)
            {
                const Segment& a=seg[i];const Segment& b=seg[j];
                if (std::max(a.a.u,a.b.u)+tol<std::min(b.a.u,b.b.u) ||
                    std::max(b.a.u,b.b.u)+tol<std::min(a.a.u,a.b.u) ||
                    std::max(a.a.v,a.b.v)+tol<std::min(b.a.v,b.b.v) ||
                    std::max(b.a.v,b.b.v)+tol<std::min(a.a.v,a.b.v)) continue;
                const Point2D r=sub(a.b,a.a), s=sub(b.b,b.a), q=sub(b.a,a.a);
                const double det=cross(r,s);
                const double lim=1.0e-12*distance(a.a,a.b)*distance(b.a,b.b);
                if (std::abs(det)>lim)
                {
                    const double t=cross(q,s)/det, u=cross(q,r)/det;
                    const double ta=tol/distance(a.a,a.b),tb=tol/distance(b.a,b.b);
                    if (t>=-ta && t<=1.0+ta && u>=-tb && u<=1.0+tb)
                    {
                        cuts[i].push_back(std::clamp(t,0.0,1.0));
                        cuts[j].push_back(std::clamp(u,0.0,1.0));
                        if (t>ta && t<1.0-ta && u>tb && u<1.0-tb)
                            ++report.splitIntersections;
                    }
                }
                else if (onSegment(a,b.a,tol) || onSegment(a,b.b,tol) ||
                         onSegment(b,a.a,tol) || onSegment(b,a.b,tol))
                {
                    for (const Point2D p:{b.a,b.b}) if(onSegment(a,p,tol)) cuts[i].push_back(std::clamp(segmentParameter(a,p),0.0,1.0));
                    for (const Point2D p:{a.a,a.b}) if(onSegment(b,p,tol)) cuts[j].push_back(std::clamp(segmentParameter(b,p),0.0,1.0));
                }
            }

        std::vector<Vertex> vertices;
        // Quantized buckets; check 9 neighboring buckets so close points on a
        // bucket boundary are not accidentally treated as disconnected.
        using Cell=std::pair<long long,long long>;
        std::map<Cell,std::vector<int>> buckets;
        auto addVertex=[&](const Point2D& p)
        {
            const long long x=static_cast<long long>(std::floor((p.u-xmin)/tol));
            const long long y=static_cast<long long>(std::floor((p.v-ymin)/tol));
            for (int dx=-1;dx<=1;++dx)
                for (int dy=-1;dy<=1;++dy)
                {
                    auto it=buckets.find({x+dx,y+dy});
                    if (it!=buckets.end())
                        for (int id:it->second) if(distance(vertices[id].p,p)<=tol) return id;
                }
            const int id=static_cast<int>(vertices.size());
            vertices.push_back({p,{}});buckets[{x,y}].push_back(id);return id;
        };
        std::vector<MeshEdge> edges;
        std::set<std::pair<int,int>> used;
        for (std::size_t i=0;i<seg.size();++i)
        {
            auto& t=cuts[i];std::sort(t.begin(),t.end());
            t.erase(std::unique(t.begin(),t.end(),[&](double a,double b)
              {return std::abs(a-b)*distance(seg[i].a,seg[i].b)<=tol;}),t.end());
            for (std::size_t j=1;j<t.size();++j)
            {
                const Point2D pa=at(seg[i],t[j-1]),pb=at(seg[i],t[j]);
                if (distance(pa,pb)<=tol) continue;
                const int a=addVertex(pa),b=addVertex(pb);
                if (a==b) continue;
                const auto key=std::minmax(a,b);
                if (!used.emplace(key.first,key.second).second)
                {++report.duplicateSegments;continue;}
                edges.push_back({a,b,TopoDS_Edge(),true});
            }
        }
        // Degree-1 branches do not enclose an area. Remove them ONLY from the
        // face-construction graph; the original DXF objects remain untouched.
        std::vector<std::vector<int>> incidence(vertices.size());
        std::vector<int> degrees(vertices.size());
        for (int i=0;i<static_cast<int>(edges.size());++i)
        {
            incidence[edges[i].a].push_back(i);incidence[edges[i].b].push_back(i);
            ++degrees[edges[i].a];++degrees[edges[i].b];
        }
        std::vector<int> stack;
        for (int i=0;i<static_cast<int>(vertices.size());++i) if(degrees[i]==1) stack.push_back(i);
        while (!stack.empty())
        {
            const int v=stack.back();stack.pop_back();
            if (degrees[v]!=1)continue;
            for (int e:incidence[v]) if(edges[e].live)
            {
                edges[e].live=false;++report.danglingSegments;
                const int w=edges[e].a==v?edges[e].b:edges[e].a;
                --degrees[v];--degrees[w];
                if(degrees[w]==1)stack.push_back(w);
                break;
            }
        }
        std::vector<TopoDS_Vertex> cadVertices(vertices.size());
        for (std::size_t i=0;i<vertices.size();++i)
            cadVertices[i]=BRepBuilderAPI_MakeVertex(toWorldPoint(settings,vertices[i].p.u,vertices[i].p.v)).Vertex();
        std::vector<HalfEdge> halves;
        for (int i=0;i<static_cast<int>(edges.size());++i) if(edges[i].live)
        {
            auto& e=edges[i];
            BRepBuilderAPI_MakeEdge maker(cadVertices[e.a],cadVertices[e.b]);
            if (!maker.IsDone() || maker.Edge().IsNull())
            {report.error=QStringLiteral("线网包含无法构造的退化几何边。");return report;}
            e.cadEdge=maker.Edge();
            const int h=static_cast<int>(halves.size());
            halves.push_back({e.a,e.b,i,h+1});
            halves.push_back({e.b,e.a,i,h});
            vertices[e.a].leaving.push_back(h);
            vertices[e.b].leaving.push_back(h+1);
        }
        for (Vertex& v:vertices)
            std::sort(v.leaving.begin(),v.leaving.end(),[&](int l,int r)
            {
                const Point2D a=sub(vertices[halves[l].to].p,v.p);
                const Point2D b=sub(vertices[halves[r].to].p,v.p);
                return std::atan2(a.v,a.u)<std::atan2(b.v,b.u);
            });
        for (HalfEdge& h:halves)
        {
            const auto& out=vertices[h.to].leaving;
            auto it=std::find(out.begin(),out.end(),h.twin);
            if (it==out.end()) {report.error=QStringLiteral("构面拓扑关系不完整。");return report;}
            h.next=(it==out.begin()?out.back():*(it-1));
        }
        std::vector<Loop> loops;
        for (int i=0;i<static_cast<int>(halves.size());++i)
        {
            if(halves[i].visited)continue;
            Loop loop;
            int h=i;
            do
            {
                if (h<0 || halves[h].visited) break;
                halves[h].visited=true;
                loop.half.push_back(h);loop.nodes.push_back(halves[h].from);
                h=halves[h].next;
            } while(h!=i && loop.half.size()<=halves.size());
            if(h!=i || loop.nodes.size()<3) {++report.invalidFaces;continue;}
            loop.area=signedArea(loop,vertices);
            if (std::abs(loop.area)<=areaTol) continue;
            // Repeated vertices indicate a non-simple contour, such as a spur;
            // do not generate a potentially invalid/overlapping CAD face from it.
            std::set<int> distinct(loop.nodes.begin(),loop.nodes.end());
            if(distinct.size()!=loop.nodes.size()){++report.invalidFaces;continue;}
            loops.push_back(std::move(loop));
        }
        std::vector<int> outers,inners;
        for(int i=0;i<static_cast<int>(loops.size());++i)
            (loops[i].area>0?outers:inners).push_back(i);
        if(outers.size()>2000)
        {report.error=QStringLiteral("发现超过 2000 个封闭小区域，请排除密集辅助线后重新构面。");return report;}
        // Assign each clockwise hole loop to its smallest containing positive
        // loop. Exterior clockwise outlines have no such parent.
        std::map<int,std::vector<int>> holes;
        for (int inner:inners)
        {
            const Point2D sample=leftSample(loops[inner],vertices,std::max(tol*5.0,scale*1.0e-8));
            int parent=-1;
            double smallestArea=std::numeric_limits<double>::max();
            for(int outer:outers)
            {
                if (loops[outer].area<=-loops[inner].area+areaTol) continue;
                if(loops[outer].area>=smallestArea) continue;
                if(pointInPolygon(sample,loops[outer],vertices,tol))
                {parent=outer;smallestArea=loops[outer].area;}
            }
            if(parent>=0) holes[parent].push_back(inner);
        }
        std::vector<TopoDS_Shape> faces;
        for (int outer:outers)
        {
            TopoDS_Wire boundary;
            if(!makeWire(loops[outer],halves,edges,boundary)) {++report.invalidFaces;continue;}
            BRepBuilderAPI_MakeFace maker(boundary,Standard_True);
            if(!maker.IsDone()){++report.invalidFaces;continue;}
            bool failed=false;
            for(int inner:holes[outer])
            {
                TopoDS_Wire hole;
                if(!makeWire(loops[inner],halves,edges,hole)){failed=true;break;}
                maker.Add(hole);
            }
            if(failed){++report.invalidFaces;continue;}
            maker.Build();
            if(!maker.IsDone() || maker.Face().IsNull() || !BRepCheck_Analyzer(maker.Face()).IsValid())
            {++report.invalidFaces;continue;}
            faces.push_back(maker.Face());
        }
        report.faces=static_cast<int>(faces.size());
        if(faces.empty())
        {report.error=QStringLiteral("未找到有效闭合区域。请检查外边界端点、缺口和重叠线，不能按外接矩形自动补面。");return report;}
        if (faces.size()==1) {report.shape=faces.front();return report;}
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        for(const auto& f:faces)builder.Add(compound,f);
        report.shape=compound;
    }
    catch (const Standard_Failure& e)
    {
        const char* msg=e.GetMessageString();
        report.error=QStringLiteral("OCC 构面失败：%1").arg(msg?QString::fromLocal8Bit(msg):QStringLiteral("未知错误"));
    }
    catch(const std::exception& e)
    {report.error=QStringLiteral("构面失败：%1").arg(QString::fromLocal8Bit(e.what()));}
    return report;
}
} // namespace GeometrySketch
