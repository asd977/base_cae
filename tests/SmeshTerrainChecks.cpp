#include "../plugins/Base_Mesh/SmeshMeshGenerator.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <vtkUnstructuredGrid.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Pln.hxx>
#include <SurfaceDomainData.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGridReader.h>
#include <vtkNew.h>

static void check(bool ok,const char* message)
{if(!ok)throw std::runtime_error(message);}

static void generateOrThrow(const SmeshMeshingRequest& request,SmeshMeshingResult& result)
{
    QString error;
    if(!SmeshMeshGenerator::generate(request,&result,&error))throw std::runtime_error(error.toStdString());
}

static void checkTerrainMesh(const SmeshMeshingResult& result)
{
    check(result.grid&&result.regionCount==5&&result.sourceRegionCount==5,"terrain preserves five contour domains");
    auto* materials=result.grid->GetCellData()->GetArray("MATYPE");
    auto* parts=result.grid->GetCellData()->GetArray("src_part");
    check(materials&&parts,"mesh must carry material/domain arrays");
    auto* names=vtkStringArray::SafeDownCast(result.grid->GetCellData()->GetAbstractArray("GroupName"));
    check(names&&names->GetNumberOfValues()==result.grid->GetNumberOfCells(),"mesh carries readable contour domain names");
    std::map<std::pair<vtkIdType,vtkIdType>,int> edges;
    for(vtkIdType i=0;i<result.grid->GetNumberOfCells();++i)
    {
        auto* cell=result.grid->GetCell(i);check(cell->GetNumberOfPoints()==3,"first-order terrain triangles");
        const int domain=int(parts->GetTuple1(i));
        check(materials->GetTuple1(i)==3&&domain>=1&&domain<=5,"contour domains are distinct but share default material");
        double lo=1e300,hi=-1e300;
        for(int p=0;p<3;++p){const double z=result.grid->GetPoint(cell->GetPointId(p))[2];lo=std::min(lo,z);hi=std::max(hi,z);}
        check(lo>=100+10*(domain-1)-1e-6&&hi<=100+10*domain+1e-6,"no computational element crosses a contour domain boundary");
        for(int j=0;j<3;++j)++edges[std::minmax(cell->GetPointId(j),cell->GetPointId((j+1)%3))];
    }
    int outerEdges=0;
    for(const auto& edge:edges)
    {
        check(edge.second<=2,"terrain mesh is manifold");
        if(edge.second==1)
        {
            ++outerEdges;
            // Every unpaired edge must be on the actual outer boundary, at Z=100.
            check(std::abs(result.grid->GetPoint(edge.first.first)[2]-100)<1e-6&&
                  std::abs(result.grid->GetPoint(edge.first.second)[2]-100)<1e-6,
                  "no disconnected internal seams between terrain facets");
        }
    }
    check(outerEdges>0,"open terrain has an outer boundary");
    double bounds[6];result.grid->GetBounds(bounds);
    check(std::abs(bounds[4]-100)<1e-6&&std::abs(bounds[5]-150)<1e-6,"terrain elevations retained in computational mesh");
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    if(argc<2)return 2;
    try
    {
    SmeshMeshingRequest request;
    request.inputGeometryPath=QString::fromLocal8Bit(argv[1]);
    request.dimension=2;request.elemType=1;request.lenMin=.5;request.lenMax=10;
    request.exportVtk=true;request.exportNas=false;
    request.vtkPath=QFileInfo(request.inputGeometryPath).dir().filePath("terrain-computational-mesh.vtk");
    request.optimize=true;
    SmeshMeshingResult result;QString error;
    if(!SmeshMeshGenerator::generate(request,&result,&error))
    {std::cerr<<"FAIL: "<<error.toUtf8().constData()<<std::endl;return 1;}
    checkTerrainMesh(result);
    vtkNew<vtkUnstructuredGridReader> reader;reader->SetFileName(QFile::encodeName(request.vtkPath).constData());reader->Update();
    auto loaded=result;loaded.grid=reader->GetOutput();checkTerrainMesh(loaded);
    request.exportVtk=false;
    std::cout<<"PASS: terrain surface meshing: "<<result.pointCount<<" points, "<<result.surfaceElementCount
             <<" elements, "<<result.regionCount<<" domain"<<std::endl;

    const int coarseElements=result.surfaceElementCount;
    request.lenMax=3;
    generateOrThrow(request,result);checkTerrainMesh(result);
    check(result.surfaceElementCount>coarseElements,"mesh element size controls refinement independently of terrain sampling");
    std::cout<<"PASS: mesh size refinement: "<<coarseElements<<" -> "<<result.surfaceElementCount<<" elements, five domains and one material"<<std::endl;

    TopoDS_Shape terrain;BRep_Builder builder;
    check(BRepTools::Read(terrain,QFile::encodeName(request.inputGeometryPath).constData(),builder),"read terrain shell");
    QTemporaryDir temp;check(temp.isValid(),"temporary regression geometry folder");
    const QString mixedPath=temp.filePath("mixed.brep");
    gp_Trsf translation;translation.SetTranslation(gp_Vec(300,0,0));
    TopoDS_Compound two;builder.MakeCompound(two);builder.Add(two,terrain);
    builder.Add(two,BRepBuilderAPI_Transform(terrain,translation,true).Shape());
    check(BRepTools::Write(two,QFile::encodeName(mixedPath).constData()),"write two terrain objects");
    request.inputGeometryPath=mixedPath;request.lenMax=10;
    generateOrThrow(request,result);
    check(result.regionCount==10&&result.sourceRegionCount==10,"two contour-partitioned terrain objects retain ten domains");
    std::cout<<"PASS: two terrain surfaces retain ten independent contour domains"<<std::endl;

    TopoDS_Compound mixed;builder.MakeCompound(mixed);builder.Add(mixed,terrain);
    builder.Add(mixed,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(500300,3000000,100),gp_Dir(0,0,1)),0,10,0,10).Face());
    check(BRepTools::Write(mixed,QFile::encodeName(mixedPath).constData()),"write terrain and loose face");
    generateOrThrow(request,result);
    check(result.regionCount==6,"ordinary face retains a domain separate from five terrain domains");
    auto* parts=result.grid->GetCellData()->GetArray("src_part");
    for(vtkIdType i=0;i<result.grid->GetNumberOfCells();++i)
    {
        auto* cell=result.grid->GetCell(i);double center=0;
        for(int p=0;p<cell->GetNumberOfPoints();++p)center+=result.grid->GetPoint(cell->GetPointId(p))[0]/cell->GetNumberOfPoints();
        check(center>500200?parts->GetTuple1(i)==6:parts->GetTuple1(i)>=1&&parts->GetTuple1(i)<=5,"surface domain mapping matches geometry after conformal processing");
    }
    std::cout<<"PASS: terrain plus ordinary face preserves correct domain/material mapping"<<std::endl;
    TopoDS_Compound planar;builder.MakeCompound(planar);
    builder.Add(planar,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,10,0,10).Face());
    builder.Add(planar,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),10,20,0,10).Face());
    check(BRepTools::Write(planar,QFile::encodeName(mixedPath).constData()),"write ordinary material faces");
    request.lenMax=2;
    generateOrThrow(request,result);
    check(result.regionCount==2,"adjacent ordinary material faces are not merged");
    std::cout<<"PASS: terrain plus ordinary faces, correct domain mapping, existing planar material regions preserved"<<std::endl;
    QFile metadataSource(SurfaceDomainData::fileName(QString::fromLocal8Bit(argv[1])));
    check(metadataSource.open(QIODevice::ReadOnly),"read valid domain metadata for stale-data check");
    QFile stale(SurfaceDomainData::fileName(mixedPath));check(stale.open(QIODevice::WriteOnly),"create mismatched ownership fixture");
    stale.write(metadataSource.readAll());stale.close();
    check(!SmeshMeshGenerator::generate(request,&result,&error)&&error.contains(QStringLiteral("不匹配")),"stale domain metadata is rejected before meshing");
    std::cout<<"PASS: mismatched geometry/domain ownership is rejected"<<std::endl;
    return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<std::endl;return 1;}
}
