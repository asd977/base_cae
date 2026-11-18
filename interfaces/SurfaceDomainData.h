#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QVector>

// Explicit geometry domain ownership accompanies the temporary BREP. Domain
// identity and material identity are separate: contour bands share a default
// material group, while existing material faces keep independent groups.
namespace SurfaceDomainData
{
struct Definition
{
    QVector<int> faceDomains;
    QMap<int,int> materialGroups;
    QMap<int,QString> names;
};
inline QString fileName(const QString& geometry){return geometry+QStringLiteral(".domains.json");}
inline QByteArray digest(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};
    return hash.result().toHex();
}
inline bool write(const QString& geometry,const Definition& data,QString& error)
{
    const auto hash=digest(geometry);if(hash.isEmpty()){error=QStringLiteral("无法校验网格几何文件。");return false;}
    QJsonArray faces,domains;for(int id:data.faceDomains)faces.append(id);
    for(auto it=data.materialGroups.cbegin();it!=data.materialGroups.cend();++it)
        domains.append(QJsonObject{{"id",it.key()},{"materialGroup",it.value()},{"name",data.names.value(it.key())}});
    QSaveFile file(fileName(geometry));const auto bytes=QJsonDocument(QJsonObject{
        {"schema","GeometrySurfaceDomains.v1"},{"geometrySha256",QString::fromLatin1(hash)},
        {"faceDomains",faces},{"domains",domains}}).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
    {error=QStringLiteral("面域归属文件写入失败：%1").arg(file.errorString());return false;}
    return true;
}
inline bool read(const QString& geometry,int faceCount,Definition& data,QString& error)
{
    data={};QFile file(fileName(geometry));if(!file.exists())return true;
    if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024)
    {error=QStringLiteral("无法读取面域归属文件。");return false;}
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);const auto obj=doc.object();
    if(parse.error!=QJsonParseError::NoError||obj["schema"].toString()!="GeometrySurfaceDomains.v1"||
       obj["geometrySha256"].toString().toLatin1()!=digest(geometry)||obj["faceDomains"].toArray().size()!=faceCount)
    {error=QStringLiteral("面域归属与当前几何不匹配，请重新准备网格几何。");return false;}
    for(const auto& value:obj["domains"].toArray())
    {
        const auto domain=value.toObject();const int id=domain["id"].toInt(),group=domain["materialGroup"].toInt();
        if(id<=0||group<=0||data.materialGroups.contains(id))
        {error=QStringLiteral("面域编号或材料分组无效。");return false;}
        data.materialGroups[id]=group;data.names[id]=domain["name"].toString();
    }
    QSet<int> used;
    for(const auto& face:obj["faceDomains"].toArray())
    {
        const int id=face.toInt();if(!data.materialGroups.contains(id)){error=QStringLiteral("部分几何面缺少面域归属。");return false;}
        data.faceDomains.append(id);used.insert(id);
    }
    if(used.size()!=data.materialGroups.size()){error=QStringLiteral("面域归属文件包含空域。");return false;}
    return true;
}
}
