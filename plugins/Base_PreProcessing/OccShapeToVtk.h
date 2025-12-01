// OccShapeToVtk.h
#pragma once

#include <TopoDS_Shape.hxx>
#include <vtkSmartPointer.h>

class vtkPolyData;

namespace OCC
{
vtkSmartPointer<vtkPolyData> ShapeEdgesToVtkLines(const TopoDS_Shape& shape,
                                                  double deflection = 0.001);
vtkSmartPointer<vtkPolyData> ShapeToVtkPolyData(const TopoDS_Shape& shape,
                                                double deflection = 0.001);
void TriangulateOneShape(const TopoDS_Shape& shape, double factor);
}
