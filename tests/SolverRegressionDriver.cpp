#include <QtWidgets>
#include <PluginInterface.h>
#include <ProjectNode.h>
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <EventManager.h>
#include <services/UiFrameService.h>
#include <services/ProjectManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/MaterialManagerService.h>
#include <services/PhysicalFieldService.h>
#include <services/SolverService.h>
#include <services/OutputService.h>
#include <services/PostProcessingService.h>
#include <iostream>
#include <cmath>
#include "RegressionBoundaryExport.h"
#include <services/VisualWidgetService.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkUnstructuredGrid.h>
#include <vtkActor.h>
#include <vtkMapper.h>
static bool solverLaunched = false;
static bool postPrepared = false;
static QtMessageHandler previousDriverHandler = nullptr;
class TestVisual : public VisualWidgetService
{
public:
    QHash<QString,vtkSmartPointer<vtkProp>> props;
    vtkNew<vtkRenderer> scene;
    vtkNew<vtkRenderWindow> window;
    TestVisual(){window->AddRenderer(scene);window->SetSize(950,700);window->SetOffScreenRendering(1);scene->SetBackground(.96,.97,.99);}
    int renderCalls=0;
    void render() override {++renderCalls;}
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


class Frame : public UiFrameService {
public:
 QWidget* logWidget=nullptr;
 QWidget* displayWidget=nullptr;
 QAction* addWidget(QWidget* w,QString title) override {if(title==QStringLiteral("日志"))logWidget=w;if(w->findChild<QTreeWidget*>("treeWidgetFilterValues"))displayWidget=w;return new QAction(w);}
 QAction* addWidgetForTabwidget(QWidget* w,QString s) override{return addWidget(w,s);}
 void setWidgetVisible(QWidget*,bool) override{}
 void showWidgetInLogArea(QWidget*,QString) override{}
};
class Tree : public ProjectManagerService {
public:
 QTreeWidget tree;
 void addTreeItem(ProjectNode* n) override{tree.addTopLevelItem(n);}
 void setCurrentItem(ProjectNode* n) override{tree.setCurrentItem(n);}
 ProjectNode* findNodeByUuid(const QString& id) override {
  for(QTreeWidgetItemIterator i(&tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getUuid()==id)return n;
  return nullptr;
 }
 void setNodeDimmed(const QString&,bool) override{}
};
class Output : public OutputService {
public:void outputLog(QString s) override{std::cout<<s.toUtf8().constData()<<std::endl;}
};
int main(int argc,char** argv) {
 std::cerr<<"ENTER_MAIN"<<std::endl;
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);std::cerr<<"QT_READY"<<std::endl;QCoreApplication::setOrganizationName("CAERegression");QCoreApplication::setApplicationName("SolverRegressionDriver");
 const auto args=app.arguments();
 if(args.size()<5){std::cerr<<"project uuid gpu|cpu phase [timeout-seconds]\n";return 2;}
 Frame frame;Tree tree;Output output;TestVisual visual;
 auto& services=ServiceRegistry::instance();services.registerService<VisualWidgetService>(&visual);services.registerService<UiFrameService>(&frame);
 services.registerService<ProjectManagerService>(&tree);services.registerService<OutputService>(&output);
 PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,QDir(args[1]).absolutePath(),"project_dir");
 int dimension=3;
 {QFile snapshot(QDir(args[1]).filePath(QStringLiteral("工程数据/mesh_snapshot.iiin.dat")));
  if(snapshot.open(QIODevice::ReadOnly)){const auto h=QString::fromLatin1(snapshot.readLine()).simplified().split(' ');if(h.size()>1)dimension=h[1].toInt();}}
 PublicVariables::instance().set("mesh_dimension",dimension,"mesh");
 bool dialogFailure=false;QTimer dialogs;
 QObject::connect(&dialogs,&QTimer::timeout,[&]{
  for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()) {
   std::cout<<"DIALOG: "<<box->windowTitle().toUtf8().constData()<<": "<<box->text().toUtf8().constData()<<std::endl;
   if(box->icon()==QMessageBox::Critical||box->icon()==QMessageBox::Warning)dialogFailure=true;
   box->done(QMessageBox::Cancel);
  }
 });dialogs.start(50);
 QList<QPluginLoader*> loaders;
 const bool withPost=args[4].endsWith(QStringLiteral("-with-post"));
 QStringList pluginNames={"Base_Mesh","Base_MaterialManager","Base_PhysicalField","Base_Solver"};if(withPost)pluginNames.append("Base_PostProcessing");
 const bool realOutput=qEnvironmentVariableIntValue("CAE_REGRESSION_REAL_OUTPUT")!=0;
 if(realOutput)pluginNames.append("Base_Output");
 for(const QString name:pluginNames) {
  std::cerr<<"LOAD "<<name.toStdString()<<std::endl;
  auto* loader=new QPluginLoader(QCoreApplication::applicationDirPath()+"/plugins/"+name+".dll");loaders.append(loader);
  if(!loader->load()){std::cerr<<loader->errorString().toStdString();return 3;}
  auto* plugin=qobject_cast<PluginInterface*>(loader->instance());if(!plugin)return 3;plugin->start();std::cerr<<"STARTED "<<name.toStdString()<<std::endl;
 }
 previousDriverHandler=qInstallMessageHandler([](QtMsgType type,const QMessageLogContext& context,const QString& m){if(m.contains("IDEVICE="))solverLaunched=true;if(m.contains(QStringLiteral("后处理结果已生成")))postPrepared=true;std::cout<<m.toUtf8().constData()<<std::endl;if(previousDriverHandler)previousDriverHandler(type,context,m);});
 auto* mesh=services.getService<MeshModuleControllerService>();
 auto* materials=services.getService<MaterialManagerService>();
 auto* physical=services.getService<PhysicalFieldService>();
 auto* solver=services.getService<SolverService>();
 if(args[4]=="check-nastran-components"){
  if(!mesh)return 30;
  if(!QDir().mkpath(QDir(args[1]).filePath(QStringLiteral("工程数据"))))return 30;
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  mesh->newProject();
  QTimer selector;QObject::connect(&selector,&QTimer::timeout,[&]{
   for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QFileDialog*>(widget);dialog&&dialog->isVisible()){
    dialog->selectFile(args[2]);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
   }
  });selector.start(50);
  EventManager::instance().sendEvent(PublicConstants::kActionPreprocessImportGrid);
  QElapsedTimer wait;wait.start();
  while(wait.elapsed()<30000&&!mesh->getVtkUnstructuredGrid())app.processEvents(QEventLoop::AllEvents,100);
  selector.stop();
  auto grid=mesh->getVtkUnstructuredGrid();if(dialogFailure||!grid||grid->GetNumberOfCells()!=52220)return 31;
  const auto verifyGroups=[&]{
   const auto domains=mesh->availableDomains("REGION");if(domains.size()!=3||mesh->materialGroupCategory()!="REGION"||mesh->excavationGroupCategory()!="REGION")return false;
   const int counts[]={39500,10160,2560};for(const auto& key:domains)if(key.id<1||key.id>3||mesh->domainCells(key).size()!=counts[key.id-1]||mesh->domainDisplayText(key)!=QString::number(key.id))return false;
   return true;};
  if(!verifyGroups()||!frame.displayWidget)return 32;
  if(auto* page=dynamic_cast<BasePage*>(frame.displayWidget))page->beforeLoad();
  auto* filters=frame.displayWidget->findChild<QTreeWidget*>("treeWidgetFilterValues");
  auto* apply=frame.displayWidget->findChild<QPushButton*>("pushButtonApplyFilter");
  auto* automatic=frame.displayWidget->findChild<QCheckBox*>("checkBoxAutoApply");
  QTreeWidgetItem* regions=nullptr;for(int i=0;filters&&i<filters->topLevelItemCount();++i)if(filters->topLevelItem(i)->data(0,Qt::UserRole+1).toString()=="REGION")regions=filters->topLevelItem(i);
  if(!regions||regions->childCount()!=3||!apply||!automatic)return 33;
  automatic->setChecked(false);const int selected=regions->child(0)->data(0,Qt::UserRole+2).toInt();
  {QSignalBlocker blocked(filters);for(int i=0;i<3;++i)regions->child(i)->setCheckState(0,i==0?Qt::Checked:Qt::Unchecked);}
  apply->click();
  QElapsedTimer displayWait;displayWait.start();
  while(frame.displayWidget->property("mesh.display.pending").toBool()
      || frame.displayWidget->property("mesh.display.workerRunning").toBool()) {
   if(displayWait.elapsed()>30000)return 34;
   app.processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
  }
  auto* actor=vtkActor::SafeDownCast(visual.getProp("mesh.imported.surface"));
  auto* visible=actor&&actor->GetMapper()?vtkDataSet::SafeDownCast(actor->GetMapper()->GetInputDataObject(0,0)):nullptr;
  auto* parts=visible?visible->GetCellData()->GetArray("src_part"):nullptr;if(!parts||parts->GetNumberOfTuples()==0)return 34;
  for(vtkIdType i=0;i<parts->GetNumberOfTuples();++i)if(int(parts->GetTuple1(i))!=selected)return 34;
  if(!mesh->saveProject()){std::cerr<<"NAS_PROJECT_SAVE_FAILED";return 35;}
  if(!mesh->loadProject()){std::cerr<<"NAS_PROJECT_RELOAD_FAILED";return 35;}
  if(!verifyGroups()){std::cerr<<"NAS_PROJECT_GROUPS_LOST";return 35;}
  std::cout<<"PASS: actual NAS import, three named regions, display filtering and project save/reload"<<std::endl;return 0;
 }
 if(!mesh||!materials||!physical||!solver||!mesh->loadProject()||!materials->loadProject()||!physical->loadProject()||!solver->loadProject())return 4;
 if(withPost){auto* post=services.getService<PostProcessingService>();if(!post)return 4;post->newProject();}
 if(args[4]=="check-quiet-boundary"){
  QJsonArray checks;
  for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage()){
   auto* widget=n->getPage()->findChild<QWidget*>("quietBoundarySelector");if(!widget)continue;
   const auto state=n->getPage()->saveJson();const auto selected=state[n->getPage()->getPageType()=="QuietBoundary"?"selection":"quiet_boundary_selection"].toObject();
   auto* owner=dynamic_cast<ProjectNode*>(n->parent());
   const bool boundaryEnabled=n->getPage()->getPageType()=="QuietBoundary"
       ?owner->getPage()->saveJson()["quiet_boundary_enabled"].toBool(false):state["quiet_boundary_enabled"].toBool(false);
   bool generated=false;if(boundaryEnabled)QMetaObject::invokeMethod(widget,"generate",Qt::DirectConnection,Q_RETURN_ARG(bool,generated));
   int count=0;for(const auto key:{"face_solid","face_line","edge_solid","edge_line"})count+=selected[key].toArray().size();
   n->getPage()->beforeLoad();
   int highlighted=0;
   for(auto prop:visual.props){
    auto* actor=vtkActor::SafeDownCast(prop);auto* ds=actor&&actor->GetMapper()?vtkDataSet::SafeDownCast(actor->GetMapper()->GetInputDataObject(0,0)):nullptr;
    auto* colors=ds&&actor->GetMapper()->GetScalarVisibility()?ds->GetCellData()->GetScalars():nullptr;
    if(colors&&QString::fromLatin1(colors->GetName()?colors->GetName():"")!="pick_face_colors")colors=nullptr;
    if(colors)for(vtkIdType row=0;row<colors->GetNumberOfTuples();++row)
     if(colors->GetComponent(row,0)==64&&colors->GetComponent(row,1)==170&&colors->GetComponent(row,2)==64)++highlighted;
   }
   const bool parentOwnsSwitch=owner&&owner->getPage()->findChild<QCheckBox*>("quietBoundaryCheck");
   const bool childOwnsSwitch=n->getPage()->findChild<QCheckBox*>("quietBoundaryCheck");
   int highlightedEdges=0;
   if(auto* edge=vtkActor::SafeDownCast(visual.props.value("pick.edge"));edge&&edge->GetVisibility())
    if(auto* ds=vtkDataSet::SafeDownCast(edge->GetMapper()->GetInputDataObject(0,0)))highlightedEdges=int(ds->GetNumberOfCells());
   n->getPage()->beforeUnload();
   bool importButton=false;for(auto* button:n->getPage()->findChildren<QPushButton*>())if(button->text().contains(QStringLiteral("导入面集")))importButton=true;
   bool clearedRejected=false,restored=false;
   if(generated&&count>0){
    for(auto* button:widget->findChildren<QPushButton*>())if(button->text().contains(QStringLiteral("清空选"))){button->click();break;}
    bool stale=false;QMetaObject::invokeMethod(widget,"generate",Qt::DirectConnection,Q_RETURN_ARG(bool,stale));clearedRejected=!stale;
    n->getPage()->loadJson(state);QMetaObject::invokeMethod(widget,"generate",Qt::DirectConnection,Q_RETURN_ARG(bool,restored));
    if(!clearedRejected||!restored)return 14;
   }
   checks.append(QJsonObject{{"title",n->text(0)},{"uuid",n->getUuid()},{"parent_title",n->parent()?n->parent()->text(0):QString()},{"node_type",n->getPage()->getPageType()},{"enabled",boundaryEnabled},{"parent_owns_switch",parentOwnsSwitch},{"child_owns_switch",childOwnsSwitch},{"highlighted_surface_cells",highlighted},{"highlighted_edges",highlightedEdges},{"selected_count",count},{"generated",generated},{"empty_selection_rejected",clearedRejected},{"selection_restored",restored},{"import_button_visible",importButton},{"state",state}});
  }
  if(!solver->saveProject())return 14;
  QFile evidence(QDir(args[1]).filePath(QStringLiteral("边界选择验证.json")));if(!evidence.open(QIODevice::WriteOnly))return 14;
  evidence.write(QJsonDocument(checks).toJson(QJsonDocument::Indented));
  if(!solver->loadProject())return 15;
  int restoredNodes=0;for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage()&&n->getPage()->getPageType()=="QuietBoundary")++restoredNodes;
  if(restoredNodes!=checks.size())return 15;
  std::cout<<"QUIET_BOUNDARY_CHECK pages="<<checks.size()<<" reload_nodes="<<restoredNodes<<std::endl;return checks.isEmpty()?14:0;
 }
 if(args[4]=="validate-project"){
  QJsonArray pages;
  for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage())
   pages.append(QJsonObject{{"uuid",n->getUuid()},{"title",n->text(0)},{"type",n->getPage()->getPageType()},{"state",n->getPage()->saveJson()}});
  const auto grid=mesh->getVtkUnstructuredGrid();
  const QJsonObject report{{"loaded",true},{"solver_launched",solverLaunched},{"mesh_nodes",grid?double(grid->GetNumberOfPoints()):0},{"mesh_cells",grid?double(grid->GetNumberOfCells()):0},{"pages",pages}};
  QFile reportFile(QDir(args[1]).filePath(QStringLiteral("前端加载检查.json")));
  if(!reportFile.open(QIODevice::WriteOnly))return 12;
  reportFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
  std::cout<<"PROJECT_LOAD_CHECK nodes="<<(grid?grid->GetNumberOfPoints():0)<<" cells="<<(grid?grid->GetNumberOfCells():0)<<" solver_launched="<<solverLaunched<<std::endl;
  return grid&&grid->GetNumberOfCells()>0&&!solverLaunched?0:12;
 }
 if(args[4]=="roundtrip-mesh-settings"){
  auto states=[&]{QJsonObject result;for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)
   if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage()){
    const auto kind=n->data(0,ProjectNode::RoleKind).toString();
    if(kind=="VolumeControl"||kind=="FaceControl"||kind=="EdgeControl")return QJsonObject{};
    if(kind.startsWith("Smesh")||kind=="GlobalMeshing")result[n->getUuid()]=n->getPage()->saveJson();
    if(kind=="SmeshSweep")for(auto* label:n->getPage()->findChildren<QLabel*>(QString(),Qt::FindDirectChildrenOnly))
     if(label->text()==QStringLiteral("未选择"))return QJsonObject{};
   }return result;};
  const auto before=states();if(before.isEmpty()||!mesh->saveProject()||!mesh->loadProject()||states()!=before)return 19;
  QFile report(QDir(args[1]).filePath(QStringLiteral("网格设置迁移验证.json")));
  if(!report.open(QIODevice::WriteOnly))return 19;report.write(QJsonDocument(before).toJson());
  std::cout<<"MESH_SETTINGS_ROUNDTRIP nodes="<<before.size()<<std::endl;return 0;
 }
 if(args[4]=="check-cave-modal-settings"){
  QJsonArray checks;
  for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage()&&n->getPage()->getPageType()=="CaveSupportComputation"){
   auto* page=n->getPage();const auto original=page->saveJson();
   auto* count=page->findChild<QSpinBox*>("spinModalCount");
   if(!count||page->findChild<QComboBox*>("comboIfMode"))return 16;
   auto verify=[&](int expected){QString section;if(!QMetaObject::invokeMethod(page,"sectionText",Qt::DirectConnection,Q_RETURN_ARG(QString,section)))return false;
    for(const auto& line:section.split('\n'))if(line.contains("IFMODE   IFWAVE"))return line.simplified().split(' ').first().toInt()==expected;
    return false;};
   auto legacy=original;legacy["ifmode"]="1";page->loadJson(legacy);
   if(count->value()!=1||!verify(1))return 16;
   legacy["ifmode"]="7";page->loadJson(legacy);if(count->value()!=7||!verify(7))return 16;
   auto selected=original;selected["ifmode"]="13";page->loadJson(selected);
   page->beforeLoad();
   if(count->value()!=13||count->isHidden()||!verify(13))return 16;
   count->setValue(11);const auto saved=page->saveJson();if(saved["ifmode"].toString()!="11"||!verify(11))return 16;
   count->setValue(0);if(count->isHidden()||!verify(0)||page->saveJson()["ifmode"].toString()!="0")return 16;
   count->setValue(11);if(count->isHidden()||!verify(11))return 16;
   page->loadJson(saved);if(count->value()!=11||!verify(11))return 16;
   checks.append(QJsonObject{{"uuid",n->getUuid()},{"legacy_0_1_preserved",true},{"numeric_count_preserved",true},{"requested_count_exported",true},{"zero_disables",true},{"roundtrip",true}});
   page->loadJson(original);page->beforeUnload();
  }
  QFile f(QDir(args[1]).filePath(QStringLiteral("模态阶数验证.json")));if(checks.isEmpty()||!f.open(QIODevice::WriteOnly))return 16;
  f.write(QJsonDocument(checks).toJson());return solverLaunched?16:0;
 }
 if(args[4]=="check-geostress-modes"){
  QJsonArray checks;
  for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage()&&n->getPage()->getPageType()=="GeoStressRegressionAnalysis"){
   auto* page=n->getPage();const auto original=page->saveJson();auto* combo=page->findChild<QComboBox*>("calcModeCombo");auto* factor=page->findChild<QLineEdit*>("factorEdit");auto* lateral=page->findChild<QLineEdit*>("lambdaEdit");if(!combo||!factor||!lateral)return 19;
   QJsonArray before;for(auto* child:n->getChildren())before.append(child->getPage()->saveJson());
   for(int mode:{1,0,1}){combo->setCurrentIndex(mode);
    if(factor->isEnabled()!=(mode!=0)||lateral->isEnabled()!=(mode==0))return 19;
    for(auto* child:n->getChildren())if(child->getPage()->isEnabled()!=(mode!=0)||child->flags().testFlag(Qt::ItemIsEnabled)!=(mode!=0))return 19;
   }
   QJsonArray after;for(auto* child:n->getChildren())after.append(child->getPage()->saveJson());if(before!=after)return 19;
   page->loadJson(original);checks.append(QJsonObject{{"uuid",n->getUuid()},{"gravity_disables_unused_nodes",true},{"regression_restores_nodes",true},{"parameters_preserved",true}});
  }
  QFile f(QDir(args[1]).filePath(QStringLiteral("地应力模式验证.json")));if(checks.isEmpty()||!f.open(QIODevice::WriteOnly))return 19;f.write(QJsonDocument(checks).toJson());return 0;
 }
 if(args[4]=="check-mesh-display-filter"){
  EventManager::instance().sendEvent(PublicConstants::kActionPreprocessMeshEdit);
  BasePage* page=nullptr;
  for(QTreeWidgetItemIterator i(&tree.tree);*i;++i)if(auto* n=dynamic_cast<ProjectNode*>(*i);n&&n->getPage()&&n->getPage()->getPageType()=="GridEditingWidget"){page=n->getPage();break;}
  if(!page)return 17;page->beforeLoad();
  auto* filters=page->findChild<QTreeWidget*>("treeWidgetFilterValues");auto* apply=page->findChild<QPushButton*>("pushButtonApplyFilter");
  auto* reset=page->findChild<QPushButton*>("pushButtonResetFilter");auto* automatic=page->findChild<QCheckBox*>("checkBoxAutoApply");
  if(!filters||!apply||!reset||!automatic)return 17;automatic->setChecked(false);
  const auto visibleValues=[&](const char* name){QSet<int> values;
   if(auto* a=vtkActor::SafeDownCast(visual.getProp("mesh.imported.surface"));a&&a->GetMapper())
    if(auto* ds=vtkDataSet::SafeDownCast(a->GetMapper()->GetInputDataObject(0,0)))
     if(auto* data=ds->GetCellData()->GetArray(name))for(vtkIdType i=0;i<data->GetNumberOfTuples();++i)values.insert(int(data->GetTuple1(i)));
   return values;};
  const auto waitView=[&] {
   QElapsedTimer elapsed;elapsed.start();
   do {app.processEvents(QEventLoop::AllEvents,10);if(elapsed.elapsed()>30000)return false;QThread::msleep(1);}
   while(page->property("mesh.display.pending").toBool()||page->property("mesh.display.workerRunning").toBool());
   return true;
  };
  auto* sourceGrid=mesh->getVtkUnstructuredGrid().GetPointer();
  const auto sourceTime=sourceGrid->GetMTime();
  auto* surfaceActor=vtkActor::SafeDownCast(visual.getProp("mesh.imported.surface"));
  auto* unchangedView=surfaceActor->GetMapper()->GetInputDataObject(0,0);
  const int unchangedRenders=visual.renderCalls;
  apply->click();if(!waitView()||visual.renderCalls!=unchangedRenders)return 36;
  auto* colors=page->findChild<QComboBox*>("comboBoxColorMode");if(!colors)return 37;
  for(int i=0;i<colors->count();++i) { colors->setCurrentIndex(i);
   if(surfaceActor->GetMapper()->GetInputDataObject(0,0)!=unchangedView)return 37; }
  page->beforeLoad();
  if(surfaceActor->GetMapper()->GetInputDataObject(0,0)!=unchangedView)return 38;
  int timerTicks=0;QTimer responsiveness;
  QObject::connect(&responsiveness,&QTimer::timeout,[&]{++timerTicks;});responsiveness.start(1);
  QJsonArray checks{{QJsonObject{{"same_filter_does_not_render",true},{"color_reuses_topology",true},{"same_grid_reentry_reuses_display",true}}}};
  for(int i=0;i<filters->topLevelItemCount();++i){auto* group=filters->topLevelItem(i);const auto category=group->data(0,Qt::UserRole+1).toString();
   QJsonArray labels;for(int j=0;j<group->childCount();++j)labels.append(group->child(j)->text(0));
   checks.append(QJsonObject{{"category",category},{"values",labels}});
  }
  for(const QString category:{QStringLiteral("MATYPE"),QStringLiteral("LAYER"),QStringLiteral("REGION")}){
   QTreeWidgetItem* group=nullptr;for(int i=0;i<filters->topLevelItemCount();++i)if(filters->topLevelItem(i)->data(0,Qt::UserRole+1).toString()==category)group=filters->topLevelItem(i);
   if(!group||!group->childCount())continue;
   reset->click();if(!waitView())return 39;const int target=group->child(0)->data(0,Qt::UserRole+2).toInt();
   {QSignalBlocker blocked(filters);for(int j=0;j<group->childCount();++j)group->child(j)->setCheckState(0,j==0?Qt::Checked:Qt::Unchecked);}
   auto* previousInput=surfaceActor->GetMapper()->GetInputDataObject(0,0);
   apply->click();
   if(page->property("mesh.display.pending").toBool()
      && surfaceActor->GetMapper()->GetInputDataObject(0,0)!=previousInput)return 40;
   if(!waitView())return 39;
   const auto visible=visibleValues(category=="REGION"?"src_part":category.toLatin1().constData());
   if(visible!=QSet<int>{target}){std::cerr<<"FILTER_FAILED "<<category.toStdString()<<" target="<<target<<" visible=";for(auto v:visible)std::cerr<<v<<",";return 17;}
   checks.append(QJsonObject{{"filtered_category",category},{"only_visible",target},{"passed",true}});reset->click();if(!waitView())return 39;
  }
  const int beforeAuto=visual.renderCalls;
  automatic->setChecked(true);if(!waitView()||visual.renderCalls!=beforeAuto)return 43;
  QTreeWidgetItem* materialsGroup=nullptr;
  for(int i=0;i<filters->topLevelItemCount();++i)
   if(filters->topLevelItem(i)->data(0,Qt::UserRole+1).toString()=="MATYPE")materialsGroup=filters->topLevelItem(i);
  if(materialsGroup&&materialsGroup->childCount()>1) {
   const int beforeCheck=visual.renderCalls;
   materialsGroup->child(0)->setCheckState(0,Qt::Unchecked);
   if(visual.renderCalls!=beforeCheck)return 44;
   app.processEvents(QEventLoop::AllEvents,1);
   materialsGroup->child(1)->setCheckState(0,Qt::Unchecked);
   app.processEvents(QEventLoop::AllEvents,1);
   reset->click();if(!waitView())return 39;
   if(PublicVariables::instance().get("mesh.display.filter.custom").toBool())return 45;
   checks.append(QJsonObject{{"checkbox_callback_does_not_render",true},{"newest_selection_wins",true},{"cancelled_results_discarded",true}});
   // Changing the scene while a view is queued must leave the committed view intact.
   auto* committedInput=surfaceActor->GetMapper()->GetInputDataObject(0,0);
   const int beforeLeave=visual.renderCalls;
   materialsGroup->child(0)->setCheckState(0,Qt::Unchecked);
   app.processEvents(QEventLoop::AllEvents,1);
   EventManager::instance().sendEvent(QStringLiteral("scene.mesh.visibility.changed"),{{"visible",false}});
   if(!waitView() || surfaceActor->GetMapper()->GetInputDataObject(0,0)!=committedInput
      || visual.renderCalls!=beforeLeave || apply->text()!=QStringLiteral("应用筛选"))return 46;
   EventManager::instance().sendEvent(QStringLiteral("scene.mesh.visibility.changed"),{{"visible",true}});
   if(!waitView())return 39;
   if(visibleValues("MATYPE").contains(materialsGroup->child(0)->data(0,Qt::UserRole+2).toInt()))return 47;
   reset->click();if(!waitView())return 39;
   checks.append(QJsonObject{{"leaving_scene_cancels_pending_view",true},{"returning_scene_applies_latest_selection",true}});
  }
  if(mesh->getVtkUnstructuredGrid().GetPointer()!=sourceGrid||sourceGrid->GetMTime()!=sourceTime)return 41;
  if(timerTicks<1)return 42;
  checks.append(QJsonObject{{"gui_timer_ticks",timerTicks},{"authoritative_grid_preserved",true}});
  if(!mesh->loadProject())return 17;
  if(filters->topLevelItemCount()==0){std::cerr<<"FILTER_RELOAD_EMPTY: visible persistent panel lost entries after loading project without MeshEdit node";return 18;}
  checks.append(QJsonObject{{"persistent_panel_refresh_after_project_load",true},{"filter_groups",filters->topLevelItemCount()}});
  QFile f(QDir(args[1]).filePath(QStringLiteral("网格筛选验证.json")));if(!f.open(QIODevice::WriteOnly))return 17;f.write(QJsonDocument(checks).toJson());
  std::cout<<"MESH_FILTER_CHECK "<<QJsonDocument(checks).toJson(QJsonDocument::Compact).constData()<<std::endl;return 0;
 }
 if(args[4]=="prepare-boundaries"){
  QString error;if(!exportRegressionBoundaryFaces(mesh,args[1],&error)){std::cerr<<error.toStdString();return 10;}return 0;
 }
 if(args[4]=="roundtrip-loads")return physical->saveProject()?0:11;
 if(args[4]=="check-slope-excavation"){
  auto* owner=tree.findNodeByUuid(args[2]);if(!owner || !owner->getPage())return 20;
  auto countChildrenOfType=[](ProjectNode* parent,const QString& type){
   int n=0;for(auto* child:parent->getChildren())if(child&&child->getPage()&&child->getPage()->getPageType()==type)++n;return n;
  };
  auto* page=owner->getPage();
  auto* enabled=page->findChild<QCheckBox*>("slopeExcavationEnabled");
  auto* add=page->findChild<QPushButton*>("slopeAddExcavationStage");
  auto* remove=page->findChild<QPushButton*>("slopeRemoveExcavationStage");
  if(!enabled||!add||!remove||countChildrenOfType(owner,"SlopeExcavationStage")!=2||!enabled->isChecked())return 21;
  const auto original=page->saveJson();
  add->click();if(countChildrenOfType(owner,"SlopeExcavationStage")!=3)return 22;
  remove->click();if(countChildrenOfType(owner,"SlopeExcavationStage")!=2)return 23;
  page->loadJson(original);if(page->saveJson()["excavation"]!=original["excavation"])return 24;
  auto values=page->saveJson()["base_values"].toArray();if(values[20].toString()!="-1"||values[21].toString()!="-2"||values[22].toString()!="1"||values[23].toString()!="0")return 25;
  page->findChild<QComboBox*>("slopeExcavationSupportMode")->setCurrentIndex(1);
  if(countChildrenOfType(owner,"SlopeReinforcementField")!=1)return 26;
  values=page->saveJson()["base_values"].toArray();if(values[22].toString()!="0"||values[23].toString()!="1")return 26;
  page->loadJson(original);page->findChild<QPushButton*>("slopeInitialStressButton")->click();
  if(countChildrenOfType(owner,"GeoStressRegressionAnalysis")<1||!solver->saveProject()||!solver->loadProject())return 27;
  owner=tree.findNodeByUuid(args[2]);
  if(!owner||owner->getPage()->saveJson()["excavation"]!=original["excavation"]||countChildrenOfType(owner,"SlopeExcavationStage")!=2)return 28;
  enabled=owner->getPage()->findChild<QCheckBox*>("slopeExcavationEnabled");enabled->setChecked(false);
  values=owner->getPage()->saveJson()["base_values"].toArray();
  if(values[22].toString()!="0"||values[23].toString()!="0"||countChildrenOfType(owner,"SlopeExcavationStage")!=0)return 29;
  std::cout<<"PASS: actual slope UI tree stages, reinforcement node, support flags, initial-stress node, save/reload and legacy disabled mode"<<std::endl;return 0;
 }
 QSettings settings;settings.setValue("Solver/ComputeDevice",args[3]);settings.setValue("Solver/GpuDeviceMode",2);settings.sync();
 auto* node=tree.findNodeByUuid(args[2]);if(!node)return 5;
 if(args[4]=="steady"||args[4]=="steady-with-post") {
  auto state=node->getPage()->saveJson();state["compute_type"]=1;node->getPage()->loadJson(state);
 }
 QString method;const auto type=node->getPage()->getPageType();
 if(args[4]=="geostress") {
  method="onComputeGeoStressRegression";
  for(auto* c:node->getChildren())if(c->getPage()->getPageType()=="GeoStressRegressionAnalysis"){node=c;break;}
 } else if(type=="CaveExcavationAnalysis")method="onComputeCaveExcavation";
 else if(type=="SeepageAnalysis")method="onComputeSeepage";
 else if(type=="DamAnalysis")method="onComputeStressStrain";
 else if(type=="SlopeStabilityAnalysis")method="onComputeSlopeStability";
 else return 6;
 auto* object=dynamic_cast<QObject*>(solver);bool started=false;QSet<QProcess*> fed;int idle=0;QElapsedTimer elapsed;elapsed.start();
 const int timeout=args.size()>5?args[5].toInt():180;
 if(!QMetaObject::invokeMethod(object,method.toLatin1().constData(),Qt::DirectConnection,Q_ARG(ProjectNode*,node)))return 7;
 if(args[4]=="analysis-manual-stop-check")QTimer::singleShot(750,object,[object]{
  std::cout<<"FIXTURE_TEST_USER_STOP"<<std::endl;
  QMetaObject::invokeMethod(object,"stopSolverExecutable",Qt::DirectConnection);
 });
 QJsonArray liveLogSamples;int lastLiveLogSize=0;
 QTimer monitor;QObject::connect(&monitor,&QTimer::timeout,[&]{
  bool running=false;for(auto* process:object->findChildren<QProcess*>()){if(process->state()!=QProcess::NotRunning){running=true;if(!fed.contains(process)){process->write("0\n");fed.insert(process);}}}
  if(running){started=true;idle=0;}else ++idle;
   if(realOutput&&running&&frame.logWidget){
    auto* text=frame.logWidget->findChild<QTextEdit*>();
    if(text&&text->toPlainText().size()!=lastLiveLogSize&&(text->toPlainText().contains("LOOP=")||text->toPlainText().contains("ISTEP="))){
     lastLiveLogSize=text->toPlainText().size();liveLogSamples.append(QJsonObject{{"elapsed_ms",double(elapsed.elapsed())},{"ui_characters",lastLiveLogSize}});
     QFile evidence(QDir(args[1]).filePath(QStringLiteral("实时日志验证.json")));
     if(evidence.open(QIODevice::WriteOnly))evidence.write(QJsonDocument(QJsonObject{{"native_process_running",true},{"iteration_visible_before_exit",true},{"elapsed_ms",double(elapsed.elapsed())},{"ui_characters",text->toPlainText().size()},{"samples",liveLogSamples}}).toJson());
    }
   }
  if(elapsed.elapsed()>timeout*1000){QMetaObject::invokeMethod(object,"stopSolverExecutable");app.exit(9);}
   else if(idle>15&&(!withPost||postPrepared||dialogFailure||!solverLaunched)){
    if(realOutput){auto* log=frame.logWidget?frame.logWidget->findChild<QTextEdit*>():nullptr;if(!log){app.exit(13);return;}QFile snapshot(QDir(args[1]).filePath(QStringLiteral("前端日志验证.txt")));if(!snapshot.open(QIODevice::WriteOnly)){app.exit(13);return;}snapshot.write(log->toPlainText().toUtf8());std::cout<<"LOG_UI_CHARACTERS="<<log->toPlainText().size()<<std::endl;}
    std::cout<<"DRIVER_FINISH started="<<(started||solverLaunched)<<" dialog_failure="<<dialogFailure<<" post_prepared="<<postPrepared<<std::endl;app.exit(!solverLaunched||dialogFailure?8:0);}
 });monitor.start(200);
 const int result=app.exec();
 // The short-lived driver owns the loaded runtime; process exit releases it.
 return result;
}
