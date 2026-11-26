#include "TerrainSurfaceBuilder.h"
#include "../builders/DxfReader.h"
#include <gdal.h>
#include <gdalgrid.h>
#include <QFileInfo>
#include <QJsonArray>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shell.hxx>
#include <Standard_Failure.hxx>
#include <vtkCellArray.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkNew.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <Geom_BSplineSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>
#include <stdexcept>
#include <limits>
#include "TerrainContinuousSurfaces.h"

namespace GeometryTerrain
{
QJsonObject GridBounds::toJson() const
{
    return {{"xmin", xmin}, {"xmax", xmax}, {"ymin", ymin}, {"ymax", ymax}};
}

GridBounds GridBounds::fromJson(const QJsonObject& json)
{
    GridBounds b;
    b.xmin = json["xmin"].toDouble(0.0);
    b.xmax = json["xmax"].toDouble(0.0);
    b.ymin = json["ymin"].toDouble(0.0);
    b.ymax = json["ymax"].toDouble(0.0);
    return b;
}

QJsonObject GridOptions::toJson() const
{
    return {
        {"spacing", spacing},
        {"bounds", bounds.toJson()},
        {"customBounds", customBounds},
        {"algorithm", algorithm},
        {"createSolid", createSolid},
        {"baseElevation", baseElevation},
        {"autoBaseElevation", autoBaseElevation},
        {"downsampleTarget", downsampleTarget}
    };
}

GridOptions GridOptions::fromJson(const QJsonObject& json)
{
    GridOptions o;
    o.spacing = json["spacing"].toDouble(5.0);
    o.bounds = GridBounds::fromJson(json["bounds"].toObject());
    o.customBounds = json["customBounds"].toBool(false);
    o.algorithm = json["algorithm"].toInt(0);
    o.createSolid = json["createSolid"].toBool(true);
    o.baseElevation = json["baseElevation"].toDouble(0.0);
    o.autoBaseElevation = json["autoBaseElevation"].toBool(true);
    o.downsampleTarget = json["downsampleTarget"].toInt(100);
    return o;
}

QJsonObject Source::toJson() const
{
    QJsonArray xyz;
    for (const auto& p : points)
        xyz.append(QJsonArray{p.X(), p.Y(), p.Z()});
    return {{"layer", layer}, {"name", name}, {"role", int(role)}, {"enabled", enabled}, {"closed", closed}, {"points", xyz}};
}

Source Source::fromJson(const QJsonObject& j)
{
    Source s;
    s.layer = GeometryDxf::normalizeLayerName(j["layer"].toString());
    s.name = j["name"].toString();
    const int role = j["role"].toInt();
    s.role = role >= 0 && role <= 3 ? Role(role) : Role::Contour;
    s.enabled = j["enabled"].toBool(true);
    s.closed = j["closed"].toBool();
    for (const auto& v : j["points"].toArray())
    {
        const auto a = v.toArray();
        if (a.size() == 3)
            s.points.push_back(gp_Pnt(
                a[0].isDouble() ? a[0].toDouble() : std::numeric_limits<double>::quiet_NaN(),
                a[1].isDouble() ? a[1].toDouble() : std::numeric_limits<double>::quiet_NaN(),
                a[2].isDouble() ? a[2].toDouble() : std::numeric_limits<double>::quiet_NaN()));
    }
    return s;
}

bool Source::constantElevation(double* value) const
{
    if (points.isEmpty())
        return false;
    const double z = points.front().Z();
    if (value)
        *value = z;
    for (const auto& p : points)
        if (std::abs(p.Z() - z) > 1e-6)
            return false;
    return true;
}

QString Result::summary() const
{
    if (isSolid)
    {
        return QStringLiteral("已生成三维闭合地质体：基底高程 %1m，6 个完整物理面（地表顶面、基底底面、4个四周侧面），高程 %2～%3m。")
            .arg(baseElevation, 0, 'f', 2).arg(minElevation, 0, 'f', 2).arg(maxElevation, 0, 'f', 2);
    }
    return QStringLiteral("已生成单张光滑地形面：%1 个面域、%2 个三角面，高程 %3～%4m。")
        .arg(domains.size()).arg(triangles).arg(minElevation, 0, 'f', 2).arg(maxElevation, 0, 'f', 2);
}

QJsonObject TerrainSurfaceBuilder::state(const QVector<Source>& sources, bool generated, double spacing,
                                         DomainMode mode, const GridOptions& gridOpts)
{
    QJsonArray a;
    for (const auto& s : sources)
        a.append(s.toJson());
    QString modeStr = "grid";
    if (mode == DomainMode::WholeSurface)
        modeStr = "whole";
    else if (mode == DomainMode::ContourRegions)
        modeStr = "contours";

    return {
        {"schema", "TerrainSurface.v4"},
        {"representation", "continuous-bands"},
        {"sources", a},
        {"generated", generated},
        {"spacing", spacing},
        {"domainMode", modeStr},
        {"gridOptions", gridOpts.toJson()}
    };
}

DomainMode TerrainSurfaceBuilder::domainMode(const QJsonObject& state)
{
    const QString modeStr = state["domainMode"].toString();
    if (modeStr == "contours")
        return DomainMode::ContourRegions;
    if (modeStr == "whole")
        return DomainMode::WholeSurface;
    return DomainMode::GridInterpolation;
}

GridOptions TerrainSurfaceBuilder::gridOptions(const QJsonObject& state)
{
    if (state.contains("gridOptions"))
        return GridOptions::fromJson(state["gridOptions"].toObject());
    GridOptions opts;
    opts.spacing = state["spacing"].toDouble(5.0);
    return opts;
}

QVector<Source> TerrainSurfaceBuilder::sourcesFromState(const QJsonObject& j)
{
    QVector<Source> sources;
    for (const auto& v : j["sources"].toArray())
        sources.push_back(Source::fromJson(v.toObject()));
    return sources;
}

bool TerrainSurfaceBuilder::readDxf(const QString& path, QVector<Source>& sources, QString& diagnostic)
{
    sources.clear();
    QVector<GeometryDxf::Entity> entities;
    if (!GeometryDxf::read(path, entities, diagnostic))
        return false;

    int varying = 0;
    for (const auto& entity : entities)
    {
        Source source;
        source.layer = entity.layer;
        source.name = entity.handle;
        source.closed = entity.closed;
        source.points = entity.points;
        if (source.points.size() == 1)
            source.role = Role::SpotHeight;
        else if (source.layer.contains("BOUNDARY", Qt::CaseInsensitive) || source.layer.contains(QStringLiteral("范围")))
            source.role = Role::Boundary;
        else if (source.layer.contains("BREAK", Qt::CaseInsensitive) || source.layer.contains(QStringLiteral("控制线")))
            source.role = Role::Breakline;
        else
            source.role = Role::Contour;

        if (source.role == Role::Contour && !source.constantElevation())
        {
            source.enabled = false;
            ++varying;
        }
        sources.push_back(std::move(source));
    }
    if (varying)
        diagnostic += QObject::tr(" %1 条非等高线默认不参与，可切换为控制线或手动修正高程。").arg(varying);
    return true;
}

bool TerrainSurfaceBuilder::computeSourcesBounds(const QVector<Source>& sources, GridBounds& bounds)
{
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    bool found = false;
    for (const auto& s : sources)
    {
        if (!s.enabled) continue;
        for (const auto& p : s.points)
        {
            if (std::isfinite(p.X()) && std::isfinite(p.Y()))
            {
                xmin = std::min(xmin, p.X());
                xmax = std::max(xmax, p.X());
                ymin = std::min(ymin, p.Y());
                ymax = std::max(ymax, p.Y());
                found = true;
            }
        }
    }
    if (found && xmax > xmin && ymax > ymin)
    {
        bounds.xmin = xmin;
        bounds.xmax = xmax;
        bounds.ymin = ymin;
        bounds.ymax = ymax;
        return true;
    }
    return false;
}

Result TerrainSurfaceBuilder::buildGrid(const QVector<Source>& sources, const GridOptions& options)
{
    GeometryDxf::initializeGdal();
    Result r;
    try
    {
        double spacing = options.spacing;
        if (spacing <= 0.001 || !std::isfinite(spacing))
            spacing = 5.0;

        std::vector<double> padfX, padfY, padfZ;
        QVector<gp_Pnt> boundary;
        int boundaryCount = 0;

        for (const auto& s : sources)
        {
            if (!s.enabled) continue;
            if (s.role == Role::Boundary && s.closed && s.points.size() >= 3)
            {
                if (++boundaryCount == 1)
                    boundary = s.points;
            }

            for (const auto& p : s.points)
            {
                if (std::isfinite(p.X()) && std::isfinite(p.Y()) && std::isfinite(p.Z()))
                {
                    padfX.push_back(p.X());
                    padfY.push_back(p.Y());
                    padfZ.push_back(p.Z());
                }
            }
        }

        if (padfX.size() < 3)
            throw std::runtime_error("参与计算的高程点不足 3 个，无法插值地表。");

        GridBounds bounds = options.bounds;
        if (!options.customBounds || !bounds.isValid())
        {
            if (!computeSourcesBounds(sources, bounds))
                throw std::runtime_error("无法计算有效平面坐标范围。");
        }

        const double columns = std::max(2.0, std::floor((bounds.xmax - bounds.xmin) / spacing) + 1);
        const double rows = std::max(2.0, std::floor((bounds.ymax - bounds.ymin) / spacing) + 1);
        if (!std::isfinite(columns * rows) || columns * rows > 2500000)
            throw std::runtime_error("方网格点总数超过 250 万，请增大方网间距或缩小计算范围。");
        const int nXSize = static_cast<int>(columns);
        const int nYSize = static_cast<int>(rows);

        // GDALGridCreate 按栅格像素中心 (dfXMin + (i + 0.5) * dx) 计算插值点，
        // 外扩半个步长使 GDAL 采样中心精确落在网格顶点 bounds.xmin + i * spacing 上。
        const double gdalXMin = bounds.xmin - 0.5 * spacing;
        const double gdalXMax = bounds.xmin + (nXSize - 0.5) * spacing;
        const double gdalYMin = bounds.ymin - 0.5 * spacing;
        const double gdalYMax = bounds.ymin + (nYSize - 0.5) * spacing;

        constexpr double noDataValue = -9999.0;
        std::vector<double> gridData(nXSize * nYSize, noDataValue);

        CPLErr err = CE_None;
        if (options.algorithm == 1) // Nearest Neighbor
        {
            GDALGridNearestNeighborOptions nnOpts;
            std::memset(&nnOpts, 0, sizeof(nnOpts));
            nnOpts.nSizeOfStructure = sizeof(nnOpts);
            nnOpts.dfRadius1 = 0.0;
            nnOpts.dfRadius2 = 0.0;
            nnOpts.dfNoDataValue = noDataValue;
            err = GDALGridCreate(GGA_NearestNeighbor, &nnOpts,
                                 static_cast<GUInt32>(padfX.size()),
                                 padfX.data(), padfY.data(), padfZ.data(),
                                 gdalXMin, gdalXMax, gdalYMin, gdalYMax,
                                 nXSize, nYSize, GDT_Float64,
                                 gridData.data(), nullptr, nullptr);
        }
        else if (options.algorithm == 2) // Linear Delaunay
        {
            GDALGridLinearOptions linOpts;
            std::memset(&linOpts, 0, sizeof(linOpts));
            linOpts.nSizeOfStructure = sizeof(linOpts);
            linOpts.dfRadius = -1.0;
            linOpts.dfNoDataValue = noDataValue;
            err = GDALGridCreate(GGA_Linear, &linOpts,
                                 static_cast<GUInt32>(padfX.size()),
                                 padfX.data(), padfY.data(), padfZ.data(),
                                 gdalXMin, gdalXMax, gdalYMin, gdalYMax,
                                 nXSize, nYSize, GDT_Float64,
                                 gridData.data(), nullptr, nullptr);
        }
        else // 0: IDW (反距离权重，全局搜索无盲区)
        {
            GDALGridInverseDistanceToAPowerOptions idwOpts;
            std::memset(&idwOpts, 0, sizeof(idwOpts));
            idwOpts.nSizeOfStructure = sizeof(idwOpts);
            idwOpts.dfPower = 2.0;
            idwOpts.dfSmoothing = 0.05 * spacing;
            idwOpts.dfAnisotropyRatio = 1.0;
            idwOpts.dfRadius1 = 0.0;
            idwOpts.dfRadius2 = 0.0;
            idwOpts.nMaxPoints = 24;
            idwOpts.nMinPoints = 1;
            idwOpts.dfNoDataValue = noDataValue;
            err = GDALGridCreate(GGA_InverseDistanceToAPower, &idwOpts,
                                 static_cast<GUInt32>(padfX.size()),
                                 padfX.data(), padfY.data(), padfZ.data(),
                                 gdalXMin, gdalXMax, gdalYMin, gdalYMax,
                                 nXSize, nYSize, GDT_Float64,
                                 gridData.data(), nullptr, nullptr);
        }

        if (err != CE_None)
            throw std::runtime_error("GDAL 空间网格插值算法执行失败。");

        double minZ = std::numeric_limits<double>::max();
        double maxZ = std::numeric_limits<double>::lowest();
        for (double z : gridData)
        {
            if (z <= -9000.0 || !std::isfinite(z))
                throw std::runtime_error("线性插值存在超出数据凸包范围的空值，请缩小计算范围或改用反距离权重 (IDW) 插值。");
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }

        auto isInsideBoundary = [&](double x, double y) -> bool
        {
            if (boundary.size() < 3) return true;
            bool inside = false;
            for (int i = 0, j = boundary.size() - 1; i < boundary.size(); j = i++)
            {
                const auto& a = boundary[j];
                const auto& b = boundary[i];
                if ((a.Y() > y) != (b.Y() > y) &&
                    x < (b.X() - a.X()) * (y - a.Y()) / (b.Y() - a.Y()) + a.X())
                    inside = !inside;
            }
            return inside;
        };

        std::vector<gp_Pnt> gridVertices(nXSize * nYSize);
        vtkNew<vtkPoints> vtkPts;
        vtkPts->SetDataTypeToDouble();
        vtkNew<vtkDoubleArray> vtkElevation;
        vtkElevation->SetName("Elevation");

        for (int j = 0; j < nYSize; ++j)
        {
            const double y = bounds.ymin + j * spacing;
            for (int i = 0; i < nXSize; ++i)
            {
                const double x = bounds.xmin + i * spacing;
                const double z = gridData[j * nXSize + i];
                const int idx = j * nXSize + i;
                gridVertices[idx] = gp_Pnt(x, y, z);
                vtkPts->InsertNextPoint(x, y, z);
                vtkElevation->InsertNextValue(z);
            }
        }

        vtkNew<vtkCellArray> vtkPolys;
        std::vector<std::array<int, 3>> triangles;

        for (int j = 0; j < nYSize - 1; ++j)
        {
            const double cellCenterY = bounds.ymin + (j + 0.5) * spacing;
            for (int i = 0; i < nXSize - 1; ++i)
            {
                const double cellCenterX = bounds.xmin + (i + 0.5) * spacing;
                if (!isInsideBoundary(cellCenterX, cellCenterY))
                    continue;

                const int idx0 = j * nXSize + i;
                const int idx1 = j * nXSize + (i + 1);
                const int idx2 = (j + 1) * nXSize + (i + 1);
                const int idx3 = (j + 1) * nXSize + i;

                triangles.push_back({idx0, idx1, idx2});
                vtkIdType t1[3] = {idx0, idx1, idx2};
                vtkPolys->InsertNextCell(3, t1);

                triangles.push_back({idx0, idx2, idx3});
                vtkIdType t2[3] = {idx0, idx2, idx3};
                vtkPolys->InsertNextCell(3, t2);
            }
        }

        if (triangles.empty())
            throw std::runtime_error("生成的地形三角网为空，请检查范围边界或点集分布。");

        vtkNew<vtkPolyData> polyData;
        polyData->SetPoints(vtkPts);
        polyData->SetPolys(vtkPolys);
        polyData->GetPointData()->SetScalars(vtkElevation);
        r.mesh = polyData;

        BRep_Builder builder;
        TopoDS_Face topFace;

        // 1. 单张光滑地形曲面拟合（适度抽稀，彻底消除碎面）
        const int targetMax = std::max(20, options.downsampleTarget);
        const int nx = std::min(nXSize, targetMax);
        const int ny = std::min(nYSize, targetMax);

        TColgp_Array2OfPnt poles(1, nx, 1, ny);
        for (int j = 0; j < ny; ++j)
        {
            const double vFrac = (ny > 1) ? (double(j) / (ny - 1)) : 0.0;
            const double srcJ = vFrac * (nYSize - 1);
            const int j0 = std::min(static_cast<int>(std::floor(srcJ)), nYSize - 1);
            const int j1 = std::min(j0 + 1, nYSize - 1);
            const double tj = srcJ - j0;
            const double curY = bounds.ymin + vFrac * (nYSize - 1) * spacing;

            for (int i = 0; i < nx; ++i)
            {
                const double uFrac = (nx > 1) ? (double(i) / (nx - 1)) : 0.0;
                const double srcI = uFrac * (nXSize - 1);
                const int i0 = std::min(static_cast<int>(std::floor(srcI)), nXSize - 1);
                const int i1 = std::min(i0 + 1, nXSize - 1);
                const double ti = srcI - i0;
                const double curX = bounds.xmin + uFrac * (nXSize - 1) * spacing;

                const double z00 = gridData[j0 * nXSize + i0];
                const double z10 = gridData[j0 * nXSize + i1];
                const double z01 = gridData[j1 * nXSize + i0];
                const double z11 = gridData[j1 * nXSize + i1];
                const double curZ = (1.0 - ti) * ((1.0 - tj) * z00 + tj * z01) +
                                    ti * ((1.0 - tj) * z10 + tj * z11);

                poles.SetValue(i + 1, j + 1, gp_Pnt(curX, curY, curZ));
            }
        }

        TColStd_Array1OfReal uKnots(1, nx), vKnots(1, ny);
        TColStd_Array1OfInteger uMults(1, nx), vMults(1, ny);
        for (int i = 1; i <= nx; ++i)
        {
            const double uFrac = (nx > 1) ? (double(i - 1) / (nx - 1)) : 0.0;
            uKnots(i) = bounds.xmin + uFrac * (nXSize - 1) * spacing;
            uMults(i) = 1;
        }
        uMults(1) = 2; uMults(nx) = 2;
        for (int j = 1; j <= ny; ++j)
        {
            const double vFrac = (ny > 1) ? (double(j - 1) / (ny - 1)) : 0.0;
            vKnots(j) = bounds.ymin + vFrac * (nYSize - 1) * spacing;
            vMults(j) = 1;
        }
        vMults(1) = 2; vMults(ny) = 2;

        Handle(Geom_BSplineSurface) bspline = new Geom_BSplineSurface(
            poles, uKnots, vKnots, uMults, vMults, 1, 1);

        BRepBuilderAPI_MakeFace mkTopFace(bspline, 1e-4);
        if (!mkTopFace.IsDone())
            throw std::runtime_error("无法构造三维单张光滑地形面，请检查网格范围与间距。");
        topFace = mkTopFace.Face();

        if (boundary.size() >= 3)
        {
            BRepBuilderAPI_MakePolygon boundaryPoly;
            for (const auto& pt : boundary)
                boundaryPoly.Add(pt);
            boundaryPoly.Close();
            if (boundaryPoly.IsDone())
            {
                BRepBuilderAPI_MakeFace mkTrimmedFace(bspline, boundaryPoly.Wire());
                if (mkTrimmedFace.IsDone())
                    topFace = mkTrimmedFace.Face();
            }
        }

        // 2. 自动封底成闭合地质体 (6 个物理面: 1顶面 + 1底面 + 4侧面)
        if (options.createSolid)
        {
            double zBase = options.baseElevation;
            if (options.autoBaseElevation || zBase >= minZ)
            {
                zBase = std::floor((minZ - 0.2 * std::max(10.0, maxZ - minZ)) / 10.0) * 10.0;
                if (zBase >= minZ)
                    zBase = minZ - 50.0;
            }
            r.baseElevation = zBase;

            const double actualXMin = bounds.xmin;
            const double actualXMax = bounds.xmin + (nXSize - 1) * spacing;
            const double actualYMin = bounds.ymin;
            const double actualYMax = bounds.ymin + (nYSize - 1) * spacing;

            // 底面 (Z = zBase)
            BRepBuilderAPI_MakePolygon bottomPoly;
            bottomPoly.Add(gp_Pnt(actualXMin, actualYMin, zBase));
            bottomPoly.Add(gp_Pnt(actualXMin, actualYMax, zBase));
            bottomPoly.Add(gp_Pnt(actualXMax, actualYMax, zBase));
            bottomPoly.Add(gp_Pnt(actualXMax, actualYMin, zBase));
            bottomPoly.Close();
            BRepBuilderAPI_MakeFace mkBottomFace(bottomPoly.Wire(), Standard_True);
            TopoDS_Face bottomFace = mkBottomFace.Face();

            // 4 个垂直外侧面
            // 南侧面 (Y = actualYMin)
            BRepBuilderAPI_MakePolygon southPoly;
            southPoly.Add(gp_Pnt(actualXMax, actualYMin, zBase));
            southPoly.Add(gp_Pnt(actualXMin, actualYMin, zBase));
            for (int i = 1; i <= nx; ++i)
                southPoly.Add(poles(i, 1));
            southPoly.Close();
            BRepBuilderAPI_MakeFace mkSouthFace(southPoly.Wire(), Standard_True);
            TopoDS_Face southFace = mkSouthFace.Face();

            // 东侧面 (X = actualXMax)
            BRepBuilderAPI_MakePolygon eastPoly;
            eastPoly.Add(gp_Pnt(actualXMax, actualYMin, zBase));
            for (int j = 1; j <= ny; ++j)
                eastPoly.Add(poles(nx, j));
            eastPoly.Add(gp_Pnt(actualXMax, actualYMax, zBase));
            eastPoly.Close();
            BRepBuilderAPI_MakeFace mkEastFace(eastPoly.Wire(), Standard_True);
            TopoDS_Face eastFace = mkEastFace.Face();

            // 北侧面 (Y = actualYMax)
            BRepBuilderAPI_MakePolygon northPoly;
            northPoly.Add(gp_Pnt(actualXMax, actualYMax, zBase));
            for (int i = nx; i >= 1; --i)
                northPoly.Add(poles(i, ny));
            northPoly.Add(gp_Pnt(actualXMin, actualYMax, zBase));
            northPoly.Close();
            BRepBuilderAPI_MakeFace mkNorthFace(northPoly.Wire(), Standard_True);
            TopoDS_Face northFace = mkNorthFace.Face();

            // 西侧面 (X = actualXMin)
            BRepBuilderAPI_MakePolygon westPoly;
            westPoly.Add(gp_Pnt(actualXMin, actualYMax, zBase));
            for (int j = ny; j >= 1; --j)
                westPoly.Add(poles(1, j));
            westPoly.Add(gp_Pnt(actualXMin, actualYMin, zBase));
            westPoly.Close();
            BRepBuilderAPI_MakeFace mkWestFace(westPoly.Wire(), Standard_True);
            TopoDS_Face westFace = mkWestFace.Face();

            // 缝合 6 个面并构造实体
            BRepBuilderAPI_Sewing sewing(1.0e-2);
            sewing.Add(topFace);
            sewing.Add(bottomFace);
            sewing.Add(southFace);
            sewing.Add(northFace);
            sewing.Add(westFace);
            sewing.Add(eastFace);
            sewing.Perform();
            TopoDS_Shape sewed = sewing.SewedShape();

            TopoDS_Solid solid;
            for (TopExp_Explorer exp(sewed, TopAbs_SHELL); exp.More(); exp.Next())
            {
                BRepBuilderAPI_MakeSolid mkSolid(TopoDS::Shell(exp.Current()));
                if (mkSolid.IsDone())
                {
                    solid = mkSolid.Solid();
                    break;
                }
            }
            if (solid.IsNull())
            {
                TopoDS_Shell shell;
                BRep_Builder b;
                b.MakeShell(shell);
                for (TopExp_Explorer exp(sewed, TopAbs_FACE); exp.More(); exp.Next())
                    b.Add(shell, exp.Current());
                BRepBuilderAPI_MakeSolid mkSolid(shell);
                if (mkSolid.IsDone())
                    solid = mkSolid.Solid();
            }

            if (!solid.IsNull())
            {
                BRepClass3d_SolidClassifier classifier(solid);
                classifier.PerformInfinitePoint(1e-4);
                if (classifier.State() == TopAbs_IN)
                    solid.Reverse();

                r.shape = solid;
                r.isSolid = true;
            }
            else
            {
                r.shape = sewed;
                r.isSolid = false;
            }
        }
        else
        {
            r.shape = topFace;
            r.isSolid = false;
        }

        // 3. 自动物理面特征分组 (地表顶面组、基底底面组、四周侧面组)
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(r.shape, TopAbs_FACE, faceMap);

        Result::FaceGroup topGroup;
        topGroup.name = QStringLiteral("地表顶面组");
        topGroup.role = QStringLiteral("Top");

        Result::FaceGroup bottomGroup;
        bottomGroup.name = QStringLiteral("基底底面组");
        bottomGroup.role = QStringLiteral("Bottom");

        Result::FaceGroup sidesGroup;
        sidesGroup.name = QStringLiteral("四周侧面组");
        sidesGroup.role = QStringLiteral("Sides");

        for (int i = 1; i <= faceMap.Extent(); ++i)
        {
            const TopoDS_Face face = TopoDS::Face(faceMap(i));
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            gp_Pnt center = props.CentreOfMass();

            BRepAdaptor_Surface surf(face);
            const double uMid = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
            const double vMid = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
            gp_Pnt p;
            gp_Vec d1u, d1v;
            surf.D1(uMid, vMid, p, d1u, d1v);
            gp_Vec norm = d1u.Crossed(d1v);
            if (norm.Magnitude() > 1e-7)
                norm.Normalize();
            if (face.Orientation() == TopAbs_REVERSED)
                norm.Reverse();

            if (r.isSolid && std::abs(center.Z() - r.baseElevation) < 1.0 && norm.Z() < -0.5)
            {
                bottomGroup.faceIndices.push_back(i);
            }
            else if (norm.Z() > 0.2 || surf.GetType() == GeomAbs_BSplineSurface)
            {
                topGroup.faceIndices.push_back(i);
            }
            else
            {
                sidesGroup.faceIndices.push_back(i);
            }
        }

        r.faceGroups.push_back(topGroup);
        if (r.isSolid)
        {
            r.faceGroups.push_back(bottomGroup);
            r.faceGroups.push_back(sidesGroup);
        }

        r.vertices = static_cast<int>(gridVertices.size());
        r.triangles = static_cast<int>(triangles.size());
        r.minElevation = minZ;
        r.maxElevation = maxZ;

        Result::Domain domain;
        domain.id = 1;
        domain.name = r.isSolid ? QStringLiteral("GDAL三维闭合地质体") : QStringLiteral("GDAL方网插值地形面");
        domain.minElevation = minZ;
        domain.maxElevation = maxZ;
        domain.triangles = r.triangles;
        r.domains.push_back(domain);
        r.faceDomains.fill(1, faceMap.Extent());
    }
    catch (const Standard_Failure& ex)
    {
        r.error = QString::fromLocal8Bit(ex.GetMessageString());
        r.shape.Nullify();
        r.mesh = nullptr;
    }
    catch (const std::exception& ex)
    {
        r.error = QString::fromUtf8(ex.what());
        r.shape.Nullify();
        r.mesh = nullptr;
    }

    return r;
}

Result TerrainSurfaceBuilder::build(const QVector<Source>& sources, double spacing, DomainMode mode,
                                    const GridOptions& gridOpts)
{
    if (mode == DomainMode::GridInterpolation)
    {
        GridOptions opts = gridOpts;
        if (spacing > 0.001)
            opts.spacing = spacing;
        return buildGrid(sources, opts);
    }

    Result r;
    try
    {
        if (!std::isfinite(spacing) || spacing <= 0)
            throw std::runtime_error("等高线采样间距必须大于零。");

        double xmin = 1e300, xmax = -1e300, ymin = xmin, ymax = xmax;
        int boundaryCount = 0;
        double minZ = std::numeric_limits<double>::max(), maxZ = -minZ;

        for (const auto& source : sources)
        {
            if (!source.enabled) continue;
            if (source.points.isEmpty()) throw std::runtime_error("参与对象没有坐标。");
            if (source.role == Role::Contour && !source.constantElevation())
                throw std::runtime_error("等高线自身高程不一致，请修正高程或改为控制线。");
            if (source.role == Role::Boundary)
            {
                if (++boundaryCount > 1 || !source.closed || source.points.size() < 3)
                    throw std::runtime_error("请提供一个闭合的地形范围边界。");
            }
            for (const auto& p : source.points)
            {
                if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()) ||
                    std::max({std::abs(p.X()), std::abs(p.Y()), std::abs(p.Z())}) > 1e12)
                    throw std::runtime_error("坐标不是有效数值或超出支持范围。");
                xmin = std::min(xmin, p.X());
                xmax = std::max(xmax, p.X());
                ymin = std::min(ymin, p.Y());
                ymax = std::max(ymax, p.Y());
                minZ = std::min(minZ, p.Z());
                maxZ = std::max(maxZ, p.Z());
            }
        }

        if (xmax <= xmin || ymax <= ymin)
            throw std::runtime_error("等高线需要覆盖有效的 XY 区域。");

        r.minElevation = minZ;
        r.maxElevation = maxZ;
        if (buildContinuousSurfaces(sources, spacing, mode, r))
            return r;

        throw std::runtime_error("该模式需要互相嵌套的闭合等高线；开放或分散的等高线请使用方网插值。");
    }
    catch (const Standard_Failure& ex)
    {
        r.error = QString::fromLocal8Bit(ex.GetMessageString());
        r.shape.Nullify();
        r.mesh = nullptr;
    }
    catch (const std::exception& ex)
    {
        r.error = QString::fromUtf8(ex.what());
        r.shape.Nullify();
        r.mesh = nullptr;
    }
    return r;
}

QVector<Source> TerrainSurfaceBuilder::example()
{
    QVector<Source> sources;
    for (int ring = 0; ring < 5; ++ring)
    {
        Source s;
        s.layer = ring == 0 ? "BOUNDARY" : QString("CONTOUR_%1").arg(100 + ring * 10);
        s.name = s.layer;
        s.role = ring == 0 ? Role::Boundary : Role::Contour;
        s.closed = true;
        for (int i = 0; i < 72; ++i)
        {
            double angle = 2 * 3.14159265358979323846 * i / 72, rx = 100 - ring * 18, ry = 80 - ring * 14;
            s.points.push_back(gp_Pnt(500000 + rx * std::cos(angle), 3000000 + ry * std::sin(angle), 100 + ring * 10));
        }
        sources.push_back(s);
    }
    Source peak;
    peak.layer = "SUMMIT";
    peak.name = "Summit 150m";
    peak.role = Role::SpotHeight;
    peak.points = {gp_Pnt(500000, 3000000, 150)};
    sources.push_back(peak);
    return sources;
}

} // namespace GeometryTerrain
