#pragma once

#include <vtkSmartPointer.h>

#include <QString>
#include <QVector>
#include <QVector3D>

class vtkUnstructuredGrid;

struct NetgenLocalSizeControl
{
    int dimension = 3;
    int entityId = 0;
    QVector3D center;
    double characteristicLength = 0.0;
    double targetSize = 0.0;
};

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
    double fineness = 0.5;
    double sizeFactor = 1.0; // legacy compatibility; no longer multiplies maxh
    int elementOrder = 1;
    int smoothing = 5;
    QVector<NetgenLocalSizeControl> localSizeControls;

    bool fixShape = true;
    double tolerance = 1e-6;

    bool exportVol = true;
    bool exportVtk = true;
    bool exportNas = true;

    // 在交给 Netgen 前对相邻面/体执行 OCC 通用分割，使材料分区共享同一套拓扑边界。
    bool conformalizeRegions = true;
};

struct NetgenMeshingResult
{
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    int pointCount = 0;
    int surfaceElementCount = 0;
    int volumeElementCount = 0;
    int regionCount = 0;
    int appliedLocalControlCount = 0;
    int localRestrictionPointCount = 0;
    int sourceRegionCount = 0;
    int conformalRegionCount = 0;
    int sharedInterfaceCount = 0;
    bool conformalized = false;

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
