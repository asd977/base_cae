#pragma once

// Explicit, non-destructive OCC topology preparation.
// The output BREP and audit map are a PAIR. They are not silently substituted
// for the Geometry module's active CAD or its persisted face/edge selections.
// Geometry must adopt the BREP and remap ALL dependent references atomically.

#include "SmeshShapeIndexMap.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

#include <BOPAlgo_Splitter.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdint>
#include <map>
#include <stdexcept>

class SmeshTopologyPreparation
{
public:
    struct Result {
        QString brepPath;
        QString mappingPath;
        int solids = 0;
        int commonFaceCount = 0;
        int aliasedOldFaceCount = 0;
        int unmappedFaces = 0;
    };

    // Explicit rebuild with rollback for the TWO output files. A failed new
    // finalization restores the previous complete pair, never the source CAD.
    static Result rebuild(const QString& sourceBrep, const QString& destinationBrep,
                          double fuzzyTolerance)
    {
        const QString map = destinationBrep + QStringLiteral(".topology-map.json");
        const QString backupBrep = destinationBrep + QStringLiteral(".previous");
        const QString backupMap = map + QStringLiteral(".previous");
        if (QFile::exists(backupBrep) || QFile::exists(backupMap))
            throw std::runtime_error("检测到旧版备份文件，请先核对备份后再执行几何最终化");
        const bool hadBrep = QFile::exists(destinationBrep);
        const bool hadMap = QFile::exists(map);
        if (hadBrep && !QFile::rename(destinationBrep, backupBrep))
            throw std::runtime_error("无法备份原最终化 BREP");
        if (hadMap && !QFile::rename(map, backupMap)) {
            if (hadBrep) QFile::rename(backupBrep, destinationBrep);
            throw std::runtime_error("无法备份旧映射报告");
        }
        try {
            Result result = create(sourceBrep, destinationBrep, fuzzyTolerance);
            if (hadBrep) QFile::remove(backupBrep);
            if (hadMap) QFile::remove(backupMap);
            return result;
        } catch (...) {
            QFile::remove(destinationBrep);
            QFile::remove(map);
            if (hadBrep && !QFile::rename(backupBrep, destinationBrep))
                throw std::runtime_error("最终化失败，旧 BREP 备份恢复失败，请检查 .previous 文件");
            if (hadMap && !QFile::rename(backupMap, map))
                throw std::runtime_error("最终化失败，旧映射备份恢复失败，请检查 .previous 文件");
            throw;
        }
    }

    static Result create(const QString& sourceBrep, const QString& destinationBrep, double fuzzyTolerance)
    {
        const QFileInfo inputInfo(sourceBrep), outputInfo(destinationBrep);
        if (!inputInfo.isFile() || !sourceBrep.endsWith(QStringLiteral(".brep"), Qt::CaseInsensitive))
            throw std::runtime_error("请选择有效的原始 BREP 文件");
        if (outputInfo.absoluteFilePath() == inputInfo.absoluteFilePath())
            throw std::runtime_error("禁止覆盖原始 CAD BREP");
        if (!std::isfinite(fuzzyTolerance) || fuzzyTolerance < 0.0)
            throw std::runtime_error("无效的 OCC Partition 容差");
        const QString mapFile = outputInfo.absoluteFilePath() + QStringLiteral(".topology-map.json");
        if (QFile::exists(destinationBrep) || QFile::exists(mapFile))
            throw std::runtime_error("目标文件或映射文件已存在：请另选输出名称");
        // Pin the exported CAD generation without reading/hashing the file.
        const QFileInfo sourceSnapshot(sourceBrep);
        const qint64 sourceBytes = sourceSnapshot.size();
        const QDateTime sourceModified = sourceSnapshot.lastModified();
        const SmeshShapeIndexMap originalUiMap = SmeshShapeIndexMap::load(sourceBrep);
        if (!originalUiMap.valid())
            throw std::runtime_error(originalUiMap.error().toStdString());

        BRep_Builder builder;
        TopoDS_Shape source;
        std::ifstream input(QFile::encodeName(sourceBrep).constData(), std::ios::binary);
        if (!input)
            throw std::runtime_error("无法打开 OCC 原始 BREP");
        BRepTools::Read(source, input, builder);
        if (source.IsNull())
            throw std::runtime_error("无法读取 OCC 原始 BREP");

        TopTools_IndexedMapOfShape oldSolids, oldFaces, oldEdges;
        TopExp::MapShapes(source, TopAbs_SOLID, oldSolids);
        TopExp::MapShapes(source, TopAbs_FACE, oldFaces);
        TopExp::MapShapes(source, TopAbs_EDGE, oldEdges);
        if (oldSolids.Extent() == 0) throw std::runtime_error("原始 BREP 不包含 Solid");

        BOPAlgo_Splitter splitter;
        splitter.SetNonDestructive(true);
        splitter.SetRunParallel(true);
        splitter.SetFuzzyValue(fuzzyTolerance);
        for (int i = 1; i <= oldSolids.Extent(); ++i)
            splitter.AddArgument(oldSolids(i));
        splitter.Perform();
        if (splitter.HasErrors() || splitter.Shape().IsNull())
            throw std::runtime_error("OCC Partition 失败；没有修改原始几何");
        const TopoDS_Shape resultShape = splitter.Shape();
        if (!BRepCheck_Analyzer(resultShape).IsValid())
            throw std::runtime_error("OCC Partition 输出几何无效；拒绝写入");

        TopTools_IndexedMapOfShape newSolids, newFaces, newEdges;
        TopExp::MapShapes(resultShape, TopAbs_SOLID, newSolids);
        TopExp::MapShapes(resultShape, TopAbs_FACE, newFaces);
        TopExp::MapShapes(resultShape, TopAbs_EDGE, newEdges);
        // No silent domain splitting/merging; material and physical field
        // bindings in the parent project depend on one-to-one Solid identity.
        if (newSolids.Extent() != oldSolids.Extent())
            throw std::runtime_error("Partition 改变了 Solid 数量；必须在几何模块重新建立域归属，已取消导出");
        std::map<int, int> newSolidToOld;
        for (int i = 1; i <= oldSolids.Extent(); ++i) {
            const auto mapped = images(splitter, oldSolids(i), TopAbs_SOLID, newSolids);
            if (mapped.size() != 1 || !newSolidToOld.emplace(mapped.front(), i).second)
                throw std::runtime_error("Partition 无法一一映射 Solid；已取消导出");
        }

        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), 2);
        manifest.insert(QStringLiteral("sourceBrep"), inputInfo.absoluteFilePath());
        manifest.insert(QStringLiteral("inputFaceCount"), oldFaces.Extent());
        manifest.insert(QStringLiteral("inputEdgeCount"), oldEdges.Extent());
        manifest.insert(QStringLiteral("outputFaceCount"), newFaces.Extent());
        manifest.insert(QStringLiteral("outputEdgeCount"), newEdges.Extent());
        manifest.insert(QStringLiteral("preparedBrep"), outputInfo.absoluteFilePath());
        manifest.insert(QStringLiteral("fuzzyTolerance"), fuzzyTolerance);
        manifest.insert(QStringLiteral("inputSolidCount"), oldSolids.Extent());
        manifest.insert(QStringLiteral("outputSolidCount"), newSolids.Extent());
        QJsonArray domainMappings;
        for (const auto& e : newSolidToOld) {
            QJsonObject entry;
            entry.insert(QStringLiteral("sourceIndex"), e.second);
            entry.insert(QStringLiteral("resultIndex"), e.first);
            domainMappings.append(entry);
        }
        manifest.insert(QStringLiteral("solidMap"), domainMappings);
        // All output indices refer to the EXACT prepared BREP. Do not apply
        // these indices to ExportedModel.brep or to existing GUI pick IDs.
        QJsonArray faceMap, edgeMap;
        std::map<int, QJsonArray> aliases;
        Result result;
        result.solids = newSolids.Extent();
        for (int i = 1; i <= oldFaces.Extent(); ++i) {
            const auto ids = images(splitter, oldFaces(i), TopAbs_FACE, newFaces);
            QJsonObject entry;
            entry.insert(QStringLiteral("sourceIndex"), i);
            QJsonArray mapped;
            for (int id : ids) {
                mapped.append(id);
                aliases[id].append(i);
            }
            entry.insert(QStringLiteral("resultIndices"), mapped);
            if (ids.empty()) ++result.unmappedFaces;
            faceMap.append(entry);
        }
        for (int i = 1; i <= oldEdges.Extent(); ++i) {
            const auto ids = images(splitter, oldEdges(i), TopAbs_EDGE, newEdges);
            QJsonObject entry;
            entry.insert(QStringLiteral("sourceIndex"), i);
            QJsonArray mapped;
            for (int id : ids) mapped.append(id);
            entry.insert(QStringLiteral("resultIndices"), mapped);
            edgeMap.append(entry);
        }
        manifest.insert(QStringLiteral("faceMap"), faceMap);
        manifest.insert(QStringLiteral("edgeMap"), edgeMap);
        QJsonArray sharedAliases;
        for (const auto& e : aliases) {
            if (e.second.size() < 2) continue;
            QJsonObject info;
            info.insert(QStringLiteral("preparedFaceIndex"), e.first);
            info.insert(QStringLiteral("originalFaceIndices"), e.second);
            sharedAliases.append(info);
            ++result.commonFaceCount;
            result.aliasedOldFaceCount += e.second.size();
        }
        manifest.insert(QStringLiteral("mergedFaceAliases"), sharedAliases);
        // Resolve the UI -> old BREP IDs through the original snapshot manifest.
        // This provides a migration *report*, not an unsafe live rewrite.
        QJsonArray originalUiFaceMap;
        std::map<int, QJsonArray> preparedUiAliases;
        for (int ui = 1; ui <= originalUiMap.uiCount(2); ++ui) {
            const int oldIndex = originalUiMap.requireIndex(2, ui);
            if (oldIndex <= 0 || oldIndex > faceMap.size()) continue;
            QJsonObject entry;
            entry.insert(QStringLiteral("uiFaceId"), ui);
            entry.insert(QStringLiteral("originalBrepIndex"), oldIndex);
            entry.insert(QStringLiteral("preparedFaceIndices"), faceMap.at(oldIndex - 1).toObject().value(QStringLiteral("resultIndices")));
            originalUiFaceMap.append(entry);
            const auto indices = faceMap.at(oldIndex - 1).toObject().value(QStringLiteral("resultIndices")).toArray();
            for (const auto& index : indices) preparedUiAliases[index.toInt()].append(ui);
        }
        manifest.insert(QStringLiteral("uiFaceMap"), originalUiFaceMap);
        QJsonArray uiAliases;
        for (const auto& alias : preparedUiAliases) {
            if (alias.second.size() <= 1) continue;
            QJsonObject item;
            item.insert(QStringLiteral("preparedFaceIndex"), alias.first);
            item.insert(QStringLiteral("sourceUiFaceIds"), alias.second);
            uiAliases.append(item);
        }
        manifest.insert(QStringLiteral("uiFaceAliases"), uiAliases);

        const QString tempBrep = destinationBrep + QStringLiteral(".writing");
        if (QFile::exists(tempBrep))
            throw std::runtime_error("临时导出文件已存在；请清理后再试");
        try {
            {
                std::ofstream output(QFile::encodeName(tempBrep).constData(), std::ios::binary);
                if (!output) throw std::runtime_error("无法写入临时 BREP");
                BRepTools::Write(resultShape, output);
                output.flush();
                if (!output) throw std::runtime_error("输出 BREP 失败");
            }
            if (!QFile::rename(tempBrep, destinationBrep))
                throw std::runtime_error("无法提交新的 BREP 文件");
            // Validate that the serialized CAD still has the expected topology.
            // Mapping IDs are scoped to this prepared output snapshot, never to
            // the old CAD or to a later Geometry re-export.
            TopoDS_Shape roundTrip;
            std::ifstream check(QFile::encodeName(destinationBrep).constData(), std::ios::binary);
            if (!check)
                throw std::runtime_error("无法打开输出的 Partition BREP");
            BRepTools::Read(roundTrip, check, builder);
            if (roundTrip.IsNull())
                throw std::runtime_error("无法重新读取输出的 Partition BREP");
            TopTools_IndexedMapOfShape verifySolids, verifyFaces, verifyEdges;
            TopExp::MapShapes(roundTrip, TopAbs_SOLID, verifySolids);
            TopExp::MapShapes(roundTrip, TopAbs_FACE, verifyFaces);
            TopExp::MapShapes(roundTrip, TopAbs_EDGE, verifyEdges);
            if (verifySolids.Extent() != newSolids.Extent() ||
                verifyFaces.Extent() != newFaces.Extent() ||
                verifyEdges.Extent() != newEdges.Extent())
                throw std::runtime_error("Partition BREP 重读后拓扑数量不一致；拒绝映射");
            // Do not persist stale mappings if geometry export ran concurrently.
            const QFileInfo sourceNow(sourceBrep);
            if (!sourceNow.isFile() || sourceNow.size() != sourceBytes ||
                sourceNow.lastModified() != sourceModified)
                throw std::runtime_error("源 CAD 在几何最终化过程中被替换；已拒绝过期拓扑映射");
            QSaveFile outMap(mapFile);
            if (!outMap.open(QIODevice::WriteOnly))
                throw std::runtime_error("无法写入拓扑映射报告");
            const QByteArray bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
            if (outMap.write(bytes) != bytes.size() || !outMap.commit())
                throw std::runtime_error("拓扑映射报告提交失败");
        } catch (...) {
            QFile::remove(tempBrep);
            QFile::remove(destinationBrep);
            throw;
        }
        result.brepPath = destinationBrep;
        result.mappingPath = mapFile;
        return result;
    }
private:
    static std::vector<int> images(BOPAlgo_Splitter& splitter, const TopoDS_Shape& original,
                                   TopAbs_ShapeEnum type, const TopTools_IndexedMapOfShape& finalMap)
    {
        TopTools_IndexedMapOfShape mapped;
        for (TopTools_ListIteratorOfListOfShape it(splitter.Modified(original)); it.More(); it.Next()) {
            TopTools_IndexedMapOfShape children;
            TopExp::MapShapes(it.Value(), type, children);
            for (int i = 1; i <= children.Extent(); ++i)
                if (finalMap.Contains(children(i))) mapped.Add(children(i));
        }
        if (mapped.IsEmpty() && finalMap.Contains(original)) mapped.Add(original);
        std::vector<int> result;
        for (int i = 1; i <= mapped.Extent(); ++i)
            result.push_back(finalMap.FindIndex(mapped(i)));
        return result;
    }
};
