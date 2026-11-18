#include "../plugins/Base_Mesh/SmeshMeshGenerator.h"
#include <SurfaceDomainData.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <vtkUnstructuredGrid.h>
#include <vtkCellData.h>
#include <vtkCell.h>
#include <vtkDataArray.h>
#include <map>
#include <set>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
static SmeshEntityReference ref(const TopoDS_Shape& shape,int dimension,int id)
{
    TopTools_IndexedMapOfShape shapes; TopExp::MapShapes(shape,dimension==3?TopAbs_SOLID:TopAbs_FACE,shapes);
    GProp_GProps properties;
    if(dimension==3)BRepGProp::VolumeProperties(shapes(id),properties);
    else BRepGProp::SurfaceProperties(shapes(id),properties);
    const auto p=properties.CentreOfMass();
    return {dimension,id,QVector3D(p.X(),p.Y(),p.Z()),std::abs(properties.Mass())};
}
static SmeshMeshingResult generate(const SmeshMeshingRequest& request)
{
    SmeshMeshingResult result;QString error;
    if(!SmeshMeshGenerator::generate(request,&result,&error))throw std::runtime_error(error.toStdString());
    check(result.grid&&result.grid->GetNumberOfCells()>0,"Empty grid");return result;
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir work;check(work.isValid(),"Temporary directory");
        BRep_Builder builder;TopoDS_Compound solids;builder.MakeCompound(solids);
        builder.Add(solids,BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape());
        builder.Add(solids,BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),10,10,10).Shape());
        builder.Add(solids,BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),5,5,5).Shape()); // excluded overlapping solid
        SmeshMeshingRequest request;request.inputGeometryPath=work.filePath("scope.brep");
        check(BRepTools::Write(solids,QFile::encodeName(request.inputGeometryPath).constData()),"Write CAD");
        QFile file(request.inputGeometryPath);check(file.open(QIODevice::ReadOnly),"Read CAD");
        const auto original=file.readAll();file.close();
        request.lenMin=.1;request.lenMax=5.;request.exportVtk=false;request.exportNas=false;
        request.allGeometry=false;request.geometryScope={ref(solids,3,2)};
        auto one=generate(request);double bounds[6];one.grid->GetBounds(bounds);
        check(one.regionCount==1&&bounds[0]>=9.999999&&bounds[1]<=20.000001,"Unselected solid was meshed");
        auto* ids=one.grid->GetCellData()->GetArray("src_part");check(ids,"Missing original domain ids");
        for(vtkIdType i=0;i<ids->GetNumberOfTuples();++i)check(ids->GetTuple1(i)==2,"Original Solid id changed");
        QStringList log;request.progress=[&](const QString& s){log.append(s);};
        request.geometryScope={ref(solids,3,2),ref(solids,3,1)};
        auto together=generate(request);
        check(together.regionCount==2&&together.conformalized&&together.sharedInterfaceCount>0,"Partition was not executed");
        check(log.join('\n').contains(QStringLiteral("执行 OCC Partition")),"Partition missing from logs");
        std::map<vtkIdType,std::set<int>> owners;
        auto* regions=together.grid->GetCellData()->GetArray("src_part");
        for(vtkIdType i=0;i<together.grid->GetNumberOfCells();++i) {
            auto* cell=together.grid->GetCell(i);
            for(int j=0;j<cell->GetNumberOfPoints();++j)owners[cell->GetPointId(j)].insert(int(regions->GetTuple1(i)));
        }
        int common=0;for(const auto& entry:owners)if(entry.second.size()==2)++common;
        check(common>=3,"Selected adjacent solids do not share node ids");
        request.geometryScope={ref(solids,3,2)};
        SmeshPartitionSettings cut;cut.axis=0;cut.coordinate=15.;cut.targetVolumeUiId=2;cut.target=ref(solids,3,2);
        request.workflow.partitions={cut};
        auto cutScope=generate(request);cutScope.grid->GetBounds(bounds);
        check(bounds[0]>=9.999999&&cutScope.regionCount==1,"Manual partition lost parent geometry scope");
        ids=cutScope.grid->GetCellData()->GetArray("src_part");
        for(vtkIdType i=0;i<ids->GetNumberOfTuples();++i)check(ids->GetTuple1(i)==2,"Manual partition lost original owner");
        request.workflow={};log.clear();
        request.conformalizeRegions=false;
        auto separate=generate(request);check(!separate.conformalized,"Disabled Partition still ran");
        check(!log.join('\n').contains(QStringLiteral("执行 OCC Partition")),"Disabled Partition was logged as executed");
        request.geometryScope={ref(solids,3,2)};
        SmeshSubmeshSettings outside;outside.uuid="outside";outside.dimension=3;outside.name="Outside";
        outside.maxSize=2.;outside.entities={ref(solids,3,1)};request.workflow.submeshes={outside};
        QString error;SmeshMeshingResult failed;
        check(!SmeshMeshGenerator::generate(request,&failed,&error)&&error.contains(QStringLiteral("范围")),"Outside child settings applied to wrong solid");
        request.workflow={};request.geometryScope.clear();
        check(!SmeshMeshGenerator::generate(request,&failed,&error),"Empty explicit scope fell back to all geometry");
        check(file.open(QIODevice::ReadOnly)&&file.readAll()==original,"Source CAD modified");file.close();
        std::cout<<"PASS: explicit 3D scope, original domains, selected-only Partition, shared nodes, disabled switch, outside-child rejection\n";

        TopoDS_Compound faces;builder.MakeCompound(faces);
        builder.Add(faces,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,10,0,10).Shape());
        builder.Add(faces,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(10,0,0),gp_Dir(0,0,1)),0,10,0,10).Shape());
        request.inputGeometryPath=work.filePath("faces.brep");
        check(BRepTools::Write(faces,QFile::encodeName(request.inputGeometryPath).constData()),"Write surfaces");
        SurfaceDomainData::Definition domains;domains.faceDomains={11,22};domains.materialGroups={{11,1},{22,2}};
        check(SurfaceDomainData::write(request.inputGeometryPath,domains,error),"Write surface ownership");
        request.dimension=2;request.elemType=1;request.conformalizeRegions=true;request.geometryScope={ref(faces,2,2)};
        auto surface=generate(request);surface.grid->GetBounds(bounds);
        check(surface.regionCount==1&&bounds[0]>=9.999999,"2D geometry scope ignored");
        ids=surface.grid->GetCellData()->GetArray("src_part");
        for(vtkIdType i=0;i<ids->GetNumberOfTuples();++i)check(ids->GetTuple1(i)==22,"Selected surface domain ownership lost");
        std::cout<<"PASS: explicit 2D face scope preserves domain ownership\n";
        return 0;
    }catch(const std::exception& ex){std::cerr<<"FAIL: "<<ex.what()<<std::endl;return 1;}
}
