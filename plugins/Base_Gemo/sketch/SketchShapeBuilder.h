#pragma once

#include "SketchDocument.h"

#include <QString>
#include <TopoDS_Shape.hxx>
#include <vector>

namespace GeometrySketch
{

struct ShapeBuildResult
{
    bool ok = false;
    QString message;
    TopoDS_Shape shape;
};

class SketchShapeBuilder
{
public:
    // 统一的草图平滑曲线离散算法：预览和最终生成必须共用这一个入口。
    // 点击点是插值点，曲线会经过这些点；返回的是用于显示和生成 wire 的采样点。
    static std::vector<Point2D> sampleCurvePoints(const std::vector<Point2D>& input);

    static ShapeBuildResult build(const SketchDocument& document, const PlaneSettings& planeSettings);
    static ShapeBuildResult buildMany(const std::vector<SketchDocument>& documents,
                                      const PlaneSettings& planeSettings);
};

} // namespace GeometrySketch
