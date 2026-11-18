#include <QtWidgets>
#include <StyleManager.h>
#include <PluginInterface.h>
#include <ServiceRegistry.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <ProjectNode.h>
#include <BasePage.h>
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
    auto* badge=frame->findChild<QFrame*>("powerChinaBrandingBadge");
    check(badge,"top branding");
    auto* ribbon=frame->findChild<QWidget*>("geoCaeRibbonBar");
    auto* toolbar=frame->findChild<QWidget*>("visualizationViewToolbar");
    auto* viewport=badge->parentWidget();
    check(ribbon && toolbar && viewport && viewport->inherits("QVTKOpenGLNativeWidget") &&
          !toolbar->isAncestorOf(badge) && !ribbon->isAncestorOf(badge),"branding is inside VTK canvas, outside all toolbars");
    check(!frame->windowIcon().isNull(),"native window icon");
    bool seepageFirst=false;
    for(auto* tabs:ribbon->findChildren<QTabBar*>())
        if(tabs->count()>0 && tabs->tabText(0)==QStringLiteral("渗流")) seepageFirst=true;
    check(seepageFirst,"seepage first independent module tab");
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
    std::cout<<"Integration: moderate blue surfaces, official icon, VTK canvas upper-right branding and first seepage tab passed\n";
    frame->hide();
}
int main(int argc,char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    QApplication app(argc,argv);
    try { geometryChecks();postChecks();if(argc>1) integrationChecks(app,QString::fromLocal8Bit(argv[1]),argc>2?QString::fromLocal8Bit(argv[2]):QString());return 0; }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
