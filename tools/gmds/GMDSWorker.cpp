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
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <sstream>
#include <array>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>

namespace {
using Blocking=gmds::blocking::CurvedBlocking;
struct Plane { gp_Pnt origin; gp_Dir normal; double radius=0.0; QString reason; };
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

// General sweepability check matching SMESH Prism_3D requirements:
//   - 6-face mapped hex block, OR
//   - Clean translational prism/cylinder (2 parallel planar caps with equal area,
//     equal edge count, Volume ~= Area*Dist, and every linking face/wire touching caps).
bool isSweepableRegion(const TopoDS_Shape& s)
{
    if(blockTopology(s)) return true;
    const double vol=volume(s);
    if(!(vol>1e-18)) return false;

    TopTools_IndexedMapOfShape fs; TopExp::MapShapes(s,TopAbs_FACE,fs);
    if(fs.Extent()<3) return false;

    // Planar polyhedra (e.g. L/T shapes) should be decomposed into pure 6-face
    // mapped hex blocks via GMDS; curved solids (cylinders / pierced boxes) use
    // the clean translational sweep check below.
    struct Cap { int idx=0; TopoDS_Face face; gp_Pnt c; gp_Dir n; double area=0; int edgeCount=0; };
    std::vector<Cap> caps;
    bool hasCurvedFace=false;
    for(int i=1;i<=fs.Extent();++i) {
        const TopoDS_Face f=TopoDS::Face(fs(i));
        BRepAdaptor_Surface surf(f,true);
        if(surf.GetType()!=GeomAbs_Plane) { hasCurvedFace=true; continue; }
        GProp_GProps gp; BRepGProp::SurfaceProperties(f,gp);
        const double area=std::abs(gp.Mass());
        if(!(area>1e-12)) continue;
        TopTools_IndexedMapOfShape em; TopExp::MapShapes(f,TopAbs_EDGE,em);
        caps.push_back({i,f,gp.CentreOfMass(),surf.Plane().Axis().Direction(),area,em.Extent()});
    }
    if(!hasCurvedFace || caps.size()<2) return false;

    for(size_t i=0;i<caps.size();++i) {
        for(size_t j=i+1;j<caps.size();++j) {
            const auto& a=caps[i]; const auto& b=caps[j];
            if(a.edgeCount!=b.edgeCount) continue;
            if(std::abs(a.n.Dot(b.n))<0.999) continue;
            const double areaScale=std::max(a.area,b.area);
            if(std::abs(a.area-b.area)>areaScale*5e-3) continue;
            gp_Vec tr(a.c,b.c);
            const double dist=tr.Magnitude();
            if(!(dist>1e-9)) continue;
            gp_Dir dir(tr);
            if(std::abs(a.n.Dot(dir))<0.999) continue;
            const double predVol=0.5*(a.area+b.area)*dist;
            if(std::abs(predVol-vol)>std::max(vol,predVol)*5e-3) continue;

            // Linking-face & wire-touch purity check (rejects imprinted holes on side walls).
            TopTools_IndexedMapOfShape aEdges,bEdges;
            TopExp::MapShapes(a.face,TopAbs_EDGE,aEdges);
            TopExp::MapShapes(b.face,TopAbs_EDGE,bEdges);
            bool linkingOk=true;
            for(int fi=1;fi<=fs.Extent();++fi) {
                if(fi==a.idx || fi==b.idx) continue;
                bool touchesA=false,touchesB=false;
                for(TopExp_Explorer we(fs(fi),TopAbs_WIRE);we.More();we.Next()) {
                    bool wireCap=false;
                    for(TopExp_Explorer ee(we.Current(),TopAbs_EDGE);ee.More();ee.Next()) {
                        const bool inA=aEdges.Contains(ee.Current());
                        const bool inB=bEdges.Contains(ee.Current());
                        if(inA) touchesA=true;
                        if(inB) touchesB=true;
                        if(inA || inB) wireCap=true;
                    }
                    if(!wireCap) { linkingOk=false; break; }
                }
                if(!linkingOk || !touchesA || !touchesB) { linkingOk=false; break; }
            }
            if(linkingOk) return true;
        }
    }
    return false;
}

int countBlocks(const std::vector<TopoDS_Shape>& ss) { int n=0; for(auto& s:ss) if(isSweepableRegion(s)) ++n; return n; }

double score(const std::vector<TopoDS_Shape>& ss)
{
    double all=0.0,good=0.0,penalty=0.0;
    for(auto& s:ss) {
        const double v=volume(s); all+=v;
        if(isSweepableRegion(s)) good+=v;
        else { TopTools_IndexedMapOfShape f; TopExp::MapShapes(s,TopAbs_FACE,f); penalty+=std::abs(f.Extent()-6)+2.0; }
    }
    return all>0.0 ? 100.0*good/all-0.5*penalty-0.1*ss.size() : -1e6;
}

std::vector<TopoDS_Shape> split(const std::vector<TopoDS_Shape>& ss,const Plane& p,double scale)
{
    TopTools_ListOfShape args,tools; for(auto& s:ss) args.Append(s);
    const double h=std::max(scale*3.0,1e-6);
    if(p.radius>0.0) tools.Append(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(p.origin,p.normal),p.radius),0,2*3.14159265358979323846,-h,h).Face());
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
    for(int i=1;i<=fs.Extent();++i) {
        const auto st=BRepAdaptor_Surface(TopoDS::Face(fs(i))).GetType();
        const bool ok=(st==GeomAbs_Plane || st==GeomAbs_Cylinder || st==GeomAbs_Cone || st==GeomAbs_Torus || st==GeomAbs_Sphere);
        if(!ok) { why=QStringLiteral("包含自由 NURBS/样条曲面，已跳过"); return false; }
    }
    for(int i=1;i<=es.Extent();++i) {
        const auto ct=BRepAdaptor_Curve(TopoDS::Edge(es(i))).GetType();
        const bool ok=(ct==GeomAbs_Line || ct==GeomAbs_Circle || ct==GeomAbs_Ellipse);
        if(!ok) { why=QStringLiteral("包含自由样条曲线边，已跳过"); return false; }
    }
    if(!BRepCheck_Analyzer(s).IsValid() || volume(s)<=1e-18) { why=QStringLiteral("实体无效或体积过小"); return false; }
    return true;
}

// GMDS CurvedBlocking generates sheet candidates; analytic curved-feature cutters
// (circular junction planes, cylindrical imprint propagation, axial symmetry planes)
// complement curved B-Rep solids. OCC verifies every candidate's exact CAD effect.
std::vector<Plane> candidates(const TopoDS_Shape& solid,QElapsedTimer& timer)
{
    Bnd_Box box; BRepBndLib::AddOptimal(solid,box,false,false);
    double x0,y0,z0,x1,y1,z1; box.Get(x0,y0,z0,x1,y1,z1);
    const double scale=std::sqrt((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0)+(z1-z0)*(z1-z0));
    if(!(scale>1e-12)) return {};

    std::vector<Plane> result;
    std::set<std::tuple<int,long long,long long,long long,long long>> seen;
    auto pushPlane=[&](const gp_Pnt& orig,gp_Vec n,double radius,const QString& reason) {
        if(n.SquareMagnitude()<1e-16) return;
        n.Normalize();
        if(n.X() < -1e-8 || (std::abs(n.X())<1e-8 && n.Y() < -1e-8) || (std::abs(n.X())<1e-8 && std::abs(n.Y())<1e-8 && n.Z()<0)) n.Reverse();
        const double d=radius>0.0 ? radius : gp_Vec(gp_Pnt(0,0,0),orig).Dot(n);
        const auto key=std::make_tuple(radius>0.0 ? 4 : 3,
            std::llround(n.X()*1e5),std::llround(n.Y()*1e5),std::llround(n.Z()*1e5),std::llround(d/scale*1e5));
        if(seen.insert(key).second)
            result.push_back({gp_Pnt(float(orig.X()),float(orig.Y()),float(orig.Z())),gp_Dir(float(n.X()),float(n.Y()),float(n.Z())),radius,reason});
    };

    // 1. Primary: CEA GMDS CurvedBlocking + CurvedBlockingClassifier
    try {
        gp_Trsf tr,move; move.SetTranslation(gp_Vec(-x0,-y0,-z0)); tr.SetScale(gp_Pnt(0,0,0),1.0/scale); tr.Multiply(move);
        CaeGmds::OccGeomManager geom(BRepBuilderAPI_Transform(solid,tr,true).Shape());
        Blocking blocking(&geom,true); gmds::blocking::CurvedBlockingClassifier classifier(&blocking);
        const auto cuts=classifier.list_Possible_Cuts();
        for(const auto& cut:cuts) {
            if(result.size()>=10 || timer.elapsed()>3500) break;
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
                n.Normalize(); bool flat=true; for(auto p:ps) if(std::abs(gp_Vec(ps[0],p).Dot(n))>1e-6) flat=false;
                if(!flat) continue;
                pushPlane(gp_Pnt(x0+ps[0].X()*scale,y0+ps[0].Y()*scale,z0+ps[0].Z()*scale),n,0.0,
                          QStringLiteral("GMDS Blocking 切片，OCC 试切校验通过"));
            }
        }
    } catch(...) {
        // Curved/non-polyhedral solids may trigger GMDS classifier edge cases;
        // continue safely to exact analytic curved-feature cutters below.
    }

    // 2. Curved-feature cutters (circular junctions, cylindrical imprint propagation, axial symmetry)
    TopTools_IndexedMapOfShape es,fs;
    TopExp::MapShapes(solid,TopAbs_EDGE,es);
    TopExp::MapShapes(solid,TopAbs_FACE,fs);
    for(int i=1;i<=es.Extent() && result.size()<14;++i) {
        const TopoDS_Edge e=TopoDS::Edge(es(i));
        BRepAdaptor_Curve c(e);
        if(c.GetType()!=GeomAbs_Circle) continue;
        try {
            const gp_Circ circ=c.Circle();
            const gp_Pnt cc=circ.Location();
            const gp_Dir ca=circ.Axis().Direction();
            const double rad=circ.Radius();
            if(rad>scale*0.015 && rad<scale*0.95) {
                // Circular junction carrier plane (separates cylinder from box or elbow from straight pipe)
                pushPlane(cc,gp_Vec(ca),0.0,QStringLiteral("圆形交线载平面切分"));
                // Cylindrical imprint propagation cutter
                pushPlane(cc,gp_Vec(ca),rad,QStringLiteral("圆形印刻柱面穿透切"));
                // Axial symmetry plane through topological vertex on circle (resolves tangent split arcs)
                for(TopExp_Explorer ve(e,TopAbs_VERTEX);ve.More();ve.Next()) {
                    const gp_Pnt vp=BRep_Tool::Pnt(TopoDS::Vertex(ve.Current()));
                    gp_Vec sym=gp_Vec(ca).Crossed(gp_Vec(cc,vp));
                    if(sym.SquareMagnitude()>1e-10)
                        pushPlane(cc,sym,0.0,QStringLiteral("柱体轴向对称切分"));
                }
            }
        } catch(...) {}
    }
    for(int i=1;i<=fs.Extent() && result.size()<16;++i) {
        const TopoDS_Face f=TopoDS::Face(fs(i));
        BRepAdaptor_Surface s(f,true);
        if(s.GetType()==GeomAbs_Cylinder) {
            try {
                const gp_Cylinder cyl=s.Cylinder();
                const double rad=cyl.Radius();
                if(rad>scale*0.015 && rad<scale*0.95)
                    pushPlane(cyl.Location(),gp_Vec(cyl.Axis().Direction()),rad,QStringLiteral("柱面特征穿透切"));
            } catch(...) {}
        }
    }
    return result;
}

QJsonObject planeToSuggestionJson(int volumeId,const Plane& p,double minVolRatio)
{
    int axis=3;
    double coordinate=0.0;
    gp_Pnt orig=p.origin;
    gp_Dir norm=p.normal;
    if(p.radius>0.0) {
        axis=4;
        coordinate=p.radius;
    } else {
        // Normalize axis-aligned planes to native axis=0/1/2 so the UI slider can edit them directly!
        constexpr double kAxisCos=0.9998477; // cos(1 deg)
        if(std::abs(norm.X())>=kAxisCos) {
            axis=0; norm=gp_Dir(1,0,0); coordinate=orig.X(); orig=gp_Pnt(coordinate,0,0);
        } else if(std::abs(norm.Y())>=kAxisCos) {
            axis=1; norm=gp_Dir(0,1,0); coordinate=orig.Y(); orig=gp_Pnt(0,coordinate,0);
        } else if(std::abs(norm.Z())>=kAxisCos) {
            axis=2; norm=gp_Dir(0,0,1); coordinate=orig.Z(); orig=gp_Pnt(0,0,coordinate);
        }
    }
    QJsonObject obj{
        {"targetVolumeUiId",volumeId},
        {"axis",axis},
        {"coordinate",coordinate},
        {"origin",array(orig)},
        {"normal",QJsonArray{norm.X(),norm.Y(),norm.Z()}},
        {"reason",p.reason.isEmpty() ? QStringLiteral("GMDS Blocking 切片，OCC 试切与体积校验通过") : p.reason},
        {"minVolumeRatio",minVolRatio}
    };
    if(axis==4) obj["radius"]=coordinate;
    return obj;
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
            Plane cut{o,gp_Dir(n.X(),n.Y(),n.Z()),p.value("axis").toInt()==4 ? p.value("radius").toDouble() : 0.0,{}};
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
                if(isSweepableRegion(region)) continue;
                for(const auto& plane:candidates(region,timer)) {
                    if(timer.elapsed()>3500 || trials>=24) { budget=true; break; }
                    ++trials; auto ss=split(regions,plane,scale); if(ss.size()<=regions.size() || ss.size()>32) continue;
                    double total=0,minV=1e300; for(auto& s:ss) { auto v=volume(s); total+=v; minV=std::min(minV,v); }
                    const double original=volume(bodies[bi]);
                    if(std::abs(total-original)>std::max(original*1e-6,1e-16) || minV/original<0.005) continue;
                    double s=score(ss); if(s>best+1e-6) { best=s; bestRegions=std::move(ss); bestPlane=plane; bestMin=minV/original; }
                }
                if(budget) break;
            }
            if(bestRegions.empty()) break;
            regions=std::move(bestRegions);
            suggestions.append(planeToSuggestionJson(id,bestPlane,bestMin));
        }
        after+=countBlocks(regions); afterTotal+=int(regions.size());
    }
    const bool all=afterTotal>0 && after==afterTotal && skipped.isEmpty();
    QString message=QStringLiteral("GMDS：建议 %1 个切面；可扫掠块 %2/%3 → %4/%5；跳过 %6 个实体，用时 %7 ms。")
        .arg(suggestions.size()).arg(before).arg(beforeTotal).arg(after).arg(afterTotal).arg(skipped.size()).arg(timer.elapsed());
    if(budget) message+=QStringLiteral("已达到分析预算，可采纳本次结果后继续。");
    for(auto v:skipped) { auto s=v.toObject(); message+=QStringLiteral("\n实体 %1：%2").arg(s.value("volumeId").toInt()).arg(s.value("reason").toString()); }
    return {{"valid",true},{"backend","CEA GMDS Blocking"},{"revision",CAE_GMDS_REVISION},{"suggestions",suggestions},{"skipped",skipped},
        {"beforeSweepable",before},{"beforeTotal",beforeTotal},{"afterSweepable",after},{"afterTotal",afterTotal},
        {"alreadySweepable",beforeTotal>0 && before==beforeTotal && skipped.isEmpty()},{"allSweepable",all},
        {"elapsedMs",timer.elapsed()},{"trialSplits",trials},{"budgetReached",budget},{"message",message}};
}
}

// Direct GMDS CurvedBlocking hexahedral mesh generation + interface quad extraction.
// Used to test/drive conformal hybrid meshing: GMDS Hex on Solid A -> frozen 2D quads
// injected into SMESH -> NETGEN_3D auto-creates Pyramids + Tetrahedra on Solid B.
QJsonObject generateHexMesh(const QJsonObject& request)
{
    QElapsedTimer timer; timer.start();
    QFile input(request.value("brep").toString());
    if(!input.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read CAD snapshot for GMDS hex_mesh");
    auto bytes=input.readAll();
    std::istringstream stream(std::string(bytes.constData(),size_t(bytes.size())));
    TopoDS_Shape shape; BRep_Builder builder; BRepTools::Read(shape,stream,builder);
    const auto bodies=solids(shape);
    if(bodies.empty()) throw std::runtime_error("No solid in CAD snapshot for GMDS hex_mesh");

    const TopoDS_Shape& solid=bodies[0];
    const int div=std::clamp(request.value("divisions").toInt(3),1,16);

    Bnd_Box box; BRepBndLib::AddOptimal(solid,box,false,false);
    double x0,y0,z0,x1,y1,z1; box.Get(x0,y0,z0,x1,y1,z1);
    const double scale=std::sqrt((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0)+(z1-z0)*(z1-z0));
    if(!(scale>1e-12)) throw std::runtime_error("Degenerate solid scale in GMDS hex_mesh");

    gp_Trsf tr,move; move.SetTranslation(gp_Vec(-x0,-y0,-z0)); tr.SetScale(gp_Pnt(0,0,0),1.0/scale); tr.Multiply(move);
    CaeGmds::OccGeomManager geom(BRepBuilderAPI_Transform(solid,tr,true).Shape());
    Blocking blocking(&geom,true);
    gmds::blocking::CurvedBlockingClassifier classifier(&blocking);
    classifier.classify();

    // Optional automatic GMDS sheet cuts if the solid is not yet a single 6-face block
    std::unique_ptr<Blocking> selectedBlocking;
    if(!blockTopology(solid)) {
        const auto cuts=classifier.list_Possible_Cuts();
        for(const auto& cut:cuts) {
            if(!std::isfinite(cut.second) || cut.second<=0.02 || cut.second>=0.98) continue;
            auto trial=std::make_unique<Blocking>(blocking);
            trial->cut_sheet(cut.first,cut.second);
            if(trial->gmap()->is_valid() && trial->get_nb_cells<3>()<=16) {
                selectedBlocking=std::move(trial);
                break;
            }
        }
    }

    gmds::Mesh blockMesh(gmds::MeshModel(gmds::DIM3 | gmds::R | gmds::F | gmds::E | gmds::N | gmds::R2N | gmds::F2N | gmds::E2N));
    (selectedBlocking ? *selectedBlocking : blocking).convert_to_mesh(blockMesh);

    std::vector<gp_Pnt> outNodes;
    std::map<std::tuple<long long,long long,long long>,int> nodeMap;
    auto addNode=[&](const gp_Pnt& p) -> int {
        const auto key=std::make_tuple(
            std::llround(p.X()*1e6), std::llround(p.Y()*1e6), std::llround(p.Z()*1e6));
        auto it=nodeMap.find(key);
        if(it!=nodeMap.end()) return it->second;
        const int idx=int(outNodes.size());
        outNodes.push_back(p);
        nodeMap.emplace(key,idx);
        return idx;
    };

    std::vector<std::array<int,8>> outHexes;
    for(auto rid:blockMesh.regions()) {
        gmds::Region r=blockMesh.get<gmds::Region>(rid);
        auto ns=r.get<gmds::Node>();
        if(ns.size()!=8) continue;
        gp_Pnt c[8];
        for(int k=0;k<8;++k) {
            auto p=ns[k].point();
            c[k]=gp_Pnt(x0+p.X()*scale, y0+p.Y()*scale, z0+p.Z()*scale);
        }
        // Check block center inside solid (skip exterior blocks removed from concave shapes)
        gp_Pnt mid(0,0,0);
        for(int k=0;k<8;++k) mid.SetCoord(mid.X()+c[k].X()*0.125, mid.Y()+c[k].Y()*0.125, mid.Z()+c[k].Z()*0.125);
        if(BRepClass3d_SolidClassifier(solid,mid,scale*1e-6).State()==TopAbs_OUT) continue;

        auto trilinear=[&](double u,double v,double w) -> gp_Pnt {
            const double w0=(1-u)*(1-v)*(1-w), w1=u*(1-v)*(1-w), w2=u*v*(1-w), w3=(1-u)*v*(1-w);
            const double w4=(1-u)*(1-v)*w,     w5=u*(1-v)*w,     w6=u*v*w,     w7=(1-u)*v*w;
            return gp_Pnt(
                w0*c[0].X()+w1*c[1].X()+w2*c[2].X()+w3*c[3].X()+w4*c[4].X()+w5*c[5].X()+w6*c[6].X()+w7*c[7].X(),
                w0*c[0].Y()+w1*c[1].Y()+w2*c[2].Y()+w3*c[3].Y()+w4*c[4].Y()+w5*c[5].Y()+w6*c[6].Y()+w7*c[7].Y(),
                w0*c[0].Z()+w1*c[1].Z()+w2*c[2].Z()+w3*c[3].Z()+w4*c[4].Z()+w5*c[5].Z()+w6*c[6].Z()+w7*c[7].Z());
        };

        std::vector<int> grid(size_t((div+1)*(div+1)*(div+1)),-1);
        auto gIdx=[&](int i,int j,int k) -> int& { return grid[size_t((k*(div+1)+j)*(div+1)+i)]; };
        for(int k=0;k<=div;++k)
            for(int j=0;j<=div;++j)
                for(int i=0;i<=div;++i)
                    gIdx(i,j,k)=addNode(trilinear(double(i)/div, double(j)/div, double(k)/div));

        for(int k=0;k<div;++k)
            for(int j=0;j<div;++j)
                for(int i=0;i<div;++i)
                    outHexes.push_back({
                        gIdx(i,j,k), gIdx(i+1,j,k), gIdx(i+1,j+1,k), gIdx(i,j+1,k),
                        gIdx(i,j,k+1), gIdx(i+1,j,k+1), gIdx(i+1,j+1,k+1), gIdx(i,j+1,k+1)
                    });
    }

    // Extract boundary quads on optional interfacePlane (origin, normal)
    QJsonArray interfaceQuads;
    if(request.contains("interfacePlane") && request.value("interfacePlane").isObject()) {
        const auto ip=request.value("interfacePlane").toObject();
        const gp_Pnt ipOrig=jsonPoint(ip.value("origin"));
        const gp_Pnt ipN=jsonPoint(ip.value("normal"));
        gp_Vec nvec(ipN.X(),ipN.Y(),ipN.Z());
        if(nvec.SquareMagnitude()>1e-16) {
            nvec.Normalize();
            const double planeTol=std::max(1e-5, scale*1e-4);
            static const int kFaces[6][4]={
                {0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}
            };
            std::map<std::array<int,4>,std::pair<int,std::array<int,4>>> faceCounts;
            for(const auto& h:outHexes) {
                for(int f=0;f<6;++f) {
                    std::array<int,4> raw{h[kFaces[f][0]],h[kFaces[f][1]],h[kFaces[f][2]],h[kFaces[f][3]]};
                    std::array<int,4> key=raw; std::sort(key.begin(),key.end());
                    auto& entry=faceCounts[key];
                    entry.first++;
                    entry.second=raw;
                }
            }
            for(const auto& kv:faceCounts) {
                if(kv.second.first!=1) continue; // boundary quad only
                const auto& q=kv.second.second;
                bool onPlane=true;
                for(int idx:q) {
                    if(std::abs(gp_Vec(ipOrig,outNodes[size_t(idx)]).Dot(nvec))>planeTol) { onPlane=false; break; }
                }
                if(onPlane) interfaceQuads.append(QJsonArray{q[0],q[1],q[2],q[3]});
            }
        }
    }

    QJsonArray jNodes,jHexes;
    for(const auto& p:outNodes) jNodes.append(array(p));
    for(const auto& h:outHexes) jHexes.append(QJsonArray{h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]});
    return {
        {"valid",!outHexes.empty()},
        {"mode","hex_mesh"},
        {"nodes",jNodes},
        {"hexes",jHexes},
        {"interfaceQuads",interfaceQuads},
        {"hexCount",int(outHexes.size())},
        {"interfaceQuadCount",int(interfaceQuads.size())},
        {"elapsedMs",timer.elapsed()}
    };
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv); const auto args=app.arguments(); if(args.size()!=3) return 2;
    QJsonObject result; int code=0;
    try {
        QFile f(args[1]); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read GMDS request");
        QJsonParseError error; auto doc=QJsonDocument::fromJson(f.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid GMDS request JSON"); const auto reqObj=doc.object(); result=(reqObj.value("mode").toString()=="hex_mesh") ? generateHexMesh(reqObj) : analyze(reqObj);
    } catch(const Standard_Failure& e) { result={{"valid",false},{"message",QString::fromUtf8(e.GetMessageString())}}; code=1;
    } catch(const std::exception& e) { result={{"valid",false},{"message",QString::fromUtf8(e.what())}}; code=1; }
    QSaveFile out(args[2]); if(!out.open(QIODevice::WriteOnly)) return 3;
    const auto bytes=QJsonDocument(result).toJson(); if(out.write(bytes)!=bytes.size() || !out.commit()) return 3; return code;
}
