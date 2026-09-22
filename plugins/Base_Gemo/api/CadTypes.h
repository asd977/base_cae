#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>

enum class PlaneType
{
    XY = 0,
    XZ = 1,
    YZ = 2
};

// Extrusion direction describes an unsigned axis/type. The actual sign is
// controlled by ExtrudeParameters::reverse so the UI never needs duplicated
// +X/-X, +Y/-Y or +Z/-Z entries.
enum class ExtrudeDirectionMode
{
    ProfileNormal = 0,
    XAxis = 1,
    YAxis = 2,
    ZAxis = 3,
    Custom = 4
};

enum class ExtrudeExtentMode
{
    OneSide = 0,
    TwoSides = 1,
    Symmetric = 2
};

// Retained for project compatibility with the short-lived explicit result-mode
// format. The current extrusion command is topology-driven: closed planar
// regions create solids, while open wires/edges create surfaces in the same
// operation. Builders therefore normalize this field to Auto.
enum class ExtrudeResultMode
{
    Auto = 0,
    Solid = 1,
    Surface = 2
};


// Stable reference to one B-Rep face on an existing geometry object.
// faceIndex is 1-based and follows OCC TopExp::MapShapes(..., TopAbs_FACE).
struct CadFaceRef
{
    QString uuid;
    int faceIndex = 0;
};

struct RevolveParameters
{
    double angleDeg = 360.0;
    QVector3D axisOrigin = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D axisDirection = QVector3D(1.0f, 0.0f, 0.0f);

    // Optional sub-shape source. Empty/zero keeps the legacy standalone
    // sketch/profile behavior; otherwise rotation uses this exact B-Rep face.
    CadFaceRef sourceFace;
};

struct ExtrudeParameters
{
    // OneSide: forward distance.
    // TwoSides: forward distance; secondDistance is the opposite distance.
    // Symmetric: total distance, split equally around the profile plane.
    double distance = 10.0;
    double secondDistance = 10.0;
    ExtrudeDirectionMode directionMode = ExtrudeDirectionMode::ProfileNormal;
    ExtrudeExtentMode extentMode = ExtrudeExtentMode::OneSide;
    ExtrudeResultMode resultMode = ExtrudeResultMode::Auto;
    QVector3D customDirection = QVector3D(0.0f, 0.0f, 1.0f);
    bool reverse = false;
};

// One independent translation direction of a rectangular/linear pattern.
// count == 1 disables the direction; step may point to any 3D direction.
struct PatternDirection
{
    int count = 1;
    QVector3D step;
};

// Circular pattern around an arbitrary 3D axis.
// count includes the original object. For a full 360-degree pattern the end
// position is not duplicated; for a partial angle both ends are included.
struct PolarPatternParameters
{
    int count = 4;
    double totalAngleDeg = 360.0;
    QVector3D axisOrigin;
    QVector3D axisDirection = QVector3D(0.0f, 0.0f, 1.0f);
};
