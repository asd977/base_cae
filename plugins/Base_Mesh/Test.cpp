#include "Test.h"

#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkQuadraticHexahedron.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridGeometryFilter.h>

#include <iostream>

static void printPoint(const char* prefix, vtkIdType id, const double p[3])
{
    std::cout << prefix << " " << id << " : ("
              << p[0] << ", " << p[1] << ", " << p[2] << ")\n";
}

static void dumpUG(vtkUnstructuredGrid* ug, const char* title)
{
    if (!ug)
        return;

    std::cout << "\n========== " << title << " ==========\n";
    std::cout << "NumberOfPoints = " << ug->GetNumberOfPoints() << "\n";
    std::cout << "NumberOfCells  = " << ug->GetNumberOfCells() << "\n";

    for (vtkIdType i = 0; i < ug->GetNumberOfPoints(); ++i)
    {
        double p[3];
        ug->GetPoint(i, p);
        printPoint("point", i, p);
    }

    for (vtkIdType cid = 0; cid < ug->GetNumberOfCells(); ++cid)
    {
        vtkCell* cell = ug->GetCell(cid);
        std::cout << "\ncell " << cid
                  << ", type = " << (cell ? cell->GetCellType() : -1)
                  << ", npts = " << (cell ? cell->GetNumberOfPoints() : -1)
                  << "\n";

        if (!cell)
            continue;

        vtkIdList* ids = cell->GetPointIds();
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
        {
            vtkIdType pid = ids->GetId(i);
            double p[3];
            ug->GetPoint(pid, p);
            std::cout << "  pid[" << i << "] = " << pid
                      << " -> (" << p[0] << ", " << p[1] << ", " << p[2] << ")\n";
        }
    }
}

static void dumpBoundary(vtkUnstructuredGrid* ug,
                         const char* cellArrName,
                         const char* pointArrName)
{
    if (!ug)
        return;

    std::cout << "\n========== OUTPUT BOUNDARY UG ==========\n";
    std::cout << "NumberOfPoints = " << ug->GetNumberOfPoints() << "\n";
    std::cout << "NumberOfCells  = " << ug->GetNumberOfCells() << "\n";

    auto* origCellArr = vtkIdTypeArray::SafeDownCast(
        ug->GetCellData()->GetArray(cellArrName));
    auto* origPointArr = vtkIdTypeArray::SafeDownCast(
        ug->GetPointData()->GetArray(pointArrName));

    std::cout << "\nCell array [" << cellArrName << "] = " << origCellArr << "\n";
    if (origCellArr)
    {
        std::cout << "  tuples = " << origCellArr->GetNumberOfTuples()
        << ", components = " << origCellArr->GetNumberOfComponents()
        << "\n";
        for (vtkIdType i = 0; i < origCellArr->GetNumberOfTuples(); ++i)
        {
            std::cout << "  " << cellArrName << "[" << i << "] = "
                      << origCellArr->GetValue(i) << "\n";
        }
    }

    std::cout << "\nPoint array [" << pointArrName << "] = " << origPointArr << "\n";
    if (origPointArr)
    {
        std::cout << "  tuples = " << origPointArr->GetNumberOfTuples()
        << ", components = " << origPointArr->GetNumberOfComponents()
        << "\n";
        for (vtkIdType i = 0; i < origPointArr->GetNumberOfTuples(); ++i)
        {
            std::cout << "  " << pointArrName << "[" << i << "] = "
                      << origPointArr->GetValue(i) << "\n";
        }
    }

    std::cout << "\n---- boundary points ----\n";
    for (vtkIdType i = 0; i < ug->GetNumberOfPoints(); ++i)
    {
        double p[3];
        ug->GetPoint(i, p);

        std::cout << "boundary point " << i
                  << " : (" << p[0] << ", " << p[1] << ", " << p[2] << ")";
        if (origPointArr && i < origPointArr->GetNumberOfTuples())
        {
            std::cout << ", origPointId = " << origPointArr->GetValue(i);
        }
        std::cout << "\n";
    }

    std::cout << "\n---- boundary cells ----\n";
    for (vtkIdType cid = 0; cid < ug->GetNumberOfCells(); ++cid)
    {
        vtkCell* cell = ug->GetCell(cid);
        std::cout << "boundary cell " << cid
                  << ", type = " << (cell ? cell->GetCellType() : -1)
                  << ", npts = " << (cell ? cell->GetNumberOfPoints() : -1);

        if (origCellArr && cid < origCellArr->GetNumberOfTuples())
        {
            std::cout << ", origCellId = " << origCellArr->GetValue(cid);
        }
        std::cout << "\n";

        if (!cell)
            continue;

        vtkIdList* ids = cell->GetPointIds();
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
        {
            vtkIdType pid = ids->GetId(i);
            double p[3];
            ug->GetPoint(pid, p);

            std::cout << "  local[" << i << "]"
                      << " -> boundaryPid = " << pid
                      << " -> (" << p[0] << ", " << p[1] << ", " << p[2] << ")";

            if (origPointArr && pid < origPointArr->GetNumberOfTuples())
            {
                std::cout << ", origPointId = " << origPointArr->GetValue(pid);
            }
            std::cout << "\n";
        }
    }
}

Test::Test()
{
    // vtkNew<vtkPoints> points;

    //        // 8 corner points
    // points->InsertNextPoint(0.0, 0.0, 0.0); // 0
    // points->InsertNextPoint(1.0, 0.0, 0.0); // 1
    // points->InsertNextPoint(1.0, 1.0, 0.0); // 2
    // points->InsertNextPoint(0.0, 1.0, 0.0); // 3
    // points->InsertNextPoint(0.0, 0.0, 1.0); // 4
    // points->InsertNextPoint(1.0, 0.0, 1.0); // 5
    // points->InsertNextPoint(1.0, 1.0, 1.0); // 6
    // points->InsertNextPoint(0.0, 1.0, 1.0); // 7

    //        // 12 mid-edge points
    // points->InsertNextPoint(0.5, 0.0, 0.0); // 8
    // points->InsertNextPoint(1.0, 0.5, 0.0); // 9
    // points->InsertNextPoint(0.5, 1.0, 0.0); // 10
    // points->InsertNextPoint(0.0, 0.5, 0.0); // 11
    // points->InsertNextPoint(0.5, 0.0, 1.0); // 12
    // points->InsertNextPoint(1.0, 0.5, 1.0); // 13
    // points->InsertNextPoint(0.5, 1.0, 1.0); // 14
    // points->InsertNextPoint(0.0, 0.5, 1.0); // 15
    // points->InsertNextPoint(0.0, 0.0, 0.5); // 16
    // points->InsertNextPoint(1.0, 0.0, 0.5); // 17
    // points->InsertNextPoint(1.0, 1.0, 0.5); // 18
    // points->InsertNextPoint(0.0, 1.0, 0.5); // 19

    // vtkNew<vtkQuadraticHexahedron> qhex;
    // for (int i = 0; i < 20; ++i)
    // {
    //     qhex->GetPointIds()->SetId(i, i);
    // }

    // vtkNew<vtkUnstructuredGrid> ug;
    // ug->SetPoints(points);
    // ug->InsertNextCell(qhex->GetCellType(), qhex->GetPointIds());

    // dumpUG(ug, "INPUT UNSTRUCTURED GRID");

    // vtkNew<vtkUnstructuredGridGeometryFilter> surf;
    // surf->SetInputData(ug);
    // surf->SetOriginalCellIdsName("__surf_orig_cell_id__");
    // surf->SetOriginalPointIdsName("__surf_orig_point_id__");
    // surf->PassThroughCellIdsOn();
    // surf->PassThroughPointIdsOn();
    // surf->MergingOff();
    // surf->Update();

    // vtkNew<vtkUnstructuredGrid> boundary;
    // boundary->ShallowCopy(surf->GetOutput());

    // dumpBoundary(boundary, "__surf_orig_cell_id__", "__surf_orig_point_id__");

    // vtkNew<vtkDataSetMapper> mapper;
    // mapper->SetInputData(boundary);
    // mapper->ScalarVisibilityOff();

    // vtkNew<vtkActor> actor;
    // actor->SetMapper(mapper);
    // actor->GetProperty()->SetRepresentationToSurface();
    // actor->GetProperty()->EdgeVisibilityOn();
    // actor->GetProperty()->SetLineWidth(1.0);

    // vtkNew<vtkRenderer> ren;
    // ren->AddActor(actor);
    // ren->SetBackground(0.2, 0.3, 0.4);

    // vtkNew<vtkRenderWindow> win;
    // win->AddRenderer(ren);
    // win->SetSize(900, 700);

    // vtkNew<vtkRenderWindowInteractor> iren;
    // iren->SetRenderWindow(win);

    // win->Render();
    // iren->Start();

}
