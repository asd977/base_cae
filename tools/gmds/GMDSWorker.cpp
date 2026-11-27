#include <gmds/blocking/CurvedBlocking.h>
#include <gmds/blocking/CurvedBlockingClassifier.h>
#include "OccGeomManager.h"
#include <BOPAlgo_Splitter.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <gp_Cylinder.hxx>
#include <sstream>
#include <set>

namespace {
using Blocking=gmds::blocking::CurvedBlocking;
struct Plane { gp_Pnt origin; gp_Dir normal; double radius=0.0; };
double volume(const TopoDS_Shape& s) { GProp_GProps p; BRepGProp::VolumeProperties(s,p); return std::abs(p.Mass()); }
std::vector<TopoDS_Shape> solids(const TopoDS_Shape& s)
{ TopTools_IndexedMapOfShape m; TopExp::MapShapes(s,TopAbs_SOLID,m); std::vector<TopoDS_Shape> r; for(int i=1;i<=m.Extent();++i) r.push_back(m(i)); return r; }
bool blockTopology(const TopoDS_Shape& s)
{
    TopTools_IndexedMapOfShape fs,ns; TopExp::MapShapes(s,TopAbs_FACE,fs); TopExp::MapShapes(s,TopAbs_VERTEX,ns);
    if(fs.Extent()!=6 || ns.Extent()!=8) return false;
    for(int i=1;i<=fs.Extent();++i) { TopTools_IndexedMapOfShape e; TopExp::MapShapes(fs(i),TopAbs_EDGE,e); if(e.Extent()!=4) return false; }
    return true;
}
int countBlocks(const std::vector<TopoDS_Shape>& ss) { int n=0; for(auto& s:ss) if(blockTopology(s)) ++n; return n; }
double score(const std::vector<TopoDS_Shape>& ss)
{
    double all=0.0,hex=0.0,penalty=0.0;
    for(auto& s:ss) { double v=volume(s); all+=v; if(blockTopology(s)) hex+=v; TopTools_IndexedMapOfShape f; TopExp::MapShapes(s,TopAbs_FACE,f); penalty+=std::abs(f.Extent()-6); }
    return all>0.0 ? 100.0*hex/all-0.5*penalty-0.1*ss.size() : -1e6;
}
std::vector<TopoDS_Shape> split(const std::vector<TopoDS_Shape>& ss,const Plane& p,double scale)
{
    TopTools_ListOfShape args,tools; for(auto& s:ss) args.Append(s);
    const double h=std::max(scale*3.0,1e-6);
    if(p.radius>0.0) tools.Append(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(p.origin,p.normal),p.radius),0,2*3.141592653589793,-h,h).Face());
    else tools.Append(BRepBuilderAPI_MakeFace(gp_Pln(p.origin,p.normal),-h,h,-h,h).Face());
    BOPAlgo_Splitter op; op.SetArguments(args); op.SetTools(tools); op.SetNonDestructive(true); op.SetFuzzyValue(std::max(1e-9,scale*1e-9)); op.Perform();
    if(op.HasErrors() || op.Shape().IsNull() || !BRepCheck_Analyzer(op.Shape()).IsValid()) return {};
    return solids(op.Shape());
}
gp_Pnt jsonPoint(const QJsonValue& v)
{ auto a=v.toArray(); if(a.size()!=3) throw std::runtime_error("Invalid cutter vector"); for(auto x:a) if(!x.isDouble() || !std::isfinite(x.toDouble())) throw std::runtime_error("Non-finite cutter vector"); return {a[0].toDouble(),a[1].toDouble(),a[2].toDouble()}; }
QJsonArray array(const gp_Pnt& p) { return {p.X(),p.Y(),p.Z()}; }
bool supported(const TopoDS_Shape& s,QString& why)
{
    TopTools_IndexedMapOfShape fs,es,ns;
    TopExp::MapShapes(s,TopAbs_FACE,fs); TopExp::MapShapes(s,TopAbs_EDGE,es); TopExp::MapShapes(s,TopAbs_VERTEX,ns);
    if(fs.Extent()>48 || es.Extent()>144 || ns.Extent()>96) { why=QStringLiteral("拓扑数量超过本次分析上限"); return false; }
    for(int i=1;i<=fs.Extent();++i) if(BRepAdaptor_Surface(TopoDS::Face(fs(i))).GetType()!=GeomAbs_Plane) { why=QStringLiteral("当前 GMDS 适配仅分析平面多面体，曲面实体已跳过"); return false; }
    for(int i=1;i<=es.Extent();++i) if(BRepAdaptor_Curve(TopoDS::Edge(es(i))).GetType()!=GeomAbs_Line) { why=QStringLiteral("包含曲线边，已跳过"); return false; }
    if(!BRepCheck_Analyzer(s).IsValid() || volume(s)<=1e-18) { why=QStringLiteral("实体无效或体积过小"); return false; }
    return true;
}
// GMDS generates and inserts sheets; OCC verifies their exact CAD effect.
std::vector<Plane> candidates(const TopoDS_Shape& solid,QElapsedTimer& timer)
{
    Bnd_Box box; BRepBndLib::AddOptimal(solid,box,false,false);
    double x0,y0,z0,x1,y1,z1; box.Get(x0,y0,z0,x1,y1,z1);
    const double scale=std::sqrt((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0)+(z1-z0)*(z1-z0));
    if(!(scale>1e-12)) return {};
    gp_Trsf tr,move; move.SetTranslation(gp_Vec(-x0,-y0,-z0)); tr.SetScale(gp_Pnt(0,0,0),1.0/scale); tr.Multiply(move);
    CaeGmds::OccGeomManager geom(BRepBuilderAPI_Transform(solid,tr,true).Shape());
    Blocking blocking(&geom,true); gmds::blocking::CurvedBlockingClassifier classifier(&blocking);
    const auto cuts=classifier.list_Possible_Cuts();
    std::vector<Plane> result; std::set<std::tuple<long long,long long,long long,long long>> seen;
    for(const auto& cut:cuts) {
        if(result.size()>=12 || timer.elapsed()>3500) break;
        if(!std::isfinite(cut.second) || cut.second<=0.02 || cut.second>=0.98) continue;
        Blocking trial(blocking); trial.cut_sheet(cut.first,cut.second);
        if(!trial.gmap()->is_valid() || trial.get_nb_cells<3>()>32) continue;
        for(auto f:trial.get_all_faces()) {
            if(trial.get_blocks_of_face(f).size()!=2) continue;
            auto ns=trial.get_nodes_of_face(f); if(ns.size()!=4) continue;
            std::vector<gp_Pnt> ps; for(auto n:ns) { auto p=n->info().point; ps.emplace_back(p.X(),p.Y(),p.Z()); }
            gp_Vec n;
            for(size_t k=1;k+1<ps.size();++k) { n=gp_Vec(ps[0],ps[k]).Crossed(gp_Vec(ps[0],ps[k+1])); if(n.SquareMagnitude()>1e-16) break; }
            if(n.SquareMagnitude()<1e-16) continue;
            n.Normalize(); bool flat=true; for(auto p:ps) if(std::abs(gp_Vec(ps[0],p).Dot(n))>1e-7) flat=false;
            if(!flat) continue;
            if(n.X() < -1e-8 || (std::abs(n.X())<1e-8 && n.Y() < -1e-8) || (std::abs(n.X())<1e-8 && std::abs(n.Y())<1e-8 && n.Z()<0)) n.Reverse();
            const double d=gp_Vec(gp_Pnt(0,0,0),ps[0]).Dot(n);
            auto key=std::make_tuple(std::llround(n.X()*1e7),std::llround(n.Y()*1e7),std::llround(n.Z()*1e7),std::llround(d*1e7));
            // PartitionRule persists QVector3D floats. Trial the same rounded
            // plane that the existing preview and SMESH workflow will consume.
            if(seen.insert(key).second) result.push_back({
                gp_Pnt(float(x0+ps[0].X()*scale),float(y0+ps[0].Y()*scale),float(z0+ps[0].Z()*scale)),
                gp_Dir(float(n.X()),float(n.Y()),float(n.Z()))});
        }
    }
    return result;
}
QJsonObject analyze(const QJsonObject& request)
{
    QElapsedTimer timer; timer.start();
    QFile input(request.value("brep").toString()); if(!input.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read CAD snapshot");
    auto bytes=input.readAll(); std::istringstream stream(std::string(bytes.constData(),size_t(bytes.size())));
    TopoDS_Shape shape; BRep_Builder builder; BRepTools::Read(shape,stream,builder);
    const auto bodies=solids(shape); const auto ids=request.value("bodyIds").toArray();
    if(shape.IsNull() || (bodies.empty() && request.value("preSkipped").toArray().isEmpty()) || ids.size()!=int(bodies.size())) throw std::runtime_error("Invalid CAD snapshot mapping");
    const auto existing=request.value("planes").toArray(); if(existing.size()>32) throw std::runtime_error("Too many existing partition rules");
    const int maxCuts=std::clamp(request.value("maxSuggestions").toInt(4),1,8);
    QJsonArray suggestions,skipped=request.value("preSkipped").toArray(); int before=0,after=0,beforeTotal=0,afterTotal=0,trials=0; bool budget=false;
    for(size_t bi=0;bi<bodies.size();++bi) {
        const int id=ids[int(bi)].toInt(); QString why;
        if(bi>=16 || timer.elapsed()>3500) { budget=true; why=QStringLiteral("达到本次实体数量或时间预算"); }
        if(!why.isEmpty() || !supported(bodies[bi],why)) { skipped.append(QJsonObject{{"volumeId",id},{"reason",why}}); continue; }
        Bnd_Box b; BRepBndLib::AddOptimal(bodies[bi],b,false,false); const double scale=std::sqrt(b.SquareExtent());
        std::vector<TopoDS_Shape> regions{bodies[bi]};
        for(auto item:existing) {
            auto p=item.toObject(); const int target=p.value("targetVolumeUiId").toInt(); if(target>0 && target!=id) continue;
            auto o=jsonPoint(p.value("origin")),n=jsonPoint(p.value("normal"));
            Plane cut{o,gp_Dir(n.X(),n.Y(),n.Z()),p.value("axis").toInt()==4 ? p.value("radius").toDouble() : 0.0};
            auto next=split(regions,cut,scale); if(next.empty()) throw std::runtime_error("Existing cutter failed CAD validation"); regions=std::move(next);
            if(regions.size()>32) throw std::runtime_error("Too many existing regions");
        }
        before+=countBlocks(regions); beforeTotal+=int(regions.size());
        for(const auto& region:regions) if(!supported(region,why)) break;
        if(!why.isEmpty()) {
            skipped.append(QJsonObject{{"volumeId",id},{"reason",QStringLiteral("现有分块子域：")+why}});
            after+=countBlocks(regions); afterTotal+=int(regions.size()); continue;
        }
        while(suggestions.size()<maxCuts && countBlocks(regions)<int(regions.size())) {
            if(timer.elapsed()>3500 || trials>=24 || regions.size()>=32) { budget=true; break; }
            double best=score(regions); std::vector<TopoDS_Shape> bestRegions; Plane bestPlane; double bestMin=0;
            for(const auto& region:regions) {
                if(blockTopology(region)) continue;
                for(const auto& plane:candidates(region,timer)) {
                    if(timer.elapsed()>3500 || trials>=24) { budget=true; break; }
                    ++trials; auto ss=split(regions,plane,scale); if(ss.size()<=regions.size() || ss.size()>32) continue;
                    double total=0,minV=1e300; for(auto& s:ss) { auto v=volume(s); total+=v; minV=std::min(minV,v); }
                    const double original=volume(bodies[bi]);
                    if(std::abs(total-original)>std::max(original*1e-7,1e-16) || minV/original<0.005) continue;
                    double s=score(ss); if(s>best+1e-6) { best=s; bestRegions=std::move(ss); bestPlane=plane; bestMin=minV/original; }
                }
                if(budget) break;
            }
            if(bestRegions.empty()) break;
            regions=std::move(bestRegions);
            suggestions.append(QJsonObject{{"targetVolumeUiId",id},{"axis",3},{"coordinate",0.0},
                {"origin",array(bestPlane.origin)},{"normal",QJsonArray{bestPlane.normal.X(),bestPlane.normal.Y(),bestPlane.normal.Z()}},
                {"reason",QStringLiteral("GMDS Blocking 切片，OCC 试切与体积校验通过")},{"minVolumeRatio",bestMin}});
        }
        after+=countBlocks(regions); afterTotal+=int(regions.size());
    }
    const bool all=afterTotal>0 && after==afterTotal && skipped.isEmpty();
    QString message=QStringLiteral("GMDS：建议 %1 个切面；六面块拓扑 %2/%3 → %4/%5；跳过 %6 个实体，用时 %7 ms。")
        .arg(suggestions.size()).arg(before).arg(beforeTotal).arg(after).arg(afterTotal).arg(skipped.size()).arg(timer.elapsed());
    if(budget) message+=QStringLiteral("已达到分析预算，可采纳本次结果后继续。");
    for(auto v:skipped) { auto s=v.toObject(); message+=QStringLiteral("\n实体 %1：%2").arg(s.value("volumeId").toInt()).arg(s.value("reason").toString()); }
    return {{"valid",true},{"backend","CEA GMDS Blocking"},{"revision",CAE_GMDS_REVISION},{"suggestions",suggestions},{"skipped",skipped},
        {"beforeSweepable",before},{"beforeTotal",beforeTotal},{"afterSweepable",after},{"afterTotal",afterTotal},
        {"alreadySweepable",beforeTotal>0 && before==beforeTotal && skipped.isEmpty()},{"allSweepable",all},
        {"elapsedMs",timer.elapsed()},{"trialSplits",trials},{"budgetReached",budget},{"message",message}};
}
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv); const auto args=app.arguments(); if(args.size()!=3) return 2;
    QJsonObject result; int code=0;
    try {
        QFile f(args[1]); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read GMDS request");
        QJsonParseError error; auto doc=QJsonDocument::fromJson(f.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid GMDS request JSON"); result=analyze(doc.object());
    } catch(const Standard_Failure& e) { result={{"valid",false},{"message",QString::fromUtf8(e.GetMessageString())}}; code=1;
    } catch(const std::exception& e) { result={{"valid",false},{"message",QString::fromUtf8(e.what())}}; code=1; }
    QSaveFile out(args[2]); if(!out.open(QIODevice::WriteOnly)) return 3;
    const auto bytes=QJsonDocument(result).toJson(); if(out.write(bytes)!=bytes.size() || !out.commit()) return 3; return code;
}
