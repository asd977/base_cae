#include <QtWidgets>
#include <PluginInterface.h>
#include <ServiceRegistry.h>
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <ProjectNode.h>
#include <EventManager.h>
#include <ContextMenuHub.h>
#include <services/ProjectManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkCellData.h>
#include <vtkCamera.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridWriter.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <iostream>
#include <set>
#include <stdexcept>

static void check(bool ok,const char* message)
{if(!ok)throw std::runtime_error(message);}

class TestUiFrame : public UiFrameService
{
public:
    QWidget* display=nullptr;
    QWidget* visualization=nullptr;
    QTreeWidget* tree=nullptr;
    QAction* addWidget(QWidget* widget,QString) override
    {if(widget->findChild<QComboBox*>("comboBoxColorMode"))display=widget;if(widget->findChild<QFrame*>("powerChinaBrandingBadge"))visualization=widget;if(auto* found=widget->findChild<QTreeWidget*>("projectTreeWidget"))tree=found;widget->setProperty("uiFrameDockWidget",true);return new QAction(widget);}
    QAction* addWidgetForTabwidget(QWidget* w,QString s) override{return addWidget(w,s);}
    void setWidgetVisible(QWidget*,bool) override {}
    void showWidgetInLogArea(QWidget*,QString) override {}
};
class TestProjectTree : public ProjectManagerService
{
public:
    QTreeWidget* tree=nullptr;
    void addTreeItem(ProjectNode* node) override {tree->addTopLevelItem(node);}
    void setCurrentItem(ProjectNode* node) override {tree->setCurrentItem(node);}
    ProjectNode* findNodeByUuid(const QString& uuid) override {
        for(QTreeWidgetItemIterator it(tree);*it;++it)if(auto* node=dynamic_cast<ProjectNode*>(*it);node&&node->getUuid()==uuid)return node;
        return nullptr;
    }
    void setNodeDimmed(const QString&,bool) override {}
};
class TestVisual : public VisualWidgetService
{
public:
    QHash<QString,vtkSmartPointer<vtkProp>> props;
    vtkNew<vtkRenderer> scene;
    vtkNew<vtkRenderWindow> window;
    TestVisual(){window->AddRenderer(scene);window->SetSize(950,700);window->SetOffScreenRendering(1);scene->SetBackground(.96,.97,.99);}
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
};
static QTreeWidgetItem* group(QTreeWidget* tree,const QString& category)
{
    for(int i=0;i<tree->topLevelItemCount();++i)
        if(tree->topLevelItem(i)->data(0,Qt::UserRole+1).toString()==category)return tree->topLevelItem(i);
    return nullptr;
}
static vtkPolyDataMapper* mapper(TestVisual& visual)
{
    auto* actor=vtkActor::SafeDownCast(visual.getProp("mesh.imported.surface"));
    check(actor,"mesh surface actor");
    auto* result=vtkPolyDataMapper::SafeDownCast(actor->GetMapper());check(result,"mesh surface mapper");return result;
}
static std::set<int> displayedRegions(TestVisual& visual)
{
    auto* data=mapper(visual)->GetInput();std::set<int> ids;
    auto* parts=data->GetCellData()->GetArray("src_part");
    for(vtkIdType i=0;parts&&i<parts->GetNumberOfTuples();++i)ids.insert(int(parts->GetTuple1(i)));
    return ids;
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);if(argc<2)return 2;
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");app.setFont(QFont("Microsoft YaHei",9));
    TestUiFrame frame;TestProjectTree tree;TestVisual visual;
    ServiceRegistry::instance().registerService<UiFrameService>(&frame);
    ServiceRegistry::instance().registerService<VisualWidgetService>(&visual);
    QPluginLoader projectLoader(QCoreApplication::applicationDirPath()+"/plugins/Base_ProjectManager.dll");
    QPluginLoader loader(QCoreApplication::applicationDirPath()+"/plugins/Base_Mesh.dll");
    try
    {
        check(projectLoader.load(),"load actual project manager DLL");auto* projectPlugin=qobject_cast<PluginInterface*>(projectLoader.instance());check(projectPlugin,"project plugin interface");projectPlugin->start();
        tree.tree=frame.tree;check(tree.tree,"actual project tree lifecycle");ServiceRegistry::instance().registerService<ProjectManagerService>(&tree);
        check(loader.load(),"load actual mesh DLL");auto* plugin=qobject_cast<PluginInterface*>(loader.instance());check(plugin,"mesh plugin interface");plugin->start();
        auto* mesh=ServiceRegistry::instance().getService<MeshModuleControllerService>();check(mesh,"mesh service");mesh->newProject();
        QString error;check(mesh->importGeneratedSmeshMesh(QString::fromLocal8Bit(argv[1]),&error),error.toUtf8().constData());
        EventManager::instance().sendEvent(PublicConstants::kActionPreprocessMeshGlobal);
        ProjectNode* meshSettings=nullptr;
        for(QTreeWidgetItemIterator it(tree.tree);*it;++it)if(auto* node=dynamic_cast<ProjectNode*>(*it);node&&node->data(0,ProjectNode::RoleKind).toString()=="GlobalMeshing")meshSettings=node;
        check(meshSettings&&meshSettings->getPage(),"SMESH parent settings action");
        meshSettings->getPage()->loadJson({{"dimension",3},{"max_size",9.},{"min_size",.2},{"fineness",.75}});
        auto legacySettings=meshSettings->getPage()->saveJson();
        check(!legacySettings["automatic_min_size"].toBool()&&legacySettings["smesh_fineness"].toInt()==3,"legacy minimum and precision migration");
        meshSettings->getPage()->loadJson({{"dimension",3},{"max_size",9.},{"automatic_min_size",true},
            {"manual_min_size",.2},{"smesh_fineness",5},{"growth_rate",.45},{"segments_per_edge",1.25},{"segments_per_radius",3.5}});
        auto* controller=dynamic_cast<QObject*>(mesh);check(controller,"mesh controller QObject");
        check(QMetaObject::invokeMethod(controller,"addSmeshSubmesh",Qt::DirectConnection,Q_ARG(int,3),Q_ARG(QString,meshSettings->getUuid())),"add native Solid SubMesh action");
        auto* solid=meshSettings->getChildren().last();solid->getPage()->loadJson({{"name","Domain A"},{"entity_ids",QJsonArray{1}},{"max_size",2.},{"min_size",0.}});
        check(QMetaObject::invokeMethod(controller,"addSmeshSubmesh",Qt::DirectConnection,Q_ARG(int,2),Q_ARG(QString,solid->getUuid())),"add Face child SubMesh");
        auto* face=solid->getChildren().last();face->getPage()->loadJson({{"name","Surface A"},{"entity_ids",QJsonArray{1}},{"max_size",.8}});
        check(QMetaObject::invokeMethod(controller,"addSmeshSubmesh",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(QString,face->getUuid())),"add Edge child SubMesh");
        face->getChildren().last()->getPage()->loadJson({{"name","Edge A"},{"entity_ids",QJsonArray{1}},{"edge_segments",8},{"edge_bias",1.2}});
        check(QMetaObject::invokeMethod(controller,"addSmeshSweep",Qt::DirectConnection),"add Sweep action");
        auto* sweep=meshSettings->getChildren().last();check(sweep->data(0,ProjectNode::RoleKind).toString()=="SmeshSweep","Sweep task in parent Mesh tree");
        sweep->getPage()->loadJson({{"name","Sweep B"},{"volume_id",2},{"source_face_id",7},{"target_face_id",8},{"layers",7},{"source_size",1.5}});
        const auto settingsUuid=meshSettings->getUuid();
        QMenu nativeMenu;
        ContextMenuHub::instance().buildMenu("project_tree.node",&nativeMenu,{{"module","Mesh"},{"kind","GlobalMeshing"},{"uuid",settingsUuid}});
        QStringList nativeActions;for(auto* action:nativeMenu.actions())nativeActions.push_back(action->text());
        check(nativeActions.contains(QStringLiteral("添加体 SubMesh"))&&nativeActions.contains(QStringLiteral("添加面 SubMesh"))&&nativeActions.contains(QStringLiteral("添加边 SubMesh"))&&nativeActions.contains(QStringLiteral("添加扫掠剖分")),"hierarchical SMESH actions are visible in the actual tree menu");
        check(!nativeActions.join(",").contains(QStringLiteral("边界层")),"no boundary-layer menu entry");
        const int boundaryPoints=mesh->boundaryTagSets(0).size();
        const int boundaryEdges=mesh->boundaryTagSets(1).size();
        check(boundaryPoints>0&&boundaryEdges>0,"SMESH point and edge boundary sets are imported");
        for(const auto& set:mesh->boundaryTagSets())
            check(set.key.category=="SmeshBoundary"&&!mesh->pointIdsOfTag(set.key).isEmpty(),
                  "native SMESH boundaries remain available for load selection");
        check(frame.display,"actual display control panel");
        auto* colors=frame.display->findChild<QComboBox*>("comboBoxColorMode");
        auto* filters=frame.display->findChild<QTreeWidget*>("treeWidgetFilterValues");
        auto* apply=frame.display->findChild<QPushButton*>("pushButtonApplyFilter");
        auto* reset=frame.display->findChild<QPushButton*>("pushButtonResetFilter");check(colors&&filters&&apply&&reset,"display controls");
        auto* regions=group(filters,"REGION");check(regions&&regions->childCount()==5,"five geometry regions are visible in the filter tree");
        check(group(filters,"MATYPE")->childCount()==1&&group(filters,"LAYER")->childCount()==1,"regions do not invent materials or fill layers");
        check(mesh->availableDomains("REGION").size()==5&&mesh->availableDomains("MATYPE").size()==1,"business selection retains five source regions and one material");
        check(colors->currentData().toInt()==5&&displayedRegions(visual).size()==5,"generated mesh defaults to source-region colours");
        check(QString(mapper(visual)->GetArrayName())=="src_part","region colouring uses source domains");
        int regionElements=0;for(int i=0;i<5;++i){check(regions->child(i)->text(0).contains(QStringLiteral("高程")),"readable elevation range for every region");regionElements+=regions->child(i)->text(1).toInt();}
        const auto allElements=mesh->getVtkUnstructuredGrid()->GetNumberOfCells();check(regionElements==allElements,"region counts cover every mesh element");
        colors->setCurrentIndex(colors->findData(2));check(QString(mapper(visual)->GetArrayName())=="MATYPE","material colour mode remains independent");
        colors->setCurrentIndex(colors->findData(5));
        regions->child(0)->setCheckState(0,Qt::Unchecked);apply->click();check(displayedRegions(visual).size()==4,"one contour region can be hidden independently");
        check(mesh->getVtkUnstructuredGrid()->GetNumberOfCells()==allElements,"display filter does not delete computational cells");
        regions->setCheckState(0,Qt::Unchecked);apply->click();check(mapper(visual)->GetInput()->GetNumberOfCells()==0,"unchecking region group hides all regions");
        reset->click();check(displayedRegions(visual).size()==5,"reset restores all five regions");
        std::cout<<"PASS: actual mesh DLL displays five named contour regions, one material; region colours and independent filtering"<<std::endl;

        QTemporaryDir project;check(project.isValid(),"temporary project");
        PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,project.path(),"project_dir");
        QDir().mkpath(QFileInfo(QDir(project.path()).filePath(PublicConstants::kMeshProjectFileName)).absolutePath());
        check(mesh->saveProject(),"save meshed project");
        QFile state(QDir(project.path()).filePath(PublicConstants::kMeshProjectFileName));check(state.open(QIODevice::ReadOnly),"saved mesh state");const auto saved=QJsonDocument::fromJson(state.readAll()).object();state.close();
        // Force the real DAT + mesh_map restoration path, not the VTK cache.
        for(const char* key:{"binary_cache_file","surface_cache_file","line_cache_file"})
            QFile::remove(QDir(project.path()).filePath(saved[key].toString()));
        check(mesh->loadProject(),"reload meshed project from DAT and mesh map");
        auto* restoredSettings=tree.findNodeByUuid(settingsUuid);check(restoredSettings&&restoredSettings->getChildren().size()==2,"SubMesh and Sweep tree restore");
        check(restoredSettings->getPage()->saveJson()["max_size"].toDouble()==9.,"parent Mesh settings persist");
        auto* restoredSolid=restoredSettings->getChildren()[0];
        check(restoredSolid->getPage()->saveJson()["name"].toString()=="Domain A"&&restoredSolid->getChildren().size()==1,"Solid SubMesh settings persist");
        auto* restoredFace=restoredSolid->getChildren()[0];
        check(restoredFace->getChildren()[0]->getPage()->saveJson()["edge_segments"].toInt()==8,"Face/Edge hierarchy and segment settings persist");
        check(restoredSettings->getChildren()[1]->getPage()->saveJson()["layers"].toInt()==7,"Sweep settings persist");
        const auto globalSettings=restoredSettings->getPage()->saveJson();
        check(globalSettings["automatic_min_size"].toBool()&&globalSettings["min_size"].toDouble()==0.&&
              globalSettings["manual_min_size"].toDouble()==.2&&globalSettings["smesh_fineness"].toInt()==5&&
              globalSettings["growth_rate"].toDouble()==.45&&globalSettings["segments_per_edge"].toDouble()==1.25&&
              globalSettings["segments_per_radius"].toDouble()==3.5,"complete parent mesh settings survive project save/reload");
        std::cout<<"PASS: parent Mesh, Solid/Face/Edge SubMesh hierarchy and Sweep settings survive project save/reload"<<std::endl;
        check(mesh->boundaryTagSets(0).size()==boundaryPoints&&mesh->boundaryTagSets(1).size()==boundaryEdges,
              "SMESH boundary selections survive project save/reload");
        regions=group(filters,"REGION");check(regions&&regions->childCount()==5,"project reload restores five region filter entries");
        check(mesh->getVtkUnstructuredGrid()->GetNumberOfCells()==allElements&&regions->child(0)->text(0).contains(QStringLiteral("高程")),"DAT restore retains all 2D cells and contour names");
        check(displayedRegions(visual).size()==5,"DAT project reload restores source-region colours");
        regions->child(0)->setCheckState(0,Qt::Unchecked);apply->click();
        check(mesh->loadProject(),"reload with region filter active");check(displayedRegions(visual).size()==4,"controller preserves region visibility when rebuilding the scene");
        reset->click();
        std::cout<<"PASS: DAT project restore retains region names, colours, filter choices and material independence"<<std::endl;
        frame.display->resize(410,650);frame.display->show();app.processEvents();
        frame.display->grab().save(QCoreApplication::applicationDirPath()+"/example_project/mesh-region-controls.png");
        visual.scene->GetActiveCamera()->SetPosition(500200,2999700,300);visual.scene->GetActiveCamera()->SetFocalPoint(500000,3000000,125);visual.scene->GetActiveCamera()->SetViewUp(0,0,1);visual.scene->ResetCamera();
        visual.window->Render();vtkNew<vtkWindowToImageFilter> image;image->SetInput(visual.window);image->ReadFrontBufferOff();image->Update();
        vtkNew<vtkPNGWriter> writer;writer->SetInputConnection(image->GetOutputPort());writer->SetFileName(QFile::encodeName(QCoreApplication::applicationDirPath()+"/example_project/mesh-contour-regions.png").constData());writer->Write();
        auto legacy=vtkSmartPointer<vtkUnstructuredGrid>::New();legacy->ShallowCopy(mesh->getVtkUnstructuredGrid());legacy->GetCellData()->RemoveArray("src_part");legacy->GetCellData()->RemoveArray("GroupName");
        const QString legacyPath=project.path()+"/legacy.vtk";vtkNew<vtkUnstructuredGridWriter> legacyWriter;legacyWriter->SetInputData(legacy);legacyWriter->SetFileName(QFile::encodeName(legacyPath).constData());check(legacyWriter->Write(),"legacy grid fixture");
        check(mesh->importGeneratedSmeshMesh(legacyPath,&error),"import legacy mesh without source regions");
        check(!group(filters,"REGION")&&colors->currentData().toInt()==2&&mapper(visual)->GetInput()->GetNumberOfCells()==allElements,"legacy mesh retains material colour mode and is fully visible");
        std::cout<<"PASS: meshes without source-region metadata keep existing material display behaviour"<<std::endl;
        mesh->closeProject();plugin->stop();
        QPluginLoader visualizationLoader(QCoreApplication::applicationDirPath()+"/plugins/Base_Visualization.dll");
        check(visualizationLoader.load(),"load actual visualization DLL");auto* visualizationPlugin=qobject_cast<PluginInterface*>(visualizationLoader.instance());check(visualizationPlugin,"visualization plugin interface");visualizationPlugin->start();
        QImage logo(":/branding/powerchina.png");check(!logo.isNull()&&logo.hasAlphaChannel(),"logo is embedded as a transparent Qt resource");
        auto* badge=frame.visualization?frame.visualization->findChild<QFrame*>("powerChinaBrandingBadge"):nullptr;
        auto* logoLabel=badge?badge->findChild<QLabel*>("powerChinaBrandingLogo"):nullptr;
        check(badge&&logoLabel&&!logoLabel->pixmap().isNull()&&badge->styleSheet().contains("background: transparent"),"actual branding uses the resource with no white badge");
        QWidget brandingPreview;brandingPreview.setStyleSheet("background: #d0dfcf;");auto* brandingLayout=new QHBoxLayout(&brandingPreview);brandingLayout->setContentsMargins(15,15,15,15);badge->setParent(&brandingPreview);brandingLayout->addWidget(badge);badge->show();brandingPreview.show();brandingPreview.adjustSize();app.processEvents();
        brandingPreview.grab().save(QCoreApplication::applicationDirPath()+"/example_project/branding-transparent.png");
        std::cout<<"PASS: official logo is bundled in Qt resources; branding overlay has no background frame"<<std::endl;
        visualizationPlugin->stop();projectPlugin->stop();return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<std::endl;return 1;}
}
