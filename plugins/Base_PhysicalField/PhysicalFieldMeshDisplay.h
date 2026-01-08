#ifndef PHYSICALFIELDMESHDISPLAY_H
#define PHYSICALFIELDMESHDISPLAY_H

#include <vtkSmartPointer.h>

class IiinGridModel;
class vtkUnstructuredGrid;

class PhysicalFieldMeshDisplay
{
public:
    static vtkSmartPointer<vtkUnstructuredGrid> buildGridFromModel(const IiinGridModel &model);
};

#endif // PHYSICALFIELDMESHDISPLAY_H
