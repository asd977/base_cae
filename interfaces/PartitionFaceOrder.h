#pragma once
#include <QVector>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <algorithm>
#include <cmath>

// Shared preview ordering only; no best-fit matching or geometry identity.
namespace GeometryPartitionTopology {
struct Face
{
    TopoDS_Shape shape;
    double cx = 0.0, cy = 0.0, cz = 0.0;
    double area = 0.0;
    double xmin = 0.0, ymin = 0.0, zmin = 0.0;
    double xmax = 0.0, ymax = 0.0, zmax = 0.0;
};

inline QVector<Face> orderedFaces(const TopoDS_Shape& region)
{
    QVector<Face> faces;
    for (TopExp_Explorer ex(region, TopAbs_FACE); ex.More(); ex.Next())
    {
        Face item;
        item.shape = ex.Current();
        GProp_GProps props;
        BRepGProp::SurfaceProperties(TopoDS::Face(item.shape), props);
        const gp_Pnt c = props.CentreOfMass();
        item.cx = c.X(); item.cy = c.Y(); item.cz = c.Z();
        item.area = props.Mass();

        Bnd_Box box;
        BRepBndLib::Add(item.shape, box);
        if (!box.IsVoid())
            box.Get(item.xmin, item.ymin, item.zmin, item.xmax, item.ymax, item.zmax);
        faces.push_back(item);
    }

    auto lessWithTol = [](double a, double b) -> int
    {
        const double scale = std::max({1.0, std::abs(a), std::abs(b)});
        const double eps = scale * 1e-10;
        if (a < b - eps) return -1;
        if (a > b + eps) return 1;
        return 0;
    };
    std::sort(faces.begin(), faces.end(), [&](const Face& a, const Face& b)
    {
        const double av[] = {a.cx,a.cy,a.cz,a.area,a.xmin,a.ymin,a.zmin,a.xmax,a.ymax,a.zmax};
        const double bv[] = {b.cx,b.cy,b.cz,b.area,b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax};
        for (int i = 0; i < 10; ++i)
        {
            const int c = lessWithTol(av[i], bv[i]);
            if (c < 0) return true;
            if (c > 0) return false;
        }
        return false;
    });
    return faces;
}

}
