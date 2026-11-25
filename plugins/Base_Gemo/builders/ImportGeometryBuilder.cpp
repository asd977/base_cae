#include "ImportGeometryBuilder.h"
#include "DxfReader.h"
#include <BRepBuilderAPI_MakePolygon.hxx>

#include "GeometryBuildUtils.h"
#include <GeometryDisplayManager.h>

#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IGESControl_Reader.hxx>
#include <Precision.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ColorType.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <limits>
#include <utility>
#include <TopoDS_Edge.hxx>

using namespace GeometryBuildUtils;

namespace
{
struct StepImportRegistryEntry
{
    QVector<StepImportPartInfo> parts;
    QHash<QString, int> indexBySourceKey;
    QHash<QString, int> indexBySemanticKey;
};

QString stepSemanticKey(const QString& assemblyPath, const QString& originalName)
{
    const QString path = assemblyPath.simplified().toCaseFolded();
    if (!path.isEmpty())
        return path;
    return originalName.simplified().toCaseFolded();
}

QHash<QString, StepImportRegistryEntry>& stepImportRegistry()
{
    static QHash<QString, StepImportRegistryEntry> registry;
    return registry;
}

QHash<QString, StepImportOptions>& stepImportOptionsRegistry()
{
    static QHash<QString, StepImportOptions> registry;
    return registry;
}

const QVector<StepImportPartInfo>& emptyStepImportParts()
{
    static const QVector<StepImportPartInfo> empty;
    return empty;
}

QSet<QString>& dirtyStepImports()
{
    static QSet<QString> imports;
    return imports;
}

QString extendedStringToQString(const TCollection_ExtendedString& text)
{
    QString value;
    value.reserve(text.Length());
    for (Standard_Integer i = 1; i <= text.Length(); ++i)
        value.append(QChar(static_cast<ushort>(text.Value(i))));
    return value.trimmed();
}

QString labelName(const TDF_Label& label)
{
    if (label.IsNull())
        return QString();

    Handle(TDataStd_Name) attribute;
    if (!label.FindAttribute(TDataStd_Name::GetID(), attribute) || attribute.IsNull())
        return QString();
    return extendedStringToQString(attribute->Get());
}

QString labelEntry(const TDF_Label& label)
{
    if (label.IsNull())
        return QString();
    TCollection_AsciiString entry;
    TDF_Tool::Entry(label, entry);
    return QString::fromLatin1(entry.ToCString());
}

QString shapeTypeName(const TopoDS_Shape& shape)
{
    switch (shape.ShapeType())
    {
    case TopAbs_COMPSOLID: return QObject::tr("复合实体");
    case TopAbs_SOLID: return QObject::tr("实体");
    case TopAbs_SHELL: return QObject::tr("壳");
    case TopAbs_FACE: return QObject::tr("曲面");
    case TopAbs_WIRE: return QObject::tr("线框");
    case TopAbs_EDGE: return QObject::tr("边");
    default: return QObject::tr("几何");
    }
}

bool labelColor(const Handle(XCAFDoc_ColorTool)& colorTool,
                const TDF_Label& instanceLabel,
                const TDF_Label& referredLabel,
                const TopoDS_Shape& shape,
                QColor& result)
{
    if (colorTool.IsNull())
        return false;

    Quantity_Color color;
    auto readFromLabel = [&colorTool, &color](const TDF_Label& label) -> bool
    {
        if (label.IsNull() || colorTool.IsNull())
            return false;
        // 兼容当前工程所使用的 OCCT 版本：Label 颜色接口是成员函数，
        // 必须通过 XCAFDoc_ColorTool 对象调用。
        return colorTool->GetColor(label, XCAFDoc_ColorSurf, color) ||
               colorTool->GetColor(label, XCAFDoc_ColorGen, color);
    };

    bool found = readFromLabel(instanceLabel);
    if (!found && !shape.IsNull())
    {
        found = colorTool->GetInstanceColor(shape, XCAFDoc_ColorSurf, color) ||
                colorTool->GetInstanceColor(shape, XCAFDoc_ColorGen, color) ||
                colorTool->GetColor(shape, XCAFDoc_ColorSurf, color) ||
                colorTool->GetColor(shape, XCAFDoc_ColorGen, color);
    }
    if (!found)
        found = readFromLabel(referredLabel);
    if (!found)
        return false;

    result = QColor::fromRgbF(color.Red(), color.Green(), color.Blue());
    return result.isValid();
}

void appendStepBodies(const TopoDS_Shape& shape, QVector<TopoDS_Shape>& result)
{
    if (shape.IsNull())
        return;

    // 只展开 Compound / CompSolid 的直接子对象。不能用 TopExp_Explorer 只找 Solid，
    // 否则同一 STEP 零件中与实体并存的壳或曲面会被静默丢弃。
    if (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID)
    {
        bool hasChild = false;
        for (TopoDS_Iterator iterator(shape); iterator.More(); iterator.Next())
        {
            hasChild = true;
            appendStepBodies(iterator.Value(), result);
        }
        if (hasChild)
            return;
    }

    result.push_back(shape);
}

QVector<TopoDS_Shape> splitStepShape(const TopoDS_Shape& shape)
{
    QVector<TopoDS_Shape> result;
    appendStepBodies(shape, result);
    return result;
}

void collectStepLeafParts(const TDF_Label& label,
                          const Handle(XCAFDoc_ShapeTool)& shapeTool,
                          const Handle(XCAFDoc_ColorTool)& colorTool,
                          const StepImportOptions& options,
                          const QStringList& parentPath,
                          const QString& parentKey,
                          const TopLoc_Location& parentLocation,
                          QVector<StepImportPartInfo>& parts,
                          int& encounteredLeafCount,
                          int depth = 0)
{
    if (label.IsNull() || shapeTool.IsNull() || depth > 128)
        return;

    TDF_Label referredLabel;
    const bool isReference = shapeTool->GetReferredShape(label, referredLabel);
    const TDF_Label definitionLabel = isReference ? referredLabel : label;
    const TopLoc_Location globalLocation =
        parentLocation.Multiplied(shapeTool->GetLocation(label));
    const QString entry = labelEntry(label);
    const QString currentKey = parentKey.isEmpty() ? entry : parentKey + QStringLiteral("/") + entry;

    QString name = labelName(label);
    if (name.isEmpty())
        name = labelName(definitionLabel);
    if (name.isEmpty())
        name = QObject::tr("零件 %1").arg(parts.size() + 1);

    QStringList currentPath = parentPath;
    currentPath.push_back(name);

    if (shapeTool->IsAssembly(definitionLabel))
    {
        TDF_LabelSequence components;
        if (shapeTool->GetComponents(definitionLabel, components, Standard_False) &&
            components.Length() > 0)
        {
            for (Standard_Integer i = 1; i <= components.Length(); ++i)
                collectStepLeafParts(components.Value(i), shapeTool, colorTool, options,
                                     currentPath, currentKey, globalLocation, parts,
                                     encounteredLeafCount, depth + 1);
            return;
        }
    }

    // GetShape(componentLabel) 会带上当前实例自身的位置；这里只再叠加父装配位置，
    // 避免重复应用当前实例的 Location。非引用标签则直接读取其定义 Shape。
    TopoDS_Shape locatedShape = shapeTool->GetShape(label);
    if (locatedShape.IsNull())
        locatedShape = shapeTool->GetShape(definitionLabel);
    if (locatedShape.IsNull())
        return;
    if (!parentLocation.IsIdentity())
        locatedShape = locatedShape.Moved(parentLocation);

    ++encounteredLeafCount;

    QColor color;
    const bool hasColor = options.preserveColors &&
                          labelColor(colorTool, label, definitionLabel, locatedShape, color);
    const bool visible = colorTool.IsNull() ||
                         (colorTool->IsVisible(label) &&
                          (definitionLabel.IsNull() || colorTool->IsVisible(definitionLabel)));
    if (!options.includeHidden && !visible)
        return;

    QVector<TopoDS_Shape> leafShapes;
    if (options.splitMode == StepImportSplitMode::Body)
        leafShapes = splitStepShape(locatedShape);
    else
        leafShapes.push_back(locatedShape);
    for (int localIndex = 0; localIndex < leafShapes.size(); ++localIndex)
    {
        StepImportPartInfo part;
        part.sourceKey = currentKey + QStringLiteral("#body:") + QString::number(localIndex);
        part.name = name;
        if (leafShapes.size() > 1)
        {
            part.name += QObject::tr(" - %1 %2")
                             .arg(shapeTypeName(leafShapes.at(localIndex)))
                             .arg(localIndex + 1);
        }
        part.assemblyPath = currentPath.join(QStringLiteral(" / "));
        part.colorHex = hasColor ? color.name(QColor::HexRgb).toUpper() : QString();
        part.visible = visible;
        part.shape = leafShapes.at(localIndex);
        parts.push_back(part);
    }
}

void makeUniqueStepPartNames(QVector<StepImportPartInfo>& parts)
{
    QHash<QString, int> counts;
    for (StepImportPartInfo& part : parts)
    {
        QString base = part.name.simplified();
        if (base.isEmpty())
            base = shapeTypeName(part.shape);

        QStringList pathSegments =
            part.assemblyPath.split(QStringLiteral(" / "), Qt::SkipEmptyParts);
        if (!pathSegments.isEmpty())
            pathSegments.removeLast();
        const QString parentPath = pathSegments.join(QStringLiteral(" / ")).toCaseFolded();
        const QString key = parentPath + QStringLiteral("\x1F") + base.toCaseFolded();
        const int count = counts.value(key, 0) + 1;
        counts.insert(key, count);
        part.name = count == 1 ? base : QObject::tr("%1 (%2)").arg(base).arg(count);
    }
}

bool readStepXde(const QString& importUuid,
                 const QString& filePath,
                 TopoDS_Shape& rootShape,
                 QString& errorMessage)
{
    const StepImportOptions options = stepImportOptionsRegistry().value(importUuid);
    Handle(TDocStd_Document) document;
    Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    if (application.IsNull())
    {
        errorMessage = QObject::tr("无法初始化 STEP 读取组件。");
        return false;
    }

    application->NewDocument(TCollection_ExtendedString("MDTV-XCAF"), document);
    if (document.IsNull())
    {
        errorMessage = QObject::tr("无法创建 STEP 读取文档。");
        return false;
    }

    STEPCAFControl_Reader reader;
    reader.SetColorMode(Standard_True);
    reader.SetNameMode(Standard_True);
    // 当前模型树只使用结构、名称和颜色。图层及属性读取会遍历额外的
    // STEP/XDE 数据，但结果没有被使用，大文件导入时会增加明显开销。
    reader.SetLayerMode(Standard_False);
    reader.SetPropsMode(Standard_False);

    const IFSelect_ReturnStatus status = reader.ReadFile(filePath.toUtf8().constData());
    if (status != IFSelect_RetDone)
    {
        errorMessage = QObject::tr("STEP 文件读取失败，状态码：%1")
                           .arg(static_cast<int>(status));
        application->Close(document);
        return false;
    }
    if (!reader.Transfer(document))
    {
        errorMessage = QObject::tr("STEP 文件内容无法转换为可用几何。");
        application->Close(document);
        return false;
    }

    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(document->Main());
    if (shapeTool.IsNull())
    {
        errorMessage = QObject::tr("STEP 文件中没有可识别的形状结构。");
        application->Close(document);
        return false;
    }

    QVector<StepImportPartInfo> parts;
    int encounteredLeafCount = 0;
    TDF_LabelSequence freeLabels;
    shapeTool->GetFreeShapes(freeLabels);

    // 当前工程使用的 OCCT 版本没有 XCAFDoc_ShapeTool::GetOneShape()。
    // 直接由顶层自由 Shape 组成完整导入 Shape，作用与 GetOneShape() 相同，
    // 同时保留各个自由对象原有的装配位置。
    QVector<TopoDS_Shape> freeShapes;
    freeShapes.reserve(freeLabels.Length());
    for (Standard_Integer i = 1; i <= freeLabels.Length(); ++i)
    {
        const TDF_Label freeLabel = freeLabels.Value(i);
        const TopoDS_Shape freeShape = shapeTool->GetShape(freeLabel);
        if (!freeShape.IsNull())
            freeShapes.push_back(freeShape);

        if (options.splitMode != StepImportSplitMode::WholeModel)
        {
            collectStepLeafParts(freeLabel, shapeTool, colorTool, options, {}, QString(),
                                 TopLoc_Location(), parts, encounteredLeafCount);
        }
    }

    if (freeShapes.size() == 1)
    {
        rootShape = freeShapes.front();
    }
    else if (!freeShapes.isEmpty())
    {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const TopoDS_Shape& freeShape : freeShapes)
            builder.Add(compound, freeShape);
        rootShape = compound;
    }

    if (options.splitMode == StepImportSplitMode::WholeModel && !rootShape.IsNull())
    {
        StepImportPartInfo part;
        part.sourceKey = QStringLiteral("whole-model");
        part.name = QFileInfo(filePath).completeBaseName();
        if (part.name.trimmed().isEmpty())
            part.name = QObject::tr("STEP 模型");
        part.assemblyPath = part.name;
        part.visible = true;
        part.shape = rootShape;
        parts.push_back(part);
    }

    if (parts.isEmpty() && !rootShape.IsNull() && encounteredLeafCount == 0)
    {
        const QVector<TopoDS_Shape> fallbackShapes =
            options.splitMode == StepImportSplitMode::Body
                ? splitStepShape(rootShape)
                : QVector<TopoDS_Shape>{rootShape};
        for (int i = 0; i < fallbackShapes.size(); ++i)
        {
            StepImportPartInfo part;
            part.sourceKey = QStringLiteral("fallback:%1").arg(i);
            part.name = QObject::tr("STEP %1 %2")
                            .arg(shapeTypeName(fallbackShapes.at(i)))
                            .arg(i + 1);
            part.assemblyPath = part.name;
            part.shape = fallbackShapes.at(i);
            parts.push_back(part);
        }
    }

    if (rootShape.IsNull() && !parts.isEmpty())
    {
        if (parts.size() == 1)
        {
            rootShape = parts.front().shape;
        }
        else
        {
            TopoDS_Compound compound;
            BRep_Builder builder;
            builder.MakeCompound(compound);
            for (const StepImportPartInfo& part : parts)
                builder.Add(compound, part.shape);
            rootShape = compound;
        }
    }

    if (!options.includeHidden && parts.isEmpty() && encounteredLeafCount > 0)
    {
        errorMessage = QObject::tr(
            "STEP 文件中没有可见对象；可勾选‘导入 STEP 中隐藏的对象’后重试。");
        application->Close(document);
        return false;
    }

    if (rootShape.IsNull() || parts.isEmpty())
    {
        errorMessage = QObject::tr("STEP 文件中没有有效实体或曲面。");
        application->Close(document);
        return false;
    }

    makeUniqueStepPartNames(parts);
    for (int i = 0; i < parts.size(); ++i)
    {
        parts[i].partIndex = i;
        if (parts[i].sourceKey.isEmpty())
            parts[i].sourceKey = QStringLiteral("part:%1").arg(i);
        const QByteArray digest = QCryptographicHash::hash(
            parts[i].sourceKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
        parts[i].uuid = importUuid + QStringLiteral("_step_") + QString::fromLatin1(digest);
    }
    StepImportRegistryEntry registryEntry;
    registryEntry.parts = std::move(parts);
    registryEntry.indexBySourceKey.reserve(registryEntry.parts.size());
    registryEntry.indexBySemanticKey.reserve(registryEntry.parts.size());
    for (int i = 0; i < registryEntry.parts.size(); ++i)
    {
        const StepImportPartInfo& part = registryEntry.parts.at(i);
        if (!part.sourceKey.isEmpty() &&
            !registryEntry.indexBySourceKey.contains(part.sourceKey))
        {
            registryEntry.indexBySourceKey.insert(part.sourceKey, i);
        }
        const QString semanticKey = stepSemanticKey(part.assemblyPath, part.name);
        if (!semanticKey.isEmpty() &&
            !registryEntry.indexBySemanticKey.contains(semanticKey))
        {
            registryEntry.indexBySemanticKey.insert(semanticKey, i);
        }
    }
    const int recognizedPartCount = registryEntry.parts.size();
    stepImportRegistry().insert(importUuid, std::move(registryEntry));
    dirtyStepImports().insert(importUuid);
    QString modeText;
    switch (options.splitMode)
    {
    case StepImportSplitMode::WholeModel: modeText = QObject::tr("整体"); break;
    case StepImportSplitMode::Body: modeText = QObject::tr("实体"); break;
    case StepImportSplitMode::Part:
    default: modeText = QObject::tr("零件"); break;
    }
    errorMessage = QObject::tr("按%1识别到 %2 个可独立操作的 STEP 对象。")
                       .arg(modeText)
                       .arg(recognizedPartCount);
    application->Close(document);
    return true;
}

void applyActorColor(GeomShapeObject* object, const QColor& color)
{
    if (!object || !color.isValid())
        return;
    object->setDisplayColor(color);
}

bool buildConnectedDxfShape(const TopoDS_Compound& edgeCompound,
                            TopoDS_Shape& result,
                            int& closedProfileCount,
                            int& openWireCount)
{
    result.Nullify();
    closedProfileCount = 0;
    openWireCount = 0;

    constexpr double tolerance = 1.0e-6;
    Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape();
    for (TopExp_Explorer exp(edgeCompound, TopAbs_EDGE); exp.More(); exp.Next())
        edges->Append(exp.Current());
    if (edges->IsEmpty())
        return false;

    Handle(TopTools_HSequenceOfShape) wires = new TopTools_HSequenceOfShape();
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, tolerance,
                                                   Standard_False, wires);
    if (wires->IsEmpty())
        return false;

    TopoDS_Compound closedWires;
    TopoDS_Compound openWires;
    ShapeAnalysis_FreeBounds::DispatchWires(wires, closedWires, openWires);

    struct ClosedWireInfo
    {
        TopoDS_Wire wire;
        TopoDS_Face referenceFace;
        gp_Pnt samplePoint;
        double area = 0.0;
        int parent = -1;
        int depth = 0;
    };

    QVector<ClosedWireInfo> closed;
    for (TopExp_Explorer exp(closedWires, TopAbs_WIRE); exp.More(); exp.Next())
    {
        const TopoDS_Wire wire = TopoDS::Wire(exp.Current());
        if (wire.IsNull())
            continue;

        BRepBuilderAPI_MakeFace faceMaker(wire, Standard_True);
        faceMaker.Build();
        if (!faceMaker.IsDone() || faceMaker.Face().IsNull())
            continue;

        ClosedWireInfo info;
        info.wire = wire;
        info.referenceFace = faceMaker.Face();
        try
        {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(info.referenceFace, properties);
            info.area = std::abs(properties.Mass());
        }
        catch (...)
        {
            info.area = 0.0;
        }

        TopExp_Explorer vertexExplorer(wire, TopAbs_VERTEX);
        if (!vertexExplorer.More())
            continue;
        const TopoDS_Vertex vertex = TopoDS::Vertex(vertexExplorer.Current());
        if (vertex.IsNull())
            continue;
        info.samplePoint = BRep_Tool::Pnt(vertex);
        closed.push_back(info);
    }

    // 先按面积从大到小排列，再寻找最小的包含父轮廓。奇数层是孔，偶数层是实体岛。
    std::sort(closed.begin(), closed.end(), [](const ClosedWireInfo& left,
                                               const ClosedWireInfo& right)
    {
        return left.area > right.area;
    });
    for (int i = 0; i < closed.size(); ++i)
    {
        double bestParentArea = std::numeric_limits<double>::max();
        for (int j = 0; j < i; ++j)
        {
            if (closed.at(j).area <= closed.at(i).area + tolerance)
                continue;
            try
            {
                BRepClass_FaceClassifier classifier(closed.at(j).referenceFace,
                                                     closed.at(i).samplePoint,
                                                     tolerance);
                const TopAbs_State state = classifier.State();
                if ((state == TopAbs_IN || state == TopAbs_ON) &&
                    closed.at(j).area < bestParentArea)
                {
                    closed[i].parent = j;
                    bestParentArea = closed.at(j).area;
                }
            }
            catch (...)
            {
            }
        }
        if (closed.at(i).parent >= 0)
            closed[i].depth = closed.at(closed.at(i).parent).depth + 1;
    }

    BRep_Builder builder;
    TopoDS_Compound output;
    builder.MakeCompound(output);
    int outputCount = 0;

    for (int i = 0; i < closed.size(); ++i)
    {
        if ((closed.at(i).depth % 2) != 0)
            continue;

        BRepBuilderAPI_MakeFace faceMaker(closed.at(i).wire, Standard_True);
        for (int j = 0; j < closed.size(); ++j)
        {
            if (closed.at(j).parent == i && (closed.at(j).depth % 2) == 1)
                faceMaker.Add(closed.at(j).wire);
        }
        faceMaker.Build();
        if (faceMaker.IsDone() && !faceMaker.Face().IsNull())
            builder.Add(output, faceMaker.Face());
        else
            builder.Add(output, closed.at(i).wire);
        ++closedProfileCount;
        ++outputCount;
    }

    for (TopExp_Explorer exp(openWires, TopAbs_WIRE); exp.More(); exp.Next())
    {
        const TopoDS_Wire wire = TopoDS::Wire(exp.Current());
        if (wire.IsNull())
            continue;
        builder.Add(output, wire);
        ++openWireCount;
        ++outputCount;
    }

    if (outputCount <= 0)
        return false;
    result = output;
    return true;
}

bool readDxfRecordsImpl(const QString& path, QVector<DxfGeometryRecord>& records, QString& message)
{
    records.clear();
    QVector<GeometryDxf::Entity> entities;
    if (!GeometryDxf::read(path, entities, message))
        return false;

    for (const auto& entity : entities)
    {
        TopoDS_Shape shape;
        if (entity.points.size() == 1)
            shape = BRepBuilderAPI_MakeVertex(entity.points.first()).Vertex();
        else
        {
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& p : entity.points) polygon.Add(p);
            if (entity.closed) polygon.Close();
            if (!polygon.IsDone())
            {
                records.clear();
                message = QObject::tr("DXF 对象 %1 无法构造有效线框。").arg(entity.handle);
                return false;
            }
            shape = polygon.Wire();
        }
        records.push_back({entity.layer, entity.handle,
                           entity.points.size() == 1 ? QStringLiteral("POINT") : QStringLiteral("POLYLINE"),
                           shape});
    }
    return !records.isEmpty();
}

bool readDxfFile(const QString& path, TopoDS_Shape& shape, QString& message)
{
    QVector<DxfGeometryRecord> records;
    if(!readDxfRecordsImpl(path,records,message)) return false;
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    bool hasEdges = false;
    for (const auto& record : records)
        for (TopExp_Explorer edge(record.shape, TopAbs_EDGE); edge.More(); edge.Next())
        {
            builder.Add(compound, edge.Current());
            hasEdges = true;
        }
    TopoDS_Shape connected;
    int closed = 0, open = 0;
    if (hasEdges)
    {
        if (!buildConnectedDxfShape(compound, connected, closed, open))
        {
            message = QObject::tr("未找到可连接的有效曲线。");
            return false;
        }
        // Curves are connected to wires/faces; standalone survey points survive too.
        builder.MakeCompound(compound);
        builder.Add(compound, connected);
    }
    for (const auto& record : records)
        if (record.type == QStringLiteral("POINT")) builder.Add(compound, record.shape);
    shape = compound;
    message += QObject::tr(" 连接为 %1 个闭合轮廓、%2 条开放线框。").arg(closed).arg(open);
    return true;
}

QString occFailureMessage(const Standard_Failure& failure)
{
    const char* message = failure.GetMessageString();
    return message ? QString::fromLocal8Bit(message) : QObject::tr("几何处理失败");
}
} // namespace

void ImportGeometryBuilder::setStepImportOptions(const QString& importUuid,
                                                     const StepImportOptions& options)
{
    if (!importUuid.isEmpty())
        stepImportOptionsRegistry().insert(importUuid, options);
}

StepImportOptions ImportGeometryBuilder::stepImportOptions(const QString& importUuid)
{
    return importUuid.isEmpty()
        ? StepImportOptions{}
        : stepImportOptionsRegistry().value(importUuid, StepImportOptions{});
}

void ImportGeometryBuilder::clearStepImportOptions(const QString& importUuid)
{
    if (!importUuid.isEmpty())
        stepImportOptionsRegistry().remove(importUuid);
}

bool ImportGeometryBuilder::readDxfRecords(const QString& path, QVector<DxfGeometryRecord>& records, QString& message)
{
    try {return readDxfRecordsImpl(path,records,message);}
    catch(const Standard_Failure& ex) {records.clear();message=occFailureMessage(ex);return false;}
    catch(const std::exception& ex) {records.clear();message=QString::fromLocal8Bit(ex.what());return false;}
}

bool ImportGeometryBuilder::readDxfShape(const QString& filePath,
                                         TopoDS_Shape& shape,
                                         QString& errorMsg)
{
    shape.Nullify();
    errorMsg.clear();
    try
    {
        return readDxfFile(filePath, shape, errorMsg);
    }
    catch (const Standard_Failure& failure)
    {
        errorMsg = QObject::tr("DXF 解析失败：%1").arg(occFailureMessage(failure));
    }
    catch (const std::exception& error)
    {
        errorMsg = QObject::tr("DXF 解析失败：%1")
                       .arg(QString::fromLocal8Bit(error.what()));
    }
    catch (...)
    {
        errorMsg = QObject::tr("DXF 解析失败：未知异常。");
    }
    return false;
}

bool ImportGeometryBuilder::recreateImportGeometry(const QString& uuid,
                                                    const QString& name,
                                                    const QString& filePath,
                                                    QString& errorMsg)
{
    errorMsg.clear();
    const QFileInfo fileInfo(filePath);
    if (uuid.isEmpty())
    {
        errorMsg = QObject::tr("几何对象标识为空。");
        return false;
    }
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        errorMsg = QObject::tr("文件不存在：%1").arg(QDir::toNativeSeparators(filePath));
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    const bool isStep = suffix == QStringLiteral("step") || suffix == QStringLiteral("stp");
    const auto previousStepIt = stepImportRegistry().constFind(uuid);
    const bool hadPreviousStepEntry = previousStepIt != stepImportRegistry().constEnd();
    const StepImportRegistryEntry previousStepEntry = hadPreviousStepEntry
        ? previousStepIt.value()
        : StepImportRegistryEntry{};
    const bool previousStepWasDirty = dirtyStepImports().contains(uuid);
    auto restorePreviousStepEntry = [&]
    {
        if (!isStep)
            return;
        if (hadPreviousStepEntry)
            stepImportRegistry().insert(uuid, previousStepEntry);
        else
            stepImportRegistry().remove(uuid);
        if (previousStepWasDirty)
            dirtyStepImports().insert(uuid);
        else
            dirtyStepImports().remove(uuid);
    };

    std::unique_ptr<TopoDS_Shape> shape(new TopoDS_Shape());
    bool success = false;

    try
    {
        if (isStep)
        {
            // readStepXde() 先在局部文档中完成读取，成功后才替换 registry。
            // 失败时保留旧 STEP 零件，使重新导入具备事务语义。
            success = readStepXde(uuid, filePath, *shape, errorMsg);
        }
        else if (suffix == QStringLiteral("iges") || suffix == QStringLiteral("igs"))
        {
            IGESControl_Reader reader;
            const IFSelect_ReturnStatus status = reader.ReadFile(filePath.toUtf8().constData());
            if (status != IFSelect_RetDone)
            {
                errorMsg = QObject::tr("IGES 文件读取失败，状态码：%1").arg(static_cast<int>(status));
            }
            else if (reader.TransferRoots() <= 0)
            {
                errorMsg = QObject::tr("IGES 文件中没有可转换的几何根对象。");
            }
            else
            {
                *shape = reader.OneShape();
                success = !shape->IsNull();
            }
        }
        else if (suffix == QStringLiteral("brep"))
        {
            BRep_Builder builder;
            BRepTools::Read(*shape, filePath.toUtf8().constData(), builder);
            success = !shape->IsNull();
            if (!success)
                errorMsg = QObject::tr("几何文件读取失败。");
        }
        else if (suffix == QStringLiteral("dxf"))
        {
            success = readDxfFile(filePath, *shape, errorMsg);
        }
        else
        {
            errorMsg = QObject::tr(
                "不支持的文件格式：.%1\n支持 STEP/STP、IGES/IGS、BREP 和 DXF。")
                           .arg(suffix);
        }
    }
    catch (const Standard_Failure& failure)
    {
        errorMsg = QObject::tr("几何文件解析失败：%1").arg(occFailureMessage(failure));
        success = false;
    }
    catch (const std::exception& error)
    {
        errorMsg = QObject::tr("几何文件解析失败：%1")
                       .arg(QString::fromLocal8Bit(error.what()));
        success = false;
    }
    catch (...)
    {
        errorMsg = QObject::tr("几何文件解析失败：未知异常。");
        success = false;
    }

    if (!success || shape->IsNull())
    {
        restorePreviousStepEntry();
        if (errorMsg.isEmpty())
            errorMsg = QObject::tr("几何文件中没有有效形状。");
        return false;
    }

    // STEP 根 Shape 只用于完整文件缓存和恢复。后续会按 XDE 结构创建
    // 独立实体节点，因此这里不再为根 Shape 生成一套重复的 VTK 网格。
    if (isStep)
        GeometryDisplayManager::setVisible(uuid, false, false);
    registerShape(uuid, name, shape.release(), !isStep);
    if (!validShapeObject(uuid))
    {
        restorePreviousStepEntry();
        errorMsg = QObject::tr("几何已读取，但注册到当前工程失败。");
        return false;
    }
    if (!isStep)
        clearStepImportParts(uuid);
    return true;
}

const QVector<StepImportPartInfo>& ImportGeometryBuilder::stepImportParts(const QString& importUuid)
{
    const auto it = stepImportRegistry().constFind(importUuid);
    return it == stepImportRegistry().constEnd() ? emptyStepImportParts() : it->parts;
}

bool ImportGeometryBuilder::stepImportNeedsSynchronization(const QString& importUuid)
{
    return !importUuid.isEmpty() && dirtyStepImports().contains(importUuid);
}

void ImportGeometryBuilder::markStepImportSynchronized(const QString& importUuid)
{
    if (!importUuid.isEmpty())
        dirtyStepImports().remove(importUuid);
}

bool ImportGeometryBuilder::recreateStepImportPart(const QString& uuid,
                                                   const QString& name,
                                                   const QString& importUuid,
                                                   int partIndex,
                                                   const QString& sourceKey,
                                                   const QString& originalName,
                                                   const QString& assemblyPath,
                                                   const QString& colorHex,
                                                   QString& errorMsg)
{
    errorMsg.clear();
    if (uuid.isEmpty() || importUuid.isEmpty() ||
        (partIndex < 0 && sourceKey.isEmpty() && originalName.isEmpty() && assemblyPath.isEmpty()))
    {
        errorMsg = QObject::tr("STEP 实体恢复参数无效。");
        return false;
    }

    const auto registryIt = stepImportRegistry().constFind(importUuid);
    if (registryIt == stepImportRegistry().constEnd())
    {
        errorMsg = QObject::tr("STEP 源对象尚未恢复；请重新导入源 STEP 文件。");
        return false;
    }

    const StepImportRegistryEntry& registryEntry = registryIt.value();
    const StepImportPartInfo* matchedPart = nullptr;
    if (!sourceKey.isEmpty())
    {
        const auto sourceIt = registryEntry.indexBySourceKey.constFind(sourceKey);
        if (sourceIt != registryEntry.indexBySourceKey.constEnd() &&
            sourceIt.value() >= 0 && sourceIt.value() < registryEntry.parts.size())
        {
            matchedPart = &registryEntry.parts.at(sourceIt.value());
        }
    }
    if (!matchedPart)
    {
        const QString semanticKey = stepSemanticKey(assemblyPath, originalName);
        const auto semanticIt = registryEntry.indexBySemanticKey.constFind(semanticKey);
        if (!semanticKey.isEmpty() && semanticIt != registryEntry.indexBySemanticKey.constEnd() &&
            semanticIt.value() >= 0 && semanticIt.value() < registryEntry.parts.size())
        {
            matchedPart = &registryEntry.parts.at(semanticIt.value());
        }
    }
    if (!matchedPart && partIndex >= 0 && partIndex < registryEntry.parts.size())
    {
        const StepImportPartInfo& indexedPart = registryEntry.parts.at(partIndex);
        if (originalName.trimmed().isEmpty() ||
            indexedPart.name.simplified().compare(originalName.simplified(), Qt::CaseInsensitive) == 0)
        {
            matchedPart = &indexedPart;
        }
    }
    if (!matchedPart)
    {
        errorMsg = QObject::tr("STEP 实体不存在；请重新导入源 STEP 文件。");
        return false;
    }

    const StepImportPartInfo& part = *matchedPart;
    if (part.shape.IsNull())
    {
        errorMsg = QObject::tr("STEP 实体 %1 的几何为空。")
                       .arg(part.partIndex + 1);
        return false;
    }

    registerShape(uuid, name, new TopoDS_Shape(part.shape));
    if (!validShapeObject(uuid))
    {
        errorMsg = QObject::tr("STEP 实体已识别，但注册到当前工程失败。");
        return false;
    }

    applyStepColor(uuid, colorHex.isEmpty() ? part.colorHex : colorHex);
    return true;
}

void ImportGeometryBuilder::applyStepColor(const QString& uuid, const QString& colorHex)
{
    if (uuid.isEmpty() || colorHex.trimmed().isEmpty())
        return;

    const QColor color(colorHex.trimmed());
    if (!color.isValid())
        return;

    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = manager ? manager->getGeomShapeObject(uuid) : nullptr;
    applyActorColor(object, color);
}

void ImportGeometryBuilder::clearStepImportParts(const QString& importUuid)
{
    if (importUuid.isEmpty())
        return;
    stepImportRegistry().remove(importUuid);
    dirtyStepImports().remove(importUuid);
}
