#pragma once

#include <vtkSmartPointer.h>

#include <QString>
#include <QVector>
#include <QVector3D>
#include <functional>
#include <SmeshWorkflowData.h>

class vtkUnstructuredGrid;

struct SmeshLocalSizeControl
{
    int dimension = 3;
    int entityId = 0;
    double targetSize = 0.0;
};

struct SmeshMeshingRequest
{
    QString inputGeometryPath;
    QString vtkPath;
    QString nasPath;

    int dimension = 3;
    int elemType = 3;
    double lenMin = 0.5;
    double lenMax = 5.0;
    double fineness = 0.5;
    int nativeFineness = -1; // -1 reads legacy 0..1 fineness; 0..4 presets, 5 custom.
    double growthRate = 0.3;
    double segmentsPerEdge = 1.0;
    double segmentsPerRadius = 2.0;
    int elementOrder = 1;
    bool optimize = true;
    QVector<SmeshLocalSizeControl> localSizeControls;

    bool surfaceCurvature = true;
    double tolerance = 1e-6;

    bool exportVtk = true;
    bool exportNas = true;

    // 在交给 SMESH 前对相邻面/体执行 OCC 通用分割，使材料分区共享同一套拓扑边界。
    bool conformalizeRegions = true;
    bool allGeometry = true;
    QVector<SmeshEntityReference> geometryScope;
    std::function<void(const QString&)> progress;
    SmeshWorkflowSettings workflow;
};

struct SmeshMeshingResult
{
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    int pointCount = 0;
    int surfaceElementCount = 0;
    int volumeElementCount = 0;
    int regionCount = 0;
    int appliedLocalControlCount = 0;
    int nativeSubmeshCount = 0;
    int sourceRegionCount = 0;
    int conformalRegionCount = 0;
    int sharedInterfaceCount = 0;
    bool conformalized = false;

    // Hex8 (Tet split) quality summary. For non-Hex8 meshes these keep
    // their default values.
    int invalidElementCount = 0;
    double minScaledJacobian = 1.0;
};

class SmeshMeshGenerator
{
  public:
    static bool generate(const SmeshMeshingRequest& request, SmeshMeshingResult* result,
                         QString* errMsg = nullptr);

    static bool writeNastran(vtkUnstructuredGrid* grid, const QString& fileName,
                             QString* errMsg = nullptr);
};
