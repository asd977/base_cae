#include <GmdsPartitionRunner.h>
#include "../plugins/Base_Mesh/SmeshMeshGenerator.h"
#include "../plugins/Base_Mesh/core/SmeshOccPartitioner.h"
#include <PartitionFaceOrder.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTimer>
#include <vtkUnstructuredGrid.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkCell.h>
#include <vtkCellType.h>
#include <sstream>
#include <iostream>
#include <set>
#include <map>

static void check(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
static QByteArray serialize(const TopoDS_Shape& s)
{ std::ostringstream o; BRepTools::Write(s,o,false,false,TopTools_FormatVersion_CURRENT); auto bytes=o.str(); return QByteArray(bytes.data(),qsizetype(bytes.size())); }
static QVariantMap run(GmdsPartitionRunner& runner,const QByteArray& brep,const QJsonObject& request,bool cancel=false,int timeoutMs=6000)
{
    QEventLoop loop; QVariantMap result;
    const auto c=QObject::connect(&runner,&GmdsPartitionRunner::finished,&loop,[&](const QVariantMap& r) { result=r; loop.quit(); });
    check(runner.start(brep,request,timeoutMs),"Could not start GMDS worker");
    if(cancel) QTimer::singleShot(0,&runner,&GmdsPartitionRunner::cancel);
    QTimer deadline; deadline.setSingleShot(true); QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit); deadline.start(10000);
    loop.exec(); QObject::disconnect(c); check(!result.isEmpty(),"Worker did not finish within test deadline"); return result;
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        GmdsPartitionRunner runner;
        QJsonObject request{{"bodyIds",QJsonArray{1}},{"planes",QJsonArray{}},{"maxSuggestions",4}};
        auto box=run(runner,serialize(BRepPrimAPI_MakeBox(2,2,2).Shape()),request);
        check(box.value("valid").toBool() && box.value("alreadySweepable").toBool() && box.value("suggestions").toList().isEmpty(),"Box should not be cut");
        BRepBuilderAPI_MakePolygon polygon;
        for(auto p:{gp_Pnt(0,0,0),gp_Pnt(2,0,0),gp_Pnt(2,1,0),gp_Pnt(1,1,0),gp_Pnt(1,2,0),gp_Pnt(0,2,0)}) polygon.Add(p);
        polygon.Close(); const auto shape=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(0,0,2)).Shape();
        const auto source=serialize(shape); auto result=run(runner,source,request);
        std::cout<<result.value("message").toString().toStdString()<<std::endl;
        check(result.value("valid").toBool(),"GMDS failed to analyze L solid");
        const auto suggestions=result.value("suggestions").toList();
        check(!suggestions.isEmpty() && result.value("allSweepable").toBool(),"GMDS did not decompose L solid into blocks");
        check(serialize(shape)==source,"Source CAD was modified");
        check(run(runner,source,request).value("cacheHit").toBool(),"Identical input did not reuse cached result");
        auto limited=request; limited["maxSuggestions"]=1; limited["bodyIds"]=QJsonArray{17};
        auto partial=run(runner,source,limited);
        check(partial.value("valid").toBool() && partial.value("suggestions").toList().size()==1,"Suggestion limit ignored");
        auto firstCut=partial.value("suggestions").toList().first().toMap();
        check(firstCut.value("targetVolumeUiId").toInt()==17,"Global target ID was renumbered");
        QJsonObject oldCut=QJsonObject::fromVariantMap(firstCut);
        for(auto key:{QStringLiteral("origin"),QStringLiteral("normal")}) { auto v=firstCut.value(key).value<QVector3D>(); oldCut[key]=QJsonArray{double(v.x()),double(v.y()),double(v.z())}; }
        auto continued=request; continued["bodyIds"]=QJsonArray{17}; continued["planes"]=QJsonArray{oldCut};
        auto resumed=run(runner,source,continued);
        check(resumed.value("valid").toBool() && resumed.value("allSweepable").toBool(),"Existing cut rules were not preserved during GMDS analysis");
        gp_Trsf scale,shift; scale.SetScale(gp_Pnt(0,0,0),1000); shift.SetTranslation(gp_Vec(50000,-30000,10000)); shift.Multiply(scale);
        auto moved=run(runner,serialize(BRepBuilderAPI_Transform(shape,shift,true).Shape()),request);
        check(moved.value("valid").toBool() && moved.value("allSweepable").toBool(),"Scale/translation changed GMDS partition result");
        QTemporaryDir dir; check(dir.isValid(),"Temporary directory");
        SmeshMeshingRequest mesh; mesh.inputGeometryPath=dir.filePath("cad.brep"); mesh.lenMin=.05; mesh.lenMax=.5; mesh.exportVtk=false; mesh.exportNas=false;
        check(BRepTools::Write(shape,QFile::encodeName(mesh.inputGeometryPath).constData()),"Write source CAD");
        for(auto value:suggestions) {
            auto s=value.toMap(); SmeshPartitionSettings p; p.axis=3; p.targetVolumeUiId=1; p.target={3,1};
            p.origin=s.value("origin").value<QVector3D>(); p.normal=s.value("normal").value<QVector3D>(); mesh.workflow.partitions.push_back(p);
        }
        SmeshOccPartitioner::Settings split; split.inputStepPath=mesh.inputGeometryPath; split.outputBrepPath=dir.filePath("partition.brep"); split.rules=mesh.workflow.partitions;
        const auto partition=SmeshOccPartitioner::partition(split);
        check(partition.regionCount>=2,"GMDS cutters not accepted by OCC partition workflow");
        TopoDS_Shape splitShape; BRep_Builder builder; check(BRepTools::Read(splitShape,QFile::encodeName(partition.brepPath).constData(),builder),"Read partition");
        TopTools_IndexedMapOfShape blocks; TopExp::MapShapes(splitShape,TopAbs_SOLID,blocks);
        for(int i=1;i<=blocks.Extent();++i) {
            auto caps=GeometryPartitionTopology::orderedFaces(blocks(i)); int a=0,b=0; double lo=1e100,hi=-1e100;
            for(int j=0;j<caps.size();++j) { if(caps[j].cz<lo) {lo=caps[j].cz;a=j+1;} if(caps[j].cz>hi) {hi=caps[j].cz;b=j+1;} }
            SmeshSweepSettings sweep; sweep.uuid=QString("gmds-block-%1").arg(i); sweep.name=sweep.uuid; sweep.partitionRegion=true;
            sweep.volumeId=i; sweep.volume={3,i}; sweep.sourceFaceId=a; sweep.targetFaceId=b; sweep.source={2,a,i}; sweep.target={2,b,i}; sweep.layers=3; sweep.sourceSize=.5;
            mesh.workflow.sweeps.push_back(sweep);
        }
        SmeshMeshingResult output; QString error;
        check(SmeshMeshGenerator::generate(mesh,&output,&error),qPrintable(error));
        check(output.grid && output.volumeElementCount>0,"SMESH did not generate GMDS blocks");
        int hexes=0; std::map<vtkIdType,std::set<int>> owners;
        auto* region=output.grid->GetCellData()->GetArray("src_part"); check(region,"Missing region ownership");
        for(vtkIdType i=0;i<output.grid->GetNumberOfCells();++i) {
            hexes+=output.grid->GetCellType(i)==VTK_HEXAHEDRON; auto* cell=output.grid->GetCell(i);
            // src_part deliberately preserves original material ownership. Find
            // child-block membership geometrically for this interface check.
            double center[3]{};
            for(int p=0;p<cell->GetNumberOfPoints();++p) { double xyz[3]; output.grid->GetPoint(cell->GetPointId(p),xyz); for(int k=0;k<3;++k) center[k]+=xyz[k]/cell->GetNumberOfPoints(); }
            int owner=0;
            for(int b=1;b<=blocks.Extent();++b) if(BRepClass3d_SolidClassifier(blocks(b),gp_Pnt(center[0],center[1],center[2]),1e-7).State()==TopAbs_IN) { owner=b; break; }
            check(owner>0,"Mesh cell outside GMDS partition blocks");
            check(int(region->GetTuple1(i))==1,"Original material domain ownership changed");
            for(int p=0;p<cell->GetNumberOfPoints();++p) owners[cell->GetPointId(p)].insert(owner);
        }
        int shared=0; for(auto& o:owners) if(o.second.size()>1) ++shared;
        check(hexes>0 && shared>0,"GMDS -> OCC -> SMESH lost hexahedra or shared interface nodes");
        std::cout<<"PASS: GMDS candidates -> exact OCC cuts -> native SMESH: "<<hexes<<" hexes, "<<shared<<" shared nodes\n";
        auto curved=run(runner,serialize(BRepPrimAPI_MakeCylinder(1,2).Shape()),request);
        check(curved.value("valid").toBool() && curved.value("skipped").toList().size()==1 && curved.value("suggestions").toList().isEmpty(),"Curved geometry not skipped");
        auto cylindricalCut=request;
        cylindricalCut["planes"]=QJsonArray{QJsonObject{{"axis",4},{"radius",0.4},{"targetVolumeUiId",1},{"origin",QJsonArray{1,1,1}},{"normal",QJsonArray{0,0,1}}}};
        auto residual=run(runner,serialize(BRepPrimAPI_MakeBox(2,2,2).Shape()),cylindricalCut);
        check(residual.value("valid").toBool() && residual.value("skipped").toList().size()==1,"Curved residuals from saved manual cuts were not skipped");
        BRepBuilderAPI_MakePolygon many;
        for(int i=0;i<60;++i) { const double a=i*2.0*3.141592653589793/60.0; many.Add(gp_Pnt(std::cos(a),std::sin(a),0)); }
        many.Close(); const auto complex=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(many.Wire()).Face(),gp_Vec(0,0,1)).Shape();
        auto skipped=run(runner,serialize(complex),request);
        check(skipped.value("valid").toBool() && skipped.value("skipped").toList().size()==1 && skipped.value("trialSplits").toInt()==0,"Complex topology not skipped before trial splitting");
        auto cancelled=run(runner,source,request,true); check(cancelled.value("cancelled").toBool(),"Cancel was ignored");
        auto bad=run(runner,"invalid BREP",request); check(!bad.value("valid").toBool(),"Invalid CAD accepted");
        GmdsPartitionRunner timedRunner;
        auto timeout=run(timedRunner,source,request,false,0);
        check(!timeout.value("valid").toBool() && timeout.value("timedOut").toBool(),"Worker timeout did not stop computation");
        check(run(timedRunner,source,request).value("valid").toBool(),"Worker did not recover after timeout");
        check(timedRunner.start(source,request),"Could not queue cached result"); timedRunner.cancel();
        auto replacement=run(timedRunner,serialize(BRepPrimAPI_MakeBox(2,2,2).Shape()),request);
        check(replacement.value("alreadySweepable").toBool(),"Cancelled cached callback completed a newer request");
        std::cout<<"PASS: cache, global IDs, existing rules, scale/translation, complex/curve skipping, cancellation, invalid CAD\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<std::endl; return 1; }
}
