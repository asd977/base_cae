#include <QtWidgets>
#include <BasePage.h>
#include <EventManager.h>
#include <PluginInterface.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <StyleManager.h>
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include "../plugins/Base_Gemo/builders/DxfReader.h"
#include "../plugins/Base_Gemo/display/GeometryDisplayManager.h"
#include <vtkProperty.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <SmeshShapeIndexMap.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

static void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

class TestTree : public ProjectManagerService
{
public:
    QTreeWidget* tree;
    explicit TestTree(QTreeWidget* value) : tree(value) {}
    void addTreeItem(ProjectNode* node) override { tree->addTopLevelItem(node); }
    void setCurrentItem(ProjectNode* node) override { tree->setCurrentItem(node); }
    ProjectNode* findNodeByUuid(const QString& uuid) override
    {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if (auto* node = dynamic_cast<ProjectNode*>(*it); node && node->getUuid() == uuid)
                return node;
        return nullptr;
    }
    void setNodeDimmed(const QString&, bool) override {}
};

static void encodingChecks(const QString& directory)
{
    const QString layer = QStringLiteral("CL-首曲线");
    for (int encoding = 0; encoding < 3; ++encoding)
    {
        const QString path = directory + QString("/encoding%1.dxf").arg(encoding);
        const QByteArray name = encoding == 1 ? QByteArray::fromHex("434c2dcad7c7facfdf") : layer.toUtf8();
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "create DXF encoding fixture");
        file.write("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\n");
        file.write(encoding == 2 ? "UTF-8\n" : "ANSI_936\n");
        file.write("0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n");
        file.write(name);
        file.write("\n10\n0\n20\n0\n30\n50\n11\n10\n21\n0\n31\n50\n0\nENDSEC\n0\nEOF\n");
        file.close();
        QVector<GeometryDxf::Entity> entities;
        QString diagnostic;
        check(GeometryDxf::read(path, entities, diagnostic), qPrintable(diagnostic));
        check(entities.size() == 1 && entities[0].layer == layer, "UTF-8/misdeclared UTF-8/GBK Chinese layer round-trip");
        check(entities[0].points[0].Z() == 50, "encoding leaves elevations unchanged");
    }
    check(GeometryDxf::normalizeLayerName(QStringLiteral("CL-璁℃洸绾")) == QStringLiteral("CL-计曲线"), "legacy index contour name repair");
    check(GeometryDxf::normalizeLayerName(QStringLiteral("CL-棣栨洸绾")) == layer, "legacy intermediate contour name repair");
    for (const auto& name : {layer, QStringLiteral("地质边界"), QStringLiteral("CONTOUR_50"), QStringLiteral("岩层α")})
        check(GeometryDxf::normalizeLayerName(name) == name, "valid Unicode layer names preserved");
    std::cout << "DXF: UTF-8, incorrect code page header, GBK and legacy names passed\n";
}

static QByteArray syntheticProject()
{
    QJsonArray points;
    for (int i = 0; i < 310000; ++i)
        points.append(QJsonArray{double(i % 1000), double(i / 1000), 50.0});
    QJsonObject state{{"generated", true}, {"domainMode", "grid"}, {"showSurface", true},
                      {"showContours", false}, {"sources", QJsonArray{QJsonObject{
                          {"layer", QStringLiteral("CL-棣栨洸绾")}, {"name", "1"}, {"role", 0},
                          {"enabled", true}, {"points", points}}}}};
    std::ostringstream stream;
    BRepTools::Write(BRepPrimAPI_MakeBox(100, 100, 50).Shape(), stream);
    QJsonObject item{{"uuid", "terrain-display-regression"}, {"title", QStringLiteral("等高线地形")},
                     {"type", "terrain_surface"}, {"kind", "TerrainSurface"}, {"module", "Geometry"},
                     {"visible", true}, {"state", state}, {"cachedBrep", QString::fromStdString(stream.str())}};
    return QJsonDocument(QJsonObject{{"geom_items", QJsonArray{item}}}).toJson(QJsonDocument::Compact);
}

static void productionStorageChecks(PreProcessingService* pre, const QString& root)
{
    pre->closeProject();
    TopoDS_Compound assembly;BRep_Builder builder;builder.MakeCompound(assembly);
    for (int i=0;i<16;++i)
        builder.Add(assembly,BRepPrimAPI_MakeBox(gp_Pnt(i*2,0,0),1,1,1).Shape());
    std::ostringstream stream;BRepTools::Write(assembly,stream);
    const QJsonObject item{{"uuid","storage-roundtrip"},{"title","Assembly"},
        {"type","terrain_surface"},{"kind","TerrainSurface"},{"module","Geometry"},
        {"visible",true},{"state",QJsonObject{{"generated",true}}},
        {"cachedBrep",QString::fromStdString(stream.str())}};
    const QString jsonPath=root+QStringLiteral("/工程数据/preprocessing.json");
    auto probe=item;probe["uuid"]="display-probe";probe["title"]="Probe";
    std::ostringstream probeStream;BRepTools::Write(BRepPrimAPI_MakeBox(gp_Pnt(40,0,0),1,1,1).Shape(),probeStream);
    probe["cachedBrep"]=QString::fromStdString(probeStream.str());
    { QFile file(jsonPath);check(file.open(QIODevice::WriteOnly),"write assembly fixture");
      file.write(QJsonDocument(QJsonObject{{"geom_items",QJsonArray{item,probe}}}).toJson()); }
    check(pre->loadProject() && pre->saveProject(),"production save of compressed CAD assets");
    const auto savedAsset=[&]() {
        QFile file(jsonPath);check(file.open(QIODevice::ReadOnly),"read saved assembly");
        const auto saved=QJsonDocument::fromJson(file.readAll()).object()["geom_items"].toArray()[0].toObject();
        check(saved["cachedBrep"].toString().isEmpty(),"large BREP removed from JSON");
        return saved["cachedBrepAsset"].toString();
    };
    const QString asset=savedAsset();check(!asset.isEmpty(),"project-owned BREP asset");
    auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>();
    GeometryDisplayManager::setPresentation({"storage-roundtrip"},0,.45);
    GeometryDisplayManager::isolateObjects({"storage-roundtrip"});
    check(!visual->getProp("display-probe_face")->GetVisibility(),"isolation hides other geometry");
    visual->setGroupVisible("Geometry",false,false);visual->setGroupVisible("Geometry",true,false);
    check(!visual->getProp("display-probe_face")->GetVisibility(),"module navigation preserves object isolation");
    GeometryDisplayManager::restorePresentation();
    check(visual->getProp("display-probe_face")->GetVisibility(),"restore returns original object visibility");
    GeometryDisplayManager::setPresentation({"storage-roundtrip"},1,.45);
    check(pre->saveProject() && savedAsset()==asset,"unchanged Shape reuses asset without hashing");
    QString exported,diagnostic;
    check(pre->prepareMeshingGeometry(3,exported,diagnostic),qPrintable(diagnostic));
    const auto exportTime=QFileInfo(exported).lastModified();
    check(pre->prepareMeshingGeometry(3,exported,diagnostic)
        && QFileInfo(exported).lastModified()==exportTime,"unchanged topology preserves export generation");
    const auto indices=SmeshShapeIndexMap::load(exported);
    check(indices.valid() && indices.uiCount(3)==17 && indices.requireIndex(3,17)>0,
          "production export carries exact UI topology indices");
    QTemporaryDir moved;QDir(moved.path()).mkpath("工程数据");QDir(moved.path()).mkpath("geometry-data");
    check(QFile::copy(jsonPath,moved.path()+QStringLiteral("/工程数据/preprocessing.json"))
        && QFile::copy(root+"/"+asset,moved.path()+"/"+asset),"relocate complete CAD project");
    pre->closeProject();
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,moved.path(),"storage-regression");
    check(pre->loadProject() && pre->listEntityIds(3).size()==17,"production reopen after project move without source CAD");
    auto* restored=vtkActor::SafeDownCast(visual->getProp("storage-roundtrip_face"));
    check(restored && std::abs(restored->GetProperty()->GetOpacity()-.45)<1.e-6
        && restored->GetProperty()->GetRepresentation()==VTK_WIREFRAME,"wireframe and opacity survive project move/reopen");
    check(pre->saveProject(),"resave relocated project");
    pre->closeProject();
    std::cout<<"CAD storage: asset reuse, explicit BREP indices and moved-project reopen passed\n";
}

int main(int argc, char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    QApplication app(argc, argv);
    try
    {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary project");
        encodingChecks(temporary.path());
        QByteArray original;
        if (argc > 1)
        {
            QFile file(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/工程数据/preprocessing.json"));
            check(file.open(QIODevice::ReadOnly), "read user project");
            original = file.readAll();
        }
        else original = syntheticProject();
        QDir().mkpath(temporary.path() + QStringLiteral("/工程数据"));
        QFile copy(temporary.path() + QStringLiteral("/工程数据/preprocessing.json"));
        check(copy.open(QIODevice::WriteOnly), "write disposable project copy");
        copy.write(original);
        copy.close();

        StyleManager::applyTheme(app, "blue");
        auto& registry = ServiceRegistry::instance();
        for (const QString& name : {"Base_UiFrame", "Base_Visualization", "Base_ProjectManager", "Base_Output"})
        {
            auto* loader = new QPluginLoader(QCoreApplication::applicationDirPath() + "/plugins/" + name + ".dll");
            check(loader->load(), qPrintable(loader->errorString()));
            auto* plugin = qobject_cast<PluginInterface*>(loader->instance());
            check(plugin, "plugin interface");
            plugin->start();
        }
        auto* frameService = registry.getService<UiFrameService>();
        auto* frame = dynamic_cast<QWidget*>(frameService);
        auto* visual = registry.getService<VisualWidgetService>();
        check(frame && visual, "native frame and renderer");
        auto* tree = frame->findChild<QTreeWidget*>("projectTreeWidget");
        check(tree, "model tree");
        TestTree testTree(tree);
        registry.registerService<ProjectManagerService>(&testTree);
        PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, temporary.path(), "terrain-regression");
        auto* loader = new QPluginLoader(QCoreApplication::applicationDirPath() + "/plugins/Base_Gemo.dll");
        check(loader->load(), qPrintable(loader->errorString()));
        qobject_cast<PluginInterface*>(loader->instance())->start();
        auto* pre = registry.getService<PreProcessingService>();
        check(pre && pre->loadProject(), "restore terrain project");

        auto findTerrain = [&]() -> ProjectNode* {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if (auto* n = dynamic_cast<ProjectNode*>(*it); n && n->getPage() &&
                    n->getPage()->findChild<QCheckBox*>("showTerrainSurfaceCheck")) return n;
            return nullptr;
        };
        auto* node = findTerrain();
        check(node, "terrain page restored");
        auto* page = node->getPage();
        page->applyDeferredLoadJson();
        const QString uuid = page->getUuid();
        auto* actor = vtkActor::SafeDownCast(visual->getProp(uuid + "_face"));
        auto* mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        check(mapper && mapper->GetInput()->GetNumberOfPolys() > 0, "restored CAD mesh");
        auto* mesh = mapper->GetInput();
        frame->resize(1500, 950);
        frame->show();
        frameService->addWidget(page, QStringLiteral("地形面"));
        page->beforeLoad();
        app.processEvents();
        auto* color = page->findChild<QCheckBox*>("showTerrainSurfaceCheck");
        auto* contours = page->findChild<QCheckBox*>("showTerrainContoursCheck");
        auto* table = page->findChild<QTableWidget*>("terrainSourceTable");
        check(!table->item(0, 1)->text().contains(QStringLiteral("洸绾")), "project's legacy Chinese labels repaired");
        node->setCheckState(0, Qt::Checked);
        color->setChecked(false);
        check(actor->GetVisibility() && !mapper->GetScalarVisibility(), "plain surface remains visible");
        color->setChecked(true);
        check(actor->GetVisibility() && mapper->GetScalarVisibility(), "color surface remains visible after reopening");
        check(mapper->GetInput() == mesh && mesh->GetPointData()->GetArray("TerrainElevation"), "color uses original CAD geometry");
        auto* lookupTable = mapper->GetLookupTable();
        const auto meshTime = mesh->GetMTime();
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 10; ++i)
        {
            node->setCheckState(0, Qt::Unchecked);
            check(!actor->GetVisibility(), "tree hides colored terrain");
            node->setCheckState(0, Qt::Checked);
            check(actor->GetVisibility(), "tree restores colored terrain");
        }
        const auto toggleMs = timer.elapsed();
        check(mapper->GetLookupTable() == lookupTable && mesh->GetMTime() == meshTime, "tree toggles reuse LUT and mesh");
        std::cout << "20 model tree toggles: " << toggleMs << " ms (" << toggleMs / 20.0 << " ms/toggle)\n";
        check(toggleMs < 10000, "visibility responsive with 310000 points");
        contours->setChecked(true);
        auto* lines = visual->getProp(uuid + "_terrain_sources");
        check(lines && lines->GetVisibility(), "contour preview visible");
        node->setCheckState(0, Qt::Unchecked);
        check(!lines->GetVisibility() && !actor->GetVisibility(), "tree hides contours and colored surface together");
        color->setChecked(false);
        color->setChecked(true);
        check(!actor->GetVisibility(), "color toggle respects hidden tree node");
        node->setCheckState(0, Qt::Checked);
        contours->setChecked(false);
        page->beforeUnload();
        page->beforeLoad();
        check(actor->GetVisibility() && mapper->GetScalarVisibility(), "leave and reopen retains terrain color");
        auto* camera = visual->renderer()->GetActiveCamera();
        double bounds[6]; mesh->GetBounds(bounds);
        const double span = std::max(bounds[1] - bounds[0], bounds[3] - bounds[2]);
        camera->SetFocalPoint((bounds[0] + bounds[1]) / 2, (bounds[2] + bounds[3]) / 2, (bounds[4] + bounds[5]) / 2);
        camera->SetPosition(bounds[1] + span, bounds[2] - span, bounds[5] + span);
        camera->SetViewUp(0, 0, 1);
        visual->resetCamera(false);
        visual->render();
        app.processEvents();
        const QString output = QDir::currentPath() + "/tmp/terrain-display-checks";
        QDir().mkpath(output);
        check(frame->grab().save(output + "/terrain-color.png"), "capture native color viewport");
        check(pre->saveProject(), "save disposable project");
        check(pre->loadProject(), "reopen saved disposable project");
        node = findTerrain();
        check(node, "saved terrain node");
        page = node->getPage();
        page->applyDeferredLoadJson();
        page->beforeLoad();
        actor = vtkActor::SafeDownCast(visual->getProp(page->getUuid() + "_face"));
        mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        check(mapper && mapper->GetScalarVisibility() && actor->GetVisibility(), "saved color survives project reload");

        // Visibility history must remain undoable while reusing committed source data.
        node->setCheckState(0, Qt::Unchecked);
        EventManager::instance().sendEvent(PublicConstants::kActionGeomUndo);
        node = findTerrain();
        check(node && node->checkState(0) == Qt::Checked, "undo restores tree visibility");
        EventManager::instance().sendEvent(PublicConstants::kActionGeomRedo);
        node = findTerrain();
        check(node && node->checkState(0) == Qt::Unchecked, "redo restores hidden state");
        check(pre->saveProject() && pre->loadProject(), "save and reload hidden terrain");
        node = findTerrain();
        check(node && node->checkState(0) == Qt::Unchecked, "hidden terrain remains hidden on reload");
        page = node->getPage();
        page->applyDeferredLoadJson();
        page->beforeLoad();
        page->findChild<QCheckBox*>("showTerrainSurfaceCheck")->setChecked(false);
        node->setCheckState(0, Qt::Checked);
        actor = vtkActor::SafeDownCast(visual->getProp(page->getUuid() + "_face"));
        mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        check(mapper && actor->GetVisibility() && !mapper->GetScalarVisibility(), "lazy hidden presentation materializes as plain surface");
        check(pre->saveProject() && pre->loadProject(), "save and reload plain terrain");
        node = findTerrain();
        page = node->getPage();
        page->applyDeferredLoadJson();
        page->beforeLoad();
        actor = vtkActor::SafeDownCast(visual->getProp(page->getUuid() + "_face"));
        mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        check(mapper && actor->GetVisibility() && !mapper->GetScalarVisibility(), "plain display survives project reload");
        page->findChild<QCheckBox*>("showTerrainSurfaceCheck")->setChecked(true);
        check(actor->GetVisibility() && mapper->GetScalarVisibility(), "plain saved project switches back to color");
        productionStorageChecks(pre,temporary.path());
        frame->hide();
        pre->closeProject();
        registry.registerService<ProjectManagerService>(nullptr);
        std::cout << "PASS: color/plain display, contour/tree visibility, page navigation, undo/redo, hidden and plain save/reload\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
