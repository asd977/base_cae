#include "BooleanBuilder.h"

#include "GeometryBuildUtils.h"

#include "CadDocument.h"
#include "CadBody.h"
#include <GeometryDisplayManager.h>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Trsf.hxx>

#include <QStringList>
#include <QVector>

#include <sstream>

using namespace GeometryBuildUtils;

namespace
{
constexpr Standard_Real kBooleanFuzzyValue = 1.0e-7;
constexpr int kFastCommonMinParts = 8;

enum class FastCommonStatus
{
    NotApplicable,
    Succeeded,
    Failed,
    NoOverlap
};

class ScopedRenderSuspender
{
public:
    ScopedRenderSuspender()
        : m_wasSuspended(GeometryDisplayManager::isRenderSuspended())
    {
        if (!m_wasSuspended)
            GeometryDisplayManager::setRenderSuspended(true);
    }

    ~ScopedRenderSuspender()
    {
        if (!m_wasSuspended)
            GeometryDisplayManager::setRenderSuspended(false);
    }

private:
    bool m_wasSuspended = false;
};

bool readBrepFromString(const QString& brep, TopoDS_Shape& shape)
{
    if (brep.isEmpty())
        return false;

    try
    {
        // OCCT 不同版本的流式 BRepTools::Read 返回值不一致。
        // 这里不依赖返回值，统一以读取后的 TopoDS_Shape 是否为空作为判断。
        std::istringstream iss(brep.toStdString());
        BRep_Builder builder;
        BRepTools::Read(shape, iss, builder);
        return !shape.IsNull();
    }
    catch (...)
    {
        shape.Nullify();
        return false;
    }
}

bool hasTopology(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return false;

    for (TopExp_Explorer exp(shape, TopAbs_VERTEX); exp.More(); exp.Next())
        return true;
    return false;
}

Bnd_Box shapeBoundingBox(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    if (shape.IsNull())
        return box;

    try
    {
        BRepBndLib::Add(shape, box);
        if (!box.IsVoid())
            box.Enlarge(kBooleanFuzzyValue * 10.0);
    }
    catch (...)
    {
    }
    return box;
}

bool boxesOverlap(const Bnd_Box& a, const Bnd_Box& b)
{
    if (a.IsVoid() || b.IsVoid())
        return true;
    return !a.IsOut(b);
}

QVector<TopoDS_Shape> collectSolidParts(const TopoDS_Shape& shape)
{
    QVector<TopoDS_Shape> parts;
    if (shape.IsNull())
        return parts;

    TopTools_IndexedMapOfShape solids;
    try
    {
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
    }
    catch (...)
    {
        return parts;
    }

    if (solids.Extent() <= 1)
        return parts;

    parts.reserve(solids.Extent());
    for (int i = 1; i <= solids.Extent(); ++i)
    {
        const TopoDS_Shape solid = solids(i);
        if (!solid.IsNull() && hasTopology(solid))
            parts.append(solid);
    }
    return parts;
}

TopoDS_Shape fixedShape(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return shape;

    try
    {
        ShapeFix_Shape fixer(shape);
        fixer.Perform();
        const TopoDS_Shape fixed = fixer.Shape();
        if (!fixed.IsNull() && hasTopology(fixed))
            return fixed;
    }
    catch (...)
    {
    }
    return shape;
}

TopoDS_Shape materializedShape(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return shape;

    try
    {
        // 阵列为了显示和保存性能，会把同一个 TShape 通过不同 Location 复用。
        // 这种“轻量实例”显示没有问题，但复杂布尔时 OCCT 更容易在 pave/filler 阶段失败。
        // 这里在真正参与布尔前，把候选实例实体化成独立拓扑，只处理候选件，避免把整个大阵列都深拷贝。
        gp_Trsf identity;
        BRepBuilderAPI_Transform copier(shape, identity, Standard_True);
        copier.Build();
        const TopoDS_Shape copied = copier.IsDone() ? copier.Shape() : TopoDS_Shape();
        if (!copied.IsNull() && hasTopology(copied))
            return copied;
    }
    catch (...)
    {
    }
    return shape;
}

bool finalizeResult(TopoDS_Shape& shape, bool tryFix)
{
    if (!hasTopology(shape))
        return false;

    try
    {
        BRepTools::Clean(shape);
    }
    catch (...)
    {
    }

    if (tryFix)
        shape = fixedShape(shape);

    // 不再用 BRepCheck_Analyzer 作为硬失败条件。
    // 对复杂布尔结果，OCCT 有时会给出可显示/可继续处理的 Shape，
    // 但严格拓扑检查返回 invalid，原先会因此误报“生成失败”。
    return hasTopology(shape);
}

QVector<TopoDS_Shape> collectInputShapes(const QStringList& selectedKeys, bool tryFix)
{
    QVector<TopoDS_Shape> shapes;
    QStringList visited;

    for (const QString& key : selectedKeys)
    {
        if (key.isEmpty() || visited.contains(key))
            continue;
        visited.append(key);

        GeomShapeObject* obj = validShapeObject(key);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;

        TopoDS_Shape shape = *(obj->getShape());
        if (tryFix)
            shape = fixedShape(shape);

        // 这里只做便宜的拓扑存在性判断，不做昂贵的 BRepCheck/ShapeFix。
        // 大阵列参与布尔时，逐对象修复和检查会明显拖慢。
        if (hasTopology(shape))
            shapes.append(shape);
    }

    return shapes;
}

TopTools_ListOfShape toShapeList(const QVector<TopoDS_Shape>& shapes, int beginIndex = 0)
{
    TopTools_ListOfShape list;
    for (int i = beginIndex; i < shapes.size(); ++i)
        list.Append(shapes.at(i));
    return list;
}

TopTools_ListOfShape toShapeList(const QVector<TopoDS_Shape>& shapes,
                                 int beginIndex,
                                 bool materialize)
{
    TopTools_ListOfShape list;
    for (int i = beginIndex; i < shapes.size(); ++i)
    {
        TopoDS_Shape shape = shapes.at(i);
        if (materialize)
            shape = materializedShape(shape);
        list.Append(shape);
    }
    return list;
}

QVector<TopoDS_Shape> collectCandidatePartsByBox(const TopoDS_Shape& shape,
                                                 const Bnd_Box& referenceBox,
                                                 bool materialize)
{
    QVector<TopoDS_Shape> candidates;
    const QVector<TopoDS_Shape> parts = collectSolidParts(shape);
    if (parts.size() < 2)
        return candidates;

    candidates.reserve(parts.size());
    for (const TopoDS_Shape& part : parts)
    {
        if (!boxesOverlap(shapeBoundingBox(part), referenceBox))
            continue;
        candidates.append(materialize ? materializedShape(part) : part);
    }
    return candidates;
}

template <typename TBooleanAlgo>
void configureBooleanAlgo(TBooleanAlgo& algo)
{
    algo.SetNonDestructive(Standard_True);
    algo.SetRunParallel(Standard_True);
    algo.SetFuzzyValue(kBooleanFuzzyValue);
}

template <typename TBooleanAlgo>
bool buildBooleanResult(TBooleanAlgo& algo, TopoDS_Shape& result, bool tryFixResult)
{
    try
    {
        configureBooleanAlgo(algo);
        algo.Build();
        if (!algo.IsDone())
            return false;

        try
        {
            // 统一共面面和共线边，减少布尔后碎面/碎边。
            algo.SimplifyResult(Standard_True, Standard_True, Precision::Angular());
        }
        catch (...)
        {
        }

        result = algo.Shape();
        return finalizeResult(result, tryFixResult);
    }
    catch (...)
    {
        return false;
    }
}

bool buildFuseMulti(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    TopTools_ListOfShape arguments = toShapeList(shapes);
    BRepAlgoAPI_Fuse fuse;
    fuse.SetArguments(arguments);
    return buildBooleanResult(fuse, result, tryFixResult);
}

bool buildFuseSequential(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    result = shapes.front();
    for (int i = 1; i < shapes.size(); ++i)
    {
        TopTools_ListOfShape arguments;
        arguments.Append(result);
        TopTools_ListOfShape tools;
        tools.Append(shapes.at(i));

        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(arguments);
        fuse.SetTools(tools);
        if (!buildBooleanResult(fuse, result, tryFixResult))
            return false;
    }
    // 每一步 buildBooleanResult() 已完成统一、校验和可选修复。
    return !result.IsNull();
}

bool buildFuseImpl(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    if (shapes.size() < 2)
        return false;

    // 大多数场景多参数布尔更快；失败时再退回逐步布尔，兼容复杂/老版本 OCCT。
    if (buildFuseMulti(shapes, result, tryFixResult))
        return true;
    return buildFuseSequential(shapes, result, tryFixResult);
}

bool buildCutMulti(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    TopTools_ListOfShape arguments;
    arguments.Append(shapes.front());
    TopTools_ListOfShape tools = toShapeList(shapes, 1);

    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments);
    cut.SetTools(tools);
    return buildBooleanResult(cut, result, tryFixResult);
}

bool buildCutSequential(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    result = shapes.front();
    for (int i = 1; i < shapes.size(); ++i)
    {
        TopTools_ListOfShape arguments;
        arguments.Append(result);
        TopTools_ListOfShape tools;
        tools.Append(shapes.at(i));

        BRepAlgoAPI_Cut cut;
        cut.SetArguments(arguments);
        cut.SetTools(tools);
        if (!buildBooleanResult(cut, result, tryFixResult))
            return false;
    }
    return !result.IsNull();
}

bool buildCutWithFilteredCompoundTools(const QVector<TopoDS_Shape>& shapes,
                                       TopoDS_Shape& result,
                                       bool tryFixResult)
{
    if (shapes.size() < 2)
        return false;

    const TopoDS_Shape target = shapes.front();
    const Bnd_Box targetBox = shapeBoundingBox(target);

    QVector<TopoDS_Shape> optimizedShapes;
    optimizedShapes.reserve(shapes.size());
    optimizedShapes.append(target);

    bool usedCompoundFiltering = false;
    for (int i = 1; i < shapes.size(); ++i)
    {
        const QVector<TopoDS_Shape> candidates = collectCandidatePartsByBox(shapes.at(i), targetBox, true);
        if (candidates.isEmpty())
        {
            // 不是多实体 Compound，或者包围盒无法可靠过滤：保留原工具。
            const QVector<TopoDS_Shape> parts = collectSolidParts(shapes.at(i));
            if (parts.size() >= 2)
            {
                // 多实体阵列里没有任何候选件与目标包围盒相交，说明该工具不会切掉目标。
                usedCompoundFiltering = true;
                continue;
            }
            optimizedShapes.append(shapes.at(i));
            continue;
        }

        usedCompoundFiltering = true;
        for (const TopoDS_Shape& candidate : candidates)
            optimizedShapes.append(candidate);
    }

    if (!usedCompoundFiltering)
        return false;

    if (optimizedShapes.size() < 2)
    {
        result = target;
        return finalizeResult(result, tryFixResult);
    }

    // 对阵列工具先做“候选件过滤 + 实体化”。
    // 这能同时解决两个问题：
    // 1) 没有接触目标的阵列件不再进入布尔，速度明显变快；
    // 2) 轻量 Location 实例转成独立拓扑后，BRepAlgoAPI_Cut 稳定很多。
    if (buildCutMulti(optimizedShapes, result, tryFixResult))
        return true;
    return buildCutSequential(optimizedShapes, result, tryFixResult);
}

bool buildCutImpl(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    if (shapes.size() < 2)
        return false;

    // 差集语义：第 1 个对象是被切目标，其余都是切割工具。
    // 如果工具是阵列 Compound，优先只切真正可能接触目标的阵列件。
    if (buildCutWithFilteredCompoundTools(shapes, result, tryFixResult))
        return true;

    // 普通对象仍走多工具切割；失败后再退回逐个切割。
    if (buildCutMulti(shapes, result, tryFixResult))
        return true;
    return buildCutSequential(shapes, result, tryFixResult);
}

bool buildCommonDefaultPair(const TopoDS_Shape& left,
                            const TopoDS_Shape& right,
                            TopoDS_Shape& result,
                            bool tryFixResult)
{
    TopTools_ListOfShape arguments;
    arguments.Append(left);
    TopTools_ListOfShape tools;
    tools.Append(right);

    BRepAlgoAPI_Common common;
    common.SetArguments(arguments);
    common.SetTools(tools);
    return buildBooleanResult(common, result, tryFixResult);
}

FastCommonStatus buildCommonCompoundFast(const TopoDS_Shape& compoundSide,
                                         const TopoDS_Shape& otherSide,
                                         TopoDS_Shape& result,
                                         bool tryFixResult)
{
    const QVector<TopoDS_Shape> parts = collectSolidParts(compoundSide);
    if (parts.size() < kFastCommonMinParts)
        return FastCommonStatus::NotApplicable;

    const Bnd_Box otherBox = shapeBoundingBox(otherSide);
    QVector<TopoDS_Shape> candidates;
    candidates.reserve(parts.size());

    for (const TopoDS_Shape& part : parts)
    {
        if (boxesOverlap(shapeBoundingBox(part), otherBox))
            candidates.append(part);
    }

    if (candidates.isEmpty())
    {
        result.Nullify();
        return FastCommonStatus::NoOverlap;
    }

    // 阵列对象通常是一个 Compound，内部包含大量互不接触的 Solid。
    // 直接拿“拉伸体 ∩ 整个 Compound”求交时，OCCT 会把所有阵列单元都放进布尔管线，
    // 与目标包围盒完全不相交的单元也会参与预处理，速度会非常慢。
    // 这里先按包围盒筛掉不可能相交的阵列单元，再只对候选单元做一次 Common。
    TopTools_ListOfShape arguments;
    for (const TopoDS_Shape& candidate : candidates)
        arguments.Append(materializedShape(candidate));

    TopTools_ListOfShape tools;
    tools.Append(otherSide);

    BRepAlgoAPI_Common common;
    common.SetArguments(arguments);
    common.SetTools(tools);
    if (buildBooleanResult(common, result, tryFixResult))
        return FastCommonStatus::Succeeded;

    return FastCommonStatus::Failed;
}

bool buildCommonPair(const TopoDS_Shape& left,
                     const TopoDS_Shape& right,
                     TopoDS_Shape& result,
                     bool tryFixResult)
{
    TopoDS_Shape candidate;
    FastCommonStatus status = buildCommonCompoundFast(left, right, candidate, tryFixResult);
    if (status == FastCommonStatus::Succeeded)
    {
        result = candidate;
        return true;
    }
    if (status == FastCommonStatus::NoOverlap)
    {
        result.Nullify();
        return false;
    }

    candidate.Nullify();
    status = buildCommonCompoundFast(right, left, candidate, tryFixResult);
    if (status == FastCommonStatus::Succeeded)
    {
        result = candidate;
        return true;
    }
    if (status == FastCommonStatus::NoOverlap)
    {
        result.Nullify();
        return false;
    }

    candidate.Nullify();
    if (!buildCommonDefaultPair(left, right, candidate, tryFixResult))
        return false;

    result = candidate;
    return true;
}

bool buildCommonImpl(const QVector<TopoDS_Shape>& shapes, TopoDS_Shape& result, bool tryFixResult)
{
    if (shapes.size() < 2)
        return false;

    // 多对象求交必须保持“逐步取交”的语义：A∩B∩C。
    // 单步 A∩(B∪C) 会导致三对象及以上时结果不对。
    result = shapes.front();
    for (int i = 1; i < shapes.size(); ++i)
    {
        if (!buildCommonPair(result, shapes.at(i), result, tryFixResult))
            return false;
    }
    return !result.IsNull();
}

using BooleanBuilderFn = bool (*)(const QVector<TopoDS_Shape>&, TopoDS_Shape&, bool);

bool buildWithOptionalRepair(const QStringList& selectedKeys,
                             BooleanBuilderFn builder,
                             TopoDS_Shape& resultShape)
{
    QVector<TopoDS_Shape> shapes = collectInputShapes(selectedKeys, false);
    if (shapes.size() < 2)
        return false;

    if (builder(shapes, resultShape, false))
        return true;

    // 只有第一次失败时才做 ShapeFix，避免复杂模型/阵列每次都被修复检查拖慢。
    shapes = collectInputShapes(selectedKeys, true);
    if (shapes.size() < 2)
        return false;
    return builder(shapes, resultShape, true);
}

bool recreateBoolean(const QString& uuid,
                     const QString& name,
                     const QStringList& selectedKeys,
                     const QStringList& operationKeys,
                     const QString& savedResultBrep,
                     BooleanBuilderFn builder)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || uuid.isEmpty())
        return false;

    TopoDS_Shape resultShape;
    const bool restoringFromCache = !savedResultBrep.isEmpty();
    if (restoringFromCache)
    {
        if (!readBrepFromString(savedResultBrep, resultShape) || !finalizeResult(resultShape, false))
            return false;
    }
    else
    {
        // 旧工程和外部生成工程有时只保存 operationKeys。selectedKeys 只是
        // 当前面板选择，不能因为它为空就丢失真实的布尔历史输入。
        const QStringList buildKeys = selectedKeys.isEmpty() ? operationKeys : selectedKeys;
        if (!buildWithOptionalRepair(buildKeys, builder, resultShape))
            return false;
    }

    if (resultShape.IsNull())
        return false;

    ScopedRenderSuspender suspendRender;
    registerShape(uuid, name, new TopoDS_Shape(resultShape));

    // 输入对象必须保留在 CadDocument / 模型树 / FeatureData 里，只默认隐藏显示。
    // 暂停即时渲染后批量隐藏，避免“注册结果 + 隐藏每个来源”连续触发多次 Render。
    if (!restoringFromCache)
    {
        QStringList sourcesToHide;
        for (const QString& key : operationKeys)
        {
            if (key.isEmpty() || key == uuid || sourcesToHide.contains(key))
                continue;
            sourcesToHide.append(key);
        }
        if (!sourcesToHide.isEmpty())
            GeometryDisplayManager::hideSources(sourcesToHide, false);
    }

    return true;
}
} // namespace

bool BooleanBuilder::buildFuseShape(const QStringList& selectedKeys, TopoDS_Shape& result)
{
    return buildWithOptionalRepair(selectedKeys, buildFuseImpl, result);
}

bool BooleanBuilder::buildCutShape(const QStringList& selectedKeys, TopoDS_Shape& result)
{
    return buildWithOptionalRepair(selectedKeys, buildCutImpl, result);
}

bool BooleanBuilder::buildCommonShape(const QStringList& selectedKeys, TopoDS_Shape& result)
{
    return buildWithOptionalRepair(selectedKeys, buildCommonImpl, result);
}

bool BooleanBuilder::buildCutShape(const TopoDS_Shape& target,
                                   const TopoDS_Shape& tool,
                                   TopoDS_Shape& result)
{
    result.Nullify();
    if (target.IsNull() || tool.IsNull())
        return false;

    QVector<TopoDS_Shape> shapes{target, tool};
    if (buildCutImpl(shapes, result, false))
        return true;

    shapes[0] = fixedShape(target);
    shapes[1] = fixedShape(tool);
    return buildCutImpl(shapes, result, true);
}

bool BooleanBuilder::recreateBooleanFuse(const QString& uuid,
                                         const QString& name,
                                         const QStringList& selectedKeys,
                                         const QStringList& operationKeys,
                                         const QString& savedResultBrep)
{
    return recreateBoolean(uuid, name, selectedKeys, operationKeys, savedResultBrep, buildFuseImpl);
}

bool BooleanBuilder::recreateBooleanCut(const QString& uuid,
                                        const QString& name,
                                        const QStringList& selectedKeys,
                                        const QStringList& operationKeys,
                                        const QString& savedResultBrep)
{
    return recreateBoolean(uuid, name, selectedKeys, operationKeys, savedResultBrep, buildCutImpl);
}

bool BooleanBuilder::recreateBooleanCommon(const QString& uuid,
                                           const QString& name,
                                           const QStringList& selectedKeys,
                                           const QStringList& operationKeys,
                                           const QString& savedResultBrep)
{
    return recreateBoolean(uuid, name, selectedKeys, operationKeys, savedResultBrep, buildCommonImpl);
}

bool BooleanBuilder::excavateFromGeology(const QString& geologyUuid,
                                       const TopoDS_Shape& excavationTool,
                                       TopoDS_Shape& cutGeology,
                                       QVector<int>& newExcavationFaceIndices,
                                       QString& errorMsg)
{
    newExcavationFaceIndices.clear();
    cutGeology.Nullify();
    errorMsg.clear();

    if (excavationTool.IsNull())
    {
        errorMsg = QStringLiteral("开挖体几何为空。");
        return false;
    }

    auto* doc = GeomManager::CadDocument::getInstance();
    if (!doc)
    {
        errorMsg = QStringLiteral("无法获取几何管理器。");
        return false;
    }

    CadBody* geoBody = doc->getGeomShapeObject(geologyUuid);
    if (!geoBody || !geoBody->getShape() || geoBody->getShape()->IsNull())
    {
        errorMsg = QStringLiteral("未找到目标地质体或其几何为空。");
        return false;
    }

    const TopoDS_Shape geologyShape = *(geoBody->getShape());

    TopTools_ListOfShape arguments;
    arguments.Append(geologyShape);
    TopTools_ListOfShape tools;
    tools.Append(excavationTool);

    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments);
    cut.SetTools(tools);
    configureBooleanAlgo(cut);

    bool ok = false;
    try
    {
        cut.Build();
        if (cut.IsDone())
        {
            cutGeology = cut.Shape();
            ok = finalizeResult(cutGeology, false);
        }
    }
    catch (...)
    {
        ok = false;
    }

    if (!ok)
    {
        try
        {
            TopTools_ListOfShape fixArgs;
            fixArgs.Append(fixedShape(geologyShape));
            TopTools_ListOfShape fixTools;
            fixTools.Append(fixedShape(excavationTool));
            BRepAlgoAPI_Cut cutRetry;
            cutRetry.SetArguments(fixArgs);
            cutRetry.SetTools(fixTools);
            configureBooleanAlgo(cutRetry);
            cutRetry.SetFuzzyValue(1.0e-5);
            cutRetry.Build();
            if (cutRetry.IsDone())
            {
                cutGeology = cutRetry.Shape();
                ok = finalizeResult(cutGeology, true);
            }
        }
        catch (...)
        {
            ok = false;
        }
    }

    if (!ok || cutGeology.IsNull())
    {
        errorMsg = QStringLiteral("地质体开挖布尔裁剪失败。");
        return false;
    }

    TopTools_IndexedMapOfShape toolFaces;
    TopExp::MapShapes(excavationTool, TopAbs_FACE, toolFaces);

    TopTools_IndexedMapOfShape origGeoFaces;
    TopExp::MapShapes(geologyShape, TopAbs_FACE, origGeoFaces);

    TopTools_IndexedMapOfShape generatedInnerFaces;
    for (int i = 1; i <= toolFaces.Extent(); ++i)
    {
        const TopTools_ListOfShape& genList = cut.Generated(toolFaces(i));
        for (TopTools_ListIteratorOfListOfShape it(genList); it.More(); it.Next())
        {
            if (it.Value().ShapeType() == TopAbs_FACE)
                generatedInnerFaces.Add(it.Value());
        }
        const TopTools_ListOfShape& modList = cut.Modified(toolFaces(i));
        for (TopTools_ListIteratorOfListOfShape it(modList); it.More(); it.Next())
        {
            if (it.Value().ShapeType() == TopAbs_FACE)
                generatedInnerFaces.Add(it.Value());
        }
    }

    Bnd_Box toolBox;
    BRepBndLib::Add(excavationTool, toolBox);
    toolBox.Enlarge(1.0e-2);

    TopTools_IndexedMapOfShape resultFaces;
    TopExp::MapShapes(cutGeology, TopAbs_FACE, resultFaces);

    for (int i = 1; i <= resultFaces.Extent(); ++i)
    {
        const TopoDS_Shape& face = resultFaces(i);
        bool isExcavation = generatedInnerFaces.Contains(face);
        if (!isExcavation && !origGeoFaces.Contains(face))
        {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            gp_Pnt p = props.CentreOfMass();
            if (!toolBox.IsOut(p))
            {
                isExcavation = true;
            }
        }
        if (isExcavation)
        {
            newExcavationFaceIndices.append(i);
        }
    }

    return true;
}

bool BooleanBuilder::fitDamToValley(const QString& geologyUuid,
                                   const TopoDS_Shape& rawDam,
                                   TopoDS_Shape& fittedDam,
                                   QVector<int>& contactFaceIndices,
                                   QString& errorMsg)
{
    contactFaceIndices.clear();
    fittedDam.Nullify();
    errorMsg.clear();

    if (rawDam.IsNull())
    {
        errorMsg = QStringLiteral("原始大坝几何为空。");
        return false;
    }

    auto* doc = GeomManager::CadDocument::getInstance();
    if (!doc)
    {
        errorMsg = QStringLiteral("无法获取几何管理器。");
        return false;
    }

    CadBody* geoBody = doc->getGeomShapeObject(geologyUuid);
    if (!geoBody || !geoBody->getShape() || geoBody->getShape()->IsNull())
    {
        errorMsg = QStringLiteral("未找到河谷地质体或其几何为空。");
        return false;
    }

    const TopoDS_Shape geologyShape = *(geoBody->getShape());

    TopTools_ListOfShape arguments;
    arguments.Append(rawDam);
    TopTools_ListOfShape tools;
    tools.Append(geologyShape);

    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments);
    cut.SetTools(tools);
    configureBooleanAlgo(cut);

    bool ok = false;
    try
    {
        cut.Build();
        if (cut.IsDone())
        {
            fittedDam = cut.Shape();
            ok = finalizeResult(fittedDam, false);
        }
    }
    catch (...)
    {
        ok = false;
    }

    if (!ok)
    {
        try
        {
            TopTools_ListOfShape fixArgs;
            fixArgs.Append(fixedShape(rawDam));
            TopTools_ListOfShape fixTools;
            fixTools.Append(fixedShape(geologyShape));
            BRepAlgoAPI_Cut cutRetry;
            cutRetry.SetArguments(fixArgs);
            cutRetry.SetTools(fixTools);
            configureBooleanAlgo(cutRetry);
            cutRetry.SetFuzzyValue(1.0e-5);
            cutRetry.Build();
            if (cutRetry.IsDone())
            {
                fittedDam = cutRetry.Shape();
                ok = finalizeResult(fittedDam, true);
            }
        }
        catch (...)
        {
            ok = false;
        }
    }

    if (!ok || fittedDam.IsNull())
    {
        errorMsg = QStringLiteral("坝体与河谷地质体布尔贴合失败（可能坝体未与河谷相交或完全在地下）。");
        return false;
    }

    TopTools_IndexedMapOfShape rawDamFaces;
    TopExp::MapShapes(rawDam, TopAbs_FACE, rawDamFaces);

    TopTools_IndexedMapOfShape geoFaces;
    TopExp::MapShapes(geologyShape, TopAbs_FACE, geoFaces);

    TopTools_IndexedMapOfShape contactFaces;
    for (int i = 1; i <= geoFaces.Extent(); ++i)
    {
        const TopTools_ListOfShape& genList = cut.Generated(geoFaces(i));
        for (TopTools_ListIteratorOfListOfShape it(genList); it.More(); it.Next())
        {
            if (it.Value().ShapeType() == TopAbs_FACE)
                contactFaces.Add(it.Value());
        }
        const TopTools_ListOfShape& modList = cut.Modified(geoFaces(i));
        for (TopTools_ListIteratorOfListOfShape it(modList); it.More(); it.Next())
        {
            if (it.Value().ShapeType() == TopAbs_FACE)
                contactFaces.Add(it.Value());
        }
    }

    TopTools_IndexedMapOfShape resultFaces;
    TopExp::MapShapes(fittedDam, TopAbs_FACE, resultFaces);

    for (int i = 1; i <= resultFaces.Extent(); ++i)
    {
        const TopoDS_Shape& face = resultFaces(i);
        bool isContact = contactFaces.Contains(face);
        if (!isContact && !rawDamFaces.Contains(face))
        {
            isContact = true;
        }
        if (isContact)
        {
            contactFaceIndices.append(i);
        }
    }

    return true;
}
