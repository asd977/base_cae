#pragma once
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <GeometryDisplayManager.h>
#include <GeometryBuildUtils.h>
#include <GeometryCleanup.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <BRepPrimAPI_MakeBox.hxx>

// Exercise the real panel and display manager against a small VTK scene.
class TerrainTestVisual : public VisualWidgetService
{
public:
    QHash<QString,vtkSmartPointer<vtkProp>> props;
    vtkNew<vtkRenderer> scene;
    vtkNew<vtkRenderWindow> window;
    TerrainTestVisual(){window->AddRenderer(scene);window->SetSize(900,650);window->SetOffScreenRendering(1);scene->SetBackground(.96,.97,.99);}
    void render() override {}
    void resetCamera(bool=true) override {scene->ResetCamera();}
    vtkRenderer* renderer() const override {return scene;}
    vtkRenderWindow* renderWindow() const override {return window;}
    vtkRenderWindowInteractor* interactor() const override {return nullptr;}
    bool addProp(const QString& id,vtkSmartPointer<vtkProp> prop,const QString& ={},const QString& ={},bool=false,bool=true) override
    {removeProp(id,false);props[id]=prop;scene->AddViewProp(prop);return true;}
    bool removeProp(const QString& id,bool=true) override
    {if(!props.contains(id))return false;scene->RemoveViewProp(props.take(id));return true;}
    void clearAll(bool=true) override {scene->RemoveAllViewProps();props.clear();}
    vtkProp* getProp(const QString& id) const override {return props.value(id);}
    QList<QString> allIds() const override {return props.keys();}
    bool setVisible(const QString& id,bool on,bool=true) override {auto* p=getProp(id);if(p)p->SetVisibility(on);return p;}
    void setGroupVisible(const QString&,bool,bool=true) override {}
    bool setOpacity(const QString&,double,bool=true) override {return false;}
    bool setTransform(const QString&,vtkTransform*,bool=true) override {return false;}
    void setLookupTable(vtkLookupTable*,bool=false,bool=true) override {}
    vtkLookupTable* lookupTable() const override {return nullptr;}
    vtkScalarBarActor* scalarBarActor() const override {return nullptr;}
    void showScalarBar(bool,bool=true) override {}
    bool isScalarBarVisible() const override {return false;}
    void setScalarBarTitle(const QString&,bool=true) override {}
    void setScalarBarNumberOfLabels(int,bool=true) override {}
    bool applyLookupTableToActor(const QString&,const QString&,double,double,bool,bool=true) override {return false;}
    bool updateActorPolyData(const QString&,vtkPolyData*,bool=true) override {return false;}
    bool showCurveFromTraceFile(const QString&,const QString&,const QStringList&,QString* =nullptr) override {return false;}
    void showVisualizationView() override {}
    void setViewToolbarVisible(bool) override {}
    void setBrandingVisible(bool,bool=true) override {}
    bool isBrandingVisible() const override {return false;}
    void screenshot(const QString& path)
    {
        window->Render();vtkNew<vtkWindowToImageFilter> image;image->SetInput(window);image->ReadFrontBufferOff();image->Update();
        vtkNew<vtkPNGWriter> writer;writer->SetInputConnection(image->GetOutputPort());writer->SetFileName(path.toUtf8().constData());writer->Write();
    }
};

static void terrainDisplayChecks(const QString& output)
{
    TerrainTestVisual visual;
    ServiceRegistry::instance().registerService<VisualWidgetService>(&visual);
    struct ResetService {~ResetService(){ServiceRegistry::instance().registerService<VisualWidgetService>(nullptr);}} reset;
    TerrainSurfacePanel panel;QString diagnostic;
    require(panel.importFile(output+"/contours.dxf",diagnostic),"preview DXF import");
    const QString previewId=panel.getUuid()+"_terrain_sources";
    auto* preview=vtkActor::SafeDownCast(visual.getProp(previewId));
    require(preview&&preview->GetVisibility(),"DXF import immediately displays preview");
    auto* data=vtkPolyDataMapper::SafeDownCast(preview->GetMapper())->GetInput();
    require(data->GetNumberOfLines()==5&&data->GetNumberOfVerts()==1,"preview preserves closed contours and summit point");
    double bounds[6];data->GetBounds(bounds);require(bounds[4]==100&&bounds[5]==150,"preview retains original elevations");
    auto* table=panel.findChild<QTableWidget*>("terrainSourceTable");
    table->item(1,0)->setCheckState(Qt::Unchecked);
    data=vtkPolyDataMapper::SafeDownCast(vtkActor::SafeDownCast(visual.getProp(previewId))->GetMapper())->GetInput();
    require(data->GetNumberOfLines()==4,"unchecking source updates preview");
    table->item(1,0)->setCheckState(Qt::Checked);
    visual.screenshot(output+"/contours-import-preview.png");
    require(panel.generate(diagnostic),"generate from visible preview");
    auto* face=vtkActor::SafeDownCast(visual.getProp(GeometryDisplayManager::facePropId(panel.getUuid())));
    auto* object=GeomManager::CadDocument::getInstance()->getGeomShapeObject(panel.getUuid());
    auto edge=object->getEdgeActor();
    require(face&&face->GetVisibility()&&edge&&!edge->GetVisibility(),"terrain defaults to filled face without triangle edges");
    require(!visual.getProp(GeometryDisplayManager::edgePropId(panel.getUuid())),"module group visibility cannot resurrect internal terrain edges");
    auto* faceMapper=vtkPolyDataMapper::SafeDownCast(face->GetMapper());
    require(faceMapper->GetScalarVisibility()&&faceMapper->GetInput()->GetPointData()->GetArray("TerrainElevation"),"terrain uses elevation colours");
    GeometryDisplayManager::hide(panel.getUuid(),false);GeometryDisplayManager::show(panel.getUuid(),false);
    require(!edge->GetVisibility(),"tree hide/show retains terrain edge preference");
    visual.screenshot(output+"/terrain-surface-preview.png");
    require(!panel.findChild<QCheckBox*>("showTerrainTrianglesCheck"),"no internal triangle display control");
    require(panel.findChild<QDoubleSpinBox*>("terrainSpacingSpin")->toolTip().contains(QStringLiteral("计算网格")),"sampling and mesh size are explained separately");
    QStringList boxes;
    for(int i=0;i<24;++i)
    {
        const QString id="terrain-display-box-"+QString::number(i);boxes<<id;
        GeometryBuildUtils::registerShape(id,id,new TopoDS_Shape(BRepPrimAPI_MakeBox(1,1,1).Shape()));
    }
    require(visual.getProp("__geometry_cad_edge_batch"),"exercise large-scene edge batching");
    auto* batchMapper=vtkPolyDataMapper::SafeDownCast(vtkActor::SafeDownCast(visual.getProp("__geometry_cad_edge_batch"))->GetMapper());
    require(batchMapper->GetInput()->GetNumberOfLines()<500,"hidden terrain triangles are excluded from edge batch");
    for(const auto& id:boxes){GeometryCleanup::removeExistingGeometry(id,false);GeometryDisplayManager::forgetVisibilityState(id);}
    auto* contours=panel.findChild<QCheckBox*>("showTerrainContoursCheck");contours->setChecked(false);
    require(!visual.getProp(previewId),"source visibility toggle removes preview");
    contours->setChecked(true);panel.beforeUnload();require(!visual.getProp(previewId),"leaving terrain page removes temporary preview");
    panel.beforeLoad();require(visual.getProp(previewId),"returning terrain page restores preview");
    TerrainSurfacePanel restored;auto legacy=panel.saveJson();legacy["showTriangles"]=true;restored.loadJson(legacy);
    require(!restored.saveJson().contains("showTriangles"),"legacy triangle display preference is discarded");
    GeometryCleanup::removeExistingGeometry(panel.getUuid(),false);GeometryDisplayManager::forgetVisibilityState(panel.getUuid());
    std::cout<<"PASS: DXF visible preview, source edits, elevation colours, filled surface display, tree visibility, edge batching, page lifecycle"<<std::endl;
}
