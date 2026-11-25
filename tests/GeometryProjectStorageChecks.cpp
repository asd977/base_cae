#include <GeometryProjectStorage.h>
#include <SmeshShapeIndexMap.h>
#include <SurfaceDomainData.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <sstream>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir original, moved;
    TopoDS_Compound shape;BRep_Builder builder;builder.MakeCompound(shape);
    for (int i=0;i<16;++i) builder.Add(shape,BRepPrimAPI_MakeBox(gp_Pnt(i*2,0,0),1,1,1).Shape());
    std::ostringstream stream;BRepTools::Write(shape,stream);
    const QByteArray raw=QByteArray::fromStdString(stream.str());
    if (raw.size()<16384) return 1;
    QJsonArray items{QJsonObject{{"uuid","part"},{"cachedBrep",QString::fromUtf8(raw)}}};
    if (!GeometryProjectStorage::externalize(original.path(),items)) return 2;
    const auto item=items[0].toObject();
    const auto asset=item.value("cachedBrepAsset").toString();
    if (item.contains("cachedBrep") || !GeometryProjectStorage::safePath(asset)) return 3;
    // Moving a complete project must retain geometry without its source CAD.
    QDir(moved.path()).mkpath("geometry-data");
    if (!QFile::copy(QDir(original.path()).filePath(asset),QDir(moved.path()).filePath(asset))) return 4;
    QByteArray loaded;
    if (!GeometryProjectStorage::read(moved.path(),asset,loaded) || loaded!=raw) return 5;
    std::istringstream input(loaded.toStdString());TopoDS_Shape restored;
    BRepTools::Read(restored,input,builder);
    TopTools_IndexedMapOfShape solids;TopExp::MapShapes(restored,TopAbs_SOLID,solids);
    if (solids.Extent()!=16) return 6;
    if (GeometryProjectStorage::read(moved.path(),"../outside.brep.qz",loaded)) return 7;
    if (GeometryProjectStorage::read(original.path(),asset+"missing",loaded)) return 8;
    // Index manifests explicitly handle a different export order, no geometry scoring.
    const QString brep=QDir(moved.path()).filePath("input.brep");
    { QFile file(brep);if(!file.open(QIODevice::WriteOnly)||file.write(raw)!=raw.size())return 9; }
    { QFile file(brep+".shape-index.json");if(!file.open(QIODevice::WriteOnly))return 10;
      file.write(QJsonDocument(QJsonObject{{"format",1},{"3",QJsonArray{2,1}},{"count3",2}}).toJson()); }
    const auto map=SmeshShapeIndexMap::load(brep);
    if (!map.valid()||map.requireIndex(3,1)!=2||map.requireIndex(3,2)!=1||map.requireIndex(3,3)!=0) return 11;
    SurfaceDomainData::Definition domains;domains.faceDomains.fill(1,96);domains.materialGroups[1]=1;
    QString error;
    if (!SurfaceDomainData::write(brep,domains,error)) return 12;
    QFile domainFile(SurfaceDomainData::fileName(brep));
    if (!domainFile.open(QIODevice::ReadOnly)) return 13;
    if (QJsonDocument::fromJson(domainFile.readAll()).object().contains("geometrySha256")) return 14;
    SurfaceDomainData::Definition readDomains;
    if (!SurfaceDomainData::read(brep,96,readDomains,error)
        || SurfaceDomainData::read(brep,95,readDomains,error)) return 15;
    if (SurfaceDomainData::write(brep+"missing",domains,error)) return 16;
    std::cout<<"PASS: compressed CAD project move/reopen, invalid assets and explicit topology map\n";
    return 0;
}
