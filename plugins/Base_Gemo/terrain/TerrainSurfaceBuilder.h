#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <vtkSmartPointer.h>
class vtkPolyData;

namespace GeometryTerrain
{
enum class Role { Contour=0, Boundary=1, Breakline=2, SpotHeight=3 };
enum class DomainMode { ContourRegions=0, WholeSurface=1 };
struct Source
{
    QString layer, name;
    Role role=Role::Contour;
    bool enabled=true, closed=false;
    QVector<gp_Pnt> points;
    QJsonObject toJson() const;
    static Source fromJson(const QJsonObject& json);
    bool constantElevation(double* value=nullptr) const;
};
struct Result
{
    struct Domain {int id=0, triangles=0;double minElevation=0,maxElevation=0;QString name;};
    TopoDS_Shape shape;
    vtkSmartPointer<vtkPolyData> mesh;
    QString error;
    int vertices=0, triangles=0, constraints=0, flatTriangles=0;
    double minElevation=0, maxElevation=0;
    QVector<Domain> domains;
    QVector<int> faceDomains; // In TopExp::MapShapes(shape, FACE) order.
    bool ok() const {return error.isEmpty() && !shape.IsNull() && triangles>0;}
    QString summary() const;
};
class TerrainSurfaceBuilder
{
public:
    static bool readDxf(const QString& path, QVector<Source>& sources, QString& diagnostic);
    static Result build(const QVector<Source>& sources, double maxSegmentLength=5.0,
                        DomainMode mode=DomainMode::ContourRegions);
    static QVector<Source> example();
    static QJsonObject state(const QVector<Source>& sources, bool generated=true, double spacing=5.0,
                             DomainMode mode=DomainMode::ContourRegions);
    static DomainMode domainMode(const QJsonObject& state);
    static QVector<Source> sourcesFromState(const QJsonObject& state);
};
}
