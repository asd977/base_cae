#include "TerrainSurfaceBuilder.h"
#include "../builders/ImportGeometryBuilder.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>
#include <QJsonArray>
#include <vtkCellArray.h>
#include <vtkDelaunay2D.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkIntArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkNew.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <limits>
#include <queue>
#include <tuple>
#include "TerrainContinuousSurfaces.h"

namespace GeometryTerrain
{
QJsonObject Source::toJson() const
{
    QJsonArray xyz;for(const auto& p:points) xyz.append(QJsonArray{p.X(),p.Y(),p.Z()});
    return {{"layer",layer},{"name",name},{"role",int(role)},{"enabled",enabled},{"closed",closed},{"points",xyz}};
}
Source Source::fromJson(const QJsonObject& j)
{
    Source s;s.layer=j["layer"].toString();s.name=j["name"].toString();
    const int role=j["role"].toInt();s.role=role>=0&&role<=3?Role(role):Role::Contour;
    s.enabled=j["enabled"].toBool(true);s.closed=j["closed"].toBool();
    for(const auto& v:j["points"].toArray())
    {
        const auto a=v.toArray();
        if(a.size()==3)s.points.push_back(gp_Pnt(a[0].isDouble()?a[0].toDouble():std::numeric_limits<double>::quiet_NaN(),
            a[1].isDouble()?a[1].toDouble():std::numeric_limits<double>::quiet_NaN(),a[2].isDouble()?a[2].toDouble():std::numeric_limits<double>::quiet_NaN()));
    }
    return s;
}
bool Source::constantElevation(double* value) const
{
    if(points.isEmpty())return false;
    const double z=points.front().Z();if(value)*value=z;
    for(const auto& p:points)if(std::abs(p.Z()-z)>1e-6)return false;
    return true;
}
QString Result::summary() const
{
    return QStringLiteral("已生成地形面：%1 个面域、%2 个连续面，高程 %3～%4。可进入网格模块设置单元尺寸并划分网格。")
        .arg(domains.size()).arg(faceDomains.size()).arg(minElevation,0,'f',3).arg(maxElevation,0,'f',3);
}
QJsonObject TerrainSurfaceBuilder::state(const QVector<Source>& sources,bool generated,double spacing,DomainMode mode)
{
    QJsonArray a;for(const auto& s:sources)a.append(s.toJson());
    return {{"schema","TerrainSurface.v3"},{"representation","continuous-bands"},{"sources",a},{"generated",generated},{"spacing",spacing},
            {"domainMode",mode==DomainMode::WholeSurface?"whole":"contours"}};
}
DomainMode TerrainSurfaceBuilder::domainMode(const QJsonObject& state)
{return state["domainMode"].toString()=="whole"?DomainMode::WholeSurface:DomainMode::ContourRegions;}
QVector<Source> TerrainSurfaceBuilder::sourcesFromState(const QJsonObject& j)
{
    QVector<Source> sources;for(const auto& v:j["sources"].toArray())sources.push_back(Source::fromJson(v.toObject()));return sources;
}
bool TerrainSurfaceBuilder::readDxf(const QString& path,QVector<Source>& sources,QString& diagnostic)
{
    QVector<DxfGeometryRecord> records;
    if(!ImportGeometryBuilder::readDxfRecords(path,records,diagnostic))return false;
    QVector<Source> result;int varying=0;
    try
    {
        for(const auto& record:records)
        {
            Source source;source.layer=record.layer;source.name=record.handle;
            if(record.layer.contains("BOUNDARY",Qt::CaseInsensitive)||record.layer.contains(QStringLiteral("范围")))source.role=Role::Boundary;
            else if(record.layer.contains("BREAK",Qt::CaseInsensitive)||record.layer.contains(QStringLiteral("控制线")))source.role=Role::Breakline;
            for(TopExp_Explorer it(record.shape,TopAbs_EDGE);it.More();it.Next())
            {
                BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
                QVector<gp_Pnt> points;
                if(curve.GetType()==GeomAbs_Line)points={curve.Value(curve.FirstParameter()),curve.Value(curve.LastParameter())};
                else
                {
                    GCPnts_QuasiUniformDeflection sampler(curve,0.05);
                    if(!sampler.IsDone()) {diagnostic=QStringLiteral("对象 %1 曲线采样失败。").arg(record.handle);return false;}
                    for(int i=1;i<=sampler.NbPoints();++i)points.push_back(sampler.Value(i));
                }
                if(!source.points.isEmpty() && source.points.back().Distance(points.back())<1e-6)std::reverse(points.begin(),points.end());
                for(const auto& p:points)if(source.points.isEmpty()||source.points.back().Distance(p)>1e-8)source.points.push_back(p);
            }
            if(source.points.isEmpty())
            {
                for(TopExp_Explorer it(record.shape,TopAbs_VERTEX);it.More();it.Next())source.points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())));
                source.role=Role::SpotHeight;
            }
            if(source.points.size()>2 && source.points.front().Distance(source.points.back())<1e-6) {source.closed=true;source.points.pop_back();}
            if(source.points.isEmpty())continue;
            if(source.role==Role::Contour&&!source.constantElevation()) {source.enabled=false;++varying;}
            result.push_back(source);
        }
    }
    catch(const Standard_Failure& ex) {diagnostic=QString::fromLocal8Bit(ex.GetMessageString());return false;}
    if(result.isEmpty()) {diagnostic=QStringLiteral("文件中没有等高线或高程点。");return false;}
    sources=result;
    diagnostic+=QStringLiteral(" %1 条非等高对象默认不参与；可修正高程或改为控制线。").arg(varying);
    QVector<double> levels;bool hasBoundary=false;
    for(const auto& source:sources)if(source.enabled)
    {
        hasBoundary|=source.role==Role::Boundary;double z=0;
        if(source.role==Role::Contour&&source.constantElevation(&z)&&std::isfinite(z)&&
           std::none_of(levels.cbegin(),levels.cend(),[&](double height){return std::abs(z-height)<1e-6;}))levels.append(z);
    }
    if(levels.size()<2)diagnostic+=QStringLiteral(" 当前等高线不足两个不同高程；若高程只写在文字或图层名中，请先在高程列填写真实数值，软件不会据名称猜测高程。");
    if(!hasBoundary)diagnostic+=QStringLiteral(" 未指定范围边界，构面范围将采用输入点凸包；建议提供真实闭合外边界。");
    return true;
}

namespace
{
double cross(const gp_Pnt& a,const gp_Pnt& b,const gp_Pnt& c)
{return (b.X()-a.X())*(c.Y()-a.Y())-(b.Y()-a.Y())*(c.X()-a.X());}
double xyDistance(const gp_Pnt& a,const gp_Pnt& b) {return std::hypot(a.X()-b.X(),a.Y()-b.Y());}
bool inside(const gp_Pnt& p,const QVector<gp_Pnt>& boundary,double tol)
{
    bool result=false;
    for(int i=0,j=boundary.size()-1;i<boundary.size();j=i++)
    {
        const auto& a=boundary[j];const auto& b=boundary[i];
        if(std::abs(cross(a,b,p))<=tol*std::max(1.0,xyDistance(a,b)) &&
           p.X()>=std::min(a.X(),b.X())-tol&&p.X()<=std::max(a.X(),b.X())+tol&&
           p.Y()>=std::min(a.Y(),b.Y())-tol&&p.Y()<=std::max(a.Y(),b.Y())+tol)return true;
        if((a.Y()>p.Y())!=(b.Y()>p.Y()) && p.X()<(b.X()-a.X())*(p.Y()-a.Y())/(b.Y()-a.Y())+a.X())result=!result;
    }
    return result;
}
}
Result TerrainSurfaceBuilder::build(const QVector<Source>& sources,double spacing,DomainMode mode)
{
    Result r;
    try
    {
        if(!std::isfinite(spacing)||spacing<=0)throw std::runtime_error("等高线采样间距必须大于零。");
        QVector<gp_Pnt> points,boundary;std::vector<std::pair<int,int>> segments;std::vector<int> spots;
        std::vector<bool> contourSegments;
        double xmin=1e300,xmax=-1e300,ymin=xmin,ymax=xmax;
        int boundaryCount=0;
        for(const auto& source:sources) if(source.enabled)
        {
            if(source.points.isEmpty())throw std::runtime_error("参与对象没有坐标。");
            if(source.role==Role::Contour&&!source.constantElevation())throw std::runtime_error("等高线自身高程不一致，请修正高程或改为控制线。");
            if(source.role==Role::Boundary)
            {
                if(++boundaryCount>1||!source.closed||source.points.size()<3)throw std::runtime_error("请提供一个闭合的地形范围边界。");
                boundary=source.points;
            }
            for(const auto& p:source.points)
            {
                if(!std::isfinite(p.X())||!std::isfinite(p.Y())||!std::isfinite(p.Z())||std::max({std::abs(p.X()),std::abs(p.Y()),std::abs(p.Z())})>1e12)
                    throw std::runtime_error("坐标不是有效数值或超出支持范围。");
                xmin=std::min(xmin,p.X());xmax=std::max(xmax,p.X());ymin=std::min(ymin,p.Y());ymax=std::max(ymax,p.Y());
            }
        }
        if(xmax<=xmin||ymax<=ymin)throw std::runtime_error("等高线需要覆盖有效的 XY 区域。");
        const double tol=std::max(1e-8,std::hypot(xmax-xmin,ymax-ymin)*1e-9);
        using Cell=std::pair<long long,long long>;std::map<Cell,std::vector<int>> buckets;
        auto add=[&](const gp_Pnt& p)
        {
            const long long x=std::llround((p.X()-xmin)/tol),y=std::llround((p.Y()-ymin)/tol);
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
            {
                const auto it=buckets.find({x+dx,y+dy});if(it==buckets.end())continue;
                for(int id:it->second)if(xyDistance(points[id],p)<=tol)
                {if(std::abs(points[id].Z()-p.Z())>1e-6)throw std::runtime_error("同一 XY 位置存在冲突高程；地形必须是单值表面。");return id;}
            }
            if(points.size()>=12000)throw std::runtime_error("地形采样点超过 12000，请增大等高线采样间距或缩小范围。");
            const int id=points.size();points.push_back(p);buckets[{x,y}].push_back(id);return id;
        };
        for(const auto& source:sources)if(source.enabled)
        {
            if(source.role==Role::SpotHeight) {for(const auto& p:source.points)spots.push_back(add(p));continue;}
            if(source.points.size()<2)throw std::runtime_error("曲线至少需要两个点。");
            const int count=source.points.size();
            for(int i=0;i<count-(source.closed?0:1);++i)
            {
                const auto a=source.points[i],b=source.points[(i+1)%count];
                const double length=xyDistance(a,b);
                if(length<=tol) {add(a);add(b);continue;}
                const double sampleCount=std::ceil(length/spacing);
                if(sampleCount>12000)throw std::runtime_error("采样步长过小。");
                const int n=std::max(1,int(sampleCount));int last=add(a);
                for(int j=1;j<=n;++j)
                {
                    const double t=double(j)/n;int next=add(gp_Pnt(a.X()+(b.X()-a.X())*t,a.Y()+(b.Y()-a.Y())*t,a.Z()+(b.Z()-a.Z())*t));
                    if(last!=next){segments.emplace_back(last,next);contourSegments.push_back(source.role==Role::Contour);}last=next;
                }
            }
        }
        if(segments.size()>16000)throw std::runtime_error("约束线段过多，请增大等高线采样间距。");
        // Node all crossings and T junctions before passing a planar straight-line graph to VTK.
        std::vector<std::vector<std::pair<double,int>>> cuts(segments.size());
        for(size_t i=0;i<segments.size();++i)cuts[i]={{0,segments[i].first},{1,segments[i].second}};
        for(size_t i=0;i<segments.size();++i)for(int id:spots)
        {
            const auto a=points[segments[i].first],b=points[segments[i].second],p=points[id];
            const double dx=b.X()-a.X(),dy=b.Y()-a.Y(),t=((p.X()-a.X())*dx+(p.Y()-a.Y())*dy)/(dx*dx+dy*dy);
            if(t>=0&&t<=1&&std::abs(cross(a,b,p))<=tol*xyDistance(a,b))
            {if(std::abs(p.Z()-a.Z()-(b.Z()-a.Z())*t)>1e-6)throw std::runtime_error("高程点与所在等高线的高程冲突。");cuts[i].push_back({t,id});}
        }
        for(size_t i=0;i<segments.size();++i)for(size_t j=i+1;j<segments.size();++j)
        {
            const auto a=points[segments[i].first],b=points[segments[i].second],c=points[segments[j].first],d=points[segments[j].second];
            if(std::max(a.X(),b.X())+tol<std::min(c.X(),d.X())||std::max(c.X(),d.X())+tol<std::min(a.X(),b.X())||
               std::max(a.Y(),b.Y())+tol<std::min(c.Y(),d.Y())||std::max(c.Y(),d.Y())+tol<std::min(a.Y(),b.Y()))continue;
            const double ax=b.X()-a.X(),ay=b.Y()-a.Y(),bx=d.X()-c.X(),by=d.Y()-c.Y(),det=ax*by-ay*bx;
            if(std::abs(det)>1e-12*xyDistance(a,b)*xyDistance(c,d))
            {
                const double rx=c.X()-a.X(),ry=c.Y()-a.Y(),t=(rx*by-ry*bx)/det,u=(rx*ay-ry*ax)/det;
                if(t>=-1e-10&&t<=1+1e-10&&u>=-1e-10&&u<=1+1e-10)
                {
                    const double z=a.Z()+(b.Z()-a.Z())*t,z2=c.Z()+(d.Z()-c.Z())*u;
                    if(std::abs(z-z2)>1e-6)throw std::runtime_error("等高线或控制线在交点处高程冲突。");
                    const int id=add(gp_Pnt(a.X()+ax*t,a.Y()+ay*t,z));cuts[i].push_back({std::clamp(t,0.0,1.0),id});cuts[j].push_back({std::clamp(u,0.0,1.0),id});
                }
            }
            else
            {
                const auto splitAt=[&](size_t index,int id,const gp_Pnt& from,const gp_Pnt& to)
                {
                    const auto& p=points[id];const double dx=to.X()-from.X(),dy=to.Y()-from.Y();
                    const double t=((p.X()-from.X())*dx+(p.Y()-from.Y())*dy)/(dx*dx+dy*dy);
                    if(t>=0&&t<=1&&std::abs(cross(from,to,p))<=tol*xyDistance(from,to))
                    {if(std::abs(p.Z()-from.Z()-(to.Z()-from.Z())*t)>1e-6)throw std::runtime_error("重叠约束线高程冲突。");cuts[index].push_back({t,id});}
                };
                splitAt(i,segments[j].first,a,b);splitAt(i,segments[j].second,a,b);splitAt(j,segments[i].first,c,d);splitAt(j,segments[i].second,c,d);
            }
        }
        std::set<std::pair<int,int>> constraints,contourConstraints;
        for(size_t segment=0;segment<cuts.size();++segment)
        {
            auto& list=cuts[segment];
            std::sort(list.begin(),list.end());
            for(size_t i=1;i<list.size();++i)if(list[i-1].second!=list[i].second)
            {
                const auto edge=std::minmax(list[i-1].second,list[i].second);constraints.insert(edge);
                if(contourSegments[segment])contourConstraints.insert(edge);
            }
        }
        if(!boundary.empty())
        {
            double area=0;for(int i=0;i<boundary.size();++i){const auto& a=boundary[i];const auto& b=boundary[(i+1)%boundary.size()];area+=(a.X()-xmin)*(b.Y()-ymin)-(b.X()-xmin)*(a.Y()-ymin);}
            if(std::abs(area)<tol*tol)throw std::runtime_error("地形范围边界面积为零或自交。");
            for(int i=0;i<boundary.size();++i)for(int j=i+2;j<boundary.size();++j)
            {
                if(i==0&&j==boundary.size()-1)continue;
                const auto& a=boundary[i];const auto& b=boundary[(i+1)%boundary.size()];const auto& c=boundary[j];const auto& d=boundary[(j+1)%boundary.size()];
                if(cross(a,b,c)*cross(a,b,d)<0&&cross(c,d,a)*cross(c,d,b)<0)throw std::runtime_error("地形范围边界不能自交。");
            }
            for(const auto& p:points)if(!inside(p,boundary,tol))throw std::runtime_error("参与线条或高程点超出地形范围，请调整选择或范围边界。");
        }
        r.minElevation=r.maxElevation=points.front().Z();for(const auto& p:points){r.minElevation=std::min(r.minElevation,p.Z());r.maxElevation=std::max(r.maxElevation,p.Z());}
        if(r.maxElevation-r.minElevation<1e-6)throw std::runtime_error("所有输入高程相同，无法据此恢复起伏地形；请检查 Z 或指定等高线高程。");
        if(buildContinuousSurfaces(sources,spacing,mode,r))return r;
        vtkNew<vtkPoints> planarPoints;planarPoints->SetDataTypeToDouble();
        // Normalize large survey coordinates for robust triangulation, restore XYZ afterwards.
        for(const auto& p:points)planarPoints->InsertNextPoint(p.X()-xmin,p.Y()-ymin,0);
        vtkNew<vtkPolyData> input;input->SetPoints(planarPoints);
        vtkNew<vtkPolyData> source;source->SetPoints(planarPoints);vtkNew<vtkCellArray> lines;
        for(const auto& edge:constraints) {vtkIdType ids[2]={edge.first,edge.second};lines->InsertNextCell(2,ids);}
        source->SetLines(lines);
        vtkNew<vtkDelaunay2D> delaunay;delaunay->SetInputData(input);delaunay->SetSourceData(source);
        delaunay->SetTolerance(0.0);delaunay->SetOffset(10);delaunay->BoundingTriangulationOff();delaunay->Update();
        vtkNew<vtkCellArray> triangles;std::vector<std::array<int,3>> cells;std::set<std::pair<int,int>> meshEdges;
        vtkIdType n=0;const vtkIdType* ids=nullptr;auto* polygons=delaunay->GetOutput()->GetPolys();polygons->InitTraversal();
        while(polygons->GetNextCell(n,ids))if(n==3)
        {
            int a=int(ids[0]),b=int(ids[1]),c=int(ids[2]);
            if(a<0||b<0||c<0||std::max({a,b,c})>=points.size())throw std::runtime_error("地形三角网返回了无效顶点。");
            double area=cross(points[a],points[b],points[c]);if(std::abs(area)<=tol*tol)continue;
            const gp_Pnt center((points[a].X()+points[b].X()+points[c].X())/3,(points[a].Y()+points[b].Y()+points[c].Y())/3,0);
            if(!boundary.empty()&&!inside(center,boundary,tol))continue;
            if(area<0)std::swap(b,c);
            cells.push_back({a,b,c});vtkIdType face[3]={a,b,c};triangles->InsertNextCell(3,face);
            meshEdges.insert(std::minmax(a,b));meshEdges.insert(std::minmax(b,c));meshEdges.insert(std::minmax(c,a));
            if(std::max({points[a].Z(),points[b].Z(),points[c].Z()})-std::min({points[a].Z(),points[b].Z(),points[c].Z()})<1e-6)++r.flatTriangles;
        }
        if(cells.empty())throw std::runtime_error("等高线没有形成可用的地形三角网。");
        for(const auto& edge:constraints)if(!meshEdges.count(edge))throw std::runtime_error("三角网未保留全部等高线约束，请增加采样密度或检查重叠/相交线。");
        // Partition connectivity across contour constraints, never across every
        // tessellation edge. Open contours split domains only when they actually
        // separate the surface; ridgelines/breaklines only constrain geometry.
        std::map<std::pair<int,int>,std::vector<int>> owners;
        for(int t=0;t<int(cells.size());++t)for(int i=0;i<3;++i)
            owners[std::minmax(cells[t][i],cells[t][(i+1)%3])].push_back(t);
        std::vector<std::vector<int>> neighbours(cells.size());
        for(const auto& entry:owners)
        {
            if(entry.second.size()>2)throw std::runtime_error("地形面存在非流形接缝。");
            if(entry.second.size()!=2||(mode==DomainMode::ContourRegions&&contourConstraints.count(entry.first)))continue;
            const int a=entry.second[0],b=entry.second[1];neighbours[a].push_back(b);neighbours[b].push_back(a);
        }
        std::vector<int> triangleDomain(cells.size(),-1);
        struct Region {std::vector<int> triangles;double lo=1e300,hi=-1e300,x=0,y=0;};
        std::vector<Region> regions;
        for(int start=0;start<int(cells.size());++start)if(triangleDomain[start]<0)
        {
            const int id=int(regions.size());regions.emplace_back();auto& region=regions.back();
            std::queue<int> pending;pending.push(start);triangleDomain[start]=id;
            while(!pending.empty())
            {
                const int t=pending.front();pending.pop();region.triangles.push_back(t);
                for(int vertex:cells[t]){region.lo=std::min(region.lo,points[vertex].Z());region.hi=std::max(region.hi,points[vertex].Z());region.x+=points[vertex].X()-xmin;region.y+=points[vertex].Y()-ymin;}
                for(int n:neighbours[t])if(triangleDomain[n]<0){triangleDomain[n]=id;pending.push(n);}
            }
            region.x/=region.triangles.size()*3;region.y/=region.triangles.size()*3;
        }
        std::vector<int> order(regions.size());for(int i=0;i<int(order.size());++i)order[i]=i;
        std::sort(order.begin(),order.end(),[&](int a,int b){const auto& x=regions[a];const auto& y=regions[b];return std::tie(x.lo,x.hi,x.x,x.y)<std::tie(y.lo,y.hi,y.x,y.y);});
        std::vector<int> domainIds(regions.size());
        for(int i=0;i<int(order.size());++i)
        {
            const auto& region=regions[order[i]];domainIds[order[i]]=i+1;
            Result::Domain domain;domain.id=i+1;domain.triangles=int(region.triangles.size());domain.minElevation=region.lo;domain.maxElevation=region.hi;
            domain.name=QStringLiteral("区域 %1：高程 %2～%3").arg(i+1).arg(region.lo,0,'g',10).arg(region.hi,0,'g',10);r.domains.push_back(domain);
        }
        vtkNew<vtkPoints> world;world->SetDataTypeToDouble();vtkNew<vtkDoubleArray> heights;heights->SetName("Elevation");
        QVector<TopoDS_Vertex> vertices;for(const auto& p:points) {world->InsertNextPoint(p.X(),p.Y(),p.Z());heights->InsertNextValue(p.Z());vertices.push_back(BRepBuilderAPI_MakeVertex(p).Vertex());}
        r.mesh=vtkSmartPointer<vtkPolyData>::New();r.mesh->SetPoints(world);r.mesh->SetPolys(triangles);r.mesh->GetPointData()->SetScalars(heights);
        vtkNew<vtkIntArray> domainArray;domainArray->SetName("TerrainDomain");
        BRep_Builder builder;QVector<TopoDS_Shell> shells(regions.size());for(auto& shell:shells)builder.MakeShell(shell);
        std::map<std::pair<int,int>,TopoDS_Edge> edges;QVector<TopoDS_Face> cadFaces;
        for(int t=0;t<int(cells.size());++t)
        {
            const auto& cell=cells[t];
            BRepBuilderAPI_MakeWire wire;
            for(int i=0;i<3;++i)
            {
                int a=cell[i],b=cell[(i+1)%3];auto key=std::minmax(a,b);auto it=edges.find(key);
                if(it==edges.end())it=edges.emplace(key,BRepBuilderAPI_MakeEdge(vertices[key.first],vertices[key.second]).Edge()).first;
                TopoDS_Edge edge=it->second;if(a>b)edge.Reverse();wire.Add(edge);
            }
            BRepBuilderAPI_MakeFace face(wire.Wire());if(!face.IsDone())throw std::runtime_error("三角地形 CAD 面构造失败。");
            const int domain=domainIds[triangleDomain[t]];domainArray->InsertNextValue(domain);
            builder.Add(shells[domain-1],face.Face());cadFaces.push_back(face.Face());
        }
        r.mesh->GetCellData()->AddArray(domainArray);
        if(shells.size()==1)r.shape=shells.front();
        else{TopoDS_Compound compound;builder.MakeCompound(compound);for(const auto& shell:shells)builder.Add(compound,shell);r.shape=compound;}
        if(!BRepCheck_Analyzer(r.shape).IsValid())throw std::runtime_error("生成的地形拓扑无效。");
        TopTools_IndexedMapOfShape faceMap;TopExp::MapShapes(r.shape,TopAbs_FACE,faceMap);r.faceDomains.fill(0,faceMap.Extent());
        for(int t=0;t<cadFaces.size();++t)r.faceDomains[faceMap.FindIndex(cadFaces[t])-1]=domainIds[triangleDomain[t]];
        mergePlanarTerrainDomains(r);
        r.vertices=points.size();r.triangles=int(cells.size());r.constraints=int(constraints.size());
        r.minElevation=r.maxElevation=points.front().Z();for(const auto& p:points){r.minElevation=std::min(r.minElevation,p.Z());r.maxElevation=std::max(r.maxElevation,p.Z());}
        if(r.maxElevation-r.minElevation<1e-6)throw std::runtime_error("所有输入高程相同，无法据此恢复起伏地形；请检查 Z 或指定等高线高程。");
    }
    catch(const Standard_Failure& ex) {r.error=QString::fromLocal8Bit(ex.GetMessageString());r.shape.Nullify();r.mesh=nullptr;}
    catch(const std::exception& ex) {r.error=QString::fromUtf8(ex.what());r.shape.Nullify();r.mesh=nullptr;}
    return r;
}
QVector<Source> TerrainSurfaceBuilder::example()
{
    QVector<Source> sources;
    for(int ring=0;ring<5;++ring)
    {
        Source s;s.layer=ring==0?"BOUNDARY":QString("CONTOUR_%1").arg(100+ring*10);s.name=s.layer;
        s.role=ring==0?Role::Boundary:Role::Contour;s.closed=true;
        for(int i=0;i<72;++i)
        {
            double angle=2*3.14159265358979323846*i/72,rx=100-ring*18,ry=80-ring*14;
            s.points.push_back(gp_Pnt(500000+rx*std::cos(angle),3000000+ry*std::sin(angle),100+ring*10));
        }
        sources.push_back(s);
    }
    Source peak;peak.layer="SUMMIT";peak.name="Summit 150m";peak.role=Role::SpotHeight;peak.points={gp_Pnt(500000,3000000,150)};sources.push_back(peak);
    return sources;
}
}
