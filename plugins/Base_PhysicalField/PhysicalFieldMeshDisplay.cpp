#include "PhysicalFieldMeshDisplay.h"

#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkIntArray.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include "IiinGridModel.h"

vtkSmartPointer<vtkUnstructuredGrid>
PhysicalFieldMeshDisplay::buildGridFromModel(const IiinGridModel &model)
{
    const auto &nodes = model.nodes();
    const auto &elems = model.elems();
    if (nodes.isEmpty() || elems.isEmpty())
        return nullptr;

    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(nodes.size());

    for (vtkIdType i = 0; i < static_cast<vtkIdType>(nodes.size()); ++i) {
        const Node &n = nodes[static_cast<int>(i)];
        points->SetPoint(i, n.x, n.y, n.z);
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);

    auto matypeArray = vtkSmartPointer<vtkIntArray>::New();
    matypeArray->SetName("MATYPE");
    matypeArray->SetNumberOfComponents(1);

    auto nodeIdArray = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArray->SetName("NodeId");
    nodeIdArray->SetNumberOfComponents(1);
    nodeIdArray->SetNumberOfTuples(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        nodeIdArray->SetValue(i, nodes[i].id);
    }
    grid->GetPointData()->AddArray(nodeIdArray);

    auto expectedNodeCountForKetype = [](int ketype) -> int {
        switch (ketype) {
        case 1:
        case 2:
        case 10:
        case 21:
            return 2;
        case 3:
        case 4:
        case 5:
        case 7:
        case 8:
        case 11:
        case 13:
        case 14:
            return 4;
        case 9:
        case 12:
            return 8;
        case 6:
        case 15:
            return 3;
        case 16:
            return 6;
        case 17:
            return 10;
        case 18:
            return 15;
        case 19:
            return 20;
        default:
            return 0;
        }
    };

    auto ketypeToVtkCellType = [](int ketype) -> int {
        switch (ketype) {
        case 1:
        case 2:
        case 10:
        case 21:
            return VTK_LINE;
        case 3:
        case 4:
        case 5:
        case 7:
        case 8:
        case 11:
        case 13:
        case 14:
            return VTK_QUAD;
        case 6:
        case 15:
            return VTK_TRIANGLE;
        case 9:
        case 12:
            return VTK_HEXAHEDRON;
        case 16:
            return VTK_WEDGE;
        case 17:
            return VTK_QUADRATIC_TETRA;
        case 18:
            return VTK_QUADRATIC_PYRAMID;
        case 19:
            return VTK_QUADRATIC_HEXAHEDRON;
        default:
            return -1;
        }
    };

    for (const Elem &e : elems) {
        int expectedNodes = expectedNodeCountForKetype(e.ketype);
        int vtkCellType = ketypeToVtkCellType(e.ketype);
        if (vtkCellType < 0)
            continue;

        if (e.nodeIds.size() < expectedNodes)
            continue;

        QVector<vtkIdType> cellPtIds;
        cellPtIds.reserve(expectedNodes);
        for (int i = 0; i < expectedNodes; ++i) {
            int nid = e.nodeIds[i];
            if (nid <= 0 || nid > nodes.size())
                continue;
            cellPtIds.push_back(nid - 1);
        }

        if (cellPtIds.size() != expectedNodes)
            continue;

        grid->InsertNextCell(vtkCellType,
                             cellPtIds.size(),
                             cellPtIds.data());
        matypeArray->InsertNextValue(e.matype);
    }

    grid->GetCellData()->AddArray(matypeArray);
    grid->GetCellData()->SetScalars(matypeArray);
    return grid;
}
