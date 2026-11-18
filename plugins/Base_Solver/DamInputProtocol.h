#pragma once
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace DamInputProtocol
{
inline bool prepareMaterialSets(const QDir& input,bool homogeneous,QString* error)
{
    auto fail=[&](QString s){if(error)*error=s;return false;};
    QFile mesh(input.filePath(QStringLiteral("IIIN.DAT")));
    if(!mesh.open(QIODevice::ReadOnly))return fail(QStringLiteral("缺少填筑网格 IIIN.DAT。"));
    const auto lines=QString::fromLatin1(mesh.readAll()).split('\n');
    auto fields=[](QString s){return s.split(QRegularExpression(QStringLiteral("\\s+")),Qt::SkipEmptyParts);};
    if(lines.size()<2)return fail(QStringLiteral("填筑网格头无效。"));
    const auto h=fields(lines[0]);if(h.size()<5)return fail(QStringLiteral("填筑网格头无效。"));
    const int start=2+h[0].toInt(),count=h[2].toInt(),width=h[3].toInt();
    if(start<2||count<1||width<2||lines.size()<start+count)return fail(QStringLiteral("填筑网格不完整。"));
    QSet<int> rock,fill;
    for(int i=0;i<count;++i)
    {
        const auto row=fields(lines[start+i]);if(row.size()<width+4)return fail(QStringLiteral("填筑单元属性不完整。"));
        const int type=row[width+1].toInt(),material=row[width+2].toInt(),layer=row[width+3].toInt();
        if(type==1||material<=1)continue;
        if(layer<1)return fail(QStringLiteral("填筑计算的实体层号必须为正数。"));
        (layer==1?rock:fill).insert(material);
    }
    if(rock.isEmpty())return fail(QStringLiteral("缺少第1层基岩材料。"));
    if(!(rock&fill).isEmpty())return fail(QStringLiteral("基岩和填筑层共用了材料，请分别设置材料。"));
    auto save=[&](QString name,QSet<int> values){
        if(QFile::exists(input.filePath(name)))return true;
        auto ids=values.values();std::sort(ids.begin(),ids.end());
        QByteArray bytes=QByteArray::number(ids.size())+"\n";
        for(int id:ids)bytes+=QByteArray::number(id)+"\n";
        QSaveFile f(input.filePath(name));return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
    };
    // AUTOPACK releases XY/JM/MAT before trying to synthesize these missing
    // files. Export the same layer-based sets before entering that legacy path.
    if(!save(QStringLiteral("MA_ROCK.DAT"),rock)||
       (homogeneous&&!save(QStringLiteral("MA_DAMUP.DAT"),fill)))
        return fail(QStringLiteral("无法写入填筑材料分组文件。"));
    return true;
}
}
