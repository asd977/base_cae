#pragma once

#include <QString>
#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;

namespace PostTecplotUtils
{
vtkSmartPointer<vtkUnstructuredGrid> readGrid(const QString& fileName,
                                              QString* errorMessage = nullptr);
}
