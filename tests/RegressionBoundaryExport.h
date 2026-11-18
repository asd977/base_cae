#pragma once
#include <services/MeshModuleControllerService.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkUnstructuredGrid.h>

inline bool exportRegressionBoundaryFaces(MeshModuleControllerService* mesh,const QString& project,QString* error)
{
    vtkUnstructuredGrid* grid=mesh->getVtkUnstructuredGridSolid();
    if(!grid)grid=mesh->getVtkUnstructuredGrid();
    if(!grid||grid->GetNumberOfCells()==0){*error="empty mesh";return false;}
    auto* nodes=grid->GetPointData()->GetArray("node_id");
    auto* elements=grid->GetCellData()->GetArray("elem_id");
    double bounds[6];grid->GetBounds(bounds);
    const int vertical=mesh->verticalAxis()-1;
    QVector<QString> rows;int width=0;
    for(vtkIdType ci=0;ci<grid->GetNumberOfCells();++ci)
    {
        auto* cell=grid->GetCell(ci);if(!cell||cell->GetCellDimension()!=3)continue;
        for(int fi=0;fi<cell->GetNumberOfFaces();++fi)
        {
            auto* face=cell->GetFace(fi);const int count=face->GetNumberOfPoints();
            bool boundary=false;
            for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side)
            {
                if(axis==vertical&&side==1)continue; // free top surface
                bool plane=true;
                for(int j=0;j<count;++j){double p[3];grid->GetPoint(face->GetPointId(j),p);plane&=std::abs(p[axis]-bounds[2*axis+side])<1.e-7;}
                boundary|=plane;
            }
            if(!boundary)continue;
            if(count!=4&&count!=8){*error="regression exporter requires quadrilateral faces";return false;}
            if(width&&width!=count){*error="mixed face orders require separate native sets";return false;}width=count;
            const QVector<int> order=count==8?QVector<int>{0,4,1,5,2,6,3,7}:QVector<int>{0,1,2,3};
            QString row=QString("%1 %2 0").arg(rows.size()+1).arg(elements?int(elements->GetTuple1(ci)):int(ci+1));
            for(int local:order){const auto p=face->GetPointId(local);row+=QString(" %1").arg(nodes?int(nodes->GetTuple1(p)):int(p+1));}
            rows.push_back(row+" 0\n");
        }
    }
    if(rows.isEmpty()){*error="no exterior faces";return false;}
    const QString path=QDir(project).filePath(QStringLiteral("工程数据/计算输入/FA_DYNA.DAT"));
    QSaveFile file(path);const QByteArray data=(QString("%1 %2 NJX MAKOD\nKJX KE1 KE2 JD... JAT\n").arg(rows.size()).arg(width+2)+QStringList(rows.begin(),rows.end()).join("")).toLatin1();
    if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()){*error=file.errorString();return false;}
    std::cout<<"EXPORTED_BOUNDARY faces="<<rows.size()<<" nodes_per_face="<<width<<std::endl;return true;
}
