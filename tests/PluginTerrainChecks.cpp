#include <QtWidgets>
#include <PluginInterface.h>
#include <ServiceRegistry.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ProjectNode.h>
#include <services/ProjectManagerService.h>
#include <services/PreProcessingService.h>
#include <services/UiFrameService.h>
#include <SurfaceDomainData.h>
#include <CadDocument.h>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message)
{if(!ok)throw std::runtime_error(message);}

// Load the shipped geometry DLL, using its real event handlers and document.
// No geometry implementation objects are linked into this executable.
class TestUiFrame : public UiFrameService
{
public:
    QTreeWidget* tree=nullptr;
    QAction* addWidget(QWidget* widget,QString) override
    {if(auto* found=widget->findChild<QTreeWidget*>("projectTreeWidget"))tree=found;return new QAction(widget);}
    QAction* addWidgetForTabwidget(QWidget* widget,QString name) override{return addWidget(widget,name);}
    void setWidgetVisible(QWidget*,bool) override {}
    void showWidgetInLogArea(QWidget*,QString) override {}
};
class TestProjectTree : public ProjectManagerService
{
public:
    QTreeWidget* tree=nullptr;
    explicit TestProjectTree(QTreeWidget* value):tree(value){}
    void addTreeItem(ProjectNode* node) override {tree->addTopLevelItem(node);}
    void setCurrentItem(ProjectNode* node) override
    {
        tree->setCurrentItem(node); // Actual ProjectTreeWidget signal/slot lifecycle.
    }
    ProjectNode* findNodeByUuid(const QString& uuid) override
    {
        for(QTreeWidgetItemIterator i(tree);*i;++i)
        {
            auto* node=dynamic_cast<ProjectNode*>(*i);
            if(node&&(node->getUuid()==uuid||node->data(0,ProjectNode::RoleUuid).toString()==uuid))return node;
        }
        return nullptr;
    }
    void setNodeDimmed(const QString& uuid,bool dimmed) override
    {if(auto* node=findNodeByUuid(uuid))node->setData(0,ProjectNode::RoleDimmed,dimmed);}
};

static void checkExport(PreProcessingService* pre,const QString& description,int expectedDomains=5)
{
    QString path,diagnostic;
    if(!pre->prepareMeshingGeometry(2,path,diagnostic))
        throw std::runtime_error((description+": "+diagnostic).toStdString());
    TopoDS_Shape shape;BRep_Builder builder;
    check(BRepTools::Read(shape,QFile::encodeName(path).constData(),builder),"read exported geometry");
    TopTools_IndexedMapOfShape shells,faces;
    TopExp::MapShapes(shape,TopAbs_SHELL,shells);TopExp::MapShapes(shape,TopAbs_FACE,faces);
    check(shells.Extent()==expectedDomains&&faces.Extent()==5,"plugin exports continuous contour surfaces, not preview triangles");
    SurfaceDomainData::Definition domains;QString error;
    check(SurfaceDomainData::read(path,faces.Extent(),domains,error),"read terrain domain ownership");
    check(domains.materialGroups.size()==expectedDomains,"preserved terrain domain count");
    for(int group:domains.materialGroups)check(group==1,"contour domains share default material group");
    const QString output=QCoreApplication::applicationDirPath()+"/example_project/plugin-meshing-input.brep";
    QDir().mkpath(QFileInfo(output).absolutePath());
    QFile source(path);check(source.open(QIODevice::ReadOnly),"open plugin mesh input");
    QSaveFile destination(output);check(destination.open(QIODevice::WriteOnly),"open mesh regression output");
    const auto bytes=source.readAll();check(destination.write(bytes)==bytes.size()&&destination.commit(),"save plugin mesh input");
    check(SurfaceDomainData::write(output,domains,error),"save domain ownership with plugin mesh input");
    std::cout<<"PASS: "<<description.toUtf8().constData()<<std::endl;
}

int main(int argc,char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    TestUiFrame frame;ServiceRegistry::instance().registerService<UiFrameService>(&frame);
    QPluginLoader projectLoader(QCoreApplication::applicationDirPath()+"/plugins/Base_ProjectManager.dll");
    check(projectLoader.load(),"load actual project manager plugin");
    auto* projectPlugin=qobject_cast<PluginInterface*>(projectLoader.instance());check(projectPlugin,"project manager plugin interface");
    projectPlugin->start();check(frame.tree,"actual project tree widget");
    TestProjectTree tree(frame.tree);
    ServiceRegistry::instance().registerService<ProjectManagerService>(&tree);
    QPluginLoader loader(QCoreApplication::applicationDirPath()+"/plugins/Base_Gemo.dll");
    PluginInterface* plugin=nullptr;
    try
    {
        if(!loader.load())throw std::runtime_error(loader.errorString().toStdString());
        plugin=qobject_cast<PluginInterface*>(loader.instance());check(plugin,"load geometry plugin");plugin->start();
        auto* pre=ServiceRegistry::instance().getService<PreProcessingService>();check(pre,"geometry service from DLL");pre->newProject();
        auto* geometryRoot=dynamic_cast<ProjectNode*>(tree.tree->topLevelItem(0));check(geometryRoot,"geometry root");
        tree.setCurrentItem(geometryRoot);app.processEvents();
        check(geometryRoot->getPage()&&geometryRoot->getPage()->getPageType()=="DashboardPage","actual project manager attaches a dashboard to the geometry root");
        EventManager::instance().sendEvent(PublicConstants::kActionGeomTerrainSurface);
        ProjectNode* terrain=nullptr;
        for(QTreeWidgetItemIterator i(tree.tree);*i;++i)
        {auto* node=dynamic_cast<ProjectNode*>(*i);if(node&&node->data(0,ProjectNode::RoleKind).toString()=="TerrainSurface")terrain=node;}
        check(terrain&&terrain->getPage(),"terrain created through real geometry action");
        auto* page=terrain->getPage();
        auto* import=page->findChild<QPushButton*>("importContoursButton");
        auto* build=page->findChild<QPushButton*>("buildTerrainButton");check(import&&build,"terrain panel buttons from DLL");
        const QString input=QCoreApplication::applicationDirPath()+"/example_project/contours.dxf";
        check(QFile::exists(input),"DXF import test fixture");
        QTimer::singleShot(0,[input]{auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(dialog,"actual DXF file chooser");dialog->selectFile(input);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
        import->click();check(page->saveJson()["sources"].toArray().size()==6,"DXF file imported through actual import button");
        build->click();app.processEvents();
        checkExport(pre,"actual DLL terrain build to mesh export");
        check(!(geometryRoot->flags()&Qt::ItemIsUserCheckable)&&!geometryRoot->data(0,Qt::CheckStateRole).isValid(),"geometry root dashboard has no checkbox");
        check(terrain->flags()&Qt::ItemIsUserCheckable,"concrete geometry keeps visibility control");
        tree.setCurrentItem(geometryRoot);app.processEvents();
        checkExport(pre,"return to geometry dashboard does not hide terrain from meshing");
        auto* meshNode=new ProjectNode(QStringLiteral("网格划分"));meshNode->setData(0,ProjectNode::RoleModule,"Mesh");tree.tree->addTopLevelItem(meshNode);
        tree.setCurrentItem(meshNode);app.processEvents();
        checkExport(pre,"switch away from terrain page to mesh export");
        auto* mode=page->findChild<QComboBox*>("terrainDomainModeCombo");check(mode&&mode->currentIndex()==0,"contour partitioning is the default");
        mode->setCurrentIndex(1);build->click();checkExport(pre,"whole-surface mode preserves one domain",1);
        mode->setCurrentIndex(0);build->click();checkExport(pre,"switching back restores five contour regions");
        const auto saved=page->saveJson();
        auto* document=GeomManager::CadDocument::getInstance();
        document->appendGeomShapeObject(new CadBody(page->getUuid(),new TopoDS_Shape(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(1,0,0)).Shape()),"old cached curves"));
        checkExport(pre,"wire-only cache is rebuilt from saved terrain parameters");
        document->removeGeomShapeObject(page->getUuid());
        checkExport(pre,"missing runtime cache is rebuilt from saved terrain parameters");
        page->loadJson({});page->deferLoadJson(saved);
        checkExport(pre,"unvisited deferred terrain state is available for meshing");
        page->applyDeferredLoadJson();
        auto invalid=saved;invalid["generated"]=false;page->loadJson(invalid);
        QString path,diagnostic;check(!pre->prepareMeshingGeometry(2,path,diagnostic)&&diagnostic.contains(QStringLiteral("地形")),"unbuilt terrain gets a relevant diagnostic");
        std::cout<<"PASS: unbuilt terrain is rejected clearly"<<std::endl;
        page->loadJson(saved);build->click();
        QTemporaryDir projectDirectory;check(projectDirectory.isValid(),"temporary project directory");
        check(QDir().mkpath(QFileInfo(QDir(projectDirectory.path()).filePath(PublicConstants::kPreProcessingProjectFileName)).absolutePath()),"create project data directory");
        check(PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,projectDirectory.path(),"terrain-regression"),"set project directory");
        tree.setCurrentItem(geometryRoot);app.processEvents();
        check(pre->saveProject(),"save terrain with geometry dashboard selected");
        QFile projectFile(QDir(projectDirectory.path()).filePath(PublicConstants::kPreProcessingProjectFileName));
        check(projectFile.open(QIODevice::ReadOnly),"read saved terrain project");
        const auto items=QJsonDocument::fromJson(projectFile.readAll()).object()["geom_items"].toArray();
        check(items.size()==1&&!items[0].toObject()["cachedBrep"].toString().isEmpty(),"save contains the terrain geometry, not the root dashboard");
        projectFile.close();
        check(pre->loadProject(),"reload imported terrain project");app.processEvents();
        geometryRoot=nullptr;
        for(QTreeWidgetItemIterator i(tree.tree);*i;++i)
        {auto* node=dynamic_cast<ProjectNode*>(*i);if(node&&node->data(0,ProjectNode::RoleKind).toString()=="GeometryRoot")geometryRoot=node;}
        check(geometryRoot,"reloaded geometry root");tree.setCurrentItem(geometryRoot);app.processEvents();
        check(!(geometryRoot->flags()&Qt::ItemIsUserCheckable)&&!geometryRoot->data(0,Qt::CheckStateRole).isValid(),"reloaded geometry root has no checkbox");
        checkExport(pre,"save and reopen preserves imported terrain mesh domains");
        plugin->stop();plugin=nullptr;
        ServiceRegistry::instance().registerService<ProjectManagerService>(nullptr);
        projectPlugin->stop();ServiceRegistry::instance().registerService<UiFrameService>(nullptr);
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL: "<<error.what()<<std::endl;
        if(plugin)plugin->stop();
        ServiceRegistry::instance().registerService<ProjectManagerService>(nullptr);
        projectPlugin->stop();ServiceRegistry::instance().registerService<UiFrameService>(nullptr);
        return 1;
    }
}
