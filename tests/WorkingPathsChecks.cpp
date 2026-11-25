#include <MeshWorkingPaths.h>
#include "../plugins/Base_Mesh/core/MeshSnapshotState.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <vtkPoints.h>
#include <vtkIntArray.h>
#include <vtkFieldData.h>
#include <iostream>

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir first,second;
    auto& variables=PublicVariables::instance();
    variables.set(PublicConstants::kProjectDirectoryKey,first.path(),"project_dir");
    const QString a=MeshWorkingPaths::root();
    if(!a.startsWith(first.path()+"/.meshing-cache/")||!MeshWorkingPaths::prepare())return 1;
    QFile scratch(QDir(a).filePath("scratch.txt"));if(!scratch.open(QIODevice::WriteOnly))return 2;
    scratch.write("temporary");scratch.close();
    QFile durable(first.filePath("keep.txt"));if(!durable.open(QIODevice::WriteOnly))return 3;durable.write("keep");durable.close();
    variables.set(PublicConstants::kProjectDirectoryKey,second.path(),"project_dir");
    const QString b=MeshWorkingPaths::root();
    if(a==b||!MeshWorkingPaths::prepare()||!MeshWorkingPaths::cleanup()||!QFileInfo::exists(a))return 4;
    variables.set(PublicConstants::kProjectDirectoryKey,first.path(),"project_dir");
    if(!MeshWorkingPaths::cleanup()||QFileInfo::exists(a)||!QFileInfo::exists(durable.fileName()))return 5;

    auto grid=vtkSmartPointer<vtkUnstructuredGrid>::New();auto points=vtkSmartPointer<vtkPoints>::New();
    points->InsertNextPoint(0,0,0);points->InsertNextPoint(1,0,0);points->InsertNextPoint(0,1,0);points->InsertNextPoint(0,0,1);
    grid->SetPoints(points);vtkIdType ids[4]{0,1,2,3};grid->InsertNextCell(VTK_TETRA,4,ids);
    const QString snapshot=first.filePath("snapshot.dat");
    {QFile file(snapshot);if(!file.open(QIODevice::WriteOnly))return 6;file.write("snapshot");}
    MeshSnapshotState saved;saved.remember(grid,snapshot);if(!saved.matches(grid,snapshot))return 7;
    grid->GetCells()->GetConnectivityArray()->Modified();if(saved.matches(grid,snapshot))return 8;
    saved.remember(grid,snapshot);
    auto field=vtkSmartPointer<vtkIntArray>::New();field->SetName("state");field->InsertNextValue(1);grid->GetFieldData()->AddArray(field);
    if(saved.matches(grid,snapshot))return 9;saved.remember(grid,snapshot);
    {QFile file(snapshot);if(!file.open(QIODevice::Append))return 10;file.write("external change");}
    if(saved.matches(grid,snapshot))return 11;
    std::cout<<"PASS: project/process scratch isolation, owned cleanup and snapshot invalidation\n";
    return 0;
}
