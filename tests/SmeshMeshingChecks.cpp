#include "../plugins/Base_Mesh/SmeshMeshGenerator.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>
#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

static void check(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

static SmeshMeshingResult generate(const SmeshMeshingRequest& request)
{
    SmeshMeshingResult result; QString error;
    if (!SmeshMeshGenerator::generate(request, &result, &error))
        throw std::runtime_error(error.toStdString());
    for (const char* name : {"MATYPE", "LAYER", "KETYPE", "src_part", "elem_id"})
        check(result.grid->GetCellData()->GetArray(name), "Missing solver metadata");
    auto* facets = result.grid->GetFieldData()->GetArray("SmeshBoundaryFacets");
    check(facets && facets->GetNumberOfTuples() > 0, "Missing native CAD boundaries");
    std::set<int> dimensions;
    for (vtkIdType i=0; i<facets->GetNumberOfTuples(); ++i) {
        dimensions.insert(int(facets->GetComponent(i,0)));
        const int count=int(facets->GetComponent(i,2));
        for (int n=0; n<count; ++n) {
            const auto id=vtkIdType(facets->GetComponent(i,n+3));
            check(id>=0 && id<result.grid->GetNumberOfPoints(), "Invalid boundary connectivity");
        }
    }
    for (int dim=0; dim<request.dimension; ++dim)
        check(dimensions.count(dim), "Missing point/edge/face boundary dimension");
    return result;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir work; check(work.isValid(), "Temporary folder");
        BRep_Builder builder; TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (int i=0; i<2; ++i)
            builder.Add(compound, BRepBuilderAPI_MakeFace(
                gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)), i*10., (i+1)*10., 0., 10.).Face());
        const auto input=work.filePath("surfaces.brep");
        check(BRepTools::Write(compound,QFile::encodeName(input).constData()), "Write surfaces");
        SmeshMeshingRequest request;
        request.inputGeometryPath=input; request.dimension=2; request.elemType=1;
        request.lenMin=.1; request.lenMax=3.; request.exportNas=false;
        request.vtkPath=work.filePath("result.vtk");
        auto coarse=generate(request);
        check(coarse.regionCount==2 && coarse.sharedInterfaceCount==1, "Two conformal surface regions");
        std::map<std::pair<vtkIdType,vtkIdType>,std::set<int>> owners;
        auto* regions=coarse.grid->GetCellData()->GetArray("src_part");
        for (vtkIdType i=0;i<coarse.grid->GetNumberOfCells();++i) {
            auto* cell=coarse.grid->GetCell(i);
            check(cell->GetCellType()==VTK_TRIANGLE,"Triangle output");
            for (int j=0;j<3;++j)
                owners[std::minmax(cell->GetPointId(j),cell->GetPointId((j+1)%3))].insert(int(regions->GetTuple1(i)));
        }
        int shared=0; for(const auto& edge:owners) if(edge.second.size()==2) ++shared;
        check(shared>0,"Adjacent surface domains share mesh nodes");
        request.localSizeControls={{2,1,.8}, {1,1,.5}};
        auto fine=generate(request);
        check(fine.nativeSubmeshCount>=2 && fine.surfaceElementCount>coarse.surfaceElementCount,
              "Native Face and Edge SubMeshes refine the mesh");
        request.localSizeControls.clear(); request.elementOrder=2;
        auto quadratic=generate(request);
        check(quadratic.grid->GetCellType(0)==VTK_QUADRATIC_TRIANGLE,"Quadratic triangles");
        request.elementOrder=1; request.elemType=2;
        auto quad=generate(request); int quads=0;
        for(vtkIdType i=0;i<quad.grid->GetNumberOfCells();++i) if(quad.grid->GetCellType(i)==VTK_QUAD) ++quads;
        check(quads>0,"Quad-dominated surface output");
        std::cout << "PASS: conformal surfaces, native local sizes, quadratic and quad elements\n";

        builder.MakeCompound(compound);
        for(int i=0;i<2;++i) builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(i*10.,0,0),10,10,10).Shape());
        const auto solid=work.filePath("solids.brep");
        check(BRepTools::Write(compound,QFile::encodeName(solid).constData()),"Write solids");
        request.inputGeometryPath=solid; request.dimension=3; request.elemType=3;
        request.lenMax=5.; request.exportNas=true; request.nasPath=work.filePath("mesh.nas");
        auto tet=generate(request);
        check(tet.regionCount==2 && tet.sharedInterfaceCount==1 && tet.volumeElementCount>0,"Two volume regions");
        auto* parts=tet.grid->GetCellData()->GetArray("src_part");
        std::map<vtkIdType,std::set<int>> nodeRegions;
        for(vtkIdType i=0;i<tet.grid->GetNumberOfCells();++i) {
            auto* cell=tet.grid->GetCell(i); check(cell->GetCellType()==VTK_TETRA,"Tet4 output");
            for(int j=0;j<cell->GetNumberOfPoints();++j) nodeRegions[cell->GetPointId(j)].insert(int(parts->GetTuple1(i)));
        }
        int common=0; for(const auto& node:nodeRegions) if(node.second.size()==2) ++common;
        check(common>=3,"Shared volume interface uses common nodes");
        QFile nas(request.nasPath); check(nas.open(QIODevice::ReadOnly) && nas.readAll().contains("CTETRA"),"Nastran export");
        request.localSizeControls={{3,1,2.}};
        auto localTet=generate(request);
        std::cout << "Solid sizing: " << tet.volumeElementCount << " -> " << localTet.volumeElementCount << " cells\n";
        check(localTet.nativeSubmeshCount>0 && localTet.volumeElementCount>tet.volumeElementCount,"Native Solid sizing");
        request.localSizeControls.clear(); request.elementOrder=2;
        auto tet10=generate(request); check(tet10.grid->GetCellType(0)==VTK_QUADRATIC_TETRA,"Tet10 output");
        request.elementOrder=1; request.elemType=4;
        auto hex=generate(request);
        check(hex.grid->GetCellType(0)==VTK_HEXAHEDRON && hex.invalidElementCount==0 && hex.minScaledJacobian>0,
              "Quality-checked Tet4 to Hex8 conversion");
        auto reader=vtkSmartPointer<vtkUnstructuredGridReader>::New();
        reader->SetFileName(QFile::encodeName(request.vtkPath).constData()); reader->Update();
        check(reader->GetOutput()->GetNumberOfCells()==hex.volumeElementCount,"VTK export roundtrip");
        std::cout << "PASS: conformal volumes, Solid sizing, Tet4/Tet10/Hex8, boundaries, VTK/Nastran\n";
        request.lenMin=100; QString error; SmeshMeshingResult failed;
        check(!SmeshMeshGenerator::generate(request,&failed,&error) && !error.isEmpty() && !failed.grid,"Reject invalid sizes");
        std::cout << "PASS: invalid requests fail without publishing a result\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
