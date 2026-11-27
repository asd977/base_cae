#pragma once

// Geometry-based NETGEN size presets. Only native OCC topology is queried;
// nothing is fragmented, healed or changed in the CAD model.
#include "MeshAutoSizingFormula.h"
#include <NativeCadSnapshot.h>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <vector>

namespace MeshAutoSizing {
struct Scale {
    bool valid = false;
    double extent = 0.0;        // Largest dimension of the native OCC bounding box
    double feature = 0.0;       // Robust short CAD edge length (not smallest/tolerance sliver)
    int edgesSampled = 0;
};
struct Preset {
    double maxSize = 0.0;
    double minSize = 0.0;
    int fineness = 2;
};

// ids empty means all native CAD solids. Do not use SMESH transient shape ids;
// NativeCadSnapshot::entities[3] is indexed by the stable geometry UI ID.
inline Scale measure(const NativeCadSnapshot& cad, const QVector<int>& ids = {}) {
    Scale result;
    if (cad.shape.IsNull()) return result;
    TopoDS_Compound selected;
    BRep_Builder builder;
    builder.MakeCompound(selected);
    const auto& solids = cad.entities[3];
    if (ids.empty()) {
        for (int i=1; i<=solids.Extent(); ++i)
            builder.Add(selected, solids(i));
    } else {
        for (int id : ids) {
            if (id <= 0 || id > solids.Extent()) return result; // Fail, do not guess an entity.
            builder.Add(selected, solids(id));
        }
    }
    if (solids.Extent() == 0) return result;
    Bnd_Box bounds;
    BRepBndLib::Add(selected, bounds);
    if (bounds.IsVoid()) return result;
    Standard_Real xmin,ymin,zmin,xmax,ymax,zmax;
    bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
    result.extent = std::max({double(xmax-xmin), double(ymax-ymin), double(zmax-zmin)});
    if (!std::isfinite(result.extent) || result.extent <= 0.0) return result;

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(selected, TopAbs_EDGE, edges);
    // Bound the GUI-side estimate even for CAD with millions of sub-edges.
    const int limit = std::min(edges.Extent(), 1024);
    std::vector<double> lengths;
    lengths.reserve(limit);
    for (int i=0; i<limit; ++i) {
        const int index = 1 + int((static_cast<long long>(i) * edges.Extent()) / limit);
        GProp_GProps props;
        BRepGProp::LinearProperties(edges(index), props);
        const double length = props.Mass();
        if (std::isfinite(length) && length > result.extent*1.0e-10)
            lengths.push_back(length);
    }
    if (!lengths.empty()) {
        std::sort(lengths.begin(), lengths.end());
        // 10th percentile suppresses near-zero CAD tolerance slivers.
        result.feature = lengths[std::size_t((lengths.size()-1)*0.1)];
    }
    result.edgesSampled = int(lengths.size());
    result.valid = true;
    return result;
}

// The three choices are *absolute functions of the same CAD*: repeatedly
// switching presets never compounds a previous size multiplier.
inline Preset infer(const Scale& scale, const QString& level) {
    Preset p;
    if (!scale.valid) return p;
    const auto density = level == QStringLiteral("few")
        ? MeshAutoSizingFormula::Density::Few
        : level == QStringLiteral("fine")
            ? MeshAutoSizingFormula::Density::Fine
            : MeshAutoSizingFormula::Density::Normal;
    const auto numeric = MeshAutoSizingFormula::infer(scale.extent, scale.feature, density);
    p.maxSize = numeric.maximum;
    p.minSize = numeric.minimum;
    p.fineness = numeric.fineness;
    return p;
}
}
