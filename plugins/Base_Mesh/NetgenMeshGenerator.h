#pragma once

#include <vtkSmartPointer.h>

#include <QString>

class vtkUnstructuredGrid;

struct NetgenMeshingRequest
{
    QString inputGeometryPath;
    QString volPath;
    QString vtkPath;
    QString nasPath;

    int dimension = 3;
    int elemType = 3;
    double lenMin = 0.5;
    double lenMax = 5.0;
    double sizeFactor = 1.0;
    int elementOrder = 1;
    int smoothing = 5;

    bool fixShape = true;
    double tolerance = 1e-6;

    bool exportVol = true;
    bool exportVtk = true;
    bool exportNas = true;
};

struct NetgenMeshingResult
{
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    int pointCount = 0;
    int surfaceElementCount = 0;
    int volumeElementCount = 0;
    int regionCount = 0;

    // Hex8 (Tet split) quality summary. For non-Hex8 meshes these keep
    // their default values.
    int invalidElementCount = 0;
    double minScaledJacobian = 1.0;
};

class NetgenMeshGenerator
{
  public:
    static bool generate(const NetgenMeshingRequest& request, NetgenMeshingResult* result,
                         QString* errMsg = nullptr);

    static bool writeNastran(vtkUnstructuredGrid* grid, const QString& fileName,
                             QString* errMsg = nullptr);
};
