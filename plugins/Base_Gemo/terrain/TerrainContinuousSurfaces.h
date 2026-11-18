#pragma once
#include "TerrainSurfaceBuilder.h"
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <GeomFill_Generator.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>

namespace GeometryTerrain
{
// A contour band is one CAD surface. The triangulation used to inspect input
// connectivity and draw/export a preview must never become meshing geometry.
static bool buildContinuousSurfaces(const QVector<Source>& sources, double spacing,
                                    DomainMode mode, Result& result)
{
    struct Ring {QVector<gp_Pnt> points;double area=0,z=0;Role role=Role::Contour;};
    QVector<Ring> rings;
    QVector<gp_Pnt> spots;
    bool nested = true;
    for (const auto& source : sources)
    {
        if (!source.enabled || source.points.isEmpty())
            continue;
        if (source.role == Role::SpotHeight || source.points.size() == 1)
        {
            spots += source.points;
            continue;
        }
        Ring ring;ring.points = source.points;ring.role=source.role;
        if (ring.points.size() > 1 && ring.points.first().Distance(ring.points.last()) < 1e-7)
            ring.points.removeLast();
        if (!source.closed || !source.constantElevation(&ring.z) || ring.points.size() < 3)
        {nested = false;break;}
        const auto origin = ring.points.first();
        for (int i = 0; i < ring.points.size(); ++i)
        {
            const auto a = ring.points[i], b = ring.points[(i+1)%ring.points.size()];
            ring.area += (a.X()-origin.X())*(b.Y()-origin.Y())-(b.X()-origin.X())*(a.Y()-origin.Y());
        }
        if (ring.area < 0)
            std::reverse(ring.points.begin(), ring.points.end());
        ring.area = std::abs(ring.area);
        // Keep a consistent seam without depending on DXF vertex order.
        const auto first = std::max_element(ring.points.begin(), ring.points.end(),
            [](const gp_Pnt& a,const gp_Pnt& b){return std::make_pair(a.X(),a.Y())<std::make_pair(b.X(),b.Y());});
        std::rotate(ring.points.begin(), first, ring.points.end());
        rings.push_back(ring);
    }
    std::sort(rings.begin(), rings.end(), [](const Ring& a,const Ring& b){return a.area>b.area;});
    nested = nested && !rings.isEmpty() && spots.size() <= 1;
    auto contains = [](const QVector<gp_Pnt>& polygon,const gp_Pnt& p)
    {
        bool inside=false;
        for(int i=0,j=polygon.size()-1;i<polygon.size();j=i++)
        {
            const auto a=polygon[i],b=polygon[j];
            if((a.Y()>p.Y())!=(b.Y()>p.Y()) &&
               p.X()<(b.X()-a.X())*(p.Y()-a.Y())/(b.Y()-a.Y())+a.X())inside=!inside;
        }
        return inside;
    };
    for (int i=1; nested && i<rings.size(); ++i)
        for (const auto& p:rings[i].points)
            if (!contains(rings[i-1].points,p)) {nested=false;break;}
    if (nested && !spots.isEmpty() && !contains(rings.last().points,spots.first()))
        nested=false;

    if (!nested)
        return false;

    result.domains.clear();
    if (mode==DomainMode::WholeSurface)
    {
        Result::Domain domain;domain.id=1;domain.minElevation=result.minElevation;domain.maxElevation=result.maxElevation;
        domain.name=QStringLiteral("整体地形：高程 %1～%2").arg(domain.minElevation).arg(domain.maxElevation);result.domains.push_back(domain);
    }

    QVector<TopoDS_Face> faces;
    QVector<int> domains;
    bool startDomain=true;
    auto domainForRange = [&](double lo,double hi)
    {
        if (mode==DomainMode::WholeSurface)return 1;
        if(!startDomain && !result.domains.isEmpty())
        {
            auto& domain=result.domains.last();domain.minElevation=std::min(domain.minElevation,lo);domain.maxElevation=std::max(domain.maxElevation,hi);
            domain.name=QStringLiteral("区域 %1：高程 %2～%3").arg(domain.id).arg(domain.minElevation,0,'g',10).arg(domain.maxElevation,0,'g',10);return domain.id;
        }
        Result::Domain domain;domain.id=result.domains.size()+1;domain.minElevation=lo;domain.maxElevation=hi;
        domain.name=QStringLiteral("区域 %1：高程 %2～%3").arg(domain.id).arg(lo,0,'g',10).arg(hi,0,'g',10);
        result.domains.push_back(domain);return domain.id;
    };
    if (nested)
    {
        QVector<Handle(Geom_BSplineCurve)> curves;
        for (const auto& ring:rings)
        {
            // One exact closed curve retains the original survey polyline.
            // Interior knots are parameters, not CAD edges or mesh constraints.
            const int n=ring.points.size();
            TColgp_Array1OfPnt poles(1,n+1);TColStd_Array1OfReal knots(1,n+1);
            TColStd_Array1OfInteger mults(1,n+1);
            double length=0;
            for(int i=0;i<=n;++i)
            {
                poles(i+1)=ring.points[i%n];
                if(i)length+=ring.points[(i-1)%n].Distance(ring.points[i%n]);
                knots(i+1)=length;mults(i+1)=(i==0||i==n)?2:1;
            }
            for(int i=1;i<=n+1;++i)knots(i)/=length;
            curves.push_back(new Geom_BSplineCurve(poles,knots,mults,1));
        }
        for(int i=0;i<curves.size();++i)
        {
            startDomain=i==0 || rings[i].role==Role::Contour;
            Handle(Geom_Surface) surface;
            double nextZ;
            if(i+1<curves.size())
            {
                GeomFill_Generator generator;generator.AddCurve(curves[i]);generator.AddCurve(curves[i+1]);generator.Perform(1e-10);surface=generator.Surface();nextZ=rings[i+1].z;
            }
            else if(!spots.isEmpty())
            {
                const auto curve=curves[i];
                TColgp_Array2OfPnt poles(1,curve->NbPoles(),1,2);
                for(int p=1;p<=curve->NbPoles();++p){poles(p,1)=curve->Pole(p);poles(p,2)=spots.first();}
                TColStd_Array1OfReal knots(1,curve->NbKnots()),vknots(1,2);
                TColStd_Array1OfInteger mults(1,curve->NbKnots()),vmults(1,2);
                curve->Knots(knots);curve->Multiplicities(mults);vknots(1)=0;vknots(2)=1;vmults.Init(2);
                surface=new Geom_BSplineSurface(poles,knots,vknots,mults,vmults,curve->Degree(),1);nextZ=spots.first().Z();
            }
            else
            {
                BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(curves[i]));
                BRepBuilderAPI_MakeFace cap(wire.Wire(),true);
                if(!cap.IsDone())throw std::runtime_error("最内侧等高线封面失败。");
                faces.push_back(cap.Face());domains.push_back(domainForRange(rings[i].z,rings[i].z));break;
            }
            BRepBuilderAPI_MakeFace face(surface,1e-7);
            if(!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
                throw std::runtime_error("等高带连续曲面构造失败，请检查等高线是否交叉或发生退化。");
            faces.push_back(face.Face());domains.push_back(domainForRange(std::min(rings[i].z,nextZ),std::max(rings[i].z,nextZ)));
        }
    }
    BRepBuilderAPI_Sewing sewing(1e-6);
    for(const auto& face:faces)sewing.Add(face);
    sewing.Perform();
    for(auto& face:faces)
        if(sewing.IsModifiedSubShape(face))face=TopoDS::Face(sewing.ModifiedSubShape(face));
    BRep_Builder builder;QVector<TopoDS_Shell> shells(result.domains.size());
    for(auto& shell:shells)builder.MakeShell(shell);
    for(int i=0;i<faces.size();++i)builder.Add(shells[domains[i]-1],faces[i]);
    if(shells.size()==1)result.shape=shells.first();
    else{TopoDS_Compound compound;builder.MakeCompound(compound);for(const auto& shell:shells)builder.Add(compound,shell);result.shape=compound;}
    if(!BRepCheck_Analyzer(result.shape).IsValid())throw std::runtime_error("连续地形曲面拓扑检查失败。");
    TopTools_IndexedMapOfShape mapped;TopExp::MapShapes(result.shape,TopAbs_FACE,mapped);
    result.faceDomains.fill(0,mapped.Extent());
    for(int i=0;i<faces.size();++i)result.faceDomains[mapped.FindIndex(faces[i])-1]=domains[i];

    // Display tessellation is generated from the completed CAD surfaces. It is
    // never sent back into the geometry or used as computational mesh edges.
    BRepMesh_IncrementalMesh triangulate(result.shape,std::max(0.01,spacing*0.05),false,0.2,false);
    vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> cells;
    vtkNew<vtkDoubleArray> elevation;elevation->SetName("Elevation");
    vtkNew<vtkIntArray> regions;regions->SetName("TerrainDomain");
    result.flatTriangles=0;
    for(int f=1;f<=mapped.Extent();++f)
    {
        TopLoc_Location location;auto mesh=BRep_Tool::Triangulation(TopoDS::Face(mapped(f)),location);
        if(mesh.IsNull())throw std::runtime_error("连续曲面显示数据生成失败。");
        const vtkIdType offset=points->GetNumberOfPoints();
        for(int p=1;p<=mesh->NbNodes();++p)
        {const auto xyz=mesh->Node(p).Transformed(location.Transformation());points->InsertNextPoint(xyz.X(),xyz.Y(),xyz.Z());elevation->InsertNextValue(xyz.Z());}
        for(int t=1;t<=mesh->NbTriangles();++t)
        {
            int a,b,c;mesh->Triangle(t).Get(a,b,c);if(mapped(f).Orientation()==TopAbs_REVERSED)std::swap(b,c);
            vtkIdType ids[3]={offset+a-1,offset+b-1,offset+c-1};cells->InsertNextCell(3,ids);regions->InsertNextValue(result.faceDomains[f-1]);
            result.domains[result.faceDomains[f-1]-1].triangles++;
        }
    }
    result.mesh=vtkSmartPointer<vtkPolyData>::New();result.mesh->SetPoints(points);result.mesh->SetPolys(cells);
    result.mesh->GetPointData()->SetScalars(elevation);result.mesh->GetCellData()->AddArray(regions);
    result.vertices=points->GetNumberOfPoints();result.triangles=cells->GetNumberOfCells();result.constraints=0;
    for(const auto& ring:rings)result.constraints+=ring.points.size();
    return true;
}

static void mergePlanarTerrainDomains(Result& result)
{
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);result.faceDomains.clear();
    int domain=0;
    for(TopExp_Explorer shell(result.shape,TopAbs_SHELL);shell.More();shell.Next())
    {
        ShapeUpgrade_UnifySameDomain merge(shell.Current(),true,true,true);merge.Build();
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(merge.Shape(),TopAbs_FACE,faces);
        if(faces.Extent()!=1)
            throw std::runtime_error("当前连续曲面构面需要互相嵌套的闭合等高线；开放或分叉的非共面等高线请先分成连续地形区。未使用三角面片替代曲面。");
        TopoDS_Shell merged;builder.MakeShell(merged);builder.Add(merged,faces(1));builder.Add(compound,merged);result.faceDomains.push_back(++domain);
    }
    result.shape=compound;
}
}
