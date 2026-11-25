#pragma once

#include <vtkCleanPolyData.h>
#include <vtkFeatureEdges.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

struct SlopeRegionDisplay
{
    vtkSmartPointer<vtkPolyData> fill;
    vtkSmartPointer<vtkPolyData> boundary;
};

inline SlopeRegionDisplay makeSlopeRegionDisplay(vtkPolyData* selectedCells)
{
    // Cutters may give neighbouring triangles separate IDs for coincident
    // vertices. Weld those IDs before extracting edges, otherwise every
    // triangle is incorrectly outlined as an isolated region.
    auto clean = vtkSmartPointer<vtkCleanPolyData>::New();
    clean->SetInputData(selectedCells);
    clean->PointMergingOn();
    clean->SetTolerance(0.0);
    clean->SetOutputPointsPrecision(vtkAlgorithm::DOUBLE_PRECISION);
    clean->Update();
    SlopeRegionDisplay result;
    result.fill = vtkSmartPointer<vtkPolyData>::New();
    result.fill->ShallowCopy(clean->GetOutput());

    auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
    edges->SetInputData(result.fill);
    edges->BoundaryEdgesOn();
    edges->FeatureEdgesOff();
    edges->ManifoldEdgesOff();
    edges->NonManifoldEdgesOff();
    edges->ColoringOff();
    edges->Update();
    result.boundary = vtkSmartPointer<vtkPolyData>::New();
    result.boundary->ShallowCopy(edges->GetOutput());
    return result;
}
