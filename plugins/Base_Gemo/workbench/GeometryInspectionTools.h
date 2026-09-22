#pragma once

#include <QString>
#include <QStringList>
#include <QVector3D>

#include <TopoDS_Shape.hxx>

namespace GeometryInspectionTools
{
struct CheckOptions
{
    double smallEdgeLength = 1.0e-3;
    double smallFaceArea = 1.0e-4;
    double repairTolerance = 1.0e-6;
    bool sewFaces = true;
    bool unifySameDomain = false;
};

struct CheckReport
{
    bool hasShape = false;
    bool valid = false;
    bool brepValid = false;
    bool bopValid = false;
    bool closed = false;
    QString shapeType;

    int vertexCount = 0;
    int edgeCount = 0;
    int wireCount = 0;
    int faceCount = 0;
    int shellCount = 0;
    int solidCount = 0;
    int compoundCount = 0;
    int freeEdgeCount = 0;
    int nonManifoldEdgeCount = 0;
    int degeneratedEdgeCount = 0;
    int smallEdgeCount = 0;
    int smallFaceCount = 0;

    double totalLength = 0.0;
    double totalArea = 0.0;
    double totalVolume = 0.0;
    double maxTolerance = 0.0;

    QVector3D center;
    QVector3D boundsMin;
    QVector3D boundsMax;
    QStringList issues;
};

struct EntityInfo
{
    bool valid = false;
    QString typeName;
    QString geometryType;
    double measure = 0.0;
    QVector3D center;
    QVector3D boundsMin;
    QVector3D boundsMax;
    QStringList properties;
};

struct PairMeasureResult
{
    bool valid = false;
    double distance = 0.0;
    bool hasAngle = false;
    double angleDeg = 0.0;
    QVector3D pointA;
    QVector3D pointB;
    QString message;
};

CheckReport inspect(const TopoDS_Shape& shape, const CheckOptions& options = {});

bool repair(const TopoDS_Shape& source,
            const CheckOptions& options,
            TopoDS_Shape& repaired,
            QStringList& actions,
            QString& error);

bool entityShape(const TopoDS_Shape& root, int dim, int id, TopoDS_Shape& out);
EntityInfo entityInfo(const TopoDS_Shape& root, int dim, int id);
PairMeasureResult measurePair(const TopoDS_Shape& rootA, int dimA, int idA,
                              const TopoDS_Shape& rootB, int dimB, int idB);
}
