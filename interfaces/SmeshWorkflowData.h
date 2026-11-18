#pragma once
#include <QString>
#include <QVector>
#include <QVector3D>

// UI ids are captured with geometric signatures; native BREP indices are resolved
// only after the current geometry has been exported and made conformal.
struct SmeshEntityReference {
    int dimension = 3;
    int entityId = 0;
    QVector3D center;
    double measure = 0.0;
};

struct SmeshSubmeshSettings {
    QString uuid;
    QString parentUuid;
    QString name;
    int dimension = 3;
    QVector<int> entityIds;
    QVector<SmeshEntityReference> entities;
    // Negative settings inherit the nearest enclosing domain / parent Mesh.
    double maxSize = -1.0;
    double minSize = -1.0;
    int fineness = -1;
    int optimize = -1;
    int surfaceCurvature = -1;
    bool surfaceSizing = true;
    int edgeSegments = 0;
    double edgeBias = 1.0;
    int curvatureSegments = 0;
};

struct SmeshSweepSettings {
    QString uuid;
    QString name;
    int volumeId = 0;
    int sourceFaceId = 0;
    int targetFaceId = 0;
    SmeshEntityReference volume;
    SmeshEntityReference source;
    SmeshEntityReference target;
    int layers = 10;
    double sourceSize = 5.0;
    bool quadrangleSource = true;
    bool partitionRegion = false;
};

// Cutters apply to a temporary meshing copy, never the project CAD.
struct SmeshPartitionSettings {
    bool enabled = true;
    int axis = 0;
    double coordinate = 0.0;
    QVector3D origin;
    QVector3D normal;
    SmeshEntityReference target;
    int targetVolumeUiId = 0;
    int sourceFaceUiId = 0;
    QVector3D planeOrigin() const {
        if(axis==3)return origin;
        return axis==0?QVector3D(coordinate,0,0):axis==1?QVector3D(0,coordinate,0):QVector3D(0,0,coordinate);
    }
    QVector3D planeNormal() const {
        return axis==3?normal.normalized():axis==0?QVector3D(1,0,0):axis==1?QVector3D(0,1,0):QVector3D(0,0,1);
    }
};

struct SmeshWorkflowSettings {
    QString meshUuid;
    QVector<SmeshSubmeshSettings> submeshes;
    QVector<SmeshSweepSettings> sweeps;
    QVector<SmeshPartitionSettings> partitions;
    bool preferHex = true;
    QString computeSubmeshUuid; // empty = rebuild the whole parent Mesh
};
