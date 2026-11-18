#include <QtCore>
#include <QtWidgets>
#include <memory>
#include <vector>
#include "sketch/SketchDocument.h"
#include "sketch/SketchShapeBuilder.h"
#include "sketch/SketchViewController.h"
#include <BasePage.h>
#define private public
#include "sketch/SketchPanel.h"
#include "../plugins/Base_Gemo/workbench/GeometryWorkbench.h"
#undef private
#include "../plugins/Base_Gemo/sketch/SketchPanel.cpp"
#include "../plugins/Base_Gemo/terrain/TerrainSurfacePanel.h"
#include "../plugins/Base_Gemo/sketch/SketchCurveTools.h"
#include "../plugins/Base_Gemo/core/FeatureData.h"
#include "../plugins/Base_Gemo/features/GeometryFeatureRegistry.h"
#include "../plugins/Base_Gemo/api/IGeometryService.h"
#include "../plugins/Base_Gemo/core/GeometryService.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepTools.hxx>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkPolyData.h>
#include <vtkNew.h>
#include <SurfaceDomainData.h>
#include <sstream>
#include <iostream>

static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
#include "TerrainDisplayChecks.h"

void extendedChecks(QApplication& app);

static double lengthOf(const GeometrySketch::SketchDocument& document)
{
    GProp_GProps properties;BRepGProp::LinearProperties(GeometrySketch::SketchCurveTools::shape(document),properties);return properties.Mass();
}
static void writeFile(const QString& path,const QByteArray& content)
{
    QSaveFile file(path);require(file.open(QIODevice::WriteOnly),"open output");require(file.write(content)==content.size()&&file.commit(),"save output");
}
void extendedChecks(QApplication& app)
{
    using namespace GeometrySketch;
    using Tools=SketchCurveTools;
    SketchDocument line,vertical,arc,circle,spline;QString message;
    line.setProfile({{0,0},{10,0}},false,true);vertical.setProfile({{5,-5},{5,5}},false,true);
    arc.setArc({{0,0},{5,5},{10,0}},true);circle.setCircle(0,0,3);
    spline.setCurve({{0,0},{3,4},{6,3},{10,0}},true);
    std::vector<SketchDocument> pieces;
    require(Tools::split(line,{5,0},0.1,pieces,message)&&pieces.size()==2,"split line");
    require(Tools::split(arc,{5,5},0.1,pieces,message),"split arc");
    require(std::abs(lengthOf(pieces[0])+lengthOf(pieces[1])-lengthOf(arc))<1e-6,"arc split preserves geometry");
    auto splineShape=Tools::shape(spline);BRepAdaptor_Curve curve(TopoDS::Edge(TopExp_Explorer(splineShape,TopAbs_EDGE).Current()));
    const auto p=curve.Value(curve.FirstParameter()+.5347*(curve.LastParameter()-curve.FirstParameter()));
    require(Tools::split(spline,{p.X(),p.Y()},.01,pieces,message),"split spline");
    require(std::abs(lengthOf(pieces[0])+lengthOf(pieces[1])-lengthOf(spline))<1e-5,"spline split retains exact curve");
    SketchDocument restored;restored.fromJson(pieces[0].toJson());require(std::abs(lengthOf(restored)-lengthOf(pieces[0]))<1e-8,"trimmed spline save/restore");
    require(Tools::intersectsBox(spline,p.X()-1e-5,p.X()+1e-5,p.Y()-1e-5,p.Y()+1e-5),"tiny selection box must hit exact spline");
    require(!Tools::intersectsBox(circle,-.1,.1,-.1,.1),"circle surrounding box is not a boundary hit");
    require(Tools::intersectsBox(circle,2.999,3.001,-.001,.001),"circle crossing box");
    require(Tools::split(circle,{3,0},.01,pieces,message)&&pieces.size()==2,"split full circle into two arcs");
    require(std::abs(lengthOf(pieces[0])+lengthOf(pieces[1])-6*3.141592653589793)<1e-6,"circle split length");
    SketchDocument target,extended;target.setCircle(15,0,2);
    require(Tools::extend(line,target,{13,0},extended,message),"extend line to circular boundary");
    require(std::abs(lengthOf(extended)-13)<1e-6,"line extends to nearest circle crossing");
    SketchDocument quarter;quarter.setArc({{1,0},{std::sqrt(.5),std::sqrt(.5)},{0,1}},true);
    target.setProfile({{-1,-2},{-1,2}},false,true);
    require(Tools::extend(quarter,target,{-1,0},extended,message),"extend arc");
    require(std::abs(lengthOf(extended)-3.141592653589793)<1e-5,"arc extends along circle");
    target.setProfile({{14,-100},{14,100}},false,true);
    require(Tools::extend(spline,target,{14,0},extended,message),"extend spline tangent");
    require(lengthOf(extended)>lengthOf(spline),"spline extension retains original plus tangent");
    auto snap=Tools::snap({line,vertical},{},{5.03,.02},.2);require(snap.hit&&snap.kind==4&&std::hypot(snap.point.u-5,snap.point.v)<1e-7,"intersection capture");
    snap=Tools::snap({line},{},{.02,.03},.2);require(snap.hit&&snap.kind==1,"endpoint capture");
    require(!Tools::snap({line},{},{15,0},.2).hit,"no capture on imaginary line extension");
    Sketch2DDialog drawing;drawing.m_committedDocuments={line};drawing.refreshObjectTable();drawing.setCommand(Command::Polyline);
    require(!drawing.ui->objectSnapCheck->isChecked(),"object capture starts off");drawing.ui->objectSnapCheck->setChecked(true);
    double u=.02,v=.03;require(drawing.snap(u,v)&&std::hypot(u,v)<1e-8,"drawing capture toggle is wired");
    Sketch2DDialog captureRestored;captureRestored.loadJson(drawing.saveJson());require(captureRestored.ui->objectSnapCheck->isChecked(),"save capture preference");
    drawing.setCommand(Command::Select);drawing.handleSketchMove(.02,.03);require(!drawing.m_cursorStatusText.contains(QStringLiteral("捕捉")),"selection mode does not capture");
    std::cout<<"PASS: exact spline/arc/circle split, curve-boundary extension, tangent extension, exact box selection, feature capture"<<std::endl;

    const QString output=QDir::cleanPath(QDir::current().absoluteFilePath("../terrain_feature_preview/example_project"));
    QDir().mkpath(output+QStringLiteral("/工程数据"));
    using namespace GeometryTerrain;
    const auto sources=TerrainSurfaceBuilder::example();auto result=TerrainSurfaceBuilder::build(sources,5);
    if(!result.ok())std::cerr<<result.error.toUtf8().constData()<<std::endl;
    require(result.ok(),"example terrain builds");require(result.triangles>500&&result.minElevation==100&&result.maxElevation==150,"example terrain bounds");
    require(result.domains.size()==5,"nested example contours create five domains");
    require(result.faceDomains.size()==5,"five contour bands are five continuous CAD faces, not preview triangles");
    auto finerPreview=TerrainSurfaceBuilder::build(sources,2);
    if(!finerPreview.ok())std::cerr<<finerPreview.error.toUtf8().constData()<<std::endl;
    require(finerPreview.ok()&&finerPreview.faceDomains.size()==5,"contour sampling does not create CAD faces or restrict computational meshing");
    for(int i=0;i<5;++i)require(result.domains[i].minElevation==100+10*i&&result.domains[i].maxElevation==110+10*i,"each contour domain spans the correct elevation band");
    const auto whole=TerrainSurfaceBuilder::build(sources,5,DomainMode::WholeSurface);
    if(!whole.ok())std::cerr<<whole.error.toUtf8().constData()<<std::endl;
    require(whole.ok()&&whole.domains.size()==1&&whole.triangles==result.triangles,"whole-surface mode retains geometry without contour partitioning");
    auto controls=sources;for(auto& source:controls)if(source.role==Role::Contour)source.role=Role::Breakline;
    require(TerrainSurfaceBuilder::build(controls).domains.size()==1,"breaklines constrain shape without creating contour domains");
    QVector<Source> openSources(2);openSources[0].role=Role::Boundary;openSources[0].closed=true;
    openSources[0].points={gp_Pnt(0,0,0),gp_Pnt(20,0,0),gp_Pnt(20,20,20),gp_Pnt(0,20,20)};
    openSources[1].points={gp_Pnt(0,10,10),gp_Pnt(20,10,10)};
    auto openRegions=TerrainSurfaceBuilder::build(openSources);
    require(openRegions.ok()&&openRegions.domains.size()==2,"open contour reaching the boundary splits two connected domains");
    openSources[1].points={gp_Pnt(5,10,10),gp_Pnt(15,10,10)};
    openRegions=TerrainSurfaceBuilder::build(openSources);
    require(openRegions.ok()&&openRegions.domains.size()==1,"dangling contour must not invent a closed domain");
    require(TerrainSurfaceBuilder::build(TerrainSurfaceBuilder::sourcesFromState(TerrainSurfaceBuilder::state(sources)),5).ok(),"terrain source state round trip");
    auto invalid=sources;invalid[1].points[0].SetZ(999);require(!TerrainSurfaceBuilder::build(invalid).ok(),"reject inconsistent contour Z");
    invalid=sources;auto conflict=invalid.back();conflict.points[0].SetZ(151);invalid.push_back(conflict);require(!TerrainSurfaceBuilder::build(invalid).ok(),"reject duplicate XY conflicting Z");
    invalid=sources;invalid[0].closed=false;require(!TerrainSurfaceBuilder::build(invalid).ok(),"reject open boundary");
    invalid=sources;invalid[0].points={gp_Pnt(499900,2999920,100),gp_Pnt(500100,3000080,100),gp_Pnt(499900,3000080,100),gp_Pnt(500100,2999920,100)};
    require(!TerrainSurfaceBuilder::build(invalid).ok(),"reject self-intersecting boundary");
    invalid=sources;conflict=invalid.back();conflict.points[0].SetX(501000);invalid.push_back(conflict);require(!TerrainSurfaceBuilder::build(invalid).ok(),"reject point outside boundary");
    QVector<Source> crossing(2);crossing[0].points={gp_Pnt(0,0,10),gp_Pnt(10,10,10)};crossing[1].points={gp_Pnt(0,10,20),gp_Pnt(10,0,20)};
    require(!TerrainSurfaceBuilder::build(crossing).ok(),"reject crossing elevations");
    vtkNew<vtkXMLPolyDataWriter> writer;writer->SetInputData(result.mesh);writer->SetWriteToOutputString(true);require(writer->Write(),"terrain VTP encoding");
    auto text=writer->GetOutputString();writeFile(output+"/terrain.vtp",QByteArray(text.data(),int(text.size())));
    vtkNew<vtkXMLPolyDataReader> reader;reader->SetFileName((output+"/terrain.vtp").toUtf8().constData());reader->Update();require(reader->GetOutput()->GetNumberOfCells()==result.triangles,"VTP round trip");
    std::ostringstream brep;BRepTools::Write(result.shape,brep);writeFile(output+"/terrain.brep",QByteArray::fromStdString(brep.str()));
    SurfaceDomainData::Definition exampleDomains;exampleDomains.faceDomains=result.faceDomains;
    for(const auto& domain:result.domains){exampleDomains.materialGroups[domain.id]=1;exampleDomains.names[domain.id]=domain.name;}
    require(SurfaceDomainData::write(output+"/terrain.brep",exampleDomains,message),"terrain BREP retains domain ownership metadata");
    FeatureData feature;feature.uuid="{66ed1159-0865-4a28-bbc0-777500000001}";feature.title=QStringLiteral("山丘地形（高程100～150米）");feature.type=PublicConstants::kPageGeomTerrainSurface;feature.kind="TerrainSurface";feature.module="Geometry";feature.state=TerrainSurfaceBuilder::state(sources);feature.cachedBrep=QString::fromStdString(brep.str());
    QJsonObject root{{"module","PreProcessing"},{"root_title",QStringLiteral("几何")},{"featureDataSchema","GeometryFeatureData.v2"},{"geom_items",QJsonArray{feature.toJson()}}};
    writeFile(output+QStringLiteral("/工程数据/preprocessing.json"),QJsonDocument(root).toJson());
    GeometryService service;
    const auto fromProject=FeatureData::fromJson(root["geom_items"].toArray()[0].toObject());
    require(GeometryFeatures::rebuildFeature(fromProject,service,&message),"native project feature registry rebuild");
    std::istringstream cache(feature.cachedBrep.toStdString());TopoDS_Shape cached;BRep_Builder cachedBuilder;BRepTools::Read(cached,cache,cachedBuilder);
    require(!cached.IsNull()&&BRepCheck_Analyzer(cached).IsValid(),"native project CAD cache is valid");
    writeFile(output+QStringLiteral("/等高线地形示例.xml"),QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><SmartCAEProject version=\"1.0\"><Name>等高线地形示例</Name><ProjectFiles><PreProcessing>工程数据/preprocessing.json</PreProcessing></ProjectFiles><GlobalValues><Value key=\"mesh_dimension\" type=\"int\">2</Value></GlobalValues></SmartCAEProject>").toUtf8());
    writeFile(output+"/terrain_sources.json",QJsonDocument(TerrainSurfaceBuilder::state(sources)).toJson());
    QJsonArray xyz,cells;for(vtkIdType i=0;i<result.mesh->GetNumberOfPoints();++i){double p[3];result.mesh->GetPoint(i,p);xyz.append(QJsonArray{p[0],p[1],p[2]});}
    vtkIdType count;const vtkIdType* ids;auto* triangles=result.mesh->GetPolys();triangles->InitTraversal();while(triangles->GetNextCell(count,ids))cells.append(QJsonArray{int(ids[0]),int(ids[1]),int(ids[2])});
    QJsonArray triangleDomains,domainDefinitions;
    for(vtkIdType i=0;i<result.mesh->GetNumberOfCells();++i)triangleDomains.append(int(result.mesh->GetCellData()->GetArray("TerrainDomain")->GetTuple1(i)));
    for(const auto& domain:result.domains)domainDefinitions.append(QJsonObject{{"id",domain.id},{"name",domain.name},{"min",domain.minElevation},{"max",domain.maxElevation}});
    writeFile(output+"/terrain_preview_data.json",QJsonDocument(QJsonObject{{"points",xyz},{"triangles",cells},{"triangleDomains",triangleDomains},{"domains",domainDefinitions}}).toJson(QJsonDocument::Compact));
    TerrainSurfacePanel panel;panel.loadJson(feature.state);panel.beforeLoad();require(panel.result().ok(),"terrain panel restore and regenerate");
    panel.resize(600,650);panel.show();app.processEvents();panel.grab().save(output+"/terrain-panel.png");panel.hide();
    panel.setSources(sources);require(!panel.saveJson()["generated"].toBool(),"terrain edits invalidate generated data");
    require(panel.generate(message),"terrain panel generate");
    std::cout<<"PASS: terrain generation, constraints, boundary/elevation rejection, project state, CAD topology, VTP round trip; "<<result.summary().toUtf8().constData()<<std::endl;

    QByteArray dxf="0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n6\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    for(const auto& source:sources)
    {
        if(source.role==Role::SpotHeight){const auto& p=source.points[0];dxf+="0\nPOINT\n8\nSUMMIT\n10\n"+QByteArray::number(p.X(),'g',16)+"\n20\n"+QByteArray::number(p.Y(),'g',16)+"\n30\n"+QByteArray::number(p.Z(),'g',16)+"\n";continue;}
        dxf+="0\nLWPOLYLINE\n8\n"+source.layer.toUtf8()+"\n90\n"+QByteArray::number(source.points.size())+"\n70\n1\n38\n"+QByteArray::number(source.points[0].Z(),'g',16)+"\n";
        for(const auto& p:source.points)dxf+="10\n"+QByteArray::number(p.X(),'g',16)+"\n20\n"+QByteArray::number(p.Y(),'g',16)+"\n";
    }
    dxf+="0\nENDSEC\n0\nEOF\n";writeFile(output+"/contours.dxf",dxf);
    terrainDisplayChecks(output);
    QVector<Source> imported;require(TerrainSurfaceBuilder::readDxf(output+"/contours.dxf",imported,message),"sample DXF read");require(imported.size()==6&&TerrainSurfaceBuilder::build(imported).ok(),"sample DXF to terrain end to end");
    writeFile(output+"/nested-block.dxf",QByteArray("0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nINNER\n10\n0\n20\n0\n30\n0\n0\nLINE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n10\n21\n0\n31\n0\n0\nENDBLK\n0\nBLOCK\n2\nOUTER\n10\n0\n20\n0\n30\n0\n0\nINSERT\n2\nINNER\n10\n0\n20\n0\n30\n0\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n8\nCONTOUR_42\n2\nOUTER\n10\n100\n20\n200\n30\n42\n41\n2\n42\n1\n50\n90\n0\nENDSEC\n0\nEOF\n"));
    QVector<DxfGeometryRecord> records;require(ImportGeometryBuilder::readDxfRecords(output+"/nested-block.dxf",records,message),"nested block import");require(records.size()==1&&records[0].layer=="CONTOUR_42","block inherits layer and is not imported twice");
    BRepAdaptor_Curve blockCurve(TopoDS::Edge(TopExp_Explorer(records[0].shape,TopAbs_EDGE).Current()));
    require(blockCurve.Value(blockCurve.FirstParameter()).Distance(gp_Pnt(100,200,42))<1e-6&&blockCurve.Value(blockCurve.LastParameter()).Distance(gp_Pnt(100,220,42))<1e-6,"block transform preserves elevation and rotation/scale");
    std::cout<<"PASS: sample DXF end to end and nested INSERT transform/layer inheritance"<<std::endl;

    GeometryWorkbench workbench;
    workbench.m_geomNode=new ProjectNode(QStringLiteral("几何"));
    auto* terrainPage=new TerrainSurfacePanel;
    auto* terrainNode=new ProjectNode("terrain",terrainPage->getUuid(),terrainPage);
    terrainNode->setData(0,ProjectNode::RoleModule,"Geometry");
    terrainNode->setData(0,ProjectNode::RoleKind,"TerrainSurface");
    workbench.m_geomNode->addChild(terrainNode);
    terrainPage->setSources(sources);require(terrainPage->generate(message),"workbench terrain source");
    QString meshingPath;
    require(workbench.prepareMeshingGeometry(2,meshingPath,message),"actual workbench 2D terrain export");
    TopoDS_Shape meshInput;BRep_Builder inputBuilder;
    require(BRepTools::Read(meshInput,QFile::encodeName(meshingPath).constData(),inputBuilder),"read workbench meshing input");
    TopTools_IndexedMapOfShape meshShells,meshFaces;
    TopExp::MapShapes(meshInput,TopAbs_SHELL,meshShells);TopExp::MapShapes(meshInput,TopAbs_FACE,meshFaces);
    require(meshShells.Extent()==5&&meshFaces.Extent()==5,"2D export sends five continuous contour faces to the mesher");
    QFile inputFile(meshingPath);require(inputFile.open(QIODevice::ReadOnly),"open workbench mesh input");
    writeFile(output+"/meshing-input.brep",inputFile.readAll());
    std::cout<<"PASS: workbench terrain to mesher preserves five complete contour domains"<<std::endl;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        Sketch2DDialog panel;
        require(!panel.ui->objectsTable->isHidden(), "object table should stay visible");
        require(!panel.ui->optionGroup->isHidden(), "drawing aids should stay visible");
        require(!panel.ui->deleteObjectButton->isHidden(), "delete should be exposed");
        GeometryPanelUi::apply(&panel);
        require(panel.ui->planarRegionButton->text().contains(QStringLiteral("二维面")), "face button must keep its specific label");
        require(!panel.findChild<QCheckBox*>("snapCheck"), "object snap should be removed");
        require(!panel.findChild<QPushButton*>("moreSettingsButton"), "more settings toggle should be removed");
        require(!panel.findChild<QComboBox*>("outputModeCombo"), "duplicate output selector should be removed");

        QJsonObject legacy;
        legacy["snap"] = false;
        legacy["gridSnap"] = false;
        legacy["outputMode"] = static_cast<int>(OutputMode::Entity);
        panel.loadJson(legacy);
        SketchDocument square, extra, construction, point;
        square.setRectangle(0, 0, 10, 10);
        extra.setProfile({{20, 20}, {21, 20}}, false, true);
        construction.setProfile({{0, 5}, {10, 5}}, false, true);
        construction.setConstruction(true);
        point.appendStandalonePoint(0, 0);
        panel.m_committedDocuments = {square, extra, construction, point};
        panel.refreshObjectTable();
        const auto sourceGeometry = SketchShapeBuilder::build(square, panel.planeSettings());
        require(sourceGeometry.ok && !TopExp_Explorer(sourceGeometry.shape, TopAbs_FACE).More(),
                "closed source geometry must not generate faces automatically, even from legacy Entity mode");
        TopoDS_Shape unapproved;
        QString unapprovedDiagnostic;
        require(!panel.buildApprovedMeshingFaces(unapproved, unapprovedDiagnostic), "unconfirmed sketch must not supply mesh faces");
        double u = 0.03, v = 0.04;
        require(!panel.snap(u, v) && u == 0.03 && v == 0.04, "legacy snap must not move coordinates");
        require(!panel.saveJson()["snap"].toBool(), "saved object snap should stay off");
        panel.ui->gridSnapCheck->setChecked(true);
        require(panel.snap(u, v) && u == 0 && v == 0, "explicit grid snap should still work");
        panel.ui->gridSnapCheck->setChecked(false);
        panel.ui->selectAllObjectsButton->click();
        require(panel.selectedCommittedIndices() == QList<int>({0, 1}), "all lines should exclude auxiliary objects");
        panel.selectObjectRows({1});
        panel.ui->deleteObjectButton->click();
        require(panel.m_committedDocuments.size() == 3, "delete one object");
        panel.ui->undoButton->click();
        require(panel.m_committedDocuments.size() == 4, "undo delete");
        panel.ui->redoButton->click();
        require(panel.m_committedDocuments.size() == 3, "redo delete");
        panel.ui->selectAllObjectsButton->click();
        QTimer::singleShot(0, []
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        panel.ui->planarRegionButton->click();
        TopoDS_Shape faces;
        QString diagnostic;
        require(panel.buildApprovedMeshingFaces(faces, diagnostic), "cleaned square should form approved mesh faces");
        Sketch2DDialog restored;
        restored.loadJson(panel.saveJson());
        require(restored.buildApprovedMeshingFaces(faces, diagnostic), "confirmed faces must survive project restore");
        panel.selectObjectRows({0});
        panel.ui->deleteObjectButton->click();
        require(!panel.buildApprovedMeshingFaces(faces, diagnostic), "editing boundary must invalidate confirmed faces");
        panel.ui->undoButton->click();
        require(panel.buildApprovedMeshingFaces(faces, diagnostic), "undo boundary edit must restore confirmed faces");
        SketchDocument circle;
        circle.setCircle(0, 0, 3);
        const auto circleWire = SketchShapeBuilder::build(circle, panel.planeSettings());
        require(circleWire.ok && !TopExp_Explorer(circleWire.shape, TopAbs_FACE).More(), "drawn circle should remain a curve");
        require(GeometrySketch::SketchPlanarRegionBuilder::build({circle}, panel.planeSettings()).ok(),
                "explicit face generation should still support circles");
        std::cout << "PASS: single explicit face entry, legacy mode ignored, closed shapes stay curves, restore/invalidate/undo faces" << std::endl;
        std::cout << "PASS: expanded panel, legacy snap, optional grid snap, select/delete/undo/redo, approved 2D face" << std::endl;

        extendedChecks(app);

        const QString customerFile=QString::fromLocal8Bit(qgetenv("CAE_DXF_TEST_FILE"));
        if(customerFile.isEmpty())return 0;
        TopoDS_Shape dxf;
        QString message;
        require(ImportGeometryBuilder::readDxfShape(customerFile, dxf, message), "m1.dxf import failed");
        Sketch2DDialog imported;
        QString importDiagnostic;
        require(imported.importDxfFile(customerFile,importDiagnostic), "actual sketch DXF import");
        require(imported.m_committedDocuments.size()==130,"all original m1 objects should remain independent");
        int importedEdges=0;
        for(const auto& document:imported.m_committedDocuments)
            for(TopExp_Explorer edge(GeometrySketch::SketchCurveTools::shape(document),TopAbs_EDGE);edge.More();edge.Next())++importedEdges;
        // The original has 237 segment records; polyline handle 340 starts with a duplicate vertex.
        require(importedEdges==236,"preserve all 236 nonzero original line segments including polylines");
        QVector<DxfGeometryRecord> originalRecords;
        require(ImportGeometryBuilder::readDxfRecords(customerFile,originalRecords,importDiagnostic),"customer source records");
        require(std::any_of(originalRecords.begin(),originalRecords.end(),[](const DxfGeometryRecord& record){return record.layer==QStringLiteral("开挖收方断面");}),"preserve Chinese GBK layer names");
        const size_t importedCount=imported.m_committedDocuments.size();
        imported.ui->selectAllObjectsButton->click();
        require(imported.selectedCommittedIndices().size() == importedCount, "all DXF curves should be selected");
        const auto original = imported.m_committedDocuments.front().toJson();
        imported.selectObjectRows({0});
        imported.ui->deleteObjectButton->click();
        require(imported.m_committedDocuments.size() + 1 == importedCount, "DXF delete");
        imported.ui->undoButton->click();
        require(imported.m_committedDocuments.front().toJson() == original, "DXF undo must restore original coordinates");
        std::cout << "PASS: m1.dxf, editable objects=" << importedCount
                  << ", delete and coordinate-preserving undo" << std::endl;
        imported.resize(360, 650);
        imported.show();
        app.processEvents();
        imported.grab().save("../terrain_feature_preview/sketch-panel.png");
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "FAIL: " << ex.what() << std::endl;
        return 1;
    }
}
