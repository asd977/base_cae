#include "../plugins/Base_Solver/CaveReinforcementFieldPage.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* message)
{
    if(!value)throw std::runtime_error(message);
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try {
        QTemporaryDir project;check(project.isValid(),"temporary reinforcement directory");
        QDir dir(project.path());
        vtkNew<vtkUnstructuredGrid> grid;vtkNew<vtkPoints> points;
        for(const auto& p:{std::array<double,3>{0,0,0},{1,0,0},{0,1,0},{0,0,1}})
            points->InsertNextPoint(p.data());
        grid->SetPoints(points);vtkIdType solid[4]={0,1,2,3},bar[2]={0,1};
        grid->InsertNextCell(VTK_TETRA,4,solid);grid->InsertNextCell(VTK_LINE,2,bar);
        vtkNew<vtkIntArray> materials;materials->SetName("MATYPE");
        materials->InsertNextValue(2);materials->InsertNextValue(3);grid->GetCellData()->AddArray(materials);
        CaveReinforcementFieldPage page;page.loadJson({{"mode",1}});
        for(double area:{0.002,2.0}) {
            QByteArray input;
            QTextStream out(&input,QIODevice::WriteOnly);out<<"3 100\nmaterials\nparameters\n";
            for(int parameter=1;parameter<=100;++parameter) {
                const double value=parameter==8?area:(parameter==37||parameter==38?10.0:0.0);
                out<<"0 0 "<<value<<' '<<parameter<<'\n';
            }
            out.flush();
            QFile imat(dir.filePath("IMAT.DAT"));check(imat.open(QIODevice::WriteOnly),"write material fixture");
            imat.write(input);imat.close();
            QString error;
            check(page.exportFieldFiles(dir.path(),grid,3,{{0,1},{1,-1}},{},&error),qPrintable(error));
            check(error.isEmpty(),"no area-limit error");
            for(const QString name:{"ALFAIE.DAT","ALFAJD.DAT"}) {
                QFile output(dir.filePath(name));check(output.open(QIODevice::ReadOnly)&&!output.readAll().isEmpty(),"reinforcement field written");
            }
            check(imat.open(QIODevice::ReadOnly)&&imat.readAll()==input,"material parameters remain unchanged");
        }
        std::cout<<"PASS: reinforcement export accepts small and large auxiliary-bar areas without changing material parameters\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
