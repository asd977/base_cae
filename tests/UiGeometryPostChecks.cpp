#include <QtWidgets>
#include <StyleManager.h>
#include <PluginInterface.h>
#include <ServiceRegistry.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <services/PostProcessingService.h>
#include <services/MaterialManagerService.h>
#include <services/SolverService.h>
#include <services/GlobalParameterService.h>
#include <services/PhySicalFieldService.h>
#include <ProjectNode.h>
#include <BasePage.h>
#include <PickManager.h>
#include "SceneVisibilityMesh.h"
#include "../plugins/Base_Gemo/sketch/SketchCurveTools.h"
#include "../plugins/Base_PostProcessing/PostThresholdDisplay.h"
#include "../plugins/Base_PostProcessing/PostVisualUtils.h"
#include "../plugins/Base_PostProcessing/PostDisplayControlWidget.h"
#include "../plugins/Base_PostProcessing/PostScientificText.h"
#include "../plugins/Base_PostProcessing/ResultDisplayNames.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <vtkPoints.h>
#include <vtkTetra.h>
#include <vtkRenderWindow.h>
#include <vtkDataSetMapper.h>
#include <vtkCamera.h>
#include <vtkXMLUnstructuredGridWriter.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <vtkSphereSource.h>
#include <vtkAppendFilter.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkOpenGLRenderWindow.h>
#include "../plugins/Base_PostProcessing/PostTecplotUtils.h"
#include <vtkFXAAOptions.h>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
using namespace GeometrySketch;
static SketchDocument line(double x1,double y1,double x2,double y2)
{ SketchDocument d; d.setProfile({{x1,y1},{x2,y2}},false,true); return d; }
static double length(const SketchDocument& d)
{ GProp_GProps p; BRepGProp::LinearProperties(SketchCurveTools::shape(d),p); return p.Mass(); }
static void geometryChecks()
{
    std::vector<std::vector<SketchDocument>> parts; QString error;
    auto horizontal=line(0,0,10,0), vertical=line(5,-5,5,5);
    check(SketchCurveTools::splitAtNodes({horizontal,vertical},1e-8,parts,error),"cross split");
    check(parts[0].size()==2 && parts[1].size()==2,"four branches at cross");
    check(SketchCurveTools::splitAtNodes({horizontal},1e-8,parts,error,{vertical}),"selected curve with unselected reference");
    check(parts.size()==1 && parts[0].size()==2,"unselected curve cuts selected curve without being replaced");
    check(std::abs(length(parts[0][0])+length(parts[0][1])-10)<1e-8,"cross preserves length");
    check(SketchCurveTools::splitAtNodes({horizontal,line(5,0,5,5)},1e-8,parts,error),"T split");
    check(parts[0].size()==2 && parts[1].size()==1,"T splits only interior");
    SketchDocument nodes; nodes.appendStandalonePoint(2,0);nodes.appendStandalonePoint(8,0);nodes.appendStandalonePoint(2,0);
    check(SketchCurveTools::splitAtNodes({horizontal,nodes},1e-8,parts,error),"point split");
    check(parts[0].size()==3 && parts[1].size()==1,"standalone nodes deduplicated");
    check(SketchCurveTools::splitAtNodes({horizontal,line(10,0,12,3)},1e-8,parts,error),"common endpoint");
    check(parts[0].size()==1 && parts[1].size()==1,"endpoint not split again");
    check(SketchCurveTools::splitAtNodes({horizontal,line(5,1e-3,5,5)},1e-8,parts,error),"near miss");
    check(parts[0].size()==1,"near miss not a crossing");
    SketchDocument circle;circle.setCircle(0,0,3);circle.setConstruction(true);
    check(SketchCurveTools::splitAtNodes({circle,line(-5,0,5,0)},1e-8,parts,error),"circle intersections");
    double total=0;
    for(const auto& d:parts[0]) { total+=length(d);check(d.isConstruction(),"construction retained");SketchDocument saved;saved.fromJson(d.toJson());check(std::abs(length(d)-length(saved))<1e-8,"split serialization"); }
    check(parts[0].size()>=2 && std::abs(total-6*3.141592653589793)<1e-7,"exact circle arcs");
    check(parts[1].size()==3,"circle gives two real crossings");
    SketchDocument polyline;polyline.setProfile({{0,0},{2,1},{3,4}},false,true);
    check(SketchCurveTools::splitAtNodes({polyline},1e-8,parts,error) && parts[0].size()==2,"polyline breaks at existing node");
    check(SketchCurveTools::splitAtNodes({horizontal,line(3,0,7,0)},1e-8,parts,error),"overlap boundaries");
    check(parts[0].size()==3,"overlap endpoints split without deleting ownership");
    std::vector<SketchDocument> dense;
    for(int i=0;i<30;++i) { double a=i*3.141592653589793/30;dense.push_back(line(-10*std::cos(a),-10*std::sin(a),10*std::cos(a),10*std::sin(a))); }
    const auto snap=SketchCurveTools::snap(dense,{},Point2D{.03,.02},.1);
    check(snap.hit && snap.kind==4 && std::hypot(snap.point.u,snap.point.v)<1e-7,"dense intersection snap");
    std::cout<<"Geometry: cross, T, points, endpoints, tolerance, exact arcs, overlap and dense snapping passed\n";
}

static vtkSmartPointer<vtkUnstructuredGrid> grid()
{
    auto g=vtkSmartPointer<vtkUnstructuredGrid>::New();auto pts=vtkSmartPointer<vtkPoints>::New();
    pts->InsertNextPoint(0,0,0);pts->InsertNextPoint(1,0,0);pts->InsertNextPoint(0,1,0);pts->InsertNextPoint(0,0,1);
    g->SetPoints(pts);vtkIdType ids[4]={0,1,2,3};g->InsertNextCell(VTK_TETRA,4,ids);
    auto values=vtkSmartPointer<vtkDoubleArray>::New();values->SetName("S");
    for(double v:{-1.,1.,1.,1.})values->InsertNextValue(v);g->GetPointData()->AddArray(values);
    auto cell=vtkSmartPointer<vtkDoubleArray>::New();cell->SetName("C");cell->InsertNextValue(2);g->GetCellData()->AddArray(cell);
    return g;
}
static void postChecks()
{
    check(!PostSceneStyle{}.showSurfaceOutline,"surface outline defaults off");
    loadPostDisplaySettings(QJsonObject());
    check(!postSceneStyle().showSurfaceOutline,"legacy missing outline setting defaults off");
    loadPostDisplaySettings(QJsonObject{{"showSurfaceOutline",true}});
    check(!postSceneStyle().showSurfaceOutline,"legacy explicit outline preference remains disabled");
    loadPostDisplaySettings(QJsonObject());
    check(!postSceneStyle().computePointNormals && postSceneStyle().splitSharpEdges
          && postSceneStyle().normalsFeatureAngle==30,"ParaView surface normal defaults");
    postSceneStyle().computePointNormals=true;postSceneStyle().splitSharpEdges=false;postSceneStyle().normalsFeatureAngle=45;
    const auto normalState=savePostDisplaySettings();loadPostDisplaySettings(QJsonObject());loadPostDisplaySettings(normalState);
    check(!postSceneStyle().computePointNormals && postSceneStyle().splitSharpEdges
          && postSceneStyle().normalsFeatureAngle==30,"surface normal settings remain at application defaults");
    loadPostDisplaySettings(QJsonObject());
    const int mode=vtkMapper::GetResolveCoincidentTopology();
    const int faces=vtkMapper::GetResolveCoincidentTopologyPolygonOffsetFaces();
    double factor,units;vtkMapper::GetResolveCoincidentTopologyPolygonOffsetParameters(factor,units);
    double lineFactor,lineUnits,pointUnits;
    vtkMapper::GetResolveCoincidentTopologyLineOffsetParameters(lineFactor,lineUnits);
    vtkMapper::GetResolveCoincidentTopologyPointOffsetParameter(pointUnits);
    setPostCoincidentTopologyActive(true);
    check(vtkMapper::GetResolveCoincidentTopologyPolygonOffsetFaces()==1,"ParaView offset mode");
    double activeFactor,activeUnits;vtkMapper::GetResolveCoincidentTopologyPolygonOffsetParameters(activeFactor,activeUnits);
    check(activeFactor==0 && activeUnits==0,"cloud surface retains original depth");
    vtkMapper::GetResolveCoincidentTopologyLineOffsetParameters(activeFactor,activeUnits);
    check(activeFactor==0 && activeUnits==-4,"ParaView line offset defaults");
    setPostCoincidentTopologyActive(false);
    double restoredFactor,restoredUnits;vtkMapper::GetResolveCoincidentTopologyPolygonOffsetParameters(restoredFactor,restoredUnits);
    check(vtkMapper::GetResolveCoincidentTopology()==mode && vtkMapper::GetResolveCoincidentTopologyPolygonOffsetFaces()==faces
          && restoredFactor==factor && restoredUnits==units,"post exit restores native global offsets");
    vtkMapper::GetResolveCoincidentTopologyLineOffsetParameters(restoredFactor,restoredUnits);
    check(restoredFactor==lineFactor && restoredUnits==lineUnits,"post exit restores line offsets");
    vtkMapper::GetResolveCoincidentTopologyPointOffsetParameter(restoredUnits);
    check(restoredUnits==pointUnits,"post exit restores point offset");
    const QJsonObject legacyDisplay{{"showSurfaceOutline",true},{"surfaceInterpolation",0},
        {"interpolateScalarsBeforeMapping",false},{"computePointNormals",true},{"splitSharpEdges",false},
        {"normalsFeatureAngle",170.0},{"representationMode",1},{"gridLineStrength",48}};
    loadPostDisplaySettings(legacyDisplay);
    check(!postSceneStyle().showSurfaceOutline && postSceneStyle().surfaceInterpolation==1 &&
          postSceneStyle().interpolateScalarsBeforeMapping && !postSceneStyle().computePointNormals &&
          postSceneStyle().splitSharpEdges && postSceneStyle().normalsFeatureAngle==30,"legacy advanced settings cannot override fixed defaults");
    check(postSceneStyle().representationMode==1,"user representation preserved");
    postSceneStyle().surfaceInterpolation=0;postSceneStyle().computePointNormals=true;
    const auto displayState=savePostDisplaySettings();
    check(!displayState.contains("gridLineStrength"),"obsolete mesh strength not saved");
    check(!displayState["showSurfaceOutline"].toBool() && displayState["surfaceInterpolation"].toInt()==1 &&
          !displayState["computePointNormals"].toBool(),"saved project uses application surface defaults");
    postSceneStyle()=PostSceneStyle{};
    loadPostDisplaySettings(QJsonObject());
    for(int legacyStrength:{0,35,48,100}) {
        loadPostDisplaySettings(QJsonObject{{"representationMode",1},{"gridLineStrength",legacyStrength}});
        vtkNew<vtkActor> edgeActor;auto* edgeProperty=edgeActor->GetProperty();edgeProperty->SetOpacity(.63);
        applyPostActorRepresentation(edgeActor);
        const double* edgeColor=edgeProperty->GetEdgeColor();
        check(edgeProperty->GetEdgeVisibility() && edgeColor[0]==0 && edgeColor[1]==0 && edgeColor[2]==0,
              "legacy strength cannot wash out native black mesh edges");
        check(edgeProperty->GetOpacity()==.63,"mesh display changed surface opacity");
        edgeProperty->SetColor(.24,.48,.72);
        postSceneStyle().representationMode=2;applyPostActorRepresentation(edgeActor);
        postSceneStyle().representationMode=0;applyPostActorRepresentation(edgeActor);
        const double* color=edgeProperty->GetColor();
        check(color[0]==.24 && color[1]==.48 && color[2]==.72 && edgeProperty->GetOpacity()==.63,
              "wireframe/surface switching preserves actor color and opacity");
    }
    postSceneStyle()=PostSceneStyle{};
    const auto g=grid();const auto time=g->GetMTime();
    auto parts=splitPostThreshold(g,"S",true,0);
    check(parts.retained->GetNumberOfCells()>0 && parts.below->GetNumberOfCells()>0,"point threshold clips both sides");
    // Clip outputs share their point storage; inspect points referenced by cells.
    for (const auto& output : {parts.retained, parts.below})
    {
        check(output->GetPointData()->GetArray("S"), "clipped output retains named scalar array");
        for (vtkIdType c=0;c<output->GetNumberOfCells();++c)
        {
            auto ids=vtkSmartPointer<vtkIdList>::New();output->GetCellPoints(c,ids);
            for(vtkIdType j=0;j<ids->GetNumberOfIds();++j)
            {
                const double v=output->GetPointData()->GetArray("S")->GetComponent(ids->GetId(j),0);
                check(output==parts.retained ? v>=-1e-8 : v<=1e-8,"threshold cell vertices on correct side");
            }
        }
    }
    check(g->GetNumberOfCells()==1 && g->GetMTime()==time,"source unchanged");
    parts=splitPostThreshold(g,"C",false,2);check(parts.retained->GetNumberOfCells()==1 && !parts.below->GetNumberOfCells(),"equal retained");
    parts=splitPostThreshold(g,"C",false,3);check(!parts.retained->GetNumberOfCells() && parts.below->GetNumberOfCells()==1,"all below");
    parts=splitPostThreshold(g,"S",true,-2);check(parts.retained->GetNumberOfCells()==1,"all above");
    auto plateau=vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points=vtkSmartPointer<vtkPoints>::New();auto values=vtkSmartPointer<vtkDoubleArray>::New();values->SetName("S");
    for(int i=0;i<2;++i)
    {
        for(vtkIdType j=0;j<4;++j) { double xyz[3];g->GetPoint(j,xyz);points->InsertNextPoint(xyz[0]+3*i,xyz[1],xyz[2]);values->InsertNextValue(i?-1.:0.); }
        vtkIdType ids[4]={4*i,4*i+1,4*i+2,4*i+3};plateau->InsertNextCell(VTK_TETRA,4,ids);
    }
    plateau->SetPoints(points);plateau->GetPointData()->AddArray(values);
    parts=splitPostThreshold(plateau,"S",true,0);
    check(parts.retained->GetNumberOfCells()==1 && parts.below->GetNumberOfCells()==1,"node-result plateau at threshold stays opaque");
    PostDisplayControlWidget controls(nullptr);
    publishPostDisplayContext("S",-1,1,true,true,true,true);
    auto* enable=controls.findChild<QCheckBox*>("postThresholdEnabled");
    auto* threshold=controls.findChild<QDoubleSpinBox*>("postThresholdValue");
    auto* transparency=controls.findChild<QSpinBox*>("postBelowTransparency");
    check(enable && enable->isEnabled(),"threshold UI active for cloud");enable->setChecked(true);
    threshold->setValue(.25);QMetaObject::invokeMethod(threshold,"editingFinished");transparency->setValue(95);
    check(postFieldDisplayStyle("S").thresholdEnabled && postFieldDisplayStyle("S").threshold==.25 && postFieldDisplayStyle("S").belowTransparency==95,"UI commits threshold");
    const auto state=savePostDisplaySettings();postFieldDisplayStyles().clear();loadPostDisplaySettings(state);
    check(postFieldDisplayStyle("S").threshold==.25 && postFieldDisplayStyle("S").belowTransparency==95,"threshold persisted");
    check(!postFieldDisplayStyle("C").thresholdEnabled,"independent field thresholds");
    loadPostDisplaySettings(QJsonObject());check(!postFieldDisplayStyle("S").thresholdEnabled,"old project leaves threshold disabled");
    auto lut=vtkSmartPointer<vtkLookupTable>::New();lut->SetRange(1000.,1000.01);lut->Build();
    auto bar=vtkSmartPointer<vtkScalarBarActor>::New();bar->SetLookupTable(lut);
    postScalarBarStyle().tickMode=1;postScalarBarStyle().tickCount=7;applyStandardPostScalarBar(bar);
    char a[128],b[128];std::snprintf(a,128,bar->GetLabelFormat(),1000.);std::snprintf(b,128,bar->GetLabelFormat(),1000.+.01/6.);
    check(QString(a)!=QString(b),"narrow offset range has distinguishable labels");
    check(PostScientificText::toHtml(ResultDisplayNames::displayName("SXX(MPa)")).contains("<sub>xx</sub>"),"stress names and subscript");
    check(PostScientificText::toHtml("γp_xy").contains("<sup>p</sup><sub>xy</sub>"),"plastic superscript and subscript");
    check(!PostScientificText::hasSubscript("FAILURE_ZONE"),"internal field identifiers not formatted as symbols");
    std::cout<<"Post: threshold geometry, equality, UI, per-field persistence and legend formatting passed\n";
}
class GeometryTestTree : public ProjectManagerService
{
public:
    QTreeWidget* tree;
    explicit GeometryTestTree(QTreeWidget* value):tree(value){}
    void addTreeItem(ProjectNode* n) override { tree->addTopLevelItem(n); }
    void setCurrentItem(ProjectNode* n) override { tree->setCurrentItem(n); }
    ProjectNode* findNodeByUuid(const QString& uuid) override {
        for(QTreeWidgetItemIterator it(tree);*it;++it)
            if(auto* n=dynamic_cast<ProjectNode*>(*it);n && n->getUuid()==uuid)return n;
        return nullptr;
    }
    void setNodeDimmed(const QString&,bool) override {}
};

class ScenePickingPage final : public BasePage
{
public:
    ScenePickingPage(const QString& type,bool picking,PickMode mode=PickMode::Node)
        :BasePage(type),m_picking(picking),m_mode(mode){}
    void beforeLoad() override {
        if(!m_picking)return;
        auto& picker=PickManager::instance();picker.updateStructure();
        picker.activate(getPageType(),getUuid(),m_mode);
        if(m_mode==PickMode::Domain)picker.setAppliedDomains(getPageType(),getUuid(),{{"MATYPE",3,1}});
    }
    void beforeUnload() override {
        if(!m_picking)return;
        auto& picker=PickManager::instance();
        if(m_mode==PickMode::Domain)picker.setAppliedDomains(getPageType(),getUuid(),{});
        picker.deactivate(getPageType(),getUuid(),m_mode);
    }
private:
    bool m_picking;
    PickMode m_mode;
};

static void sceneVisibilityChecks(QApplication& app,QWidget* frame,VisualWidgetService* visual,const QString& output)
{
    auto& registry=ServiceRegistry::instance();
    auto* tree=frame->findChild<QTreeWidget*>("projectTreeWidget");check(tree,"real project tree");
    GeometryTestTree testTree(tree);auto* originalTree=registry.getService<ProjectManagerService>();
    registry.registerService<ProjectManagerService>(&testTree);
    SceneVisibilityMesh mesh;auto* originalMesh=registry.getService<MeshModuleControllerService>();
    registry.registerService<MeshModuleControllerService>(&mesh);
    PublicVariables::instance().set(PublicConstants::kMeshDimension,3,"scene-test");
    QTemporaryDir project;check(project.isValid(),"scene project");
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,project.path(),"scene-test");
    auto points=vtkSmartPointer<vtkPoints>::New();
    auto nodeIds=vtkSmartPointer<vtkIntArray>::New();nodeIds->SetName("node_id");
    for(int part=0;part<2;++part) {
        const double x=part*2.;points->InsertNextPoint(x,0,0);points->InsertNextPoint(x+1,0,0);
        points->InsertNextPoint(x,1,0);points->InsertNextPoint(x,0,1);
        vtkIdType ids[4]={part*4,part*4+1,part*4+2,part*4+3};mesh.solid->InsertNextCell(VTK_TETRA,4,ids);
        for(int i=0;i<4;++i)nodeIds->InsertNextValue(part*4+i+1);
    }
    mesh.solid->SetPoints(points);mesh.solid->GetPointData()->AddArray(nodeIds);
    auto matypes=vtkSmartPointer<vtkIntArray>::New();matypes->SetName("MATYPE");matypes->InsertNextValue(1);matypes->InsertNextValue(2);
    mesh.solid->GetCellData()->AddArray(matypes);
    auto elements=vtkSmartPointer<vtkIntArray>::New();elements->SetName("elem_id");elements->InsertNextValue(1);elements->InsertNextValue(2);
    mesh.solid->GetCellData()->AddArray(elements);
    const auto actorForGrid=[&] {
        auto mapper=vtkSmartPointer<vtkDataSetMapper>::New();mapper->SetInputData(mesh.solid);mapper->ScalarVisibilityOff();
        auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);return actor;
    };
    auto context=actorForGrid();visual->addProp("mesh.imported.surface",context,QStringLiteral("网格"),"context",false,false);
    auto hidden=actorForGrid();visual->addProp("mesh.user-hidden",hidden,QStringLiteral("网格"),"hidden",false,false);
    visual->setVisible("mesh.user-hidden",false,false);
    auto& picker=PickManager::instance();picker.updateStructure();
    auto* post=registry.getService<PostProcessingService>();check(post,"scene post service");post->newProject();
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessNephogram);
    ProjectNode* cloud=nullptr;
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if(auto* node=dynamic_cast<ProjectNode*>(*it);node && node->getPage()
           && node->getPage()->getPageType()==PublicConstants::kPagePostProcessNephogram)cloud=node;
    check(cloud,"scene cloud page");cloud->setData(0,ProjectNode::RoleModule,"PostProcessing");
    const auto addPage=[&](const QString& type,const QString& module,bool picking,PickMode mode=PickMode::Node) {
        auto* page=new ScenePickingPage(type,picking,mode);auto* node=new ProjectNode(type,page->getUuid(),page);
        node->setData(0,ProjectNode::RoleModule,module);tree->addTopLevelItem(node);return node;
    };
    auto* geometry=addPage("SceneGeometry","Geometry",false);
    auto* material=addPage("SceneMaterial","MaterialManager",true,PickMode::Domain);
    auto* boundary=addPage("SceneBoundary","PhysicalField",true);
    auto* camera=visual->renderer()->GetActiveCamera();camera->SetPosition(-3,-4,3);camera->SetFocalPoint(1.5,.5,.5);
    camera->SetViewUp(0,0,1);visual->resetCamera(false);
    const auto capture=[&](const QString& name) {
        visual->render();auto pixels=vtkSmartPointer<vtkWindowToImageFilter>::New();pixels->SetInput(visual->renderWindow());pixels->ReadFrontBufferOff();pixels->Update();
        auto png=vtkSmartPointer<vtkPNGWriter>::New();png->SetInputConnection(pixels->GetOutputPort());png->SetFileName((output+"/"+name+".png").toUtf8());png->Write();
    };
    bool materialVisible=true,boundaryVisible=true,userHidden=true;
    QJsonArray cycles;
    for(int cycle=0;cycle<2;++cycle) {
        tree->setCurrentItem(geometry);app.processEvents();check(!context->GetVisibility(),"geometry masks mesh");
        tree->setCurrentItem(cloud);app.processEvents();check(!context->GetVisibility(),"cloud isolates mesh");
        tree->setCurrentItem(material);app.processEvents();
        auto* focus=visual->getProp("pick.domain.focus");
        const bool materialOk=context->GetVisibility() && context->GetProperty()->GetOpacity()>0
            && context->GetProperty()->GetOpacity()<1 && focus && focus->GetVisibility();
        materialVisible=materialVisible && materialOk;
        capture(QString("material-%1").arg(cycle));
        tree->setCurrentItem(boundary);app.processEvents();
        const bool boundaryOk=context->GetVisibility() && std::abs(context->GetProperty()->GetOpacity()-1)<1e-9;
        boundaryVisible=boundaryVisible && boundaryOk;userHidden=userHidden && !hidden->GetVisibility();
        capture(QString("boundary-%1").arg(cycle));
        cycles.append(QJsonObject{{"materialContextVisible",materialOk},{"boundaryMeshVisible",boundaryOk},{"hiddenChoicePreserved",!hidden->GetVisibility()}});
    }
    tree->setCurrentItem(geometry);picker.resetProjectState();
    registry.registerService<MeshModuleControllerService>(originalMesh);
    registry.registerService<ProjectManagerService>(originalTree);
    QFile report(output+"/scene-visibility.json");check(report.open(QIODevice::WriteOnly),"scene report");report.write(QJsonDocument(cycles).toJson());
    std::cout<<QJsonDocument(cycles).toJson(QJsonDocument::Compact).constData()<<'\n';
    check(materialVisible && boundaryVisible && userHidden,"material transparent context / boundary mesh / hidden object visibility must survive geometry-post transitions");
}

static int layoutOverlaps(QWidget* page)
{
    int overlaps=0;
    for(auto* layout:page->findChildren<QLayout*>()) {
        QList<QWidget*> controls;
        for(int i=0;i<layout->count();++i) {
            auto* widget=layout->itemAt(i)->widget();
            if(!widget || !widget->isVisibleTo(page))continue;
            if(auto* label=qobject_cast<QLabel*>(widget);label && label->text().isEmpty())continue;
            controls.append(widget);
        }
        for(int i=0;i<controls.size();++i)
            for(int j=i+1;j<controls.size();++j) {
                const QRect a(controls[i]->mapTo(page,QPoint()),controls[i]->size());
                const QRect b(controls[j]->mapTo(page,QPoint()),controls[j]->size());
                if(a.intersects(b))++overlaps;
            }
    }
    return overlaps;
}

static void settingsLayoutAudit(QApplication& app,QWidget* frame,const QString& output)
{
    auto& registry=ServiceRegistry::instance();
    auto* tree=frame->findChild<QTreeWidget*>("projectTreeWidget");check(tree,"layout project tree");
    GeometryTestTree testTree(tree);auto* originalTree=registry.getService<ProjectManagerService>();
    registry.registerService<ProjectManagerService>(&testTree);
    SceneVisibilityMesh mesh;auto* originalMesh=registry.getService<MeshModuleControllerService>();
    registry.registerService<MeshModuleControllerService>(&mesh);
    QTemporaryDir project;check(project.isValid(),"layout temporary project");
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,project.path(),"layout-test");
    PublicVariables::instance().set(PublicConstants::kMeshDimension,3,"layout-test");
    for(const QString& name:{"Base_GlobalParameter","Base_MaterialManager","Base_Solver","Base_Gemo"}) {
        auto* loader=new QPluginLoader(QCoreApplication::applicationDirPath()+"/plugins/"+name+".dll");
        check(loader->load(),qPrintable(loader->errorString()));
        auto* plugin=qobject_cast<PluginInterface*>(loader->instance());check(plugin,"layout plugin");plugin->start();
    }
    registry.getService<GlobalParameterService>()->newProject();
    registry.getService<PhysicalFieldService>()->newProject();
    for(const char* action:{PublicConstants::kActionPhysicalDisplacementConstraint,PublicConstants::kActionPhysicalGeoStressPoint,
        PublicConstants::kActionPhysicalGeoStressFitFace,PublicConstants::kActionPhysicalBodyForce,
        PublicConstants::kActionPhysicalConcentratedForce,PublicConstants::kActionPhysicalLinearForce,
        PublicConstants::kActionPhysicalSurfaceForce,PublicConstants::kActionPhysicalTemperatureLoad,
        PublicConstants::kActionPhysicalBeamLoad,PublicConstants::kActionPhysicalSeismicAndAddedMass})
        EventManager::instance().sendEvent(action);
    auto* material=registry.getService<MaterialManagerService>();material->newProject();material->addMaterialFromMenu("dagangshan_isotropic");
    registry.getService<SolverService>()->newProject();
    for(const char* action:{PublicConstants::kActionSlopeStabilityKey,PublicConstants::kActionSeepageKey,PublicConstants::kActionStressStrainKey})
        EventManager::instance().sendEvent(action);
    registry.getService<PreProcessingService>()->newProject();
    EventManager::instance().sendEvent(PublicConstants::kActionGeomTerrainSurface);
    registry.getService<PostProcessingService>()->newProject();
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessNephogram);
    auto* workspace=frame->findChild<QWidget*>("settingWorkspace");check(workspace,"real settings workspace");
    auto* stack=workspace->findChild<QStackedWidget*>("projectSettingStackWidget");check(stack,"real settings stack");
    QMap<QString,ProjectNode*> pages;
    const QStringList classes{"MaterialEntryWidget","SolverSettingWidget","StressBoundaryPage","SeepageBoundaryPage","SlopeStabilityWidget","TerrainSurfacePanel","NephogramSettingWidget",
        "DamAnalysisWidget","SeepageWidget","DisplacementConstraintWidget","GeoStressPointPage","GeoStressFitFacePage",
        "BodyForcePage","PointForcePage","LinearForcePage","FaceLoadPage","TemperatureLoadPage","BeamLoadPage","SeismicAndAddedMassPage"};
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if(auto* node=dynamic_cast<ProjectNode*>(*it);node && node->getPage())
            for(const auto& cls:classes)
                if((node->getPage()->inherits(cls.toUtf8()) || (cls=="TerrainSurfacePanel" && node->getPage()->getPageType()==PublicConstants::kPageGeomTerrainSurface)) &&
                   (!pages.contains(cls) || (pages[cls]->isDisabled() && !node->isDisabled())))pages.insert(cls,node);
    check(pages.size()==classes.size(),qPrintable("Missing layout pages: "+classes.join(',')));
    QJsonArray report;int totalOverlaps=0,totalWideLabels=0;
    for(const auto& cls:classes) {
        // Optional dam boundary pages are disabled in the default homogeneous
        // dam case. Inspect their real controls in this disposable fixture too.
        pages[cls]->setDisabled(false);
        auto* page=pages[cls]->getPage();tree->setCurrentItem(pages[cls]);app.processEvents();
        check(stack->currentWidget()==page->getWidget(),qPrintable("Inactive layout page: "+cls+" current="+stack->currentWidget()->metaObject()->className()+" disabled="+QString::number(pages[cls]->isDisabled())+" hidden="+QString::number(pages[cls]->isHidden())));
        if(cls=="DisplacementConstraintWidget") {
            const QList<QPair<QString,double>> values{{"xMinSpinBox",510261.375},{"xMaxSpinBox",510965.0625},
                {"yMinSpinBox",4195866.0},{"yMaxSpinBox",4196255.5},{"zMinSpinBox",2144.214599609},
                {"zMaxSpinBox",2446.555175781},{"coordToleranceSpinBox",0.0000070369}};
            for(const auto& value:values) {
                auto* spin=page->findChild<QDoubleSpinBox*>(value.first);check(spin,"survey coordinate field");spin->setValue(value.second);
            }
            app.processEvents();
        }
        if(cls=="SeismicAndAddedMassPage") {
            auto* table=page->findChild<QTableWidget*>("tableSeismic");check(table,"seismic numeric table");
            table->setRowCount(1);
            const QStringList values{"0.000000","0.250000","1000.000000","-0.125000"};
            for(int column=0;column<values.size();++column)table->setItem(0,column,new QTableWidgetItem(values[column]));
            auto* delegate=dynamic_cast<QStyledItemDelegate*>(table->itemDelegate());check(delegate,"numeric table delegate");
            check(delegate->displayText(table->item(0,1)->data(Qt::DisplayRole),table->locale())=="0.25","native numeric table has padded decimals");
        }
        const auto state=page->saveJson();const auto originalStyle=page->styleSheet();
        QByteArray materialBefore;
        const auto materialSnapshot=[&] {
            QDir().mkpath(project.path()+QStringLiteral("/工程数据"));check(material->saveProject(),"material snapshot save");
            QFile file(QDir(project.path()).filePath(PublicConstants::kMaterialProjectFileName));
            check(file.open(QIODevice::ReadOnly),"material snapshot read");return file.readAll();
        };
        if(cls=="MaterialEntryWidget") {
            for(const char* name:{"materialInfoGroup","materialAssignmentSection","materialBasicGroup","materialAdvancedGroup"})
                check(page->findChild<QGroupBox*>(name),"material native group");
            materialBefore=materialSnapshot();
        }
        const QList<int> widths{220,260,300,380,520,260,520};
        for(int sample=0;sample<widths.size();++sample) {
            const int requested=widths[sample];const bool largeFont=sample>=5;
            if(sample==5)page->setStyleSheet(originalStyle+"\nQWidget {font-size:14pt;}");
            QWidget* child=workspace;QSplitter* splitter=nullptr;
            while(child && child->parentWidget()) {
                if(auto* candidate=qobject_cast<QSplitter*>(child->parentWidget());candidate && candidate->orientation()==Qt::Horizontal){splitter=candidate;break;}
                child=child->parentWidget();
            }
            check(splitter,"settings horizontal splitter");
            auto sizes=splitter->sizes();const int index=splitter->indexOf(child);check(index>=0,"settings splitter index");
            const int delta=sizes[index]-requested;sizes[index]=requested;
            const int neighbour=index+1<sizes.size()?index+1:0;sizes[neighbour]=std::max(1,sizes[neighbour]+delta);
            splitter->setSizes(sizes);for(int i=0;i<3;++i)app.processEvents();
            check(std::abs(workspace->width()-requested)<=4,"settings dock cannot resize to requested width");
            if(cls=="SolverSettingWidget" && !largeFont && requested>=300) {
                auto* scroll=workspace->findChild<QScrollArea*>("settingsPageScrollArea");check(scroll,"settings scroll area");
                check(scroll->horizontalScrollBar()->maximum()==0,"solver overview still needs a wide dock");
            }
            int clippedValues=0,paddedDecimals=0,decimalFields=0;
            for(auto* spin:page->findChildren<QAbstractSpinBox*>()) {
                if(!spin->isVisibleTo(page))continue;
                auto* text=spin->findChild<QLineEdit*>();if(!text)continue;
                const auto margins=text->textMargins();
                if(text->contentsRect().width()-margins.left()-margins.right()+2<text->fontMetrics().horizontalAdvance(text->text()))++clippedValues;
                if(auto* number=qobject_cast<QDoubleSpinBox*>(spin)) {
                    ++decimalFields;const QString value=number->cleanText();
                    bool numeric=false;number->locale().toDouble(value,&numeric);
                    if(numeric && value.contains(number->locale().decimalPoint()) && value.endsWith(number->locale().zeroDigit()))++paddedDecimals;
                }
            }
            check(clippedValues==0,qPrintable("Clipped numeric value in "+cls));
            check(paddedDecimals==0,qPrintable("Padded numeric values in "+cls));
            const int overlaps=layoutOverlaps(page);totalOverlaps+=overlaps;
            int wideLabels=0;
            if(cls=="DisplacementConstraintWidget")
                for(const char* name:{"labelUx","labelUy","labelUz","coordinateToleranceLabel"}) {
                    auto* label=page->findChild<QLabel*>(name);check(label,"constraint label");
                    if(label->width()>label->sizeHint().width()+2)++wideLabels;
                }
            totalWideLabels+=wideLabels;
            const auto shot=QString("%1-%2%3.png").arg(cls).arg(requested).arg(largeFont?"-large-font":"");
            check(workspace->grab().save(output+"/"+shot),"layout screenshot");
            if(cls=="MaterialEntryWidget" && sample==3) {
                auto* group=page->findChild<QGroupBox*>("materialInfoGroup");auto* combo=group->findChild<QComboBox*>();check(combo,"material model choice");
                combo->showPopup();app.processEvents();
                check(combo->view()->window()->grab().save(output+"/material-model-popup.png"),"material model popup screenshot");
                combo->hidePopup();app.processEvents();
            }
            report.append(QJsonObject{{"page",cls},{"requestedWidth",requested},{"actualWidth",workspace->width()},
                {"largeFont",largeFont},{"stackMinimumWidth",stack->minimumSizeHint().width()},{"clippedNumericValues",clippedValues},
                {"overlaps",overlaps},{"wideLabels",wideLabels},
                {"paddedDecimals",paddedDecimals},{"decimalFields",decimalFields},
                {"pageMinimumWidth",page->minimumSizeHint().width()}});
            if(cls=="SlopeStabilityWidget" && sample==5) {
                auto* mode=page->findChild<QComboBox*>("line19Combo");check(mode,"slope calculation mode");
                const int original=mode->currentIndex();
                for(int index=0;index<mode->count();++index) {
                    mode->setCurrentIndex(index);for(int tick=0;tick<4;++tick)app.processEvents();
                    check(layoutOverlaps(page)==0,"overlap after changing slope analysis mode");
                    check(workspace->grab().save(output+QString("/slope-mode-%1-narrow-large-font.png").arg(index)),"slope mode screenshot");
                }
                mode->setCurrentIndex(original);app.processEvents();
            }
        }
        if(cls=="MaterialEntryWidget") {
            auto* advanced=page->findChild<QGroupBox*>("materialAdvancedGroup");advanced->setChecked(true);
            for(int tick=0;tick<4;++tick)app.processEvents();
            check(layoutOverlaps(page)==0,"material expanded groups overlap");
            check(workspace->grab().save(output+"/material-advanced-expanded.png"),"material advanced screenshot");
            advanced->setChecked(false);app.processEvents();
            check(materialSnapshot()==materialBefore,"material grouping modified saved data");
        }
        page->setStyleSheet(originalStyle);app.processEvents();check(page->saveJson()==state,"layout changes preserve page settings");
    }
    registry.registerService<MeshModuleControllerService>(originalMesh);
    registry.registerService<ProjectManagerService>(originalTree);
    QFile file(output+"/settings-layout.json");check(file.open(QIODevice::WriteOnly),"layout report");file.write(QJsonDocument(report).toJson());
    std::cout<<"Layout pages="<<pages.size()<<" samples="<<report.size()<<'\n';
    check(totalOverlaps==0,"Overlapping controls in settings pages");
    check(totalWideLabels==0,"Constraint labels consume editor space");
}

static void realProjectChecks(QApplication& app, QWidget* frame, const QString& project, const QString& output)
{
    QFile file(QDir(project).filePath(QStringLiteral("工程数据/preprocessing.json")));
    check(file.open(QIODevice::ReadOnly),"read user geometry snapshot");
    const auto bytes=file.readAll();
    const auto saved=QJsonDocument::fromJson(bytes).object();
    const auto state=saved["geom_items"].toArray()[0].toObject()["state"].toObject();
    const auto original=state["committedDocuments"].toArray();
    const double step=state["gridSnap"].toBool() ? std::min(state["gridStep"].toDouble(1),state["gridSnapStep"].toDouble(1)) : state["gridStep"].toDouble(1);
    const double tolerance=std::max(1e-9,step*1e-6);
    std::vector<SketchDocument> documents;
    for(const auto& value:original) { SketchDocument d;d.fromJson(value.toObject());documents.push_back(d); }
    check(documents.size()>1,"actual multi-line project");
    std::vector<std::vector<SketchDocument>> groups;QString error;
    check(SketchCurveTools::splitAtNodes({documents.front()},tolerance,groups,error),"old selected-only scope reproduction");
    check(groups[0].size()==1,"selected line alone does not expose its crossings");
    std::vector<SketchDocument> references(documents.begin()+1,documents.end());
    check(SketchCurveTools::splitAtNodes({documents.front()},tolerance,groups,error,references),"actual project selected-line crossings");
    const int selectedPieces=int(groups[0].size());check(selectedPieces>1,"actual selected line split at other curves");
    double total=0;for(const auto& d:groups[0])total+=length(d);
    check(std::abs(total-length(documents.front()))<1e-7,"actual line length preserved");
    check(SketchCurveTools::splitAtNodes(documents,tolerance,groups,error),"actual full line network");
    int allPieces=0;for(const auto& group:groups)allPieces+=int(group.size());
    double originalLength=0,splitLength=0;
    for(const auto& d:documents)originalLength+=length(d);
    for(const auto& group:groups)for(const auto& d:group)splitLength+=length(d);
    check(std::abs(originalLength-splitLength)<1e-6,"actual full-network length preserved");

    // Exercise the actual plugin button on a disposable copy, including undo/redo.
    QTemporaryDir temporary;check(temporary.isValid(),"disposable project");
    QDir().mkpath(temporary.path()+QStringLiteral("/工程数据"));
    QFile copy(temporary.path()+QStringLiteral("/工程数据/preprocessing.json"));
    check(copy.open(QIODevice::WriteOnly),"temporary project write");copy.write(bytes);copy.close();
    auto* tree=frame->findChild<QTreeWidget*>("projectTreeWidget");check(tree,"actual model tree");
    auto& registry=ServiceRegistry::instance();auto* oldTree=registry.getService<ProjectManagerService>();
    GeometryTestTree testTree(tree);registry.registerService<ProjectManagerService>(&testTree);
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,temporary.path(),"ui-regression");
    auto* loader=new QPluginLoader(QCoreApplication::applicationDirPath()+"/plugins/Base_Gemo.dll");
    check(loader->load(),qPrintable(loader->errorString()));
    auto* plugin=qobject_cast<PluginInterface*>(loader->instance());check(plugin,"actual geometry plugin");plugin->start();
    auto* pre=registry.getService<PreProcessingService>();check(pre && pre->loadProject(),"actual geometry project reload");
    BasePage* page=nullptr;ProjectNode* sketchNode=nullptr;
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if(auto* n=dynamic_cast<ProjectNode*>(*it);n && n->getPage() && n->getPage()->findChild<QPushButton*>("splitAtNodesButton")) { page=n->getPage();sketchNode=n;break; }
    check(page && sketchNode,"actual sketch page");testTree.setCurrentItem(sketchNode);page->applyDeferredLoadJson();app.processEvents();
    auto* table=page->findChild<QTableWidget*>("objectsTable");
    auto* split=page->findChild<QPushButton*>("splitAtNodesButton");
    auto* undo=page->findChild<QPushButton*>("undoButton");auto* redo=page->findChild<QPushButton*>("redoButton");
    check(table && split && undo && redo,"actual controls");
    const auto before=page->saveJson()["committedDocuments"].toArray();
    table->clearSelection();table->selectRow(0);app.processEvents();split->click();app.processEvents();
    const auto after=page->saveJson()["committedDocuments"].toArray();
    check(after.size()==before.size()-1+selectedPieces,"actual selected-line button split");
    for(int i=1;i<before.size();++i)check(after[i-1]==before[i],"unselected geometry unchanged");
    undo->click();check(page->saveJson()["committedDocuments"].toArray()==before,"actual undo restores geometry");
    redo->click();check(page->saveJson()["committedDocuments"].toArray()==after,"actual redo restores split");
    undo->click();table->clearSelection();app.processEvents();split->click();app.processEvents();
    std::cerr<<"Actual all-network objects="<<page->saveJson()["committedDocuments"].toArray().size()<<" expected="<<allPieces<<" tolerance="<<tolerance<<"\n";
    check(page->saveJson()["committedDocuments"].toArray().size()==allPieces,"actual no-selection full-network split");
    const auto fullResult=page->saveJson()["committedDocuments"].toArray();
    split->click();app.processEvents();
    check(page->saveJson()["committedDocuments"].toArray()==fullResult,"repeat split does not create extra fragments");
    QJsonObject report{{"inputObjects",int(documents.size())},{"selectedLinePieces",selectedPieces},{"allOutputObjects",allPieces},{"actualButtonUndoRedo",true}};
    QFile evidence(output+"/actual-project-intersections.json");check(evidence.open(QIODevice::WriteOnly),"write check evidence");evidence.write(QJsonDocument(report).toJson());
    check(frame->grab().save(output+"/actual-project-window.png"),"actual project screenshot");
    registry.registerService<ProjectManagerService>(oldTree);
    std::cout<<"Actual project: "<<documents.size()<<" objects; selected line -> "<<selectedPieces<<" pieces; all -> "<<allPieces<<" objects; actual button and undo/redo passed\n";
}

static void waitPostReady(QApplication& app,QObject* page)
{
    QElapsedTimer timeout;timeout.start();app.processEvents();
    while(page->property("post.preparation.pending").toBool() && timeout.elapsed()<60000) {
        app.processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
    }
    check(!page->property("post.preparation.pending").toBool(),"background preparation completed");
}

static void cloudLightingChecks(QApplication& app, QWidget* frame, VisualWidgetService* visual,
                                PostDisplayControlWidget* controls,const QString& output)
{
    QTemporaryDir project;check(project.isValid(),"cloud temporary project");
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,project.path(),"cloud-test");
    postSceneStyle()=PostSceneStyle{};postContourStyle().enabled=false;postFieldDisplayStyles().clear();
    visual->clearAll(false);
    auto* tree=frame->findChild<QTreeWidget*>("projectTreeWidget");check(tree,"cloud tree");
    auto& registry=ServiceRegistry::instance();auto* previousTree=registry.getService<ProjectManagerService>();
    GeometryTestTree testTree(tree);registry.registerService<ProjectManagerService>(&testTree);
    auto* post=ServiceRegistry::instance().getService<PostProcessingService>();check(post,"post service");post->newProject();
    const int originalLights=visual->renderer()->GetLights()->GetNumberOfItems();
    const bool originalAutomaticLights=visual->renderer()->GetAutomaticLightCreation();
    const int originalSamples=visual->renderWindow()->GetMultiSamples();
    const bool originalFxaa=visual->renderer()->GetUseFXAA();
    auto originalFxaaOptions=vtkSmartPointer<vtkFXAAOptions>(visual->renderer()->GetFXAAOptions());
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessNephogram);
    BasePage* page=nullptr;
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if(auto* n=dynamic_cast<ProjectNode*>(*it);n && n->getPage()
           && n->getPage()->getPageType()==PublicConstants::kPagePostProcessNephogram)page=n->getPage();
    check(page,"actual cloud page");page->beforeLoad();
    auto data=vtkSmartPointer<vtkUnstructuredGrid>::New();auto points=vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();auto scalar=vtkSmartPointer<vtkDoubleArray>::New();scalar->SetName("SXX(MPa)");
    for(int z=0;z<4;++z)for(int y=0;y<3;++y)for(int x=0;x<7;++x) {
        points->InsertNextPoint(510000.+x*10,3440000.+y*10,200.+z*10);
        scalar->InsertNextValue(x/6.0);
    }
    data->SetPoints(points);data->GetPointData()->AddArray(scalar);
    const auto id=[](int x,int y,int z)->vtkIdType{return (z*3+y)*7+x;};
    for(int x=0;x<6;++x)for(int y=0;y<2;++y)for(int z=0;z<1+x/2;++z) {
        vtkIdType ids[8]={id(x,y,z),id(x+1,y,z),id(x+1,y+1,z),id(x,y+1,z),
                         id(x,y,z+1),id(x+1,y,z+1),id(x+1,y+1,z+1),id(x,y+1,z+1)};
        data->InsertNextCell(VTK_HEXAHEDRON,8,ids);
    }
    const auto path=project.filePath("stress.vtu").toUtf8();
    auto writer=vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();writer->SetInputData(data);writer->SetFileName(path);
    check(writer->Write(),"write cloud fixture");
    auto* steps=page->findChild<QComboBox*>("stepComboBox");check(steps,"cloud file selector");
    {QSignalBlocker blocker(steps);steps->clear();steps->addItem("stress",QString::fromUtf8(path));}
    check(QMetaObject::invokeMethod(page,"on_stepComboBox_currentIndexChanged",Q_ARG(int,0)),"load actual cloud");waitPostReady(app,page);
    auto* actor=vtkActor::SafeDownCast(visual->getProp(postActorId(page->getUuid(),"surface")));
    check(actor && actor->GetMapper(),"cloud surface actor");
    auto* property=actor->GetProperty();
    check(property->GetLighting() && property->GetAmbient()==0 && property->GetDiffuse()==1
          && property->GetSpecular()==0 && property->GetInterpolation()==VTK_GOURAUD,"ParaView surface defaults");
    check(visual->renderer()->GetLights()->GetNumberOfItems()==5,"native five-light kit enabled");
    check(visual->renderWindow()->GetMultiSamples()==0 && visual->renderer()->GetUseFXAA(),"ParaView postprocessing antialiasing mode");
    check(std::abs(visual->renderer()->GetFXAAOptions()->GetHardContrastThreshold()-.045)<1e-6,"ParaView FXAA edge threshold");
    check(!visual->getProp(postActorId(page->getUuid(),"surface-outline")),"default cloud skips feature edge pipeline");
    auto source=vtkSmartPointer<vtkDataSet>(vtkDataSet::SafeDownCast(actor->GetMapper()->GetInputDataObject(0,0)));
    const auto time=source->GetMTime();
    auto* interactionMapper=actor->GetMapper();
    for(int i=0;i<3;++i)visual->interactor()->InvokeEvent(vtkCommand::InteractionEvent);
    check(actor->GetMapper()==interactionMapper && interactionMapper->GetInputDataObject(0,0)==source,
          "dragging preserves full mapper and geometry");
    visual->interactor()->InvokeEvent(vtkCommand::EndInteractionEvent);
    check(actor->GetMapper()==interactionMapper && source->GetMTime()==time,"ending drag does not swap geometry");
    auto* camera=visual->renderer()->GetActiveCamera();
    camera->SetPosition(510110.,3439900.,285.);camera->SetFocalPoint(510030.,3440010.,215.);camera->SetViewUp(0,0,1);
    visual->renderer()->ResetCamera();visual->renderer()->ResetCameraClippingRange();
    const auto capture=[&](const QString& name,int scale=1){
        visual->render();app.processEvents();auto image=vtkSmartPointer<vtkWindowToImageFilter>::New();
        image->SetInput(visual->renderWindow());image->SetScale(scale);image->ReadFrontBufferOff();image->Update();
        auto png=vtkSmartPointer<vtkPNGWriter>::New();const auto filename=(output+"/"+name+".png").toUtf8();
        png->SetFileName(filename);png->SetInputConnection(image->GetOutputPort());png->Write();
    };
    property->LightingOff();capture("cloud-before-unlit");property->LightingOn();capture("cloud-paraview");
    // Use the controls owned by the production DLL: its inline display state is
    // distinct from the standalone test executable's copy on Windows.
    QCheckBox* outline=nullptr;
    for(auto* widget:QApplication::allWidgets())
        if(auto* candidate=qobject_cast<QCheckBox*>(widget);candidate && candidate->objectName()=="postSurfaceOutline"
           && (!controls || !controls->isAncestorOf(candidate)))outline=candidate;
    check(outline && !outline->isVisible() && !outline->isEnabled() && !outline->isChecked(),"surface outline is hidden and disabled");
    outline->setChecked(true);
    check(!outline->isChecked() && !visual->getProp(postActorId(page->getUuid(),"surface-outline")),"outline cannot be re-enabled through stale controls");
    capture("cloud-fixed-defaults");
    check(actor->GetMapper()->GetInputDataObject(0,0)==source && source->GetMTime()==time,"native display changes preserve result grid");
    QWidget* productionControls=outline;
    while(productionControls && !productionControls->inherits("PostDisplayControlWidget"))
        productionControls=productionControls->parentWidget();
    check(productionControls,"production display controls");
    check(!productionControls->findChild<QSpinBox*>("postGridLineStrength"),"obsolete mesh intensity control removed");
    auto* shading=productionControls->findChild<QComboBox*>("postSurfaceInterpolation");
    auto* coloring=productionControls->findChild<QCheckBox*>("postScalarInterpolation");
    auto* displayMode=productionControls->findChild<QComboBox*>("displayModeComboBox");
    auto* mapper=vtkDataSetMapper::SafeDownCast(actor->GetMapper());
    check(shading && coloring && displayMode && mapper && coloring->isChecked(),"separate cloud interpolation controls");
    check(!shading->isVisible() && !coloring->isVisible(),"advanced interpolation options are outside the user interface");
    check(property->GetInterpolation()==VTK_GOURAUD && mapper->GetInterpolateScalarsBeforeMapping(),"production cloud retains fixed rendering defaults");
    shading->setCurrentIndex(shading->findData(0));
    check(property->GetInterpolation()==VTK_FLAT && mapper->GetInterpolateScalarsBeforeMapping(),"flat lighting preserves scalar interpolation");
    coloring->setChecked(false);
    check(property->GetInterpolation()==VTK_FLAT && !mapper->GetInterpolateScalarsBeforeMapping(),"scalar interpolation controlled independently");
    shading->setCurrentIndex(shading->findData(1));coloring->setChecked(true);
    displayMode->setCurrentIndex(displayMode->findData(1));
    check(property->GetEdgeVisibility() && property->GetLineWidth()==1.0
          && !visual->getProp(postActorId(page->getUuid(),"surface-outline")),"surface with edges uses native property only");
    const double* edgeColor=property->GetEdgeColor();
    check(edgeColor[0]==0 && edgeColor[1]==0 && edgeColor[2]==0 && property->GetOpacity()==1,
          "production mesh edges are black and leave the surface opaque");
    capture("cloud-native-black-edges");
    auto* edgeWidth=productionControls->findChild<QDoubleSpinBox*>("postMeshEdgeWidth");
    auto* edgeOpacity=productionControls->findChild<QSpinBox*>("postMeshEdgeOpacity");
    check(edgeWidth && edgeOpacity && edgeWidth->isVisibleTo(productionControls) && edgeOpacity->isVisibleTo(productionControls),"edge width and opacity controls available");
    const QString sourceId=postActorId(page->getUuid(),"surface"),edgeId=PostMeshEdges::actorId(sourceId);
    const auto surfaceTime=mapper->GetInput()->GetMTime();
    edgeWidth->setValue(3);edgeOpacity->setValue(40);
    auto* meshEdges=vtkActor::SafeDownCast(visual->getProp(edgeId));
    check(meshEdges && meshEdges->GetProperty()->GetLineWidth()==3 && meshEdges->GetProperty()->GetOpacity()==.4 && property->GetOpacity()==1,
          "native independent edge opacity leaves cloud surface opaque");
    auto* edgeMapper=vtkDataSetMapper::SafeDownCast(meshEdges->GetMapper());
    check(edgeMapper && edgeMapper->GetInputConnection(0,0)==mapper->GetInputConnection(0,0),"production edges share display input");
    const auto edgeMapperTime=edgeMapper->GetMTime();
    edgeOpacity->setValue(50);edgeWidth->setValue(2);
    check(visual->getProp(edgeId)==meshEdges && edgeMapper->GetMTime()==edgeMapperTime && mapper->GetInput()->GetMTime()==surfaceTime,
          "edge appearance changes reuse actors and immutable surface geometry");
    capture("cloud-edge-opacity-50-width-2");
    visual->setGroupVisible("post",false,false);check(!actor->GetVisibility() && !meshEdges->GetVisibility(),"post group hides both layers");
    visual->setGroupVisible("post",true,false);check(actor->GetVisibility() && meshEdges->GetVisibility(),"post group restores both layers");
    visual->setVisible(sourceId,false,false);visual->setGroupVisible("post",false,false);visual->setGroupVisible("post",true,false);
    check(!actor->GetVisibility() && !meshEdges->GetVisibility(),"hidden source remains hidden after group changes");
    visual->setVisible(sourceId,true,false);
    edgeOpacity->setValue(0);check(!visual->getProp(edgeId) && !property->GetEdgeVisibility() && property->GetOpacity()==1,"zero edge opacity hides only edges");
    capture("cloud-edge-opacity-0");
    edgeOpacity->setValue(100);check(!visual->getProp(edgeId) && property->GetEdgeVisibility(),"opaque edges use native single actor");
    capture("cloud-edge-opacity-100-width-2");edgeWidth->setValue(1);
    displayMode->setCurrentIndex(displayMode->findData(2));
    check(property->GetRepresentation()==VTK_WIREFRAME,"native wireframe mode");
    displayMode->setCurrentIndex(displayMode->findData(0));
    check(property->GetRepresentation()==VTK_SURFACE && !property->GetEdgeVisibility() && property->GetSpecular()==0,"surface mode restores no-edge no-highlight cloud");
    auto* computeNormals=productionControls->findChild<QCheckBox*>("postComputePointNormals");
    auto* splitEdges=productionControls->findChild<QCheckBox*>("postSplitSharpEdges");
    auto* featureAngle=productionControls->findChild<QDoubleSpinBox*>("postNormalsFeatureAngle");
    check(computeNormals && splitEdges && featureAngle && !computeNormals->isChecked()
          && splitEdges->isChecked() && featureAngle->value()==30,"ParaView surface normal controls");
    check(!computeNormals->isVisible() && !splitEdges->isVisible() && !featureAngle->isVisible(),"normal options are outside the user interface");
    computeNormals->setChecked(true);waitPostReady(app,page);
    auto* normalSurface=vtkPolyData::SafeDownCast(mapper->GetInput());
    check(normalSurface && normalSurface->GetPointData()->GetNormals(),"missing point normals computed on native external surface");
    check(normalSurface->GetPointData()->GetArray("vtkOriginalPointIds")
          && normalSurface->GetCellData()->GetArray("vtkOriginalCellIds"),"native extraction retains original identifiers");
    const auto normalTime=normalSurface->GetMTime();
    auto* normals=normalSurface->GetPointData()->GetNormals();
    for(vtkIdType p=0;p<normalSurface->GetNumberOfPoints();++p) {
        double n[3];normals->GetTuple(p,n);
        check(std::max({std::abs(n[0]),std::abs(n[1]),std::abs(n[2])})>.999,"thirty-degree splitting preserves ninety-degree corners");
    }
    outline->setChecked(true);check(!visual->getProp(postActorId(page->getUuid(),"surface-outline")),"feature edges stay disabled with normal display input");
    outline->setChecked(false);coloring->setChecked(false);coloring->setChecked(true);
    check(mapper->GetInput()==normalSurface && normalSurface->GetMTime()==normalTime,"color changes reuse native normal output");
    const auto splitPointCount=normalSurface->GetNumberOfPoints();
    splitEdges->setChecked(false);waitPostReady(app,page);
    check(vtkPolyData::SafeDownCast(mapper->GetInput())->GetNumberOfPoints()<splitPointCount,"turning off splitting shares corner normals");
    splitEdges->setChecked(true);waitPostReady(app,page);
    auto* thresholdValue=productionControls->findChild<QDoubleSpinBox*>("postThresholdValue");
    auto* thresholdEnabled=productionControls->findChild<QCheckBox*>("postThresholdEnabled");
    check(thresholdValue && thresholdEnabled,"production threshold controls");
    thresholdValue->setValue(.5);QMetaObject::invokeMethod(thresholdValue,"editingFinished");thresholdEnabled->setChecked(true);waitPostReady(app,page);
    auto* below=vtkActor::SafeDownCast(visual->getProp(postActorId(page->getUuid(),"threshold-below")));
    check(below && below->GetProperty()->GetLighting() && below->GetProperty()->GetAmbient()==0 && below->GetProperty()->GetDiffuse()==1,
          "transparent threshold region uses same lighting");
    check(visual->renderer()->GetUseDepthPeeling() && visual->renderer()->GetMaximumNumberOfPeels()==4
          && visual->renderer()->GetOcclusionRatio()==0,"ParaView transparent rendering settings");
    check(vtkPolyData::SafeDownCast(below->GetMapper()->GetInputDataObject(0,0))->GetPointData()->GetNormals(),"threshold transparent region computes native normals");
    thresholdEnabled->setChecked(false);computeNormals->setChecked(false);waitPostReady(app,page);
    check(mapper->GetInput()->GetNumberOfCells()==source->GetNumberOfCells()
          && source->GetMTime()==time && !mapper->GetInput()->GetPointData()->GetNormals(),"turning normals off restores full faceted surface without modifying its previous output");

    // A coarse curved surface exposes faceting without generated point normals.
    auto sphere=vtkSmartPointer<vtkSphereSource>::New();sphere->SetCenter(510030,3440010,220);
    sphere->SetOutputPointsPrecision(vtkAlgorithm::DOUBLE_PRECISION);
    sphere->SetRadius(20);sphere->SetThetaResolution(20);sphere->SetPhiResolution(14);sphere->Update();
    auto sphereSurface=vtkSmartPointer<vtkPolyData>::New();sphereSurface->ShallowCopy(sphere->GetOutput());
    sphereSurface->GetPointData()->SetNormals(nullptr);
    auto curveScalars=vtkSmartPointer<vtkDoubleArray>::New();curveScalars->SetName("SXX(MPa)");
    for(vtkIdType i=0;i<sphereSurface->GetNumberOfPoints();++i) {
        double point[3];sphereSurface->GetPoint(i,point);curveScalars->InsertNextValue((point[0]-510010)/40);
    }
    sphereSurface->GetPointData()->AddArray(curveScalars);
    auto append=vtkSmartPointer<vtkAppendFilter>::New();append->AddInputData(sphereSurface);append->Update();
    const auto curvedPath=project.filePath("curved.vtu").toUtf8();writer->SetFileName(curvedPath);writer->SetInputData(append->GetOutput());
    check(writer->Write(),"curved normal fixture");
    {QSignalBlocker blocker(steps);steps->clear();steps->addItem("curved",QString::fromUtf8(curvedPath));}
    check(QMetaObject::invokeMethod(page,"on_stepComboBox_currentIndexChanged",Q_ARG(int,0)),"load curved surface");waitPostReady(app,page);
    steps->addItem("stress",QString::fromUtf8(path));steps->setCurrentIndex(1);waitPostReady(app,page);
    check(mapper->GetInput()->GetNumberOfCells()==source->GetNumberOfCells(),"file selector automatically updates result geometry");
    steps->setCurrentIndex(0);waitPostReady(app,page);
    check(mapper->GetInput()->GetNumberOfCells()==sphereSurface->GetNumberOfCells(),"file selector automatically restores curved result");
    camera->SetPosition(510095,3439920,270);camera->SetFocalPoint(510030,3440010,220);
    visual->renderer()->ResetCamera();visual->renderer()->ResetCameraClippingRange();capture("cloud-curved-faceted");
    auto curvedSource=vtkSmartPointer<vtkDataSet>(mapper->GetInput());const auto curvedTime=curvedSource->GetMTime();
    const auto curvedVolume=PostTecplotUtils::readGrid(QString::fromUtf8(curvedPath));
    computeNormals->setChecked(true);waitPostReady(app,page);capture("cloud-curved-point-normals");
    auto* curvedNormals=mapper->GetInput()->GetPointData()->GetNormals();
    auto* originalPoints=mapper->GetInput()->GetPointData()->GetArray("vtkOriginalPointIds");
    auto* mappedScalars=mapper->GetInput()->GetPointData()->GetArray("SXX(MPa)");
    check(curvedNormals && originalPoints && mappedScalars,"curved surface normals and scalar mapping");
    for(vtkIdType i=0;i<curvedNormals->GetNumberOfTuples();++i) {
        const auto original=static_cast<vtkIdType>(originalPoints->GetComponent(i,0));
        check(mappedScalars->GetComponent(i,0)==curvedVolume->GetPointData()->GetArray("SXX(MPa)")->GetComponent(original,0),"normal splitting preserves scalar values");
    }
    const auto cachedTime=mapper->GetInput()->GetMTime();
    shading->setCurrentIndex(shading->findData(0));shading->setCurrentIndex(shading->findData(1));
    check(mapper->GetInput()->GetMTime()==cachedTime && curvedSource->GetMTime()==curvedTime,"lighting reuses normals without modifying source");
    computeNormals->setChecked(false);waitPostReady(app,page);
    check(mapper->GetInput()==curvedSource && !curvedSource->GetPointData()->GetNormals(),"native normal computation is display-only");
    const QString actualResult=app.arguments().value(3);
    if(!actualResult.isEmpty()) {
        const QFileInfo before(actualResult);
        {QSignalBlocker blocker(steps);steps->clear();steps->addItem("actual",actualResult);}
        QElapsedTimer loadTimer;loadTimer.start();
        qint64 lastTick=0,maxEventGap=0;int ticks=0;QTimer heartbeat;
        QObject::connect(&heartbeat,&QTimer::timeout,[&]{const auto now=loadTimer.elapsed();maxEventGap=std::max(maxEventGap,now-lastTick);lastTick=now;++ticks;});
        heartbeat.start(5);
        check(QMetaObject::invokeMethod(page,"on_stepComboBox_currentIndexChanged",Q_ARG(int,0)),"load actual result read-only");
        const auto loadSubmissionMs=loadTimer.elapsed();waitPostReady(app,page);heartbeat.stop();
        const auto loadReadyMs=loadTimer.elapsed();
        check(ticks>2,"UI heartbeat continues while real result prepares");
        auto* fields=page->findChild<QComboBox*>("scalarComboBox");
        if(fields->count()>1) {
            fields->setCurrentIndex((fields->currentIndex()+1)%fields->count());waitPostReady(app,page);
            check(QString::fromUtf8(mapper->GetArrayName())==ResultDisplayNames::internalName(fields),
                  "field selector automatically updates scalar mapping");
        }
        const int displacement=ResultDisplayNames::findInternalName(fields,"UH(cm)");
        if(displacement>=0)fields->setCurrentIndex(displacement);
        auto actualSource=vtkSmartPointer<vtkDataSet>(mapper->GetInput());const auto actualTime=actualSource->GetMTime();
        visual->renderer()->ResetCamera();visual->renderer()->ResetCameraClippingRange();capture("actual-result-faceted",3);
        QElapsedTimer normalTimer;normalTimer.start();computeNormals->setChecked(true);
        const auto submissionElapsed=normalTimer.elapsed();waitPostReady(app,page);
        const auto elapsed=normalTimer.elapsed();capture("actual-result-point-normals",3);
        auto* actualSurface=vtkPolyData::SafeDownCast(mapper->GetInput());
        check(actualSurface && actualSurface->GetPointData()->GetNormals(),"actual result missing normals generated");
        check(actualSurface->GetPointData()->GetArray("node_id"),"actual node identifiers retained");
        check(actualSource->GetMTime()==actualTime,"actual result grid unchanged by normal computation");
        const QFileInfo after(actualResult);
        check(before.size()==after.size() && before.lastModified()==after.lastModified(),"actual result file unchanged");
        std::cout<<"Actual result: cells="<<actualSource->GetNumberOfCells()<<" surface="<<actualSurface->GetNumberOfCells()
                 <<" normalDisplayUpdateMs="<<elapsed<<'\n';
        auto* fullMapper=actor->GetMapper();const auto fullTime=actualSurface->GetMTime();
        const QString selectedArray=QString::fromUtf8(fullMapper->GetArrayName());
        const int selectedAssociation=fullMapper->GetScalarMode();
        QElapsedTimer interactionTimer;interactionTimer.start();
        for(int i=0;i<20;++i) {
            visual->interactor()->InvokeEvent(vtkCommand::InteractionEvent);
            check(actor->GetMapper()==fullMapper && mapper->GetInput()==actualSurface,
                  "every interaction frame uses the same complete surface and mapper");
            check(QString::fromUtf8(fullMapper->GetArrayName())==selectedArray
                  && fullMapper->GetScalarMode()==selectedAssociation,"interaction preserves selected field and association");
            camera->Azimuth(1);visual->renderer()->ResetCameraClippingRange();visual->render();
            vtkOpenGLRenderWindow::SafeDownCast(visual->renderWindow())->WaitForCompletion();
        }
        const double fullFrameMs=interactionTimer.nsecsElapsed()/20.0/1e6;
        visual->interactor()->InvokeEvent(vtkCommand::EndInteractionEvent);
        check(actor->GetMapper()==fullMapper && mapper->GetInput()==actualSurface && actualSurface->GetMTime()==fullTime,
              "end interaction keeps identical full-resolution geometry");
        displayMode->setCurrentIndex(displayMode->findData(1));
        visual->interactor()->InvokeEvent(vtkCommand::InteractionEvent);
        check(actor->GetMapper()==fullMapper && mapper->GetInput()==actualSurface,"explicit mesh display retains exact grid");
        displayMode->setCurrentIndex(displayMode->findData(0));
        QJsonObject timings{{"loadSubmissionMs",loadSubmissionMs},{"loadReadyMs",loadReadyMs},
            {"uiHeartbeatTicks",ticks},{"uiMaxEventGapMs",maxEventGap},{"normalSubmissionMs",submissionElapsed},
            {"normalReadyMs",elapsed},{"fullSurfaceCells",actualSurface->GetNumberOfCells()},
            {"fullInteractionFrameMs",fullFrameMs},{"interactionUsesFullGeometry",true}};
        QFile performanceFile(output+"/performance.json");check(performanceFile.open(QIODevice::WriteOnly),"performance evidence");
        performanceFile.write(QJsonDocument(timings).toJson());performanceFile.close();
        std::cout<<QJsonDocument(timings).toJson(QJsonDocument::Compact).constData()<<'\n';
        if(app.arguments().value(4)==QLatin1String("--audit-surface")) {
            const auto volumePath=(output+"/audit-input.vtu").toUtf8();
            writer->SetFileName(volumePath);writer->SetInputData(PostTecplotUtils::readGrid(actualResult));
            check(writer->Write(),"export identical audit input");
            auto surfaceWriter=vtkSmartPointer<vtkXMLPolyDataWriter>::New();
            const auto surfacePath=(output+"/audit-current-surface.vtp").toUtf8();
            surfaceWriter->SetFileName(surfacePath);surfaceWriter->SetInputData(actualSurface);
            check(surfaceWriter->Write(),"export current audit surface");
            // Compare appearance in neutral gray, independently of color-map choice.
            const auto vectorJson=[](const double* p){return QJsonArray{p[0],p[1],p[2]};};
            auto* renderer=visual->renderer();auto* window=visual->renderWindow();
            QJsonObject renderState{{"position",vectorJson(camera->GetPosition())},
                {"focalPoint",vectorJson(camera->GetFocalPoint())},{"viewUp",vectorJson(camera->GetViewUp())},
                {"clippingRange",QJsonArray{camera->GetClippingRange()[0],camera->GetClippingRange()[1]}},
                {"parallelProjection",camera->GetParallelProjection()!=0},{"parallelScale",camera->GetParallelScale()},
                {"viewAngle",camera->GetViewAngle()},{"viewSize",QJsonArray{window->GetSize()[0],window->GetSize()[1]}},
                {"multiSamples",window->GetMultiSamples()},{"fxaa",renderer->GetUseFXAA()},
                {"ambient",property->GetAmbient()},{"diffuse",property->GetDiffuse()},{"specular",property->GetSpecular()}};
            QFile renderFile(output+"/audit-render-state.json");check(renderFile.open(QIODevice::WriteOnly),"render state export");
            renderFile.write(QJsonDocument(renderState).toJson());renderFile.close();
            const bool oldGradient=renderer->GetGradientBackground();double oldBackground[3];renderer->GetBackground(oldBackground);
            renderer->GradientBackgroundOff();renderer->SetBackground(.92,.92,.92);
            visual->showScalarBar(false,false);mapper->ScalarVisibilityOff();property->SetColor(.65,.65,.65);
            mapper->SetInputData(actualSource);capture("appearance-current-faceted",3);
            mapper->SetInputData(actualSurface);capture("appearance-current-smooth",3);
            camera->Azimuth(35);renderer->ResetCameraClippingRange();capture("appearance-current-smooth-rotated",3);
            renderState["rotatedPosition"]=vectorJson(camera->GetPosition());
            renderState["rotatedFocalPoint"]=vectorJson(camera->GetFocalPoint());
            renderState["rotatedViewUp"]=vectorJson(camera->GetViewUp());
            renderState["rotatedClippingRange"]=QJsonArray{camera->GetClippingRange()[0],camera->GetClippingRange()[1]};
            check(renderFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"rotated render state export");
            renderFile.write(QJsonDocument(renderState).toJson());renderFile.close();
            property->EdgeVisibilityOn();property->SetEdgeColor(0,0,0);property->SetLineWidth(1);
            capture("appearance-current-edges",3);property->EdgeVisibilityOff();
            visual->setOpacity(postActorId(page->getUuid(),"surface"),.5,false);
            capture("appearance-current-transparent",3);
            renderState["transparentDepthPeeling"]=renderer->GetLastRenderingUsedDepthPeeling()!=0;
            check(renderFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"transparent render state export");
            renderFile.write(QJsonDocument(renderState).toJson());renderFile.close();
            visual->setOpacity(postActorId(page->getUuid(),"surface"),1.0,false);
            renderer->SetGradientBackground(oldGradient);renderer->SetBackground(oldBackground);
        }
    }
    page->beforeUnload();
    check(visual->renderer()->GetLights()->GetNumberOfItems()==originalLights
          && (visual->renderer()->GetAutomaticLightCreation()!=0)==originalAutomaticLights,"post exit restores original lights");
    check(visual->renderWindow()->GetMultiSamples()==originalSamples && visual->renderer()->GetUseFXAA()==originalFxaa
          && visual->renderer()->GetFXAAOptions()==originalFxaaOptions,"post exit restores original antialiasing");
    registry.registerService<ProjectManagerService>(previousTree);
    std::cout<<"Cloud: ParaView surface defaults and light kit, independent interpolation controls, native representations and optional edges, survey-coordinate rotation and unchanged data passed\n";
}

static void integrationChecks(QApplication& app, const QString& output, const QString& project)
{
    check(StyleManager::applyTheme(app,"blue"),"deep blue resources");
    app.setWindowIcon(QIcon(":/resources/icon.ico"));
    auto& registry=ServiceRegistry::instance();
    // Actual plugins and the same service wiring as the application launcher.
    for (const QString& name : {"Base_UiFrame", "Base_Visualization", "Base_ProjectManager", "Base_Output", "Base_PhysicalField", "Base_PostProcessing"})
    {
        auto* loader=new QPluginLoader(QCoreApplication::applicationDirPath()+"/plugins/"+name+".dll");
        check(loader->load(),qPrintable(loader->errorString()));
        auto* plugin=qobject_cast<PluginInterface*>(loader->instance());check(plugin,"plugin interface");plugin->start();
    }
    auto* frameService=registry.getService<UiFrameService>();
    auto* frame=dynamic_cast<QWidget*>(frameService);
    auto* visual=registry.getService<VisualWidgetService>();
    check(frame && visual,"actual main frame and visualization");
    if(project==QLatin1String("--layout-audit")) {
        frame->resize(1500,950);frame->show();app.processEvents();QDir().mkpath(output);
        settingsLayoutAudit(app,frame,output);frame->hide();return;
    }
    if(project==QLatin1String("--scene-visibility")) {
        frame->resize(1400,900);frame->show();app.processEvents();QDir().mkpath(output);
        sceneVisibilityChecks(app,frame,visual,output);frame->hide();return;
    }
    if(project==QLatin1String("--cloud")) {
        frame->resize(1400,900);frame->show();app.processEvents();QDir().mkpath(output);
        cloudLightingChecks(app,frame,visual,nullptr,output);frame->hide();return;
    }
    auto* badge=frame->findChild<QFrame*>("powerChinaBrandingBadge");
    check(badge,"top branding");
    auto* ribbon=frame->findChild<QWidget*>("geoCaeRibbonBar");
    auto* toolbar=frame->findChild<QWidget*>("visualizationViewToolbar");
    auto* viewport=badge->parentWidget();
    check(ribbon && toolbar && viewport && viewport->inherits("QVTKOpenGLNativeWidget") &&
          !toolbar->isAncestorOf(badge) && !ribbon->isAncestorOf(badge),"branding is inside VTK canvas, outside all toolbars");
    check(!frame->windowIcon().isNull(),"native window icon");
    bool modulesOrdered=false;
    for(auto* tabs:ribbon->findChildren<QTabBar*>()) {
        QStringList names;for(int i=0;i<tabs->count();++i)names.append(tabs->tabText(i));
        const int seepage=names.indexOf(QStringLiteral("渗流"));
        // Modeling tabs precede the independent analysis modules in the current ribbon.
        if(names.indexOf(QStringLiteral("几何"))==0 && seepage>0 &&
           names.indexOf(QStringLiteral("土石坝"))==seepage+1 &&
           names.indexOf(QStringLiteral("洞室"))==seepage+2 &&
           names.indexOf(QStringLiteral("边坡"))==seepage+3)modulesOrdered=true;
    }
    check(modulesOrdered,"modeling tabs followed by independent analysis modules");
    auto* controls=new PostDisplayControlWidget(visual);
    frameService->addWidget(controls,QStringLiteral("结果显示"));
    auto source=grid();const auto pieces=splitPostThreshold(source,"S",true,.25);
    auto lut=vtkSmartPointer<vtkLookupTable>::New();applyStandardPostLookupTable(lut,-1,1);
    for(const auto& g : {pieces.retained,pieces.below})
    {
        auto mapper=vtkSmartPointer<vtkDataSetMapper>::New();mapper->SetInputData(g);
        mapper->SetScalarModeToUsePointFieldData();mapper->SelectColorArray("S");mapper->SetLookupTable(lut);mapper->SetScalarRange(-1,1);
        auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetOpacity(g==pieces.below ? .2 : 1.0);
        check(visual->addProp(g==pieces.below?"post.test.threshold-below":"post.test.surface",actor,"post","阈值云图",false,false),"actual display registry");
    }
    visual->setLookupTable(lut,false,false);visual->setScalarBarTitle(QStringLiteral("σ_xx（MPa）"),false);
    publishPostDisplayContext("S",-1,1,true,true,true,true);
    postFieldDisplayStyle("S").thresholdEnabled=true;postFieldDisplayStyle("S").threshold=.25;controls->syncFromState();
    frame->resize(1600,950);frame->show();
    for(int i=0;i<5;++i)app.processEvents();
    visual->renderer()->GetActiveCamera()->SetPosition(-2,-3,2);
    visual->renderer()->GetActiveCamera()->SetFocalPoint(.25,.25,.25);
    visual->resetCamera(false);visual->render();
    check(!visual->renderer()->GetRenderWindow()->GetOffScreenRendering(),"native on-screen viewport");
    QDir().mkpath(output);check(frame->grab().save(output+"/deep-blue-window.png"),"main window screenshot");
    check(viewport->rect().contains(badge->geometry()) && badge->y()==12 &&
          viewport->width()-badge->geometry().right()-1==12,"branding at VTK upper right");
    frame->resize(1100,750);app.processEvents();
    check(frame->grab().save(output+"/compact-window.png"),"compact screenshot");
    if(!project.isEmpty()){frame->resize(1600,950);app.processEvents();realProjectChecks(app,frame,project,output);}
    std::cout<<"Integration: blue surfaces, native icon, canvas branding and modeling/analysis tab order passed\n";
    frame->hide();
}
int main(int argc,char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    QApplication app(argc,argv);
    try { geometryChecks();postChecks();if(argc>1) integrationChecks(app,QString::fromLocal8Bit(argv[1]),argc>2?QString::fromLocal8Bit(argv[2]):QString());return 0; }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
