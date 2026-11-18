#pragma once
#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <cmath>

namespace ThermalLoadProtocol
{
// Native IIWL stores delta-T; expansion belongs to CONMAT(material,57).
// Reject conflicting coefficients on one material instead of silently picking
// one load page. This only changes the current analysis input copy.
inline bool apply(const QDir& input, QString* error)
{
    const auto fail=[&](const QString& text){if(error)*error=text;return false;};
    QFile alpha(input.filePath(QStringLiteral("THERMAL_ALPHA.DAT")));
    if(!alpha.exists()) return true;
    if(!alpha.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取变温荷载。"));
    const auto a=QString::fromLatin1(alpha.readAll()).split('\n');
    const auto tokens=[](const QString& s){return s.simplified().split(' ',Qt::SkipEmptyParts);};
    bool ok=false;const int count=tokens(a.value(0)).value(0).toInt(&ok);
    if(!ok||count<0||a.size()<count+1) return fail(QStringLiteral("变温荷载记录不完整。"));
    if(!count)return true;
    QMap<int,double> nodes;
    for(int i=1;i<=count;++i)
    {
        const auto t=tokens(a[i]);bool idOk=false,valueOk=false;
        const int id=t.value(0).toInt(&idOk);const double value=t.value(1).toDouble(&valueOk);
        if(!idOk||id<=0||!valueOk||!std::isfinite(value))return fail(QStringLiteral("变温荷载膨胀系数无效。"));
        nodes[id]=value;
    }
    QFile mesh(input.filePath(QStringLiteral("IIIN.DAT")));
    if(!mesh.open(QIODevice::ReadOnly))return fail(QStringLiteral("无法读取变温荷载网格。"));
    const auto lines=QString::fromLatin1(mesh.readAll()).split('\n');
    const auto header=tokens(lines.value(0));
    const int np=header.value(0).toInt(),ne=header.value(2).toInt(),width=header.value(3).toInt();
    if(np<=0||ne<=0||width<=0||lines.size()<np+ne+2)return fail(QStringLiteral("变温荷载网格不完整。"));
    QMap<int,double> materials;QSet<int> found;
    for(int e=0;e<ne;++e)
    {
        const auto t=tokens(lines[np+2+e]);const int material=t.value(width+2).toInt();
        if(material<=1)continue;
        for(int j=1;j<=width;++j)
        {
            const int id=t.value(j).toInt();if(!nodes.contains(id))continue;
            found.insert(id);const double value=nodes[id];
            if(materials.contains(material)&&materials[material]!=value)
                return fail(QStringLiteral("材料 %1 对应不同的热膨胀系数，请拆分材料分区。").arg(material));
            materials[material]=value;
        }
    }
    if(found.size()!=nodes.size())return fail(QStringLiteral("部分变温节点不属于有效材料单元。"));
    QFile file(input.filePath(QStringLiteral("IMAT.DAT")));
    if(!file.open(QIODevice::ReadOnly))return fail(QStringLiteral("无法读取变温材料参数。"));
    auto imat=QString::fromLatin1(file.readAll()).split('\n');file.close();
    const int ncon=tokens(imat.value(0)).value(1).toInt();
    if(ncon<57)return fail(QStringLiteral("材料参数缺少热膨胀系数项。"));
    QSet<int> applied;
    const QRegularExpression label(QStringLiteral("IMAT\\s*=\\s*(\\d+)"));
    for(int i=1;i<imat.size();++i)
    {
        auto matches=label.globalMatch(imat[i]);QList<int> columns;
        while(matches.hasNext())columns.append(matches.next().captured(1).toInt());
        if(columns.isEmpty())continue;
        const int row=i+57;
        if(row>=imat.size())return fail(QStringLiteral("变温材料参数表不完整。"));
        auto values=tokens(imat[row]);
        if(values.size()!=columns.size()+1||values.last().toInt()!=57)return fail(QStringLiteral("变温材料参数行格式错误。"));
        for(int j=0;j<columns.size();++j)if(materials.contains(columns[j]))
        {values[j]=QString::number(materials[columns[j]],'E',12);applied.insert(columns[j]);}
        imat[row]=values.join(' ');
        i+=ncon;
    }
    if(applied.size()!=materials.size())return fail(QStringLiteral("部分变温材料没有对应参数。"));
    QSaveFile out(input.filePath(QStringLiteral("IMAT.DAT")));
    const auto data=imat.join('\n').toLatin1();
    return (out.open(QIODevice::WriteOnly)&&out.write(data)==data.size()&&out.commit())
        ||fail(QStringLiteral("无法保存变温材料参数。"));
}
}
