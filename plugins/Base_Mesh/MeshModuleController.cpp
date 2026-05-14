#include "MeshModuleController.h"

#include <EventManager.h>
#include <GridEditingWidget.h>
#include <MeshEdgeControlWidget.h>
#include <MeshFaceControlWidget.h>
#include <MeshGlobalMeshingWidget.h>
#include <MeshGroupingWidget.h>
#include <MeshSingleMeshingWidget.h>
#include <MeshVolumeControlWidget.h>
#include <PickManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <qfiledialog.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPointLocator.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <functional>
#include <limits>


#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

static const char* kMeshMapFileName = "mesh_map.json";
static const char* kMeshMshSnapshotFileName = "mesh_snapshot.msh";

static bool copyFileOverwrite(const QString& sourcePath, const QString& targetPath)
{
    if (sourcePath.trimmed().isEmpty() || targetPath.trimmed().isEmpty())
        return false;
    if (!QFileInfo::exists(sourcePath))
        return false;

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath))
        return false;

    return QFile::copy(sourcePath, targetPath);
}

static QString currentGeneratedMshPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tempGrid/mesh.msh"));
}

static QString chooseMshSnapshotSource(const QString& sourceFileName)
{
    const QFileInfo srcInfo(sourceFileName);
    if (srcInfo.exists() && srcInfo.suffix().compare(QStringLiteral("msh"), Qt::CaseInsensitive) == 0)
        return srcInfo.absoluteFilePath();

           // m_sourceFileName 为空通常表示来自当前 Gmsh 内存模型划分，此时 MeshProcessor 会写出 tempGrid/mesh.msh。
    if (sourceFileName.trimmed().isEmpty())
    {
        const QString generated = currentGeneratedMshPath();
        if (QFileInfo::exists(generated))
            return generated;
    }

    return QString();
}

static QJsonArray meshIdVectorToJson(const QVector<MeshIdType>& ids)
{
    QJsonArray arr;
    for (MeshIdType id : ids)
        arr.append(QString::number(static_cast<qint64>(id)));
    return arr;
}

static QVector<MeshIdType> jsonToMeshIdVector(const QJsonArray& arr)
{
    QVector<MeshIdType> ids;
    ids.reserve(arr.size());
    for (const auto& v : arr)
        ids.push_back(static_cast<MeshIdType>(v.toString().toLongLong()));
    return ids;
}

static QJsonArray intVectorToJson(const QVector<int>& ids)
{
    QJsonArray arr;
    for (int id : ids)
        arr.append(id);
    return arr;
}

static QVector<int> jsonToIntVector(const QJsonArray& arr)
{
    QVector<int> ids;
    ids.reserve(arr.size());
    for (const auto& v : arr)
        ids.push_back(v.toInt());
    return ids;
}

static QJsonArray int64VectorToJson(const QVector<qint64>& ids)
{
    QJsonArray arr;
    for (qint64 id : ids)
        arr.append(QString::number(id));
    return arr;
}

static QVector<qint64> jsonToInt64Vector(const QJsonArray& arr)
{
    QVector<qint64> ids;
    ids.reserve(arr.size());
    for (const auto& v : arr)
        ids.push_back(v.toString().toLongLong());
    return ids;
}

static QJsonArray nestedIntVectorToJson(const QVector<QVector<int>>& values)
{
    QJsonArray arr;

    for (const QVector<int>& one : values)
    {
        QJsonArray item;
        for (int v : one)
            item.append(v);
        arr.append(item);
    }

    return arr;
}

static QVector<QVector<int>> jsonToNestedIntVector(const QJsonArray& arr)
{
    QVector<QVector<int>> values;
    values.reserve(arr.size());

    for (const auto& v : arr)
    {
        QVector<int> one;
        const QJsonArray item = v.toArray();
        one.reserve(item.size());

        for (const auto& x : item)
            one.push_back(x.toInt());

        values.push_back(one);
    }

    return values;
}

static QJsonObject intStringHashToJson(const QHash<int, QString>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = it.value();
    return obj;
}

static QHash<int, QString> jsonToIntStringHash(const QJsonObject& obj)
{
    QHash<int, QString> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), it.value().toString());
    return map;
}

static QJsonObject intIntHashToJson(const QHash<int, int>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = it.value();
    return obj;
}

static QHash<int, int> jsonToIntIntHash(const QJsonObject& obj)
{
    QHash<int, int> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), it.value().toInt());
    return map;
}

static QJsonObject intVectorHashToJson(const QHash<int, QVector<int>>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = intVectorToJson(it.value());
    return obj;
}

static QHash<int, QVector<int>> jsonToIntVectorHash(const QJsonObject& obj)
{
    QHash<int, QVector<int>> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), jsonToIntVector(it.value().toArray()));
    return map;
}

static QJsonObject intInt64VectorHashToJson(const QHash<int, QVector<qint64>>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = int64VectorToJson(it.value());
    return obj;
}

static QHash<int, QVector<qint64>> jsonToIntInt64VectorHash(const QJsonObject& obj)
{
    QHash<int, QVector<qint64>> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), jsonToInt64Vector(it.value().toArray()));
    return map;
}

static QJsonObject nestedIntVectorHashToJson(const QHash<int, QVector<QVector<int>>>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = nestedIntVectorToJson(it.value());
    return obj;
}

static QHash<int, QVector<QVector<int>>> jsonToNestedIntVectorHash(const QJsonObject& obj)
{
    QHash<int, QVector<QVector<int>>> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), jsonToNestedIntVector(it.value().toArray()));
    return map;
}

static QJsonObject meshTagKeyToJson(const MeshTagKey& key)
{
    QJsonObject obj;
    obj["category"] = key.category;
    obj["dim"] = key.dim;
    obj["id"] = key.id;
    return obj;
}

static MeshTagKey jsonToMeshTagKey(const QJsonObject& obj)
{
    MeshTagKey key;
    key.category = obj.value("category").toString();
    key.dim = obj.value("dim").toInt();
    key.id = obj.value("id").toInt();
    return key;
}

static QJsonArray meshTagKeyVectorToJson(const QVector<MeshTagKey>& keys)
{
    QJsonArray arr;
    for (const MeshTagKey& key : keys)
        arr.append(meshTagKeyToJson(key));
    return arr;
}

static QVector<MeshTagKey> jsonToMeshTagKeyVector(const QJsonArray& arr)
{
    QVector<MeshTagKey> keys;
    keys.reserve(arr.size());

    for (const auto& v : arr)
        keys.push_back(jsonToMeshTagKey(v.toObject()));

    return keys;
}

static QJsonObject meshIdTagKeyVectorHashToJson(const QHash<MeshIdType, QVector<MeshTagKey>>& map)
{
    QJsonObject obj;

    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        obj[QString::number(static_cast<qint64>(it.key()))] =
            meshTagKeyVectorToJson(it.value());
    }

    return obj;
}

static QHash<MeshIdType, QVector<MeshTagKey>> jsonToMeshIdTagKeyVectorHash(
    const QJsonObject& obj)
{
    QHash<MeshIdType, QVector<MeshTagKey>> map;

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        const MeshIdType id = static_cast<MeshIdType>(it.key().toLongLong());
        map.insert(id, jsonToMeshTagKeyVector(it.value().toArray()));
    }

    return map;
}

static QJsonObject meshTagSetToJson(const MeshTagSet& set)
{
    QJsonObject obj;

    obj["key"] = meshTagKeyToJson(set.key);
    obj["name"] = set.name;
    obj["source"] = set.source;
    obj["point_ids"] = meshIdVectorToJson(set.pointIds);
    obj["cell_ids"] = meshIdVectorToJson(set.cellIds);
    obj["edge_keys"] = meshIdVectorToJson(set.edgeKeys);
    obj["face_keys"] = meshIdVectorToJson(set.faceKeys);

    return obj;
}

static MeshTagSet jsonToMeshTagSet(const QJsonObject& obj)
{
    MeshTagSet set;

    set.key = jsonToMeshTagKey(obj.value("key").toObject());
    set.name = obj.value("name").toString();
    set.source = obj.value("source").toString();
    set.pointIds = jsonToMeshIdVector(obj.value("point_ids").toArray());
    set.cellIds = jsonToMeshIdVector(obj.value("cell_ids").toArray());
    set.edgeKeys = jsonToMeshIdVector(obj.value("edge_keys").toArray());
    set.faceKeys = jsonToMeshIdVector(obj.value("face_keys").toArray());

    return set;
}

static QHash<int, int> buildPointIdToNodeId(vtkUnstructuredGrid* grid)
{
    QHash<int, int> pointToNode;

    auto* nodeIdArr = grid && grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    if (!grid || !nodeIdArr)
        return pointToNode;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
        pointToNode.insert(static_cast<int>(pid), nodeIdArr->GetValue(pid));

    return pointToNode;
}

static QHash<int, int> buildNodeIdToPointId(vtkUnstructuredGrid* grid)
{
    QHash<int, int> nodeToPoint;

    auto* nodeIdArr = grid && grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    if (!grid || !nodeIdArr)
        return nodeToPoint;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
        nodeToPoint.insert(nodeIdArr->GetValue(pid), static_cast<int>(pid));

    return nodeToPoint;
}

static QVector<int> pointIdsToNodeIds(const QVector<int>& pointIds,
                                      const QHash<int, int>& pointToNode)
{
    QVector<int> nodeIds;
    nodeIds.reserve(pointIds.size());

    for (int pid : pointIds)
    {
        if (pointToNode.contains(pid))
            nodeIds.push_back(pointToNode.value(pid));
    }

    std::sort(nodeIds.begin(), nodeIds.end());
    nodeIds.erase(std::unique(nodeIds.begin(), nodeIds.end()), nodeIds.end());

    return nodeIds;
}

static QHash<int, QVector<int>> pointPhysToNodePhys(
    const QHash<int, QVector<int>>& physToPointIds,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<int>> out;

    for (auto it = physToPointIds.constBegin(); it != physToPointIds.constEnd(); ++it)
        out.insert(it.key(), pointIdsToNodeIds(it.value(), pointToNode));

    return out;
}

static QVector<int> nodeIdsToPointIds(const QVector<int>& nodeIds,
                                      const QHash<int, int>& nodeToPoint)
{
    QVector<int> pointIds;
    pointIds.reserve(nodeIds.size());

    for (int nodeId : nodeIds)
    {
        if (nodeToPoint.contains(nodeId))
            pointIds.push_back(nodeToPoint.value(nodeId));
    }

    std::sort(pointIds.begin(), pointIds.end());
    pointIds.erase(std::unique(pointIds.begin(), pointIds.end()), pointIds.end());

    return pointIds;
}

static QHash<int, QVector<int>> nodePhysToPointPhys(
    const QHash<int, QVector<int>>& physToNodeIds,
    const QHash<int, int>& nodeToPoint)
{
    QHash<int, QVector<int>> out;

    for (auto it = physToNodeIds.constBegin(); it != physToNodeIds.constEnd(); ++it)
        out.insert(it.key(), nodeIdsToPointIds(it.value(), nodeToPoint));

    return out;
}

static QHash<int, QVector<QVector<int>>> edgeNodeElemsFromKeys(
    vtkUnstructuredGrid* grid,
    const MshPickMap& pick,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<QVector<int>>> out;

    if (!grid)
        return out;

    for (auto it = pick.bndEdgePhysToEdgeKeys.constBegin();
         it != pick.bndEdgePhysToEdgeKeys.constEnd(); ++it)
    {
        const int physId = it.key();

        for (qint64 edgeKey : it.value())
        {
            const vtkIdType cid = static_cast<vtkIdType>(edgeKey >> 4);
            const int localEdgeId = static_cast<int>(edgeKey & 0xF);

            vtkCell* cell = grid->GetCell(cid);
            vtkCell* edge = cell ? cell->GetEdge(localEdgeId) : nullptr;

            if (!edge || edge->GetNumberOfPoints() < 2)
                continue;

            const int p0 = static_cast<int>(edge->GetPointId(0));
            const int p1 = static_cast<int>(edge->GetPointId(1));

            if (!pointToNode.contains(p0) || !pointToNode.contains(p1))
                continue;

            QVector<int> nodePair;
            nodePair.push_back(pointToNode.value(p0));
            nodePair.push_back(pointToNode.value(p1));
            out[physId].push_back(nodePair);
        }
    }

    return out;
}

static QHash<int, QVector<QVector<int>>> faceNodeElemsFromKeys(
    vtkUnstructuredGrid* grid,
    const MshPickMap& pick,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<QVector<int>>> out;

    if (!grid)
        return out;

    for (auto it = pick.bndFacePhysToFaceKeys.constBegin();
         it != pick.bndFacePhysToFaceKeys.constEnd(); ++it)
    {
        const int physId = it.key();

        for (qint64 faceKey : it.value())
        {
            const vtkIdType cid = static_cast<vtkIdType>(faceKey >> 4);
            const int localFaceId = static_cast<int>(faceKey & 0xF);

            vtkCell* cell = grid->GetCell(cid);
            vtkCell* face = cell ? cell->GetFace(localFaceId) : nullptr;

            if (!face || face->GetNumberOfPoints() < 3)
                continue;

            QVector<int> nodeIds;
            nodeIds.reserve(face->GetNumberOfPoints());

            for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
            {
                const int pid = static_cast<int>(face->GetPointId(i));
                if (pointToNode.contains(pid))
                    nodeIds.push_back(pointToNode.value(pid));
            }

            if (nodeIds.size() >= 3)
                out[physId].push_back(nodeIds);
        }
    }

    return out;
}

static quint64 packEdgePointIds(int a, int b)
{
    const quint32 x = static_cast<quint32>(qMin(a, b));
    const quint32 y = static_cast<quint32>(qMax(a, b));
    return (quint64(x) << 32) | quint64(y);
}

static QByteArray packFacePointIds(QVector<int> ids)
{
    std::sort(ids.begin(), ids.end());

    QByteArray key;
    key.reserve(ids.size() * int(sizeof(qint32)));

    for (int id : ids)
    {
        const qint32 v = qint32(id);
        key.append(reinterpret_cast<const char*>(&v), int(sizeof(qint32)));
    }

    return key;
}

static void rebuildCurrentNodeMaps(MshPickMap& pick, vtkUnstructuredGrid* grid)
{
    pick.nodeIdToPointId.clear();
    pick.pointIdToNodeId.clear();

    auto* nodeIdArr = grid && grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    if (!grid || !nodeIdArr)
        return;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        const int nodeId = nodeIdArr->GetValue(pid);
        const int pointId = static_cast<int>(pid);
        pick.nodeIdToPointId.insert(nodeId, pointId);
        pick.pointIdToNodeId.insert(pointId, nodeId);
    }
}

static void rebuildBoundaryRuntimeIndexFromNodeData(
    MshPickMap& pick,
    vtkUnstructuredGrid* grid,
    const QHash<int, QVector<int>>& pointPhysToNodeIds,
    const QHash<int, QVector<int>>& edgePhysToNodeIds,
    const QHash<int, QVector<int>>& facePhysToNodeIds,
    const QHash<int, QVector<QVector<int>>>& edgeNodeElems,
    const QHash<int, QVector<QVector<int>>>& faceNodeElems)
{
    pick.bndPointPhysToPointIds.clear();
    pick.bndEdgePhysToPointIds.clear();
    pick.bndFacePhysToPointIds.clear();
    pick.bndEdgePhysToEdgeKeys.clear();
    pick.bndFacePhysToFaceKeys.clear();

    if (!grid)
        return;

    const QHash<int, int> nodeToPoint = buildNodeIdToPointId(grid);

    pick.bndPointPhysToPointIds = nodePhysToPointPhys(pointPhysToNodeIds, nodeToPoint);
    pick.bndEdgePhysToPointIds = nodePhysToPointPhys(edgePhysToNodeIds, nodeToPoint);
    pick.bndFacePhysToPointIds = nodePhysToPointPhys(facePhysToNodeIds, nodeToPoint);

    QHash<quint64, qint64> edgeIndex;
    QHash<QByteArray, qint64> faceIndex;

    const vtkIdType nCells = grid->GetNumberOfCells();

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;

        const int nEdges = cell->GetNumberOfEdges();

        for (int ei = 0; ei < nEdges; ++ei)
        {
            vtkCell* edge = cell->GetEdge(ei);

            if (!edge || edge->GetNumberOfPoints() < 2)
                continue;

            const int p0 = static_cast<int>(edge->GetPointId(0));
            const int p1 = static_cast<int>(edge->GetPointId(1));
            const quint64 key = packEdgePointIds(p0, p1);
            const qint64 edgeKey = (qint64(cid) << 4) | (qint64(ei) & 0xF);

            if (!edgeIndex.contains(key))
                edgeIndex.insert(key, edgeKey);
        }

        const int nFaces = cell->GetNumberOfFaces();

        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* face = cell->GetFace(fi);

            if (!face || face->GetNumberOfPoints() < 3)
                continue;

            QVector<int> pids;
            pids.reserve(face->GetNumberOfPoints());

            for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
                pids.push_back(static_cast<int>(face->GetPointId(i)));

            const QByteArray key = packFacePointIds(pids);
            const qint64 faceKey = (qint64(cid) << 4) | (qint64(fi) & 0xF);

            if (!faceIndex.contains(key))
                faceIndex.insert(key, faceKey);
        }
    }

    for (auto it = edgeNodeElems.constBegin(); it != edgeNodeElems.constEnd(); ++it)
    {
        const int physId = it.key();

        for (const QVector<int>& nodePair : it.value())
        {
            if (nodePair.size() < 2)
                continue;

            const int p0 = nodeToPoint.value(nodePair[0], -1);
            const int p1 = nodeToPoint.value(nodePair[1], -1);

            if (p0 < 0 || p1 < 0)
                continue;

            const quint64 key = packEdgePointIds(p0, p1);
            if (edgeIndex.contains(key))
                pick.bndEdgePhysToEdgeKeys[physId].push_back(edgeIndex.value(key));
        }
    }

    for (auto it = faceNodeElems.constBegin(); it != faceNodeElems.constEnd(); ++it)
    {
        const int physId = it.key();

        for (const QVector<int>& faceNodes : it.value())
        {
            QVector<int> pids;
            pids.reserve(faceNodes.size());

            for (int nodeId : faceNodes)
            {
                const int pid = nodeToPoint.value(nodeId, -1);
                if (pid >= 0)
                    pids.push_back(pid);
            }

            if (pids.size() < 3)
                continue;

            const QByteArray key = packFacePointIds(pids);
            if (faceIndex.contains(key))
                pick.bndFacePhysToFaceKeys[physId].push_back(faceIndex.value(key));
        }
    }

    auto dedupInt = [](QVector<int>& values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };

    auto dedupI64 = [](QVector<qint64>& values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };

    for (auto it = pick.bndPointPhysToPointIds.begin(); it != pick.bndPointPhysToPointIds.end(); ++it)
        dedupInt(it.value());
    for (auto it = pick.bndEdgePhysToPointIds.begin(); it != pick.bndEdgePhysToPointIds.end(); ++it)
        dedupInt(it.value());
    for (auto it = pick.bndFacePhysToPointIds.begin(); it != pick.bndFacePhysToPointIds.end(); ++it)
        dedupInt(it.value());
    for (auto it = pick.bndEdgePhysToEdgeKeys.begin(); it != pick.bndEdgePhysToEdgeKeys.end(); ++it)
        dedupI64(it.value());
    for (auto it = pick.bndFacePhysToFaceKeys.begin(); it != pick.bndFacePhysToFaceKeys.end(); ++it)
        dedupI64(it.value());
}

static void refreshPhysicalBoundaryTagSets(MshPickMap& pick)
{
    QVector<MeshTagKey> removeKeys;

    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagKey& key = it.key();
        if (key.category == QStringLiteral("Physical") && key.dim < pick.meshDim)
            removeKeys.push_back(key);
    }

    for (const MeshTagKey& key : removeKeys)
        pick.tagSets.remove(key);

    auto addBoundarySet = [&](int physId, int dim, const QVector<int>& pointIds)
    {
        if (physId <= 0 || pointIds.isEmpty())
            return;

        MeshTagKey key;
        key.category = QStringLiteral("Physical");
        key.dim = dim;
        key.id = physId;

        MeshTagSet set;
        set.key = key;
        set.name = pick.physIdToName.value(physId, QStringLiteral("phys_%1").arg(physId));
        set.source = QStringLiteral("ProjectRestore");

        for (int pid : pointIds)
            set.pointIds.push_back(static_cast<MeshIdType>(pid));

        pick.tagSets.insert(key, set);
    };

    for (auto it = pick.bndPointPhysToPointIds.constBegin();
         it != pick.bndPointPhysToPointIds.constEnd(); ++it)
        addBoundarySet(it.key(), 0, it.value());

    for (auto it = pick.bndEdgePhysToPointIds.constBegin();
         it != pick.bndEdgePhysToPointIds.constEnd(); ++it)
        addBoundarySet(it.key(), 1, it.value());

    for (auto it = pick.bndFacePhysToPointIds.constBegin();
         it != pick.bndFacePhysToPointIds.constEnd(); ++it)
        addBoundarySet(it.key(), 2, it.value());
}

static bool saveMeshMapJson(const QString& filePath,
                            const MshPickMap& pick,
                            vtkUnstructuredGrid* grid,
                            QString* errMsg)
{
    QJsonObject root;

    const QHash<int, int> pointToNode = buildPointIdToNodeId(grid);

    root["mesh_dim"] = pick.meshDim;
    root["phys_id_to_name"] = intStringHashToJson(pick.physIdToName);
    root["phys_id_to_dim"] = intIntHashToJson(pick.physIdToDim);

    root["bnd_point_phys_to_node_ids"] =
        intVectorHashToJson(pointPhysToNodePhys(pick.bndPointPhysToPointIds, pointToNode));

    root["bnd_edge_phys_to_node_ids"] =
        intVectorHashToJson(pointPhysToNodePhys(pick.bndEdgePhysToPointIds, pointToNode));

    root["bnd_face_phys_to_node_ids"] =
        intVectorHashToJson(pointPhysToNodePhys(pick.bndFacePhysToPointIds, pointToNode));

    root["bnd_edge_phys_to_node_elems"] =
        nestedIntVectorHashToJson(edgeNodeElemsFromKeys(grid, pick, pointToNode));

    root["bnd_face_phys_to_node_elems"] =
        nestedIntVectorHashToJson(faceNodeElemsFromKeys(grid, pick, pointToNode));

    QJsonArray tagSets;
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
        tagSets.append(meshTagSetToJson(it.value()));

    root["tag_sets"] = tagSets;
    root["cell_id_to_domain_tags"] =
        meshIdTagKeyVectorHashToJson(pick.cellIdToDomainTags);

    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errMsg)
            *errMsg = QString("无法写入网格映射文件：%1").arg(filePath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

static bool loadMeshMapJson(const QString& filePath,
                            vtkUnstructuredGrid* grid,
                            QString* errMsg)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errMsg)
            *errMsg = QString("无法读取网格映射文件：%1").arg(filePath);
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const QJsonObject root = QJsonDocument::fromJson(bytes).object();

    MshPickMap pick;

    pick.meshDim = root.value("mesh_dim").toInt();
    pick.physIdToName =
        jsonToIntStringHash(root.value("phys_id_to_name").toObject());
    pick.physIdToDim =
        jsonToIntIntHash(root.value("phys_id_to_dim").toObject());

    const QJsonArray tagSets = root.value("tag_sets").toArray();

    for (const auto& v : tagSets)
    {
        const MeshTagSet set = jsonToMeshTagSet(v.toObject());
        pick.tagSets.insert(set.key, set);
    }

    pick.cellIdToDomainTags =
        jsonToMeshIdTagKeyVectorHash(root.value("cell_id_to_domain_tags").toObject());

    rebuildCurrentNodeMaps(pick, grid);

    const QHash<int, QVector<int>> pointPhysToNodeIds =
        jsonToIntVectorHash(root.value("bnd_point_phys_to_node_ids").toObject());

    const QHash<int, QVector<int>> edgePhysToNodeIds =
        jsonToIntVectorHash(root.value("bnd_edge_phys_to_node_ids").toObject());

    const QHash<int, QVector<int>> facePhysToNodeIds =
        jsonToIntVectorHash(root.value("bnd_face_phys_to_node_ids").toObject());

    const QHash<int, QVector<QVector<int>>> edgeNodeElems =
        jsonToNestedIntVectorHash(root.value("bnd_edge_phys_to_node_elems").toObject());

    const QHash<int, QVector<QVector<int>>> faceNodeElems =
        jsonToNestedIntVectorHash(root.value("bnd_face_phys_to_node_elems").toObject());

    rebuildBoundaryRuntimeIndexFromNodeData(pick,
                                            grid,
                                            pointPhysToNodeIds,
                                            edgePhysToNodeIds,
                                            facePhysToNodeIds,
                                            edgeNodeElems,
                                            faceNodeElems);

    refreshPhysicalBoundaryTagSets(pick);

    MeshGridIO::instance().setMeshMap(pick);

    return true;
}

ProjectNode* findDirectChildByKind(ProjectNode* parent, const QString& kind)
{
    if (!parent)
        return nullptr;
    for (int i = 0; i < parent->childCount(); ++i)
    {
        auto* child = dynamic_cast<ProjectNode*>(parent->child(i));
        if (!child)
            continue;
        if (child->data(0, ProjectNode::RoleKind).toString() == kind)
            return child;
    }
    return nullptr;
}

void rebuildOriginalIdArrays(vtkUnstructuredGrid* sourceGrid, vtkPolyData* poly)
{
    if (!poly)
        return;

    auto pointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    pointIds->SetName("vtkOriginalPointIds");
    pointIds->SetNumberOfComponents(1);
    pointIds->SetNumberOfTuples(poly->GetNumberOfPoints());

    auto cellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    cellIds->SetName("vtkOriginalCellIds");
    cellIds->SetNumberOfComponents(1);
    cellIds->SetNumberOfTuples(poly->GetNumberOfCells());

    for (vtkIdType pid = 0; pid < poly->GetNumberOfPoints(); ++pid)
        pointIds->SetValue(pid, -1);
    for (vtkIdType cid = 0; cid < poly->GetNumberOfCells(); ++cid)
        cellIds->SetValue(cid, -1);

    if (sourceGrid)
    {
        auto pointLocator = vtkSmartPointer<vtkPointLocator>::New();
        pointLocator->SetDataSet(sourceGrid);
        pointLocator->BuildLocator();

        const vtkIdType nSourceCells = sourceGrid->GetNumberOfCells();
        QVector<QSet<vtkIdType>> sourceCellPointSets((int)nSourceCells);
        QVector<char> sourceCellPointSetBuilt((int)nSourceCells, 0);
        auto containsAllMappedPoints = [&](vtkIdType sourceCellId,
                                           const QVector<vtkIdType>& mappedPointIds) -> bool
        {
            if (sourceCellId < 0 || sourceCellId >= nSourceCells || mappedPointIds.isEmpty())
                return false;

            if (!sourceCellPointSetBuilt[(int)sourceCellId])
            {
                vtkCell* sourceCell = sourceGrid->GetCell(sourceCellId);
                if (!sourceCell)
                    return false;
                vtkIdList* sourceIds = sourceCell->GetPointIds();
                if (!sourceIds || sourceIds->GetNumberOfIds() <= 0)
                    return false;
                auto& pointSet = sourceCellPointSets[(int)sourceCellId];
                for (vtkIdType i = 0; i < sourceIds->GetNumberOfIds(); ++i)
                    pointSet.insert(sourceIds->GetId(i));
                sourceCellPointSetBuilt[(int)sourceCellId] = 1;
            }

            const auto& pointSet = sourceCellPointSets[(int)sourceCellId];
            for (vtkIdType pid : mappedPointIds)
            {
                if (!pointSet.contains(pid))
                    return false;
            }
            return true;
        };

               // 通过几何位置回映射到 sourceGrid 点 id。
        for (vtkIdType pid = 0; pid < poly->GetNumberOfPoints(); ++pid)
        {
            double p[3] = {0.0, 0.0, 0.0};
            poly->GetPoint(pid, p);
            const vtkIdType sid = pointLocator->FindClosestPoint(p);
            if (sid >= 0)
                pointIds->SetValue(pid, sid);
        }

        auto candidateCells = vtkSmartPointer<vtkIdList>::New();
        for (vtkIdType cid = 0; cid < poly->GetNumberOfCells(); ++cid)
        {
            vtkCell* surfCell = poly->GetCell(cid);
            if (!surfCell)
                continue;

            vtkIdList* surfPids = surfCell->GetPointIds();
            if (!surfPids || surfPids->GetNumberOfIds() <= 0)
                continue;

            QVector<vtkIdType> mappedPointIds;
            mappedPointIds.reserve((int)surfPids->GetNumberOfIds());
            QSet<vtkIdType> mappedPointSet;
            bool hasInvalidPid = false;
            for (vtkIdType i = 0; i < surfPids->GetNumberOfIds(); ++i)
            {
                const vtkIdType spid = surfPids->GetId(i);
                if (spid < 0 || spid >= pointIds->GetNumberOfValues())
                {
                    hasInvalidPid = true;
                    break;
                }
                const vtkIdType mappedPid = pointIds->GetValue(spid);
                if (mappedPid < 0)
                {
                    hasInvalidPid = true;
                    break;
                }
                if (!mappedPointSet.contains(mappedPid))
                {
                    mappedPointSet.insert(mappedPid);
                    mappedPointIds.push_back(mappedPid);
                }
            }
            if (hasInvalidPid || mappedPointIds.isEmpty())
                continue;

            sourceGrid->GetPointCells(mappedPointIds.front(), candidateCells);
            vtkIdType bestSourceCellId = -1;
            int bestDim = -1;

            for (vtkIdType i = 0; i < candidateCells->GetNumberOfIds(); ++i)
            {
                const vtkIdType sourceCellId = candidateCells->GetId(i);
                vtkCell* sourceCell = sourceGrid->GetCell(sourceCellId);
                if (!sourceCell)
                    continue;
                if (!containsAllMappedPoints(sourceCellId, mappedPointIds))
                    continue;

                const int dim = sourceCell->GetCellDimension();
                // 优先匹配体单元，其次面单元。
                if (dim > bestDim)
                {
                    bestDim = dim;
                    bestSourceCellId = sourceCellId;
                }
            }

            if (bestSourceCellId >= 0)
                cellIds->SetValue(cid, bestSourceCellId);
        }
    }

    if (auto* pd = poly->GetPointData())
    {
        pd->RemoveArray("vtkOriginalPointIds");
        pd->AddArray(pointIds);
    }
    if (auto* cd = poly->GetCellData())
    {
        cd->RemoveArray("vtkOriginalCellIds");
        cd->AddArray(cellIds);
    }
}

} // namespace
#include <QCoreApplication>
#include <vtkStringArray.h>

// === 这些头文件你后面真正实现时再加 ===
// #include "MeshImportGridWidget.h"
// #include "MeshExportGridWidget.h"
// #include "MeshGlobalMeshingWidget.h"
// #include "MeshSingleMeshingWidget.h"
// #include "MeshMappedMeshingWidget.h"
// #include "MeshSweepMeshingWidget.h"
// #include "MeshVolumeControlWidget.h"
// #include "MeshFaceControlWidget.h"
// #include "MeshEdgeControlWidget.h"
// #include "MeshQualityStandardWidget.h"

static void syncPickMapGroupingFromGrid(vtkUnstructuredGrid* grid);

MeshModuleController::MeshModuleController(QObject* parent) : QObject(parent)
{
    ServiceRegistry::instance().registerService<MeshModuleControllerService>(this);
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    initAction();
}

void MeshModuleController::newProject()
{
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    m_gridNode = new ProjectNode("网格");
    m_gridNode->setData(0, ProjectNode::RoleModule, "Mesh");
    m_gridNode->setData(0, ProjectNode::RoleKind, "MeshRoot");
    projectManagerService->addTreeItem(m_gridNode);
    m_edgeRules.clear();
    m_faceRules.clear();
    m_volumeRules.clear();
}

bool MeshModuleController::saveProject()
{
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (projectDir.isEmpty())
        return false;

    QJsonObject root;
    root["module"] = "Mesh";
    root["root_title"] = "网格";

    const QString snapshotPath =
        QDir(projectDir).filePath(PublicConstants::kMeshSnapshotFileName);

    const QString meshMapPath =
        QDir(projectDir).filePath(kMeshMapFileName);

    const bool hasGrid =
        (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0);

    root["has_grid"] = hasGrid;
    root["snapshot_file"] = PublicConstants::kMeshSnapshotFileName;
    root["mesh_map_file"] = kMeshMapFileName;
    // 工程恢复固定使用 IIIN/DAT 快照 + mesh_map.json。
    // 不再优先按 msh_snapshot 恢复，避免 loadMshFile 再次把低维边界单元混入显示网格。
    root["mesh_restore_mode"] = QStringLiteral("dat");

    if (hasGrid)
    {
        QString err;

        if (!MeshGridIO::instance().exportIiinFile(snapshotPath, m_importedGrid, &err))
        {
            return false;
        }

        if (!saveMeshMapJson(meshMapPath, MeshGridIO::instance().getMeshMapRef(), m_importedGrid, &err))
        {
            return false;
        }
    }

    auto edgeRulesToJson = [this]()
    {
        QJsonArray arr;

        for (const auto& rule : m_edgeRules)
        {
            QJsonObject obj;
            obj["edge_ids"] = rule.edgeIds;
            obj["divisions"] = rule.divisions;
            obj["bias"] = rule.bias;
            arr.append(obj);
        }

        return arr;
    };

    auto faceRulesToJson = [this]()
    {
        QJsonArray arr;

        for (const auto& rule : m_faceRules)
        {
            QJsonObject obj;
            obj["face_ids"] = rule.faceIds;
            obj["local_size_factor"] = rule.localSizeFactor;
            obj["curvature_refine"] = rule.curvatureRefine;
            obj["curvature_per_2pi"] = rule.curvatureElementsPer2Pi;
            arr.append(obj);
        }

        return arr;
    };

    auto volumeRulesToJson = [this]()
    {
        QJsonArray arr;

        for (const auto& rule : m_volumeRules)
        {
            QJsonObject obj;
            obj["volume_ids"] = rule.volumeIds;
            obj["local_size_factor"] = rule.localSizeFactor;
            arr.append(obj);
        }

        return arr;
    };

    root["edge_rules"] = edgeRulesToJson();
    root["face_rules"] = faceRulesToJson();
    root["volume_rules"] = volumeRulesToJson();

    QJsonArray nodeItems;

    std::function<QJsonObject(ProjectNode*)> saveNode = [&](ProjectNode* node) -> QJsonObject
    {
        QJsonObject obj;

        if (!node)
            return obj;

        obj["uuid"] = node->data(0, ProjectNode::RoleUuid).toString();
        obj["title"] = node->text(0);
        obj["module"] = node->data(0, ProjectNode::RoleModule).toString();
        obj["kind"] = node->data(0, ProjectNode::RoleKind).toString();

        QJsonArray children;

        for (int i = 0; i < node->childCount(); ++i)
        {
            auto* child = dynamic_cast<ProjectNode*>(node->child(i));

            if (!child)
                continue;

            children.append(saveNode(child));
        }

        obj["children"] = children;
        return obj;
    };

    if (m_gridNode)
    {
        for (int i = 0; i < m_gridNode->childCount(); ++i)
        {
            auto* child = dynamic_cast<ProjectNode*>(m_gridNode->child(i));

            if (!child)
                continue;

            nodeItems.append(saveNode(child));
        }
    }

    root["nodes"] = nodeItems;

    QFile file(QDir(projectDir).filePath(PublicConstants::kMeshProjectFileName));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

bool MeshModuleController::loadProject()
{
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();

    m_gridNode = new ProjectNode("网格");
    m_gridNode->setData(0, ProjectNode::RoleModule, "Mesh");
    m_gridNode->setData(0, ProjectNode::RoleKind, "MeshRoot");
    projectManagerService->addTreeItem(m_gridNode);

    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (projectDir.isEmpty())
        return false;

    QFile file(QDir(projectDir).filePath(PublicConstants::kMeshProjectFileName));

    if (!file.open(QIODevice::ReadOnly))
        return true;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const bool hasGrid = root.value("has_grid").toBool(false);

    if (hasGrid)
    {
        const QString snapshotName =
            root.value("snapshot_file").toString(PublicConstants::kMeshSnapshotFileName);

        const QString meshMapName =
            root.value("mesh_map_file").toString(kMeshMapFileName);

        const QString snapshotPath = QDir(projectDir).filePath(snapshotName);
        const QString meshMapPath = QDir(projectDir).filePath(meshMapName);

        QString err;

        auto grid = MeshGridIO::instance().loadIiinFile(snapshotPath, &err);

        if (!grid || grid->GetNumberOfCells() <= 0)
        {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("工程网格读取失败"),
                                 err.isEmpty()
                                     ? QStringLiteral("工程网格快照读取失败。")
                                     : err);
            return false;
        }

        m_importedGrid = grid;
        m_hasGrid = true;
        m_sourceFileName.clear();

        m_splitCacheSource = nullptr;
        m_splitCacheLine = nullptr;
        m_splitCacheNonLine = nullptr;

        /*
         * 新工程：
         * 有 mesh_map.json，则恢复 Physical 边界、node_id 边界映射等。
         *          * 旧工程：
         * 没有 mesh_map.json，不能失败。
         * 直接从 DAT 网格里的 MATYPE/LAYER 重建域分组即可。
         */
        if (QFileInfo::exists(meshMapPath))
        {
            if (!loadMeshMapJson(meshMapPath, m_importedGrid, &err))
            {
                syncPickMapGroupingFromGrid(m_importedGrid);
            }
        }
        else
        {
            syncPickMapGroupingFromGrid(m_importedGrid);
        }

        displayCurrentGrid();

        PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
    }

    m_edgeRules.clear();

    for (const auto& v : root.value("edge_rules").toArray())
    {
        const QJsonObject obj = v.toObject();

        EdgeControlRule rule;
        rule.edgeIds = obj.value("edge_ids").toString();
        rule.divisions = obj.value("divisions").toInt(10);
        rule.bias = obj.value("bias").toDouble(1.0);

        m_edgeRules.push_back(rule);
    }

    m_faceRules.clear();

    for (const auto& v : root.value("face_rules").toArray())
    {
        const QJsonObject obj = v.toObject();

        FaceControlRule rule;
        rule.faceIds = obj.value("face_ids").toString();
        rule.localSizeFactor = obj.value("local_size_factor").toDouble(1.0);
        rule.curvatureRefine = obj.value("curvature_refine").toBool(true);
        rule.curvatureElementsPer2Pi = obj.value("curvature_per_2pi").toInt(20);

        m_faceRules.push_back(rule);
    }

    m_volumeRules.clear();

    for (const auto& v : root.value("volume_rules").toArray())
    {
        const QJsonObject obj = v.toObject();

        VolumeControlRule rule;
        rule.volumeIds = obj.value("volume_ids").toString();
        rule.localSizeFactor = obj.value("local_size_factor").toDouble(1.0);

        m_volumeRules.push_back(rule);
    }

    auto createPageByKind = [this](const QString& kind) -> BasePage*
    {
        if (kind == "GlobalMeshing")
            return new MeshGlobalMeshingWidget();

        if (kind == "SingleMeshing")
            return new MeshSingleMeshingWidget();

        if (kind == "VolumeControl")
            return new MeshVolumeControlWidget();

        if (kind == "FaceControl")
            return new MeshFaceControlWidget();

        if (kind == "EdgeControl")
            return new MeshEdgeControlWidget();

        if (kind == "MeshGrouping")
            return new MeshGroupingWidget();

        if (kind == "MeshEdit")
        {
            auto* w = new GridEditingWidget();
            m_gridEditingWidget = w;

            if (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0)
            {
                w->setEditableGrid(m_importedGrid, m_sourceFileName);
            }

            return w;
        }

        return nullptr;
    };

    std::function<void(ProjectNode*, const QJsonObject&)> loadNode =
        [&](ProjectNode* parent, const QJsonObject& obj)
    {
        if (!parent)
            return;

        const QString kind = obj.value("kind").toString();

        BasePage* page = createPageByKind(kind);

        if (!page)
            return;

        const QString title = obj.value("title").toString(kind);
        const QString uuid = obj.value("uuid").toString(QUuid::createUuid().toString());

        auto* node = new ProjectNode(title, uuid, page);
        node->setData(0, ProjectNode::RoleModule, "Mesh");
        node->setData(0, ProjectNode::RoleKind, kind);
        node->setData(0, ProjectNode::RoleUuid, uuid);

        parent->addChild(node);

        const QJsonArray children = obj.value("children").toArray();

        for (const auto& childVal : children)
        {
            loadNode(node, childVal.toObject());
        }
    };

    const QJsonArray nodes = root.value("nodes").toArray();

    for (const auto& n : nodes)
    {
        loadNode(m_gridNode, n.toObject());
    }

    return true;
}

void MeshModuleController::closeProject()
{
    m_edgeRules.clear();
    m_faceRules.clear();
    m_volumeRules.clear();
}

QVector<MeshTagSet> MeshModuleController::domainTagSets() const
{
    QVector<MeshTagSet> out;
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.dim == map.meshDim && !set.cellIds.isEmpty())
            out.push_back(set);
    }

    std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });
    return out;
}

QVector<MeshTagSet> MeshModuleController::boundaryTagSets(int dimFilter) const
{
    QVector<MeshTagSet> out;
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    if (!map.tagSets.isEmpty())
    {
        for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
        {
            const MeshTagSet& set = it.value();
            if (set.key.dim >= map.meshDim)
                continue;
            if (dimFilter >= 0 && set.key.dim != dimFilter)
                continue;
            out.push_back(set);
        }
    }
    return out;
}

QVector<MeshIdType> MeshModuleController::cellIdsOfTag(const MeshTagKey& key) const
{
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key))
        return map.tagSets.value(key).cellIds;
    return {};
}

QVector<MeshIdType> MeshModuleController::pointIdsOfTag(const MeshTagKey& key) const
{
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key))
        return map.tagSets.value(key).pointIds;
    return {};
}

QString MeshModuleController::displayTextOfTag(const MeshTagKey& key) const
{
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key) && !map.tagSets.value(key).name.trimmed().isEmpty())
        return QString("%1: %2").arg(key.category, map.tagSets.value(key).name);
    return QString("%1=%2").arg(key.category).arg(key.id);
}

QSet<int> MeshModuleController::getAvailableMatypes() const
{
    QSet<int> ret;
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();

        // 材料编号来自 MATYPE，既可能在实体/面单元，也可能在杆单元。
        // 这里不能只返回 map.meshDim 的最高维域，否则 lineGrid 独有的 MATYPE 无法出现在材料下拉框中。
        if (set.cellIds.isEmpty())
            continue;

        if (set.key.category.compare("MATYPE", Qt::CaseInsensitive) != 0)
            continue;

        if (set.key.id <= 0)
            continue;

        ret.insert(set.key.id);
    }

    // 兼容拆分后的 lineGrid：如果 tagSets 旧数据还没同步，也从 lineGrid 的 CellData/MATYPE 里补充。
    auto appendMatypesFromGrid = [&ret](vtkUnstructuredGrid* grid)
    {
        auto* arr = grid && grid->GetCellData()
                        ? vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"))
                        : nullptr;
        if (!grid || !arr)
            return;

        const vtkIdType n = std::min(grid->GetNumberOfCells(), arr->GetNumberOfTuples());
        for (vtkIdType cid = 0; cid < n; ++cid)
        {
            const int id = arr->GetValue(cid);
            if (id > 0)
                ret.insert(id);
        }
    };

    appendMatypesFromGrid(m_previewSurfaceGrid);
    appendMatypesFromGrid(m_previewLineGrid);

    return ret;
}

QVector<DomainKey> MeshModuleController::availableDomains(const QString& category) const
{
    QVector<DomainKey> out;
    const auto sets = domainTagSets();
    for (const MeshTagSet& set : sets)
    {
        if (!category.trimmed().isEmpty() &&
            set.key.category.compare(category, Qt::CaseInsensitive) != 0)
            continue;
        out.push_back(DomainKey{set.key.category, set.key.dim, set.key.id});
    }
    return out;
}

QSet<MeshIdType> MeshModuleController::domainCells(const DomainKey& key) const
{
    QSet<MeshIdType> out;
    if (!key.isValid())
        return out;

    const MeshTagKey tagKey{key.category, key.dim, key.id};
    for (MeshIdType cid : cellIdsOfTag(tagKey))
        out.insert(cid);
    return out;
}

QString MeshModuleController::domainDisplayText(const DomainKey& key) const
{
    if (!key.isValid())
        return QString();
    return displayTextOfTag(MeshTagKey{key.category, key.dim, key.id});
}

void MeshModuleController::highlightDomainMatypes(const QSet<int>& matypes, int selectedMatype,
                                                  int previewMatype, int preDeleteMatype)
{
    if (!m_gridEditingWidget)
    {
        if (!m_visualWidgetService)
        {
            return;
        }

        QList<vtkMapper*> targetMappers;
        if (m_previewSurfaceMapper)
            targetMappers.push_back(m_previewSurfaceMapper);
        if (m_previewLineMapper)
            targetMappers.push_back(m_previewLineMapper);
        if (targetMappers.isEmpty() && m_previewMapper)
            targetMappers.push_back(m_previewMapper);
        if (targetMappers.isEmpty())
        {
            return;
        }

        const bool hasSelection =
            !matypes.isEmpty() || selectedMatype >= 0 || previewMatype >= 0 || preDeleteMatype >= 0;

        if (!hasSelection)
        {
            for (auto* mapper : std::as_const(targetMappers))
            {
                if (mapper)
                {
                    mapper->ScalarVisibilityOff();
                }
            }
            m_visualWidgetService->render();
            return;
        }

        QSet<int> allMatypes = getAvailableMatypes();
        if (selectedMatype >= 0)
        {
            allMatypes.insert(selectedMatype);
        }
        if (previewMatype >= 0)
        {
            allMatypes.insert(previewMatype);
        }
        if (preDeleteMatype >= 0)
        {
            allMatypes.insert(preDeleteMatype);
        }
        allMatypes.unite(matypes);

        int maxMatype = -1;
        for (int v : std::as_const(allMatypes))
        {
            maxMatype = qMax(maxMatype, v);
        }
        if (maxMatype < 0)
        {
            return;
        }

        auto lut = vtkSmartPointer<vtkLookupTable>::New();
        lut->SetNumberOfTableValues(maxMatype + 1);
        lut->SetTableRange(0.0, maxMatype);
        lut->Build();

        for (int i = 0; i <= maxMatype; ++i)
        {
            lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);
        }
        for (int matype : std::as_const(matypes))
        {
            if (matype >= 0 && matype <= maxMatype)
            {
                lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
            }
        }
        if (selectedMatype >= 0 && selectedMatype <= maxMatype)
        {
            lut->SetTableValue(selectedMatype, 0.9, 0.2, 0.2, 1.0);
        }
        if (previewMatype >= 0 && previewMatype <= maxMatype)
        {
            lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
        }
        if (preDeleteMatype >= 0 && preDeleteMatype <= maxMatype)
        {
            lut->SetTableValue(preDeleteMatype, 0.9, 0.55, 0.2, 1.0);
        }

        for (auto* mapper : std::as_const(targetMappers))
        {
            if (!mapper)
            {
                continue;
            }
            mapper->SetScalarModeToUseCellFieldData();
            mapper->SelectColorArray("MATYPE");
            mapper->SetLookupTable(lut);
            mapper->UseLookupTableScalarRangeOn();
            mapper->ScalarVisibilityOn();
        }
        m_visualWidgetService->render();
        return;
    }
    m_gridEditingWidget->highlightMatypes(matypes, selectedMatype, previewMatype, preDeleteMatype);
}

void MeshModuleController::initAction()
{
    auto& em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId,
                           [this, fn](const QVariantMap&)
                           {
                               fn();
                           });
    };

           // --- IO ---
    reg(PublicConstants::kActionPreprocessImportGrid,
        [this]
        {
            onImportGrid();
        });
    reg(PublicConstants::kActionPreprocessExportGrid,
        [this]
        {
            onExportGrid();
        });

           // --- Meshing ---
    reg(PublicConstants::kActionPreprocessMeshGlobal,
        [this]
        {
            onMeshGlobal();
        });
    reg(PublicConstants::kActionPreprocessMeshSingle,
        [this]
        {
            onMeshSingle();
        });
    reg(PublicConstants::kActionPreprocessMeshMapped,
        [this]
        {
            onMeshMapped();
        });
    reg(PublicConstants::kActionPreprocessMeshSweep,
        [this]
        {
            onMeshSweep();
        });

    reg(PublicConstants::kActionPreprocessMeshEdit,
        [this]
        {
            onMeshEdit();
        });
    reg(PublicConstants::kActionPreprocessMeshGrouping,
        [this]
        {
            onMeshGrouping();
        });

           // --- Control & Quality ---
    reg(PublicConstants::kActionPreprocessCtrlVolume,
        [this]
        {
            onCtrlVolume();
        });
    reg(PublicConstants::kActionPreprocessCtrlSurface,
        [this]
        {
            onCtrlSurface();
        });
    reg(PublicConstants::kActionPreprocessCtrlEdge,
        [this]
        {
            onCtrlEdge();
        }); // NEW
    reg(PublicConstants::kActionPreprocessQualityStd,
        [this]
        {
            onQualityStd();
        });
}

// -------------------- handlers --------------------
void MeshModuleController::onImportGrid()
{
    QString fileName;
    QFileDialog dialog(nullptr, QString("选择网格文件"));
    dialog.setNameFilter(QString("Grid Files (*.nas *.inp *.flac3d *.f3grid *.DAT *.msh)"));
    dialog.setFilter(QDir::Files);
    if (dialog.exec() == QDialog::Accepted)
    {
        fileName = dialog.selectedFiles().first();
    }
    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    const QString ext = fi.suffix().toLower();

    QString err;
    vtkSmartPointer<vtkUnstructuredGrid> grid;

    if (ext == "nas")
    {
        QMessageBox::warning(nullptr, QString("不支持的格式"), QString("当前暂不支持 .nas 格式"));
        return;
    }
    else if (ext == "flac3d" || ext == "f3grid")
    {
        grid = MeshGridIO::instance().importFlac3D(fileName, &err);
    }
    else if (ext == "inp")
    {
        grid = MeshGridIO::instance().importInp(fileName, &err);
    }
    else if (ext == "dat")
    {
        grid = MeshGridIO::instance().loadIiinFile(fileName, &err);
    }
    else if (ext == "msh")
    {
        grid = MeshGridIO::instance().loadMshFile(fileName, &err);
    }
    else
    {
        QMessageBox::warning(nullptr, QString("不支持的格式"),
                             QString("当前只支持 .nas / .inp / .flac3d / .f3grid / .dat"));
        return;
    }

    if (!grid || grid->GetNumberOfCells() == 0)
    {
        QMessageBox::warning(nullptr, QString("导入失败"),
                             err.isEmpty() ? QString("网格导入失败") : err);
        return;
    }

    m_importedGrid = grid;
    m_hasGrid = true;
    m_sourceFileName = fileName;
    {
        vtkSmartPointer<vtkUnstructuredGrid> tmpLine;
        vtkSmartPointer<vtkUnstructuredGrid> tmpNonLine;
        splitGridByDim(m_importedGrid, tmpLine, tmpNonLine);
    }

    if (ext == "dat")
    {
        const QString projectDir =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (!projectDir.trimmed().isEmpty())
        {
            QDir wd(QDir(projectDir).absolutePath());
            if (!wd.exists("PREP") && !wd.mkpath("PREP"))
            {
                QMessageBox::warning(nullptr, QString("DAT 备份失败"),
                                     QString("无法创建 PREP 目录，未能备份 IIIN.DAT。"));
            }
            else
            {
                const QString targetIiin = wd.filePath("PREP/IIIN.DAT");
                if (QFile::exists(targetIiin) && !QFile::remove(targetIiin))
                {
                    QMessageBox::warning(nullptr, QString("DAT 备份失败"),
                                         QString("无法覆盖 PREP/IIIN.DAT。"));
                }
                else if (!QFile::copy(fileName, targetIiin))
                {
                    QMessageBox::warning(nullptr, QString("DAT 备份失败"),
                                         QString("导入 DAT 后备份到 PREP/IIIN.DAT 失败。"));
                }
            }
        }
    }

    syncPickMapGroupingFromGrid(m_importedGrid);

    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;

    displayCurrentGrid();

    PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
}

void MeshModuleController::importMshGrid(QString path)
{
    QString err;
    auto grid = MeshGridIO::instance().loadMshFile(path, &err);

    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        QMessageBox::warning(nullptr, QString("导入失败"),
                             err.isEmpty() ? QString("MSH 网格导入失败") : err);
        return;
    }

    m_importedGrid = grid;
    m_hasGrid = true;
    m_sourceFileName = path;

    syncPickMapGroupingFromGrid(m_importedGrid);

    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;

    displayCurrentGrid();

    PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
}

bool MeshModuleController::importCurrentGmshMesh(QString* errMsg)
{
    QString err;
    auto grid = MeshGridIO::instance().loadCurrentGmshMesh(&err);
    if (!grid || grid->GetNumberOfCells() == 0)
    {
        if (errMsg)
            *errMsg = err.isEmpty() ? QString("Gmsh 当前模型网格为空") : err;
        return false;
    }

    m_importedGrid = grid;
    m_hasGrid = true;
    m_sourceFileName.clear();
    {
        vtkSmartPointer<vtkUnstructuredGrid> tmpLine;
        vtkSmartPointer<vtkUnstructuredGrid> tmpNonLine;
        splitGridByDim(m_importedGrid, tmpLine, tmpNonLine);
    }

    displayCurrentGrid();
    syncPickMapGroupingFromGrid(m_importedGrid);
    PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
    return true;
}

MshPickMap MeshModuleController::mshPickMapSolid()
{
    return MeshGridIO::instance().getMeshMap();
}

QString MeshModuleController::matypePhysicalName(int matype, int dim) const
{
    if (matype <= 0)
    {
        return QString();
    }
    return MeshGridIO::instance().getMeshMapRef().physicalName(matype, dim);
}

void MeshModuleController::setMshPickMapSolid(const MshPickMap& pickMap)
{
    MeshGridIO::instance().setMeshMap(pickMap);
}

void MeshModuleController::onExportGrid()
{
    if (!m_hasGrid || !m_importedGrid)
    {
        QMessageBox::warning(nullptr, QString("无可导出的网格"), QString("请先导入网格后再导出。"));
        return;
    }

    QString defaultPath = m_sourceFileName.isEmpty()
                              ? QString("IIIN.dat")
                              : MeshGridIO::defaultIiinOutputPath(m_sourceFileName);

    QString fileName = QFileDialog::getSaveFileName(nullptr, QString("导出 IIIN.DAT"), defaultPath,
                                                    QString("IIIN Grid (*.dat)"));
    if (fileName.isEmpty())
        return;

    QString err;
    if (!MeshGridIO::instance().exportIiinFile(fileName, m_importedGrid, &err))
    {
        QMessageBox::warning(nullptr, QString("导出失败"),
                             err.isEmpty() ? QString("IIIN.DAT 导出失败") : err);
        return;
    }
}

void MeshModuleController::onMeshGlobal()
{
    if (auto* existed = findDirectChildByKind(m_gridNode, "GlobalMeshing"))
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existed);
        QMessageBox::information(nullptr, "整体剖分", "整体剖分节点已存在，仅允许添加一次。");
        return;
    }
    MeshGlobalMeshingWidget* w = new MeshGlobalMeshingWidget();
    auto* node = new ProjectNode("网格划分", QUuid::createUuid().toString(), (BasePage*)w);
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "GlobalMeshing");
    m_gridNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onMeshSingle()
{
    auto* w = new MeshSingleMeshingWidget();
    auto* node =
        new ProjectNode("单体剖分", QUuid::createUuid().toString(), static_cast<BasePage*>(w));
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "SingleMeshing");
    m_gridNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onMeshMapped()
{
    // auto* w = new MeshMappedMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshSweep()
{
    // auto* w = new MeshSweepMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlVolume()
{
    auto* globalNode = findDirectChildByKind(m_gridNode, "GlobalMeshing");
    if (!globalNode)
    {
        QMessageBox::warning(nullptr, "体控制", "请先添加“整体剖分(网格划分)”节点，再添加体控制。");
        return;
    }
    if (findDirectChildByKind(globalNode, "VolumeControl"))
    {
        QMessageBox::information(nullptr, "体控制", "体控制节点已存在，仅允许添加一次。");
        return;
    }
    auto* w = new MeshVolumeControlWidget();
    auto* node =
        new ProjectNode("体控制", QUuid::createUuid().toString(), static_cast<BasePage*>(w));
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "VolumeControl");
    globalNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onCtrlSurface()
{
    auto* globalNode = findDirectChildByKind(m_gridNode, "GlobalMeshing");
    if (!globalNode)
    {
        QMessageBox::warning(nullptr, "面控制", "请先添加“整体剖分(网格划分)”节点，再添加面控制。");
        return;
    }
    if (findDirectChildByKind(globalNode, "FaceControl"))
    {
        QMessageBox::information(nullptr, "面控制", "面控制节点已存在，仅允许添加一次。");
        return;
    }
    auto* w = new MeshFaceControlWidget();
    auto* node =
        new ProjectNode("面控制", QUuid::createUuid().toString(), static_cast<BasePage*>(w));
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "FaceControl");
    globalNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onCtrlEdge()
{
    auto* globalNode = findDirectChildByKind(m_gridNode, "GlobalMeshing");
    if (!globalNode)
    {
        QMessageBox::warning(nullptr, "边控制", "请先添加“整体剖分(网格划分)”节点，再添加边控制。");
        return;
    }
    if (findDirectChildByKind(globalNode, "EdgeControl"))
    {
        QMessageBox::information(nullptr, "边控制", "边控制节点已存在，仅允许添加一次。");
        return;
    }
    auto* w = new MeshEdgeControlWidget();
    auto* node =
        new ProjectNode("边控制", QUuid::createUuid().toString(), static_cast<BasePage*>(w));
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "EdgeControl");
    globalNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onQualityStd()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (projectDir.isEmpty())
    {
        QMessageBox::warning(nullptr, "网格质量检查", "当前未找到工程目录，无法执行网格质量检查。");
        return;
    }

    const QString iiidirPath = QStringLiteral("D:/IIIDIR.DAT");

    const QString projectDirNative =
        QDir::toNativeSeparators(QDir(projectDir).absolutePath());

    const QString prepDir =
        QDir::toNativeSeparators(QDir(projectDir).filePath("PREP"));

    if (!QDir(prepDir).exists())
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("未找到 PREP 目录:\n%1").arg(prepDir));
        return;
    }

    const QString gridQualityDir =
        QDir::toNativeSeparators(
            QCoreApplication::applicationDirPath() + "/grid_quality");

    if (!QDir(gridQualityDir).exists())
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("未找到网格质量检查程序目录:\n%1").arg(gridQualityDir));
        return;
    }

    const QString exePath =
        QDir::toNativeSeparators(
            QDir(gridQualityDir).filePath("A02_CKJACOBI.exe"));

    if (!QFileInfo::exists(exePath))
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("未找到可执行文件:\n%1").arg(exePath));
        return;
    }

    const auto ensureTrailingSlash = [](QString path) -> QString
    {
        path = QDir::toNativeSeparators(path);
        if (!path.endsWith("\\") && !path.endsWith("/"))
        {
            path += "\\";
        }
        return path;
    };

    const auto writeIiidir = [iiidirPath, prepDir, ensureTrailingSlash](const QString& rootDir) -> bool
    {
        const QString rootDirWithSlash = ensureTrailingSlash(rootDir);
        const QString prepDirWithSlash = ensureTrailingSlash(prepDir);

        const QString content =
            QString("'%1'\n"
                    "'1'\n"
                    "'9.81 0.0 1'\n"
                    "'D:\\NASGEWIN64\\'\n"
                    "        0  IDEVICE\n"
                    "           0  ILOG_GZKZR\n"
                    "       0  ILOG_SOLVE\n"
                    "'%2'\n"
                    "           8   \n")
                .arg(rootDirWithSlash, prepDirWithSlash);

        QFile file(iiidirPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return false;
        }

        file.write(content.toLocal8Bit());
        file.close();
        return true;
    };

    const auto writeMesgItask = [](const QString& dirPath) -> bool
    {
        const QString mesgItaskPath =
            QDir(dirPath).filePath(QStringLiteral("MESG_ITASK.DAT"));

        QFile file(mesgItaskPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return false;
        }

        file.write("2\n");
        file.close();

        return true;
    };
    /*
     * 运行检查前写入 IIIDIR.DAT。
     *
     * 这里传 prepDir，是为了让 A02_CKJACOBI 进入网格检查相关工作目录。
     * 程序结束后再恢复为 projectDirNative。
     */
    if (!writeIiidir(prepDir))
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("无法写入 %1").arg(iiidirPath));
        return;
    }

    /*
     * MESG_ITASK.DAT 要放在当前工作目录。
     *
     * 你下面 process->setWorkingDirectory(gridQualityDir)，
     * 所以这里先在 grid_quality 目录下检查/创建 MESG_ITASK.DAT。
     */
    if (!writeMesgItask(gridQualityDir))
    {
        writeIiidir(projectDirNative);

        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("无法写入 MESG_ITASK.DAT:\n%1")
                                 .arg(QDir(projectDir).filePath("MESG_ITASK.DAT")));
        return;
    }

    auto* process = new QProcess(this);
    process->setProgram(exePath);

    /*
     * 这里沿用你现在的写法：
     * 工作目录设置为 grid_quality。
     *
     * 因为 A02_CKJACOBI.exe 是控制台程序，
     * 并且可能依赖当前目录下的控制文件。
     */
    process->setWorkingDirectory(gridQualityDir);

#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args)
        {
            /*
             * 创建新的控制台窗口，也就是弹出黑窗口。
             */
            args->flags |= CREATE_NEW_CONSOLE;

            /*
             * 不让 Qt 接管标准输入输出。
             * 否则 Fortran 控制台程序可能无法正常在黑窗口中输入参数。
             */
            args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        });
#endif

    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [writeIiidir, projectDirNative, process](int exitCode, QProcess::ExitStatus exitStatus)
            {
                writeIiidir(projectDirNative);

                if (exitStatus == QProcess::NormalExit && exitCode == 0)
                {
                    QMessageBox::information(nullptr, "网格质量检查", "网格质量检查执行完成。");
                }
                else
                {
                    QMessageBox::warning(nullptr, "网格质量检查",
                                         QString("网格质量检查程序异常结束。\n退出码: %1")
                                             .arg(exitCode));
                }

                process->deleteLater();
            });

    connect(process,
            &QProcess::errorOccurred,
            this,
            [writeIiidir, projectDirNative, process](QProcess::ProcessError error)
            {
                writeIiidir(projectDirNative);

                QMessageBox::warning(nullptr, "网格质量检查",
                                     QString("网格质量检查程序启动或执行失败。\n错误码: %1")
                                         .arg(static_cast<int>(error)));

                process->deleteLater();
            });

    process->start();

    if (!process->waitForStarted(3000))
    {
        writeIiidir(projectDirNative);

        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("启动失败:\n%1").arg(exePath));

        process->deleteLater();
        return;
    }
}

void MeshModuleController::onMeshGrouping()
{
    if (auto* existed = findDirectChildByKind(m_gridNode, "MeshGrouping"))
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existed);
        QMessageBox::information(nullptr, "网格分组", "网格分组节点已存在。", QMessageBox::Ok);
        return;
    }

    auto* w = new MeshGroupingWidget();
    auto* node = new ProjectNode("网格分组", QUuid::createUuid().toString(), w);
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "MeshGrouping");
    m_gridNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onMeshEdit()
{
    auto* w = new GridEditingWidget();
    m_gridEditingWidget = w;
    if (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0)
    {
        w->setEditableGrid(m_importedGrid, m_sourceFileName);
    }

    auto* node = new ProjectNode("网格编辑", QUuid::createUuid().toString(), w);
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "MeshEdit");
    m_gridNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::displayCurrentGrid()
{
    const qint64 rev = PublicVariables::instance().get("pick_structure_revision").toLongLong();
    PublicVariables::instance().set("pick_structure_revision", rev + 1, "project_dir");

    if (!m_visualWidgetService || !m_hasGrid || !m_importedGrid)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> lineGrid;
    vtkSmartPointer<vtkUnstructuredGrid> nonLineGrid;
    splitGridByDim(m_importedGrid, lineGrid, nonLineGrid);

    static const QString kMeshSurfId = QString("mesh.imported.surface");
    static const QString kMeshLineId = QString("mesh.imported.line");
    m_visualWidgetService->removeProp(kMeshSurfId, false);
    m_visualWidgetService->removeProp(kMeshLineId, false);

    if (nonLineGrid && nonLineGrid->GetNumberOfCells() > 0)
    {
        auto surf = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
        surf->SetInputData(nonLineGrid);
        surf->Update();

        auto boundary = vtkSmartPointer<vtkPolyData>::New();
        boundary->ShallowCopy(surf->GetOutput());
        rebuildOriginalIdArrays(nonLineGrid, boundary);

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(boundary);
        mapper->ScalarVisibilityOff();

        auto surfActor = vtkSmartPointer<vtkActor>::New();
        surfActor->SetMapper(mapper);
        auto* prop = surfActor->GetProperty();
        prop->SetRepresentationToSurface();
        prop->SetColor(0.7, 0.7, 0.7);
        // prop->SetSpecular(0.0);
        prop->EdgeVisibilityOn();
        prop->SetEdgeColor(0.0, 0.0, 0.0);
        prop->SetLineWidth(1.0);

        m_visualWidgetService->addProp(kMeshSurfId, surfActor, QString("网格"),
                                       QString("导入网格-面/体"), true, true);

        m_previewSurfaceGrid = m_importedGrid;
        m_previewSurfaceActor = surfActor;
        m_previewSurfaceMapper = surfActor->GetMapper();
    }
    else
    {
        m_previewSurfaceGrid = nullptr;
        m_previewSurfaceActor = nullptr;
        m_previewSurfaceMapper = nullptr;
    }

    if (lineGrid && lineGrid->GetNumberOfCells() > 0)
    {
        auto lineActor = createLineActor(lineGrid);
        m_visualWidgetService->addProp(kMeshLineId, lineActor, QString("网格"),
                                       QString("导入网格-线"), true, true);
        m_previewLineGrid = lineGrid;
        m_previewLineActor = lineActor;
        m_previewLineMapper = lineActor->GetMapper();
    }
    else
    {
        m_previewLineGrid = nullptr;
        m_previewLineActor = nullptr;
        m_previewLineMapper = nullptr;
    }

    if (m_previewSurfaceGrid)
    {
        m_previewGrid = m_previewSurfaceGrid;
        m_previewActor = m_previewSurfaceActor;
        m_previewMapper = m_previewSurfaceMapper;
    }
    else
    {
        m_previewGrid = m_previewLineGrid;
        m_previewActor = m_previewLineActor;
        m_previewMapper = m_previewLineMapper;
    }

           // 网格导入/预览后默认只显示网格。
    m_visualWidgetService->setGroupVisible("Geometry", false, false);
    // 必须在网格 prop 完成更新后再同步 PickManager 结构，否则会抓到旧 actor/mapper。
    PickManager::instance().updateStructure();
    m_visualWidgetService->render();
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGrid()
{
    return m_importedGrid;
}

QString MeshModuleController::sourceMeshFilePath() const
{
    return m_sourceFileName;
}

bool MeshModuleController::exportIiinDatFile(const QString& filePath, QString* errMsg)
{
    if (!m_hasGrid || !m_importedGrid)
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前无可导出的网格数据");
        return false;
    }
    return MeshGridIO::instance().exportIiinFile(filePath, m_importedGrid, errMsg);
}

void MeshModuleController::splitGridByDim(vtkUnstructuredGrid* src,
                                          vtkSmartPointer<vtkUnstructuredGrid>& outLine,
                                          vtkSmartPointer<vtkUnstructuredGrid>& outNonLine)
{
    outLine = nullptr;
    outNonLine = nullptr;
    if (!src)
        return;

    if (m_splitCacheSource == src && m_splitCacheNonLine)
    {
        outLine = m_splitCacheLine;
        outNonLine = m_splitCacheNonLine;
        return;
    }

    auto lineIds = vtkSmartPointer<vtkIdList>::New();
    auto nonLineIds = vtkSmartPointer<vtkIdList>::New();

    const vtkIdType nCells = src->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* c = src->GetCell(cid);
        if (!c)
            continue;

        const int dim = c->GetCellDimension(); // 1=线,2=面,3=体
        if (dim == 1)
        {
            lineIds->InsertNextId(cid);
        }
        else
        {
            nonLineIds->InsertNextId(cid);
        }
    }

    if (lineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(src);
        ex->SetCellList(lineIds);
        ex->Update();
        outLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }

    if (nonLineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(src);
        ex->SetCellList(nonLineIds);
        ex->Update();
        outNonLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }

           // splitGridByDim 只做维度拆分，不再追加 MATYPE/LAYER/Physical 分组表面。
           // 分组表面属于业务选择/高亮数据，不能混入显示网格；否则打开工程后会显示重复壳。

    m_splitCacheSource = src;
    m_splitCacheLine = outLine;
    m_splitCacheNonLine = outNonLine;
}

vtkSmartPointer<vtkActor> MeshModuleController::createSurfaceActor(vtkDataSet* grid) const
{
    if (!grid)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    auto* p = actor->GetProperty();
    p->SetColor(0.85, 0.85, 0.85);  // 与几何默认色一致
    p->SetSpecular(0.0);            // 避免高光导致“发亮”
    p->EdgeVisibilityOn();          // 显示边
    p->SetEdgeColor(0.0, 0.0, 0.0); // 黑色边
    p->SetLineWidth(1.0);

    return actor;
}

vtkSmartPointer<vtkActor> MeshModuleController::createLineActor(vtkUnstructuredGrid* grid) const
{
    if (!grid)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    auto* p = actor->GetProperty();
    p->SetColor(0.0, 0.0, 0.0); // 黑线
    p->SetLineWidth(2.0);
    p->LightingOff();          // 关键：避免线被光照“洗白/发虚”
    p->RenderLinesAsTubesOn(); // 可选但很推荐：更粗更好点选（VTK支持则生效）

    return actor;
}

static void syncPickMapGroupingFromGrid(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return;

    MshPickMap pick = MeshGridIO::instance().getMeshMap();

    int meshDim = -1;
    const vtkIdType nCells = grid->GetNumberOfCells();

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);

        if (!cell)
            continue;

        meshDim = qMax(meshDim, cell->GetCellDimension());
    }

    if (meshDim < 0)
        return;

    // meshDim 表示模型最高维度；MATYPE/LAYER 的 DomainKey.dim 不能全部使用 meshDim。
    // 混合网格中杆单元必须生成 dim=1，否则 MaterialManager/PickManager 无法把 lineGrid
    // 当作独立域进行高亮。
    pick.meshDim = meshDim;

    QVector<MeshTagKey> removeKeys;

    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagKey& key = it.key();

        const bool isMatype =
            key.category.compare("MATYPE", Qt::CaseInsensitive) == 0;

        const bool isLayer =
            key.category.compare("LAYER", Qt::CaseInsensitive) == 0;

        if (isMatype || isLayer)
            removeKeys.push_back(key);
    }

    for (const MeshTagKey& key : removeKeys)
        pick.tagSets.remove(key);

    pick.cellIdToDomainTags.clear();

    auto* matArr =
        vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));

    auto* layArr =
        vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("LAYER"));

    auto addDomainTag = [&](const QString& category, int dim, int id, vtkIdType cid)
    {
        if (dim <= 0)
            return;

        const bool isLayer =
            category.compare("LAYER", Qt::CaseInsensitive) == 0;

        if (isLayer)
        {
            if (id == 0)
                return;
        }
        else
        {
            if (id <= 0)
                return;
        }

        MeshTagKey key;
        key.category = category;
        key.dim = dim;
        key.id = id;

        if (!pick.tagSets.contains(key))
        {
            MeshTagSet set;
            set.key = key;
            set.name = QString("%1=%2").arg(category).arg(id);
            set.source = "GridCellData";

            pick.tagSets.insert(key, set);
        }

        pick.tagSets[key].cellIds.push_back(static_cast<MeshIdType>(cid));

        auto& tags = pick.cellIdToDomainTags[static_cast<MeshIdType>(cid)];

        if (!tags.contains(key))
            tags.push_back(key);
    };

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);

        if (!cell)
            continue;

        const int cellDim = cell->GetCellDimension();
        if (cellDim <= 0)
            continue;

        const int mat = matArr ? matArr->GetValue(cid) : 0;
        const int lay = layArr ? layArr->GetValue(cid) : 0;

        addDomainTag("MATYPE", cellDim, mat, cid);
        addDomainTag("LAYER", cellDim, lay, cid);
    }

    // 保留并重建已有 Physical 域标签的 cellId -> tags 反查。
    // MATYPE/LAYER 已按单元真实维度重建；Physical 仍按原导入标签保留。
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();

        if (set.cellIds.isEmpty())
            continue;

        const bool isMatype =
            set.key.category.compare("MATYPE", Qt::CaseInsensitive) == 0;
        const bool isLayer =
            set.key.category.compare("LAYER", Qt::CaseInsensitive) == 0;

        if (isMatype || isLayer)
            continue;

        if (set.key.dim != pick.meshDim)
            continue;

        for (MeshIdType cid : set.cellIds)
        {
            auto& tags = pick.cellIdToDomainTags[cid];
            if (!tags.contains(set.key))
                tags.push_back(set.key);
        }
    }

    MeshGridIO::instance().setMeshMap(pick);
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGridSolid()
{
    return m_previewSurfaceGrid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGridLine()
{
    return m_previewLineGrid;
}

bool MeshModuleController::autoGroupingBySpatialBins(int binCount, const QString& targetArrayName,
                                                     int startValue, const QString& groupPrefix,
                                                     QString* errMsg)
{
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0)
    {
        if (errMsg)
            *errMsg = QString("当前没有可分组的网格");
        return false;
    }
    if (binCount < 2)
    {
        if (errMsg)
            *errMsg = QString("自动分组数至少为 2");
        return false;
    }
    if (startValue <= 0)
    {
        if (errMsg)
            *errMsg = QString("起始编号必须大于 0");
        return false;
    }
    if (targetArrayName.trimmed().isEmpty())
    {
        if (errMsg)
            *errMsg = QString("目标分组字段不能为空");
        return false;
    }

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    struct CellCenter
    {
        vtkIdType cid;
        double x;
        double y;
        double z;
    };
    QVector<CellCenter> centers;
    centers.reserve((int)nCells);

    double minV[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max()};
    double maxV[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                      -std::numeric_limits<double>::max()};

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        auto* cell = m_importedGrid->GetCell(cid);
        if (!cell)
            continue;
        double b[6] = {0, 0, 0, 0, 0, 0};
        cell->GetBounds(b);
        const double cx = 0.5 * (b[0] + b[1]);
        const double cy = 0.5 * (b[2] + b[3]);
        const double cz = 0.5 * (b[4] + b[5]);
        minV[0] = std::min(minV[0], cx);
        minV[1] = std::min(minV[1], cy);
        minV[2] = std::min(minV[2], cz);
        maxV[0] = std::max(maxV[0], cx);
        maxV[1] = std::max(maxV[1], cy);
        maxV[2] = std::max(maxV[2], cz);
        centers.push_back({cid, cx, cy, cz});
    }

    if (centers.isEmpty())
    {
        if (errMsg)
            *errMsg = QString("无法读取单元中心，自动分组失败");
        return false;
    }

    const double span[3] = {maxV[0] - minV[0], maxV[1] - minV[1], maxV[2] - minV[2]};
    int axis = 0;
    if (span[1] > span[axis])
        axis = 1;
    if (span[2] > span[axis])
        axis = 2;

           // 业务上「填筑层」更关注高程分层：优先按 Z 方向划分。
           // 若 Z 跨度近乎为 0（例如二维工况），再回退到最大跨度方向。
    if (targetArrayName.compare("LAYER", Qt::CaseInsensitive) == 0)
    {
        constexpr double kEps = 1e-12;
        if (span[2] > kEps)
            axis = 2;
    }

    auto* targetArr = vtkIntArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetArray(targetArrayName.toStdString().c_str()));
    if (!targetArr)
    {
        auto newArr = vtkSmartPointer<vtkIntArray>::New();
        newArr->SetName(targetArrayName.toStdString().c_str());
        newArr->SetNumberOfComponents(1);
        newArr->SetNumberOfTuples(nCells);
        newArr->FillComponent(0, 0);
        m_importedGrid->GetCellData()->AddArray(newArr);
        targetArr = newArr;
    }

    auto* groupArr =
        vtkStringArray::SafeDownCast(m_importedGrid->GetCellData()->GetAbstractArray("GroupName"));
    if (!groupArr)
    {
        auto newGroup = vtkSmartPointer<vtkStringArray>::New();
        newGroup->SetName("GroupName");
        newGroup->SetNumberOfValues(nCells);
        for (vtkIdType i = 0; i < nCells; ++i)
            newGroup->SetValue(i, "");
        m_importedGrid->GetCellData()->AddArray(newGroup);
        groupArr = newGroup;
    }
    else if (groupArr->GetNumberOfValues() != nCells)
    {
        groupArr->SetNumberOfValues(nCells);
    }

           // 使用“按主轴排序后的等数量分箱”（quantile
           // binning），避免仅按跨度均分导致某些组过密/过疏。
    std::sort(centers.begin(), centers.end(),
              [axis](const CellCenter& a, const CellCenter& b)
              {
                  const double av = (axis == 0 ? a.x : (axis == 1 ? a.y : a.z));
                  const double bv = (axis == 0 ? b.x : (axis == 1 ? b.y : b.z));
                  if (av == bv)
                      return a.cid < b.cid;
                  return av < bv;
              });

    const int n = centers.size();
    for (int i = 0; i < n; ++i)
    {
        int bin = (i * binCount) / n;
        if (bin >= binCount)
            bin = binCount - 1;
        if (bin < 0)
            bin = 0;

        const int targetValue = startValue + bin;
        const vtkIdType cid = centers[i].cid;
        targetArr->SetValue(cid, targetValue);
        groupArr->SetValue(cid, QString("%1_%2").arg(groupPrefix).arg(targetValue).toStdString());
    }

    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    return true;
}

bool MeshModuleController::groupByCoordinateRange(double xmin, double xmax, double ymin,
                                                  double ymax, double zmin, double zmax,
                                                  const QString& targetArrayName, int value,
                                                  const QString& groupName, QString* errMsg)
{
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0)
    {
        if (errMsg)
            *errMsg = QString("当前没有可分组的网格");
        return false;
    }
    if (value <= 0)
    {
        if (errMsg)
            *errMsg = QString("分组编号必须大于 0");
        return false;
    }
    if (targetArrayName.trimmed().isEmpty())
    {
        if (errMsg)
            *errMsg = QString("目标分组字段不能为空");
        return false;
    }

    if (xmin > xmax)
        std::swap(xmin, xmax);
    if (ymin > ymax)
        std::swap(ymin, ymax);
    if (zmin > zmax)
        std::swap(zmin, zmax);

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    auto* targetArr = vtkIntArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetArray(targetArrayName.toStdString().c_str()));
    if (!targetArr)
    {
        auto newArr = vtkSmartPointer<vtkIntArray>::New();
        newArr->SetName(targetArrayName.toStdString().c_str());
        newArr->SetNumberOfComponents(1);
        newArr->SetNumberOfTuples(nCells);
        newArr->FillComponent(0, 0);
        m_importedGrid->GetCellData()->AddArray(newArr);
        targetArr = newArr;
    }

    auto* groupArr =
        vtkStringArray::SafeDownCast(m_importedGrid->GetCellData()->GetAbstractArray("GroupName"));
    if (!groupArr)
    {
        auto newGroup = vtkSmartPointer<vtkStringArray>::New();
        newGroup->SetName("GroupName");
        newGroup->SetNumberOfValues(nCells);
        for (vtkIdType i = 0; i < nCells; ++i)
            newGroup->SetValue(i, "");
        m_importedGrid->GetCellData()->AddArray(newGroup);
        groupArr = newGroup;
    }
    else if (groupArr->GetNumberOfValues() != nCells)
    {
        groupArr->SetNumberOfValues(nCells);
    }

    int changed = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        auto* cell = m_importedGrid->GetCell(cid);
        if (!cell)
            continue;
        double b[6] = {0, 0, 0, 0, 0, 0};
        cell->GetBounds(b);
        const double cx = 0.5 * (b[0] + b[1]);
        const double cy = 0.5 * (b[2] + b[3]);
        const double cz = 0.5 * (b[4] + b[5]);
        if (cx >= xmin && cx <= xmax && cy >= ymin && cy <= ymax && cz >= zmin && cz <= zmax)
        {
            targetArr->SetValue(cid, value);
            groupArr->SetValue(cid, groupName.toStdString());
            ++changed;
        }
    }

    if (changed == 0)
    {
        if (errMsg)
            *errMsg = QString("范围内没有命中任何单元");
        return false;
    }

    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    return true;
}

void MeshModuleController::setEdgeControlRules(const QVector<EdgeControlRule>& rules)
{
    m_edgeRules = rules;
}

void MeshModuleController::setFaceControlRules(const QVector<FaceControlRule>& rules)
{
    m_faceRules = rules;
}

void MeshModuleController::setVolumeControlRules(const QVector<VolumeControlRule>& rules)
{
    m_volumeRules = rules;
}

QVector<EdgeControlRule> MeshModuleController::edgeControlRules() const
{
    return m_edgeRules;
}

QVector<FaceControlRule> MeshModuleController::faceControlRules() const
{
    return m_faceRules;
}

QVector<VolumeControlRule> MeshModuleController::volumeControlRules() const
{
    return m_volumeRules;
}
