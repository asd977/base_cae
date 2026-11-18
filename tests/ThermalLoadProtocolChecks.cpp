#include "../plugins/Base_Solver/ThermalLoadProtocol.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir temp;QDir dir(temp.path());
    auto write=[&](const QString& name,const QByteArray& bytes){QFile f(dir.filePath(name));return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    QByteArray mesh="8 3 1 8 5 1 0 4 2\n7 3 0 2 0 1 0 1\n";
    for(int i=1;i<=8;++i)mesh+="0 0 0 "+QByteArray::number(i)+"\n";
    mesh+="1 1 2 3 4 5 6 7 8 6 3 1 0 0\n";write("IIIN.DAT",mesh);
    QByteArray material="10 200 NMAT NCON\nCONMAT GROUP 1\n";
    for(int i=1;i<=10;++i)material+="IMAT="+QByteArray::number(i)+" ";material+="\n";
    for(int r=1;r<=200;++r){for(int i=1;i<=10;++i)material+="2.0 ";material+=QByteArray::number(r)+"\n";}
    write("IMAT.DAT",material);write("THERMAL_ALPHA.DAT","2 NODE_ALPHA\n1 1e-5\n8 1e-5\n");
    QString error;if(!ThermalLoadProtocol::apply(dir,&error))return 1;
    QFile result(dir.filePath("IMAT.DAT"));result.open(QIODevice::ReadOnly);const auto saved=result.readAll();result.close();
    const auto rows=QString::fromLatin1(saved).split('\n');const auto values=rows[59].simplified().split(' ');
    if(values[2].toDouble()!=1.e-5||values[1].toDouble()!=2.||values[3].toDouble()!=2.)return 2;
    write("THERMAL_ALPHA.DAT","2 NODE_ALPHA\n1 1e-5\n8 2e-5\n");
    if(ThermalLoadProtocol::apply(dir,&error)||error.isEmpty())return 3;
    result.open(QIODevice::ReadOnly);if(result.readAll()!=saved)return 4;result.close();
    write("THERMAL_ALPHA.DAT","1 NODE_ALPHA\n9 1e-5\n");if(ThermalLoadProtocol::apply(dir,&error))return 5;
    write("THERMAL_ALPHA.DAT","0 NODE_ALPHA\n");if(!ThermalLoadProtocol::apply(dir,&error))return 6;
    std::cout<<"PASS: native material coefficient row; conflicting/unknown node rejection; no mutation on failure\n";
}
