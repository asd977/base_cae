#include "ModifyBuilder.h"

#include "GeometryBuildUtils.h"

#include <BOPAlgo_Splitter.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepTools.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Face.hxx>

#include <QObject>
#include <CadBody.h>

#include <algorithm>
#include <cmath>

using namespace GeometryBuildUtils;

namespace
{
constexpr Standard_Real kModifyFuzzyValue = 1.0e-7;

bool validResult(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return false;
    try
    {
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
        return vertices.Extent() > 0;
    }
    catch (...)
    {
        return false;
    }
}

void cleanResult(TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return;
    try
    {
        BRepTools::Clean(shape);
    }
    catch (...)
    {
    }
}

QVector<TopoDS_Shape> extractIndependentParts(const TopoDS_Shape& shape)
{
    QVector<TopoDS_Shape> parts;
    if (shape.IsNull())
        return parts;

    auto collect = [&](TopAbs_ShapeEnum type)
    {
        TopTools_IndexedMapOfShape indexed;
        TopExp::MapShapes(shape, type, indexed);
        parts.reserve(indexed.Extent());
        for (int index = 1; index <= indexed.Extent(); ++index)
        {
            const TopoDS_Shape current = indexed(index);
            if (!current.IsNull())
                parts.push_back(current);
        }
    };

    collect(TopAbs_SOLID);
    if (parts.isEmpty())
        collect(TopAbs_SHELL);
    if (parts.isEmpty())
        collect(TopAbs_FACE);
    if (parts.isEmpty())
        parts.push_back(shape);

    struct PartKey
    {
        double measure = 0.0;
        gp_Pnt center;
    };
    auto keyForShape = [](const TopoDS_Shape& part)
    {
        PartKey key;
        try
        {
            GProp_GProps properties;
            TopTools_IndexedMapOfShape solids;
            TopExp::MapShapes(part, TopAbs_SOLID, solids);
            if (solids.Extent() > 0)
                BRepGProp::VolumeProperties(part, properties);
            else
            {
                TopTools_IndexedMapOfShape faces;
                TopExp::MapShapes(part, TopAbs_FACE, faces);
                if (faces.Extent() > 0)
                    BRepGProp::SurfaceProperties(part, properties);
                else
                    BRepGProp::LinearProperties(part, properties);
            }
            key.measure = std::abs(properties.Mass());
            key.center = properties.CentreOfMass();
        }
        catch (...)
        {
        }
        return key;
    };
    struct PartEntry
    {
        TopoDS_Shape shape;
        PartKey key;
    };
    QVector<PartEntry> entries;
    entries.reserve(parts.size());
    for (const TopoDS_Shape& part : parts)
        entries.push_back({part, keyForShape(part)});
    std::sort(entries.begin(), entries.end(), [](const PartEntry& left, const PartEntry& right)
    {
        const double scale = std::max({1.0, left.key.measure, right.key.measure});
        if (std::abs(left.key.measure - right.key.measure) > scale * 1.0e-9)
            return left.key.measure > right.key.measure;
        if (std::abs(left.key.center.X() - right.key.center.X()) > 1.0e-9)
            return left.key.center.X() < right.key.center.X();
        if (std::abs(left.key.center.Y() - right.key.center.Y()) > 1.0e-9)
            return left.key.center.Y() < right.key.center.Y();
        return left.key.center.Z() < right.key.center.Z();
    });
    parts.clear();
    parts.reserve(entries.size());
    for (const PartEntry& entry : entries)
        parts.push_back(entry.shape);
    return parts;
}
}


bool ModifyBuilder::buildFilletShape(const QString& sourceUuid,
                                     double radius,
                                     const QString& edgeSelection,
                                     TopoDS_Shape& result,
                                     QString* message)
{
    result.Nullify();
    if (radius <= kEps)
    {
        if (message)
            *message = QObject::tr("圆角半径必须大于 0。");
        return false;
    }

    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    if (!sourceObj)
    {
        if (message)
            *message = QObject::tr("源对象不存在。");
        return false;
    }

    try
    {
        const TopoDS_Shape& source = *(sourceObj->getShape());
        TopTools_IndexedMapOfShape indexedEdges;
        TopExp::MapShapes(source, TopAbs_EDGE, indexedEdges);

        bool selectionOk = true;
        const QSet<int> selectedEdges =
            resolveStableEdgeSelection(source, edgeSelection, selectionOk);
        if (!selectionOk || selectedEdges.isEmpty())
        {
            if (message)
                *message = QObject::tr("已选边无法在当前几何中定位，请重新选择边。");
            return false;
        }

        BRepFilletAPI_MakeFillet fillet(source);
        for (int edgeIndex : selectedEdges)
        {
            if (edgeIndex <= 0 || edgeIndex > indexedEdges.Extent())
                continue;
            fillet.Add(radius, TopoDS::Edge(indexedEdges(edgeIndex)));
        }
        fillet.Build();
        if (!fillet.IsDone())
        {
            if (message)
                *message = QObject::tr("圆角生成失败，请减小半径或检查相邻边。");
            return false;
        }

        result = fillet.Shape();
        cleanResult(result);
        if (!validResult(result))
        {
            if (message)
                *message = QObject::tr("圆角结果无效，请调整半径或所选边。");
            return false;
        }
        if (message)
            message->clear();
        return true;
    }
    catch (const Standard_Failure&)
    {
        if (message)
            *message = QObject::tr("圆角计算失败，请调整半径或所选边。");
        return false;
    }
    catch (...)
    {
        if (message)
            *message = QObject::tr("圆角计算失败。");
        return false;
    }
}

bool ModifyBuilder::buildChamferShape(const QString& sourceUuid,
                                      double distance,
                                      const QString& edgeSelection,
                                      TopoDS_Shape& result,
                                      QString* message)
{
    result.Nullify();
    if (distance <= kEps)
    {
        if (message)
            *message = QObject::tr("倒角距离必须大于 0。");
        return false;
    }

    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    if (!sourceObj)
    {
        if (message)
            *message = QObject::tr("源对象不存在。");
        return false;
    }

    try
    {
        const TopoDS_Shape& source = *(sourceObj->getShape());
        TopTools_IndexedMapOfShape indexedEdges;
        TopExp::MapShapes(source, TopAbs_EDGE, indexedEdges);

        bool selectionOk = true;
        const QSet<int> selectedEdges =
            resolveStableEdgeSelection(source, edgeSelection, selectionOk);
        if (!selectionOk || selectedEdges.isEmpty())
        {
            if (message)
                *message = QObject::tr("已选边无法在当前几何中定位，请重新选择边。");
            return false;
        }

        BRepFilletAPI_MakeChamfer chamfer(source);
        for (int edgeIndex : selectedEdges)
        {
            if (edgeIndex <= 0 || edgeIndex > indexedEdges.Extent())
                continue;
            chamfer.Add(distance, TopoDS::Edge(indexedEdges(edgeIndex)));
        }
        chamfer.Build();
        if (!chamfer.IsDone())
        {
            if (message)
                *message = QObject::tr("倒角生成失败，请减小距离或检查相邻面。");
            return false;
        }

        result = chamfer.Shape();
        cleanResult(result);
        if (!validResult(result))
        {
            if (message)
                *message = QObject::tr("倒角结果无效，请调整距离或所选边。");
            return false;
        }
        if (message)
            message->clear();
        return true;
    }
    catch (const Standard_Failure&)
    {
        if (message)
            *message = QObject::tr("倒角计算失败，请调整距离或所选边。");
        return false;
    }
    catch (...)
    {
        if (message)
            *message = QObject::tr("倒角计算失败。");
        return false;
    }
}

bool ModifyBuilder::buildSplitShape(const QString& sourceUuid,
                                    const QString& toolUuid,
                                    TopoDS_Shape& result,
                                    QString* message)
{
    result.Nullify();
    GeomShapeObject* sourceObj = validShapeObject(sourceUuid);
    GeomShapeObject* toolObj = validShapeObject(toolUuid);
    if (!sourceObj || !toolObj || sourceUuid == toolUuid)
    {
        if (message)
            *message = QObject::tr("分割对象或工具无效。");
        return false;
    }

    try
    {
        BOPAlgo_Splitter splitter;
        splitter.SetNonDestructive(Standard_True);
        splitter.SetRunParallel(Standard_True);
        splitter.SetFuzzyValue(kModifyFuzzyValue);
        splitter.AddArgument(*(sourceObj->getShape()));
        splitter.AddTool(*(toolObj->getShape()));
        splitter.Perform();
        if (splitter.HasErrors())
        {
            if (message)
                *message = QObject::tr("分割失败，请确认工具与源对象真实相交。");
            return false;
        }

        result = splitter.Shape();
        cleanResult(result);
        if (!validResult(result))
        {
            if (message)
                *message = QObject::tr("分割结果为空。");
            return false;
        }

        const QVector<TopoDS_Shape> sourceParts =
            extractIndependentParts(*(sourceObj->getShape()));
        const QVector<TopoDS_Shape> parts = extractIndependentParts(result);
        if (result.IsSame(*(sourceObj->getShape())) ||
            parts.size() <= sourceParts.size())
        {
            result.Nullify();
            if (message)
                *message = QObject::tr("工具没有将源对象真正切开，请确认两者相交并穿过源对象。");
            return false;
        }
        if (message)
            *message = QObject::tr("已生成 %1 个独立分块。").arg(parts.size());
        return true;
    }
    catch (const Standard_Failure&)
    {
        if (message)
            *message = QObject::tr("分割失败，请检查两个对象是否能够相交或切分。");
        return false;
    }
    catch (...)
    {
        if (message)
            *message = QObject::tr("分割计算失败。");
        return false;
    }
}

QVector<TopoDS_Shape> ModifyBuilder::splitResultParts(const TopoDS_Shape& shape)
{
    return extractIndependentParts(shape);
}

bool ModifyBuilder::recreateFillet(const QString& uuid,
                                   const QString& name,
                                   const QString& sourceUuid,
                                   double radius,
                                   const QString& edgeSelection)
{
    TopoDS_Shape result;
    if (!buildFilletShape(sourceUuid, radius, edgeSelection, result, nullptr))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(sourceUuid);
    return true;
}

bool ModifyBuilder::recreateChamfer(const QString& uuid,
                                    const QString& name,
                                    const QString& sourceUuid,
                                    double distance,
                                    const QString& edgeSelection)
{
    TopoDS_Shape result;
    if (!buildChamferShape(sourceUuid, distance, edgeSelection, result, nullptr))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(sourceUuid);
    return true;
}

bool ModifyBuilder::recreateSplit(const QString& uuid,
                                  const QString& name,
                                  const QString& sourceUuid,
                                  const QString& toolUuid)
{
    TopoDS_Shape result;
    if (!buildSplitShape(sourceUuid, toolUuid, result, nullptr))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(sourceUuid);
    // 工具只是参考对象，不应因为分割被隐藏或从最终模型中移除。
    return true;
}


bool ModifyBuilder::recreateSplitPart(const QString& uuid,
                                      const QString& name,
                                      const QString& splitUuid,
                                      int partIndex)
{
    if (partIndex < 0)
        return false;
    GeomShapeObject* splitObject = validShapeObject(splitUuid);
    if (!splitObject)
        return false;
    const QVector<TopoDS_Shape> parts = splitResultParts(*(splitObject->getShape()));
    if (partIndex >= parts.size() || parts.at(partIndex).IsNull())
        return false;
    registerShape(uuid, name, new TopoDS_Shape(parts.at(partIndex)));
    return validShapeObject(uuid) != nullptr;
}
