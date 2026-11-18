#include "../plugins/Base_Mesh/SmeshMeshGenerator.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkUnstructuredGrid.h>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static TopAbs_ShapeEnum type(int dim){return dim==3?TopAbs_SOLID:dim==2?TopAbs_FACE:TopAbs_EDGE;}
static SmeshEntityReference reference(const TopoDS_Shape& shape,int dim,int id)
{
    TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(shape,type(dim),shapes);GProp_GProps p;
    if(dim==3)BRepGProp::VolumeProperties(shapes(id),p);else if(dim==2)BRepGProp::SurfaceProperties(shapes(id),p);else BRepGProp::LinearProperties(shapes(id),p);
    const auto c=p.CentreOfMass();return {dim,id,QVector3D(c.X(),c.Y(),c.Z()),std::abs(p.Mass())};
}
static int faceAt(const TopoDS_Shape& shape,double x,double y,double z)
{
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
    for(int i=1;i<=faces.Extent();++i)if((reference(shape,2,i).center-QVector3D(x,y,z)).length()<1.e-5)return i;
    throw std::runtime_error("Face fixture missing");
}
static SmeshMeshingResult generate(const SmeshMeshingRequest& request)
{
    SmeshMeshingResult result;QString error;
    if(!SmeshMeshGenerator::generate(request,&result,&error))throw std::runtime_error(error.toStdString());
    check(result.grid&&result.volumeElementCount>0,"No workflow volume cells");return result;
}
static SmeshSubmeshSettings volumeScope(const TopoDS_Shape& shape,int id,const QString& uuid)
{
    SmeshSubmeshSettings sub;sub.uuid=uuid;sub.parentUuid="workflow";sub.name=uuid;sub.dimension=3;
    sub.entityIds={id};sub.entities={reference(shape,3,id)};return sub;
}
static void sharedNodes(vtkUnstructuredGrid* grid)
{
    std::map<vtkIdType,std::set<int>> owners;auto* regions=grid->GetCellData()->GetArray("src_part");
    for(vtkIdType i=0;i<grid->GetNumberOfCells();++i){auto* cell=grid->GetCell(i);for(int p=0;p<cell->GetNumberOfPoints();++p)owners[cell->GetPointId(p)].insert(int(regions->GetTuple1(i)));}
    int shared=0;for(const auto& item:owners)if(item.second.size()==2)++shared;check(shared>=3,"Adjacent regions do not share native mesh nodes");
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir work;check(work.isValid(),"Temporary directory");
        BRep_Builder builder;TopoDS_Compound shape;builder.MakeCompound(shape);
        builder.Add(shape,BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape());
        builder.Add(shape,BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),10,10,10).Shape());
        SmeshMeshingRequest request;request.inputGeometryPath=work.filePath("two.brep");
        check(BRepTools::Write(shape,QFile::encodeName(request.inputGeometryPath).constData()),"Write geometry");
        request.lenMin=.1;request.lenMax=5.;request.exportVtk=false;request.exportNas=false;
        auto base=generate(request);
        auto volume=volumeScope(shape,1,"body-a");volume.maxSize=2.;volume.minSize=.1;volume.fineness=3;
        SmeshSubmeshSettings face;face.uuid="face-a";face.parentUuid=volume.uuid;face.name="Face";face.dimension=2;
        const int faceId=faceAt(shape,0,5,5);face.entities={reference(shape,2,faceId)};face.maxSize=1.;face.minSize=0.;face.curvatureSegments=12;
        TopTools_IndexedMapOfShape faces,edges,faceEdges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);TopExp::MapShapes(faces(faceId),TopAbs_EDGE,faceEdges);
        const int edgeId=edges.FindIndex(faceEdges(1));
        SmeshSubmeshSettings edge;edge.uuid="edge-a";edge.parentUuid=face.uuid;edge.name="Edge";edge.dimension=1;
        edge.entities={reference(shape,1,edgeId)};edge.edgeSegments=8;edge.edgeBias=1.2;
        request.workflow.meshUuid="workflow";request.workflow.submeshes={volume,face,edge};
        auto hierarchical=generate(request);check(hierarchical.nativeSubmeshCount>=3&&hierarchical.volumeElementCount>base.volumeElementCount,"Hierarchical sizing did not refine");sharedNodes(hierarchical.grid);
        std::cout<<"PASS: Mesh -> Solid -> Face -> Edge inheritance with native hypotheses\n";
        auto bad=request;bad.workflow.submeshes[1].entities={reference(shape,2,faceAt(shape,20,5,5))};
        SmeshMeshingResult failed;QString error;
        check(!SmeshMeshGenerator::generate(bad,&failed,&error)&&error.contains(QStringLiteral("不属于")),"Reject child outside parent scope");
        bad=request;bad.workflow.submeshes.push_back(volumeScope(shape,1,"duplicate"));
        check(!SmeshMeshGenerator::generate(bad,&failed,&error)&&!failed.grid,"Reject duplicate ownership");
        std::cout<<"PASS: hierarchy membership and overlapping operation validation\n";

        auto a=volumeScope(shape,1,"a"),b=volumeScope(shape,2,"b");a.maxSize=3.;b.maxSize=4.;
        a.parentUuid=b.parentUuid="incremental";request.workflow.meshUuid="incremental";
        request.workflow.submeshes={a,b};request.workflow.computeSubmeshUuid="a";
        auto first=generate(request);check(first.regionCount==1,"First incremental SubMesh scope");
        request.workflow.computeSubmeshUuid="b";
        auto second=generate(request);check(second.regionCount==2&&second.volumeElementCount>first.volumeElementCount,"SubMesh results did not accumulate in one parent Mesh");sharedNodes(second.grid);
        std::cout<<"PASS: incremental Solid SubMeshes share one parent Mesh and interface nodes\n";

        request.workflow.computeSubmeshUuid.clear();request.workflow.submeshes.clear();request.workflow.meshUuid="workflow";
        SmeshSweepSettings sweep;sweep.uuid="sweep";sweep.name="Sweep";sweep.volumeId=1;
        sweep.volume=reference(shape,3,1);sweep.source=reference(shape,2,faceAt(shape,0,5,5));sweep.target=reference(shape,2,faceAt(shape,10,5,5));
        sweep.sourceFaceId=sweep.source.entityId;sweep.targetFaceId=sweep.target.entityId;sweep.layers=5;sweep.sourceSize=3.;
        request.workflow.sweeps={sweep};
        auto mixed=generate(request);int hexes=0,tets=0;
        for(vtkIdType i=0;i<mixed.grid->GetNumberOfCells();++i){const auto t=mixed.grid->GetCellType(i);hexes+=t==VTK_HEXAHEDRON;tets+=t==VTK_TETRA;}
        check(hexes>0&&tets>0&&mixed.regionCount==2,"Native Sweep/Tet mixed cells missing");sharedNodes(mixed.grid);
        check(mixed.grid->GetFieldData()->GetArray("SmeshBoundaryFacets"),"Sweep boundaries missing");
        check(!mixed.grid->GetCellData()->GetArray("BoundaryLayerAddedCell"),"Unrequested boundary-layer data");
        std::cout<<"PASS: native mixed Sweep/Tet, "<<hexes<<" hexes, "<<tets<<" tets, shared nodes, no boundary layers\n";
        b.parentUuid="workflow";b.maxSize=2.;b.minSize=0.;b.fineness=3;request.workflow.submeshes={b};
        auto scopedMixed=generate(request);check(scopedMixed.regionCount==2,"Mixed solid SubMesh scopes");sharedNodes(scopedMixed.grid);
        auto mixedFace=face;mixedFace.uuid="mixed-face";mixedFace.parentUuid=b.uuid;
        mixedFace.entities={reference(shape,2,faceAt(shape,20,5,5))};mixedFace.maxSize=1.;mixedFace.minSize=0.;
        request.workflow.submeshes.push_back(mixedFace);
        auto nestedMixed=generate(request);check(nestedMixed.regionCount==2,"Mixed nested Face settings");sharedNodes(nestedMixed.grid);
        std::cout<<"PASS: scoped Tet and child Face settings coexist with native Sweep\n";
        // Regression: selecting every Solid for sizing must not compete with
        // the Sweep meshing method. Sweep keeps its own source/layer controls.
        auto all=volumeScope(shape,1,"all-solid-sizing");all.entities.push_back(reference(shape,3,2));
        all.entityIds.push_back(2);all.maxSize=2.;all.minSize=0.;
        request.workflow.submeshes={all};
        auto allSized=generate(request);int allHexes=0;
        for(vtkIdType i=0;i<allSized.grid->GetNumberOfCells();++i)allHexes+=allSized.grid->GetCellType(i)==VTK_HEXAHEDRON;
        check(allSized.regionCount==2&&allHexes==hexes,"All-Solid sizing changed the Sweep mesh or lost Tet regions");sharedNodes(allSized.grid);
        std::cout<<"PASS: all-Solid sizing and Sweep coexist, with unchanged swept cells\n";
        request.nativeFineness=5;request.lenMin=0.;request.growthRate=.4;
        request.segmentsPerEdge=1.5;request.segmentsPerRadius=2.5;
        auto custom=generate(request);check(custom.regionCount==2,"Custom parent precision with inherited Solid settings");sharedNodes(custom.grid);
        auto invalid=request;invalid.growthRate=0.;
        check(!SmeshMeshGenerator::generate(invalid,&failed,&error),"Reject invalid custom growth rate");
        invalid=request;invalid.workflow.sweeps.push_back(sweep);
        check(!SmeshMeshGenerator::generate(invalid,&failed,&error)&&error.contains(QStringLiteral("多个扫掠")),"Reject two Sweep methods on the same Solid");
        std::cout<<"PASS: custom precision, automatic minimum and duplicate Sweep validation\n";
        return 0;
    }catch(const std::exception& ex){std::cerr<<"FAIL: "<<ex.what()<<std::endl;return 1;}
}
