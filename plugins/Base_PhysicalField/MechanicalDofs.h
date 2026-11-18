#pragma once
#include <QSet>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkUnstructuredGrid.h>

inline QSet<int> planarBeamNodes(vtkUnstructuredGrid* grid)
{
    QSet<int> result;if(!grid)return result;
    auto* types=grid->GetCellData()->GetArray("KETYPE");
    if(!types)types=grid->GetCellData()->GetArray("ketype");
    auto* nodes=grid->GetPointData()->GetArray("node_id");
    if(!types||!nodes)return result;
    for(vtkIdType ci=0;ci<grid->GetNumberOfCells();++ci)
        if(int(types->GetTuple1(ci))==21)
        {
            auto* cell=grid->GetCell(ci);
            for(vtkIdType j=0;j<cell->GetNumberOfPoints();++j)result.insert(int(nodes->GetTuple1(cell->GetPointId(j))));
        }
    return result;
}
