#pragma once

#include <BRepBuilderAPI_Copy.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <array>
#include <memory>

// Owned topology for a worker. UI IDs are positions in these maps, never Gmsh
// tags. Copy topology without display meshes so worker operations cannot alter
// the geometry module's TShapes or depend on its object lifetime.
struct NativeCadSnapshot
{
    TopoDS_Shape shape;
    std::array<TopTools_IndexedMapOfShape, 4> entities;

    static std::shared_ptr<const NativeCadSnapshot> copy(const TopoDS_Shape& source)
    {
        if (source.IsNull()) return {};
        auto result = std::make_shared<NativeCadSnapshot>();
        BRepBuilderAPI_Copy copier(source, Standard_False, Standard_False);
        result->shape = copier.Shape();
        const TopAbs_ShapeEnum types[] = {TopAbs_VERTEX, TopAbs_EDGE, TopAbs_FACE, TopAbs_SOLID};
        for (int dim = 0; dim < 4; ++dim) {
            TopTools_IndexedMapOfShape originals;
            TopExp::MapShapes(source, types[dim], originals);
            for (int id = 1; id <= originals.Extent(); ++id)
                result->entities[dim].Add(copier.ModifiedShape(originals(id)));
        }
        if (result->entities[0].IsEmpty() && result->entities[1].IsEmpty() &&
            result->entities[2].IsEmpty() && result->entities[3].IsEmpty()) return {};
        return result;
    }
};
