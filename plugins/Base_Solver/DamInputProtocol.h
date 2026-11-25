#pragma once
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace DamInputProtocol
{
inline bool prepareMaterialSets(const QDir& input, int damType,
                                const QSet<int>& plate, const QSet<int>& auxiliary,
                                const QSet<int>& core, QString* error)
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
    if (damType == 1)
    {
        if (plate.isEmpty()) return fail(QStringLiteral("请在区域用途中指定面板材料。"));
        if (!(plate & auxiliary).isEmpty()) return fail(QStringLiteral("面板与附属结构不能共用材料号，请分别分配材料。"));
        if (!(plate - fill).isEmpty() || !(auxiliary - fill).isEmpty())
            return fail(QStringLiteral("面板及附属结构必须使用现有填筑单元的材料，不能与基岩共用材料号。"));
        fill.subtract(plate);
        fill.subtract(auxiliary);
        if (fill.isEmpty()) return fail(QStringLiteral("请指定面板以外的坝体填筑材料。"));
    }
    else if (damType == 2 || damType == 3)
    {
        if (core.isEmpty()) return fail(QStringLiteral("请在区域用途中指定防渗心墙材料。"));
        if (!(core - fill).isEmpty())
            return fail(QStringLiteral("防渗心墙必须使用现有填筑单元的材料，不能与基岩共用材料号。"));
        QSet<int> shell = fill;
        shell.subtract(core);
        if (shell.isEmpty()) return fail(QStringLiteral("请指定心墙以外的坝壳填筑材料。"));
    }
    auto save=[&](QString name,QSet<int> values){
        auto ids=values.values();std::sort(ids.begin(),ids.end());
        QByteArray bytes=QByteArray::number(ids.size())+"\n";
        for(int id:ids)bytes+=QByteArray::number(id)+"\n";
        QSaveFile f(input.filePath(name));return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
    };
    if (!save(QStringLiteral("MA_ROCK.DAT"), rock))
        return fail(QStringLiteral("无法写入基岩材料分组文件。"));
    if (damType == 1)
    {
        if (!save(QStringLiteral("MA_DAMUP.DAT"), fill) ||
            !save(QStringLiteral("MA_PLATE.DAT"), plate) ||
            !save(QStringLiteral("MA_YA.DAT"), auxiliary))
            return fail(QStringLiteral("无法写入面板坝材料分组文件。"));
    }
    else if (damType == 2 || damType == 3)
    {
        if (!save(QStringLiteral("MA_CORE.DAT"), core))
            return fail(QStringLiteral("无法写入心墙材料分组文件。"));
        QFile::remove(input.filePath(QStringLiteral("MA_DAMUP.DAT")));
        QFile::remove(input.filePath(QStringLiteral("MA_DAMDW.DAT")));
    }
    else
    {
        if (!save(QStringLiteral("MA_DAMUP.DAT"), fill))
            return fail(QStringLiteral("无法写入填筑材料分组文件。"));
    }
    return true;
}

inline bool prepareMaterialSets(const QDir& input, int damType,
                                const QSet<int>& plate, const QSet<int>& auxiliary,
                                QString* error)
{
    return prepareMaterialSets(input, damType, plate, auxiliary, {}, error);
}

inline bool prepareMaterialSets(const QDir& input, bool homogeneous, QString* error)
{
    // Retain the original homogeneous-dam entry point for existing callers.
    return prepareMaterialSets(input, homogeneous ? 4 : 1, {}, {}, {}, error);
}
}
