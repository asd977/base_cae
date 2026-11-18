#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include "../plugins/Base_Solver/SolverDirectoryProtocol.h"
#include <iostream>

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir root;
    if(!root.isValid())return 1;
    const auto primary=root.filePath("D-IIIDIR.DAT");
    const auto mirror=root.filePath("C-IIIDIR.DAT");
    const QByteArray content("'C:\\project\\case\\'\n'1'\n'9.81 0 1'\n");
    if(!SolverDirectoryProtocol::writeMirrored(primary,mirror,content))return 2;
    auto read=[](QString p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
    if(read(primary)!=content||read(mirror)!=content)return 3;
    QString error;
    if(SolverDirectoryProtocol::writeMirrored(primary,root.filePath("missing/IIIDIR.DAT"),"changed",&error))return 4;
    if(!error.contains(root.filePath("missing/IIIDIR.DAT")))return 8;
    if(read(primary)!=content||read(mirror)!=content)return 5;
    if(!SolverDirectoryProtocol::writeMirrored(primary,mirror,"next case"))return 6;
    if(read(primary)!="next case"||read(mirror)!="next case")return 7;
    const QDir runtime(root.filePath("runtime"));
    if(SolverDirectoryProtocol::validateRuntime(runtime,&error)||!error.contains("ErrShow.exe"))return 9;
    for(const auto& name:{"C08_ErrShow/DEBUG/ErrShow.exe","KEATT_CON.DAT","KEATT_VAR.DAT","KONG.DAT","C02_TABLE/AT0128.DAT"})
    {
        QFile file(runtime.filePath(name));QDir().mkpath(QFileInfo(file).absolutePath());
        if(!file.open(QIODevice::WriteOnly))return 10;file.write("test runtime");
    }
    if(!SolverDirectoryProtocol::validateRuntime(runtime,&error))return 11;
    std::cout<<"PASS: both directory files are created, synchronized, and preserved when staging fails\n";
    return 0;
}
