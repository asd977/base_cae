#include <QCoreApplication>
#include <QTemporaryDir>
#include "../plugins/Base_Solver/DamInputProtocol.h"
#include "../plugins/Base_Solver/SolverMeshProtocol.h"
#include <iostream>
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir tmp;QDir dir(tmp.path());QString error;
    if(DamInputProtocol::prepareMaterialSets(dir,true,&error))return 1;
    const QByteArray prefix="6 2 2 4 5 1 0 0 0\n5 2 0 2 0 1 0 0\n0 0 1\n1 0 2\n1 1 3\n0 1 4\n2 0 5\n2 1 6\n";
    auto write=[&](QByteArray b){QFile f(dir.filePath("IIIN.DAT"));return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();};
    write(prefix+"1 1 2 3 4 3 3 1 0 0\n2 2 5 6 3 3 3 2 0 0\n");
    if(DamInputProtocol::prepareMaterialSets(dir,true,&error))return 2;
    write(prefix+"1 1 2 3 4 3 3 1 0 0\n2 2 5 6 3 3 6 2 0 0\n");
    if(!DamInputProtocol::prepareMaterialSets(dir,true,&error))return 3;
    auto read=[&](QString name){QFile f(dir.filePath(name));f.open(QIODevice::ReadOnly);return f.readAll();};
    if(read("MA_ROCK.DAT")!="1\n3\n"||read("MA_DAMUP.DAT")!="1\n6\n")return 4;
    {QFile f(dir.filePath("KEATT_CON.DAT"));f.open(QIODevice::WriteOnly);f.write("1\nKETYPE NODE NFACE NFAJD\n3 4 4 2\n");}
    auto lines=QString::fromLatin1(read("IIIN.DAT")).split('\n');
    if(!SolverMeshProtocol::completeFaceDimensions(lines,dir))return 5;
    if(!lines[0].endsWith(" 4 2"))return 6;
    lines[8]="1 1 2 3 4 42 3 1 0 0";
    if(SolverMeshProtocol::completeFaceDimensions(lines,dir))return 7;
    std::cout<<"PASS: missing mesh and mixed rock/fill material rejected; native material sets generated\n";return 0;
}
