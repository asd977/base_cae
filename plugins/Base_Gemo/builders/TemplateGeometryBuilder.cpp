#include "TemplateGeometryBuilder.h"

#include "GeometryBuildUtils.h"

#include "CadDocument.h"
#include "CadBody.h"
#include <GeometryDisplayManager.h>
#include <GeometryCleanup.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <BRep_Builder.hxx>

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <BRepTools.hxx>
#include <Precision.hxx>
#include <gp_Circ.hxx>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>

#include <QFile>
#include <QList>
#include <QSet>
#include <QTextStream>
#include <QVector>
#include <QPointF>
#include <QObject>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace GeometryBuildUtils;

static QVector<QPointF> buildDamSectionPoints(double damHeight, double crestWidth, double upSlope, double downSlope)
{
    const double x1 = damHeight * upSlope;
    const double x2 = x1 + crestWidth;
    const double x3 = x2 + damHeight * downSlope;

    QVector<QPointF> points;
    points << QPointF(0.0, 0.0)
           << QPointF(x1, damHeight)
           << QPointF(x2, damHeight)
           << QPointF(x3, 0.0);
    return points;
}

bool TemplateGeometryBuilder::recreateGravityDam(const QString& uuid, const QString& name, double height, double crestWidth, double upSlope, double downSlope, double length, int layerCount, double x, double y, double z)
{
    if (height <= 0.0 || crestWidth <= 0.0 || upSlope < 0.0 || downSlope < 0.0 || length <= kEps || layerCount <= 0)
        return false;

    const auto section = buildDamSectionPoints(height, crestWidth, upSlope, downSlope);
    if (section.size() < 4)
        return false;

    TopoDS_Compound layeredDam;
    BRep_Builder builder;
    builder.MakeCompound(layeredDam);

    const double layerH = height / layerCount;

    auto intersectionRangeAtZ = [&](double zQuery, double& xMin, double& xMax) -> bool
    {
        QVector<double> xs;
        for (int i = 0; i < section.size(); ++i)
        {
            const QPointF a = section[i];
            const QPointF b = section[(i + 1) % section.size()];
            const double zA = a.y();
            const double zB = b.y();
            const double low = qMin(zA, zB);
            const double high = qMax(zA, zB);

            if (zQuery < low - kEps || zQuery > high + kEps)
                continue;

            if (qAbs(zA - zB) < kEps)
            {
                xs << a.x() << b.x();
                continue;
            }

            const double t = (zQuery - zA) / (zB - zA);
            xs << (a.x() + t * (b.x() - a.x()));
        }

        if (xs.isEmpty())
            return false;

        xMin = xs[0];
        xMax = xs[0];
        for (double value : xs)
        {
            xMin = qMin(xMin, value);
            xMax = qMax(xMax, value);
        }
        return xMax - xMin > kEps;
    };

    for (int i = 0; i < layerCount; ++i)
    {
        const double z0 = z + layerH * i;
        const double z1 = z + layerH * (i + 1);

        double xL0 = 0.0, xR0 = 0.0, xL1 = 0.0, xR1 = 0.0;
        if (!intersectionRangeAtZ(z0 - z, xL0, xR0) || !intersectionRangeAtZ(z1 - z, xL1, xR1))
            continue;

        BRepBuilderAPI_MakePolygon layerProfile;
        layerProfile.Add(gp_Pnt(x + xL0, y, z0));
        layerProfile.Add(gp_Pnt(x + xL1, y, z1));
        layerProfile.Add(gp_Pnt(x + xR1, y, z1));
        layerProfile.Add(gp_Pnt(x + xR0, y, z0));
        layerProfile.Close();

        TopoDS_Wire layerWire = layerProfile.Wire();
        BRepBuilderAPI_MakeFace faceMaker(layerWire);
        if (!faceMaker.IsDone())
            continue;

        TopoDS_Shape layerSolid = BRepPrimAPI_MakePrism(faceMaker.Face(), gp_Vec(0.0, length, 0.0)).Shape();
        builder.Add(layeredDam, layerSolid);
    }

    registerShape(uuid, name, new TopoDS_Shape(layeredDam));
    return true;
}

static TopoDS_Shape makePrismFromProfile(const QVector<QPointF>& profile, double length)
{
    if (profile.size() < 3 || length <= kEps)
        return TopoDS_Shape();

    BRepBuilderAPI_MakePolygon polygon;
    QPointF lastPoint;
    bool hasLast = false;
    int validCount = 0;

    for (const QPointF& point : profile)
    {
        if (hasLast && qAbs(point.x() - lastPoint.x()) < kEps && qAbs(point.y() - lastPoint.y()) < kEps)
            continue;
        polygon.Add(gp_Pnt(point.x(), 0.0, point.y()));
        lastPoint = point;
        hasLast = true;
        ++validCount;
    }

    if (validCount < 3)
        return TopoDS_Shape();

    polygon.Close();
    BRepBuilderAPI_MakeFace faceMaker(polygon.Wire());
    if (!faceMaker.IsDone())
        return TopoDS_Shape();

    return BRepPrimAPI_MakePrism(faceMaker.Face(), gp_Vec(0.0, length, 0.0)).Shape();
}

static bool appendBand(TopoDS_Compound& compound,
                      BRep_Builder& builder,
                      double outerLeft0,
                      double outerRight0,
                      double outerLeft1,
                      double outerRight1,
                      double regionLeft0,
                      double regionRight0,
                      double regionLeft1,
                      double regionRight1,
                      double z0,
                      double z1,
                      double length)
{
    const double left0 = qMax(outerLeft0, regionLeft0);
    const double right0 = qMin(outerRight0, regionRight0);
    const double left1 = qMax(outerLeft1, regionLeft1);
    const double right1 = qMin(outerRight1, regionRight1);

    if ((right0 - left0 <= kEps) && (right1 - left1 <= kEps))
        return false;

    QVector<QPointF> profile;
    if (right0 - left0 > kEps)
        profile << QPointF(left0, z0) << QPointF(right0, z0);
    if (right1 - left1 > kEps)
        profile << QPointF(right1, z1) << QPointF(left1, z1);

    TopoDS_Shape solid = makePrismFromProfile(profile, length);
    if (solid.IsNull())
        return false;

    builder.Add(compound, solid);
    return true;
}

bool TemplateGeometryBuilder::recreateEarthRockfillDam(const QString& uuid, const QString& name, double height, double crestWidth, double upSlope, double downSlope, double length, int layerCount, double coreTop, double coreBottom, double filter, double coreOffset)
{
    if (height <= kEps || crestWidth <= kEps || upSlope <= kEps || downSlope <= kEps || length <= kEps || layerCount <= 0 ||
        coreTop <= kEps || coreBottom <= kEps || filter <= kEps)
        return false;

    const double totalWidth = upSlope + crestWidth + downSlope;
    const double center = upSlope + crestWidth * 0.5 + coreOffset;

    const double topLeftWithFilter = center - coreTop * 0.5 - filter;
    const double topRightWithFilter = center + coreTop * 0.5 + filter;
    const double bottomLeftWithFilter = center - coreBottom * 0.5 - filter;
    const double bottomRightWithFilter = center + coreBottom * 0.5 + filter;

    if (topLeftWithFilter <= upSlope + kEps || topRightWithFilter >= upSlope + crestWidth - kEps)
        return false;
    if (bottomLeftWithFilter <= kEps || bottomRightWithFilter >= totalWidth - kEps)
        return false;

    const double layerH = height / static_cast<double>(layerCount);

    auto outerLeftAt = [&](double z)
    {
        return upSlope * z / height;
    };
    auto outerRightAt = [&](double z)
    {
        return totalWidth - downSlope * z / height;
    };
    auto coreHalfAt = [&](double z)
    {
        return coreBottom * 0.5 + (coreTop - coreBottom) * 0.5 * z / height;
    };

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    for (int i = 0; i < layerCount; ++i)
    {
        const double z0 = i * layerH;
        const double z1 = (i + 1) * layerH;

        const double outerL0 = outerLeftAt(z0);
        const double outerR0 = outerRightAt(z0);
        const double outerL1 = outerLeftAt(z1);
        const double outerR1 = outerRightAt(z1);

        const double half0 = coreHalfAt(z0);
        const double half1 = coreHalfAt(z1);
        const double coreL0 = center - half0;
        const double coreR0 = center + half0;
        const double coreL1 = center - half1;
        const double coreR1 = center + half1;

        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   -1.0e12, coreL0 - filter, -1.0e12, coreL1 - filter, z0, z1, length); // 上游坝壳
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreL0 - filter, coreL0, coreL1 - filter, coreL1, z0, z1, length);    // 左反滤层
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreL0, coreR0, coreL1, coreR1, z0, z1, length);            // 心墙
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreR0, coreR0 + filter, coreR1, coreR1 + filter, z0, z1, length);    // 右反滤层
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreR0 + filter, 1.0e12, coreR1 + filter, 1.0e12, z0, z1, length);    // 下游坝壳
    }

    registerShape(uuid, name, new TopoDS_Shape(compound));
    return true;
}

static QVector<QPointF> buildSlopeProfile2D(double height,
                                           double topPlatformWidth,
                                           double middleBenchWidth,
                                           double stageHorizontalWidth,
                                           double toeExtensionLength,
                                           double baseHeight,
                                           int stageCount)
{
    QVector<QPointF> profile;
    if (height <= kEps || topPlatformWidth < 0.0 || middleBenchWidth < 0.0 ||
        stageHorizontalWidth <= kEps || toeExtensionLength < 0.0 || baseHeight <= kEps ||
        stageCount <= 0)
        return profile;

    profile << QPointF(0.0, -baseHeight);   // 左下远场底边
    profile << QPointF(0.0, height);        // 左上远场地表

    double x = 0.0;
    double z = height;
    const double dz = height / static_cast<double>(stageCount);

    if (topPlatformWidth > kEps)
    {
        x += topPlatformWidth;
        profile << QPointF(x, z);           // 坡顶平台
    }

    for (int i = 0; i < stageCount; ++i)
    {
        x += stageHorizontalWidth;
        z = qMax(0.0, height - (i + 1) * dz);
        profile << QPointF(x, z);           // 第 i 级坡面终点

        if (i != stageCount - 1 && middleBenchWidth > kEps)
        {
            x += middleBenchWidth;
            profile << QPointF(x, z);       // 中间平台
        }
    }

    if (toeExtensionLength > kEps)
    {
        x += toeExtensionLength;
        profile << QPointF(x, 0.0);         // 坡脚外延平台面
    }

    profile << QPointF(x, -baseHeight);     // 外延/坡脚底部
    return profile;
}

bool TemplateGeometryBuilder::recreateSlopeTemplate(const QString& uuid, const QString& name, double height, double benchWidth, double slope, double length, int benchCount, double top, double toe, double baseH)
{
    if (height <= kEps || top < 0.0 || benchWidth < 0.0 || slope <= kEps || toe < 0.0 ||
        baseH <= kEps || length <= kEps || benchCount <= 0)
        return false;

    const QVector<QPointF> profile = buildSlopeProfile2D(height, top, benchWidth, slope, toe, baseH, benchCount);
    TopoDS_Shape slopeSolid = makePrismFromProfile(profile, length);
    if (slopeSolid.IsNull())
        return false;

    registerShape(uuid, name, new TopoDS_Shape(slopeSolid));
    return true;
}

static bool addProfileSolid(TopoDS_Compound& compound, BRep_Builder& builder, const QVector<QPointF>& profile, double length)
{
    TopoDS_Shape solid = makePrismFromProfile(profile, length);
    if (solid.IsNull())
        return false;
    builder.Add(compound, solid);
    return true;
}

static bool addBandSolid(TopoDS_Compound& compound,
                        BRep_Builder& builder,
                        double left0,
                        double right0,
                        double left1,
                        double right1,
                        double z0,
                        double z1,
                        double length)
{
    if (z1 - z0 <= kEps)
        return false;
    if ((right0 - left0 <= kEps) && (right1 - left1 <= kEps))
        return false;

    QVector<QPointF> profile;
    if (right0 - left0 > kEps)
        profile << QPointF(left0, z0) << QPointF(right0, z0);
    if (right1 - left1 > kEps)
        profile << QPointF(right1, z1) << QPointF(left1, z1);

    return addProfileSolid(compound, builder, profile, length);
}

static double cavernLeftAtZ(double span, double wallH, double archH, double z)
{
    if (z <= wallH || archH <= kEps)
        return 0.0;
    const double t = qBound(0.0, (z - wallH) / archH, 1.0);
    return span * 0.25 * t;
}

static double cavernRightAtZ(double span, double wallH, double archH, double z)
{
    if (z <= wallH || archH <= kEps)
        return span;
    const double t = qBound(0.0, (z - wallH) / archH, 1.0);
    return span - span * 0.25 * t;
}

bool TemplateGeometryBuilder::recreateCavernTemplate(const QString& uuid, const QString& name, double span, double wallH, double archH, double length, double stage, double sideRock, double topRock, double bottomRock)
{
    const int stageCount = qMax(1, qRound(stage));
    if (span <= kEps || wallH <= kEps || archH <= kEps || length <= kEps || stageCount <= 0 ||
        sideRock <= kEps || topRock <= kEps || bottomRock <= kEps)
        return false;

    const double cavernH = wallH + archH;
    const double minX = -sideRock;
    const double maxX = span + sideRock;
    const double minZ = -bottomRock;
    const double maxZ = cavernH + topRock;

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    addBandSolid(compound, builder, minX, maxX, minX, maxX, minZ, 0.0, length);

    for (int i = 0; i < stageCount; ++i)
    {
        const double z0 = cavernH * i / static_cast<double>(stageCount);
        const double z1 = cavernH * (i + 1) / static_cast<double>(stageCount);
        const double left0 = cavernLeftAtZ(span, wallH, archH, z0);
        const double right0 = cavernRightAtZ(span, wallH, archH, z0);
        const double left1 = cavernLeftAtZ(span, wallH, archH, z1);
        const double right1 = cavernRightAtZ(span, wallH, archH, z1);

        addBandSolid(compound, builder, minX, left0, minX, left1, z0, z1, length);       // 左围岩
        addBandSolid(compound, builder, left0, right0, left1, right1, z0, z1, length);  // 开挖级
        addBandSolid(compound, builder, right0, maxX, right1, maxX, z0, z1, length);    // 右围岩
    }

    addBandSolid(compound, builder, minX, maxX, minX, maxX, cavernH, maxZ, length);

    registerShape(uuid, name, new TopoDS_Shape(compound));
    return true;
}

