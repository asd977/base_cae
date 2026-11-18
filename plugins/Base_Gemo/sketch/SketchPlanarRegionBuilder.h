#pragma once

#include "SketchDocument.h"
#include <QString>
#include <TopoDS_Shape.hxx>
#include <vector>

namespace GeometrySketch
{
// Build bounded planar regions from an explicitly chosen line network.
// This is deliberately not a "fill the DXF bounding box" operation:
// disconnected, dangling and ambiguous curves are never silently closed.
struct PlanarRegionReport
{
    TopoDS_Shape shape;
    QString error;
    int sourceObjects = 0;
    int sourceSegments = 0;
    int splitIntersections = 0;
    int duplicateSegments = 0;
    int danglingSegments = 0;
    int ignoredOpenCurves = 0;
    int faces = 0;
    int invalidFaces = 0;
    bool ok() const { return !shape.IsNull() && faces > 0 && error.isEmpty(); }
};

class SketchPlanarRegionBuilder
{
public:
    static PlanarRegionReport build(const std::vector<SketchDocument>& documents,
                                    const PlaneSettings& settings);
};
} // namespace GeometrySketch
