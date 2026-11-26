#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <vtkSmartPointer.h>
#include <cmath>
class vtkPolyData;

namespace GeometryTerrain
{
enum class Role { Contour=0, Boundary=1, Breakline=2, SpotHeight=3 };
enum class DomainMode { ContourRegions=0, WholeSurface=1, GridInterpolation=2 };

struct GridBounds
{
    double xmin = 0.0, xmax = 0.0;
    double ymin = 0.0, ymax = 0.0;
    bool isValid() const { return std::isfinite(xmin) && std::isfinite(xmax) &&
                                 std::isfinite(ymin) && std::isfinite(ymax) &&
                                 (xmax > xmin) && (ymax > ymin); }
    QJsonObject toJson() const;
    static GridBounds fromJson(const QJsonObject& json);
};

struct GridOptions
{
    double spacing = 5.0; // 方网间距（米）
    GridBounds bounds;
    bool customBounds = false;
    int algorithm = 0; // 0=IDW(反距离权重), 1=最近邻, 2=线性插值(Linear Delaunay)
    bool simplifyDxf = false;
    double simplifyTolerance = 0.0;
    bool smoothDiagonals = false;
    bool createSolid = true; // 默认生成三维闭合地质体
    double baseElevation = 0.0; // 基底高程（米）
    bool autoBaseElevation = true; // 自动计算基底高程
    int downsampleTarget = 100; // CAD曲面控制点目标规模（如100x100量级），防止过拟合与布尔卡顿
    QJsonObject toJson() const;
    static GridOptions fromJson(const QJsonObject& json);
};

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
    struct FaceGroup
    {
        QString name;
        QString role; // "Top", "Bottom", "Sides", "Excavation", etc.
        QVector<int> faceIndices; // 1-based index in TopExp::MapShapes(shape, TopAbs_FACE)
    };
    TopoDS_Shape shape;
    vtkSmartPointer<vtkPolyData> mesh;
    QString error;
    int vertices=0, triangles=0, constraints=0, flatTriangles=0;
    double minElevation=0, maxElevation=0;
    QVector<Domain> domains;
    QVector<int> faceDomains; // In TopExp::MapShapes(shape, FACE) order.
    QVector<FaceGroup> faceGroups;
    bool isSolid = false;
    double baseElevation = 0.0;
    bool ok() const {return error.isEmpty() && !shape.IsNull() && triangles>0;}
    QString summary() const;
};

class TerrainSurfaceBuilder
{
public:
    static bool readDxf(const QString& path, QVector<Source>& sources, QString& diagnostic);
    static bool computeSourcesBounds(const QVector<Source>& sources, GridBounds& bounds);
    static Result buildGrid(const QVector<Source>& sources, const GridOptions& options);
    static Result build(const QVector<Source>& sources, double maxSegmentLength=5.0,
                        DomainMode mode=DomainMode::GridInterpolation,
                        const GridOptions& gridOpts=GridOptions{});
    static QVector<Source> example();
    static QJsonObject state(const QVector<Source>& sources, bool generated=true, double spacing=5.0,
                             DomainMode mode=DomainMode::GridInterpolation,
                             const GridOptions& gridOpts=GridOptions{});
    static DomainMode domainMode(const QJsonObject& state);
    static GridOptions gridOptions(const QJsonObject& state);
    static QVector<Source> sourcesFromState(const QJsonObject& state);
    static QJsonObject builtState(const QJsonObject& state)
    {
        const auto built = state.value(QStringLiteral("builtState")).toObject();
        return built.isEmpty() ? state : built;
    }
};
}
