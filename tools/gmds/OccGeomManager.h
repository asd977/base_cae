#pragma once

#include <gmds/cad/GeomManager.h>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomLProp_SLProps.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <memory>
#include <algorithm>

namespace CaeGmds {
namespace cad = gmds::cad;
using Point = gmds::math::Point;

template<class Base> class Entity : public Base
{
public:
    TopoDS_Shape shape;
    int number;
    double bounds[6]{};
    Entity(const TopoDS_Shape& s, int id) : shape(s), number(id)
    {
        Bnd_Box b; BRepBndLib::AddOptimal(s, b, false, false);
        b.Get(bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]);
    }
    int id() const override { return number; }
    double computeArea() const override
    {
        GProp_GProps p;
        if (this->dim() == 2) BRepGProp::SurfaceProperties(shape, p);
        return p.Mass();
    }
    void computeBoundingBox(double lo[3], double hi[3]) const override
    { for (int i=0; i<3; ++i) { lo[i]=bounds[i]; hi[i]=bounds[i+3]; } }
    std::tuple<double,double,double,double,double,double> BBox() const override
    { return {bounds[0],bounds[1],bounds[2],bounds[3],bounds[4],bounds[5]}; }
    Point closestPoint(const Point& p) const override
    {
        BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(gp_Pnt(p.X(),p.Y(),p.Z())).Vertex(), shape);
        if (!d.IsDone() || d.NbSolution() == 0) throw std::runtime_error("OCC projection failed");
        auto q=d.PointOnShape2(1); return {q.X(),q.Y(),q.Z()};
    }
    void project(Point& p) const override { p=closestPoint(p); }
};

struct Vertex : Entity<cad::GeomPoint>
{
    using Entity::Entity;
    std::vector<cad::GeomCurve*> es;
    std::vector<cad::GeomSurface*> fs;
    std::vector<cad::GeomVolume*> vs;
    double X() const override { return bounds[0]; }
    double Y() const override { return bounds[1]; }
    double Z() const override { return bounds[2]; }
    std::vector<cad::GeomCurve*>& curves() override { return es; }
    std::vector<cad::GeomSurface*>& surfaces() override { return fs; }
    std::vector<cad::GeomVolume*>& volumes() override { return vs; }
};
struct Edge : Entity<cad::GeomCurve>
{
    using Entity::Entity;
    std::vector<cad::GeomPoint*> ns;
    std::vector<cad::GeomSurface*> fs;
    std::vector<cad::GeomVolume*> vs;
    double length() const override { GProp_GProps p; BRepGProp::LinearProperties(shape,p); return p.Mass(); }
    double computeDihedralAngle() const override
    {
        if(fs.size()!=2) return 0.0;
        BRepAdaptor_Curve c(TopoDS::Edge(shape));
        gp_Pnt mid=c.Value((c.FirstParameter()+c.LastParameter())*0.5);
        const Point p{mid.X(),mid.Y(),mid.Z()};
        gmds::math::Vector3d a,b; fs[0]->computeNormal(p,a); fs[1]->computeNormal(p,b);
        return std::acos(std::clamp(a.dot(b),-1.0,1.0));
    }
    gmds::math::Vector3d tangent(int end) const override
    {
        BRepAdaptor_Curve c(TopoDS::Edge(shape)); gp_Pnt p; gp_Vec t;
        c.D1(end ? c.LastParameter() : c.FirstParameter(),p,t);
        if(end) t.Reverse();
        if(t.SquareMagnitude()<=1e-24) return {{1.0,0.0,0.0}};
        t.Normalize(); return {{t.X(),t.Y(),t.Z()}};
    }
    bool isALoop() const override { return ns.size()<2; }
    CurvatureInfo getCurvatureInfo() const override
    {
        if(fs.size()!=2 || vs.empty()) return Flat;
        BRepAdaptor_Curve c(TopoDS::Edge(shape)); gp_Pnt p=c.Value((c.FirstParameter()+c.LastParameter())*0.5);
        gmds::math::Vector3d a,b; fs[0]->computeNormal({p.X(),p.Y(),p.Z()},a); fs[1]->computeNormal({p.X(),p.Y(),p.Z()},b);
        gp_Vec n(a.X()+b.X(),a.Y()+b.Y(),a.Z()+b.Z());
        if(n.SquareMagnitude()<1e-16) return Flat;
        n.Normalize(); p.Translate(n*1e-6);
        auto* v=dynamic_cast<Entity<cad::GeomVolume>*>(vs[0]);
        return BRepClass3d_SolidClassifier(v->shape,p,1e-8).State()==TopAbs_IN ? Concave : Convex;
    }
    std::vector<cad::GeomPoint*>& points() override { return ns; }
    std::vector<cad::GeomSurface*>& surfaces() override { return fs; }
    std::vector<cad::GeomVolume*>& volumes() override { return vs; }
};
struct Face : Entity<cad::GeomSurface>
{
    using Entity::Entity;
    std::vector<cad::GeomPoint*> ns;
    std::vector<cad::GeomCurve*> es;
    std::vector<cad::GeomVolume*> vs;
    std::vector<std::vector<cad::GeomCurve*>> wires;
    void computeNormal(const Point& pt,gmds::math::Vector3d& n) const override
    {
        const TopoDS_Face face=TopoDS::Face(shape);
        BRepAdaptor_Surface surf(face,true);
        const GeomAbs_SurfaceType stype=surf.GetType();
        gp_Vec nv(0,0,1);
        bool ok=false;
        if(stype==GeomAbs_Plane) {
            gp_Dir d=surf.Plane().Axis().Direction();
            nv=gp_Vec(d.X(),d.Y(),d.Z()); ok=true;
        } else if(stype==GeomAbs_Cylinder) {
            const gp_Cylinder cyl=surf.Cylinder();
            const gp_Pnt q(pt.X(),pt.Y(),pt.Z());
            const gp_Vec v(cyl.Location(),q);
            const gp_Dir ax=cyl.Axis().Direction();
            gp_Vec rad=v-gp_Vec(ax)*v.Dot(gp_Vec(ax));
            if(rad.SquareMagnitude()>1e-20) { rad.Normalize(); nv=rad; ok=true; }
        } else if(stype==GeomAbs_Sphere) {
            const gp_Sphere sph=surf.Sphere();
            gp_Vec rad(sph.Location(),gp_Pnt(pt.X(),pt.Y(),pt.Z()));
            if(rad.SquareMagnitude()>1e-20) { rad.Normalize(); nv=rad; ok=true; }
        }
        if(!ok) {
            Handle(Geom_Surface) gs=BRep_Tool::Surface(face);
            if(!gs.IsNull()) {
                ShapeAnalysis_Surface sas(gs);
                const gp_Pnt2d uv=sas.ValueOfUV(gp_Pnt(pt.X(),pt.Y(),pt.Z()),1e-6);
                GeomLProp_SLProps props(gs,uv.X(),uv.Y(),1,1e-6);
                if(props.IsNormalDefined()) {
                    gp_Dir d=props.Normal();
                    nv=gp_Vec(d.X(),d.Y(),d.Z()); ok=true;
                }
            }
        }
        if(shape.Orientation()==TopAbs_REVERSED) nv.Reverse();
        n={{nv.X(),nv.Y(),nv.Z()}};
    }
    std::vector<cad::GeomPoint*>& points() override { return ns; }
    std::vector<cad::GeomCurve*>& curves() override { return es; }
    std::vector<cad::GeomVolume*>& volumes() override { return vs; }
    std::vector<std::vector<cad::GeomCurve*>> loops() override { return wires; }
};
struct Volume : Entity<cad::GeomVolume>
{
    using Entity::Entity;
    std::vector<cad::GeomPoint*> ns;
    std::vector<cad::GeomCurve*> es;
    std::vector<cad::GeomSurface*> fs;
    std::vector<cad::GeomPoint*>& points() override { return ns; }
    std::vector<cad::GeomCurve*>& curves() override { return es; }
    std::vector<cad::GeomSurface*>& surfaces() override { return fs; }
};

// Owns all entities for one exact, normalized OCC solid (planar & curved). No faceting or GTS.
class OccGeomManager final : public cad::GeomManager
{
    std::vector<std::unique_ptr<Vertex>> nodes;
    std::vector<std::unique_ptr<Edge>> edges;
    std::vector<std::unique_ptr<Face>> faces;
    std::unique_ptr<Volume> volume;
    template<class Base,class T> static std::vector<Base*> refs(const std::vector<std::unique_ptr<T>>& v)
    { std::vector<Base*> r; for(auto& x:v) r.push_back(x.get()); return r; }
    template<class T> static T* at(const std::vector<std::unique_ptr<T>>& v,int id)
    { return id>0 && id<=int(v.size()) ? v[id-1].get() : nullptr; }
public:
    explicit OccGeomManager(const TopoDS_Shape& s)
    {
        TopTools_IndexedMapOfShape nv,ne,nf;
        TopExp::MapShapes(s,TopAbs_VERTEX,nv); TopExp::MapShapes(s,TopAbs_EDGE,ne); TopExp::MapShapes(s,TopAbs_FACE,nf);
        volume=std::make_unique<Volume>(s,1);
        for(int i=1;i<=nv.Extent();++i) { nodes.push_back(std::make_unique<Vertex>(nv(i),i)); nodes.back()->vs={volume.get()}; }
        for(int i=1;i<=ne.Extent();++i) { edges.push_back(std::make_unique<Edge>(ne(i),i)); edges.back()->vs={volume.get()}; }
        for(int i=1;i<=nf.Extent();++i) { faces.push_back(std::make_unique<Face>(nf(i),i)); faces.back()->vs={volume.get()}; }
        for(auto& e:edges) {
            TopTools_IndexedMapOfShape v; TopExp::MapShapes(e->shape,TopAbs_VERTEX,v);
            for(int i=1;i<=v.Extent();++i) { auto* p=nodes[nv.FindIndex(v(i))-1].get(); e->ns.push_back(p); p->es.push_back(e.get()); }
        }
        for(auto& f:faces) {
            TopTools_IndexedMapOfShape v,e; TopExp::MapShapes(f->shape,TopAbs_VERTEX,v); TopExp::MapShapes(f->shape,TopAbs_EDGE,e);
            for(int i=1;i<=v.Extent();++i) { auto* p=nodes[nv.FindIndex(v(i))-1].get(); f->ns.push_back(p); p->fs.push_back(f.get()); }
            for(int i=1;i<=e.Extent();++i) { auto* p=edges[ne.FindIndex(e(i))-1].get(); f->es.push_back(p); p->fs.push_back(f.get()); }
            for(TopExp_Explorer w(f->shape,TopAbs_WIRE);w.More();w.Next()) {
                std::vector<cad::GeomCurve*> loop;
                for(TopExp_Explorer e(w.Current(),TopAbs_EDGE);e.More();e.Next()) loop.push_back(edges[ne.FindIndex(e.Current())-1].get());
                f->wires.push_back(loop);
            }
        }
        volume->ns=getPoints(); volume->es=getCurves(); volume->fs=getSurfaces();
    }
    cad::GeomPoint* newPoint() override { throw std::runtime_error("Read-only OCC geometry"); }
    cad::GeomCurve* newCurve() override { throw std::runtime_error("Read-only OCC geometry"); }
    cad::GeomSurface* newSurface() override { throw std::runtime_error("Read-only OCC geometry"); }
    cad::GeomVolume* newVolume() override { throw std::runtime_error("Read-only OCC geometry"); }
    gmds::TInt getNbPoints() const override { return gmds::TInt(nodes.size()); }
    gmds::TInt getNbCurves() const override { return gmds::TInt(edges.size()); }
    gmds::TInt getNbSurfaces() const override { return gmds::TInt(faces.size()); }
    gmds::TInt getNbVolumes() const override { return 1; }
    std::vector<cad::GeomPoint*> getPoints() const override { return refs<cad::GeomPoint>(nodes); }
    std::vector<cad::GeomCurve*> getCurves() const override { return refs<cad::GeomCurve>(edges); }
    std::vector<cad::GeomSurface*> getSurfaces() const override { return refs<cad::GeomSurface>(faces); }
    std::vector<cad::GeomVolume*> getVolumes() const override { return {volume.get()}; }
    void getPoints(std::vector<cad::GeomPoint*>& v) const override { v=getPoints(); }
    void getCurves(std::vector<cad::GeomCurve*>& v) const override { v=getCurves(); }
    void getSurfaces(std::vector<cad::GeomSurface*>& v) const override { v=getSurfaces(); }
    void getVolumes(std::vector<cad::GeomVolume*>& v) const override { v=getVolumes(); }
    cad::GeomPoint* getPoint(gmds::TInt id) override { return at(nodes,id); }
    cad::GeomCurve* getCurve(gmds::TInt id) override { return at(edges,id); }
    cad::GeomSurface* getSurface(gmds::TInt id) override { return at(faces,id); }
    cad::GeomVolume* getVolume(gmds::TInt id) override { return id==1 ? volume.get() : nullptr; }
    cad::GeomEntity* getEntity(gmds::TInt id,gmds::TInt dim) override
    { switch(dim) { case 0:return getPoint(id); case 1:return getCurve(id); case 2:return getSurface(id); case 3:return getVolume(id); default:return nullptr; } }
    int getCommonCurve(cad::GeomPoint* a,cad::GeomPoint* b) const override
    { for(auto* e:a->curves()) if(std::find(b->curves().begin(),b->curves().end(),e)!=b->curves().end()) return e->id(); return -1; }
    std::vector<int> getCommonSurfaces(cad::GeomPoint* a,cad::GeomPoint* b) const override
    { std::vector<int> r; for(auto* f:a->surfaces()) if(std::find(b->surfaces().begin(),b->surfaces().end(),f)!=b->surfaces().end()) r.push_back(f->id()); return r; }
    int getCommonSurface(cad::GeomCurve* a,cad::GeomCurve* b) const override
    { for(auto* f:a->surfaces()) if(std::find(b->surfaces().begin(),b->surfaces().end(),f)!=b->surfaces().end()) return f->id(); return -1; }
    bool is_in(Point p) const override
    { auto state=BRepClass3d_SolidClassifier(volume->shape,gp_Pnt(p.X(),p.Y(),p.Z()),1e-8).State(); return state==TopAbs_IN || state==TopAbs_ON; }
};
}
