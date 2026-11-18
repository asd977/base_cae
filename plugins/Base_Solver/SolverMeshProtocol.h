#pragma once
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QMap>

namespace SolverMeshProtocol
{
inline bool completeFaceDimensions(QStringList& lines,const QDir& runtime)
{
    auto split=[](QString s){return s.split(QRegularExpression(QStringLiteral("\\s+")),Qt::SkipEmptyParts);};
    if(lines.size()<2)return false;
    auto header=split(lines[0]);if(header.size()<9)return false;
    const int start=2+header[0].toInt(),count=header[2].toInt(),width=header[3].toInt();
    if(start<2||count<1||width<1||lines.size()<start+count)return false;
    QSet<int> types;
    for(int i=0;i<count;++i){const auto row=split(lines[start+i]);if(row.size()<=width+1)return false;types.insert(row[width+1].toInt());}
    QFile table(runtime.filePath(QStringLiteral("KEATT_CON.DAT")));
    if(!table.open(QIODevice::ReadOnly))return false;
    const auto rows=QString::fromLatin1(table.readAll()).split('\n');
    QMap<int,QPair<int,int>> dimensions;
    for(int i=2;i<rows.size();++i){const auto row=split(rows[i]);if(row.size()>=4)dimensions.insert(row[0].toInt(),{row[2].toInt(),row[3].toInt()});}
    int faces=0,nodes=0;
    for(int type:types){if(!dimensions.contains(type))return false;faces=qMax(faces,dimensions[type].first);nodes=qMax(nodes,dimensions[type].second);}
    // INIT_IIWL uses CDN/CDE directly for allocation. Zero placeholders cause
    // zero-width face arrays even though IIJX contains valid node numbers.
    header[7]=QString::number(faces);header[8]=QString::number(nodes);
    lines[0]=header.join(' ');return true;
}
}
