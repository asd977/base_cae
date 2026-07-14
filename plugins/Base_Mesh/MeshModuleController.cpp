#include "MeshModuleController.h"

#include <EventManager.h>
#include <GridEditingWidget.h>
#include <MeshEdgeControlWidget.h>
#include <MeshFaceControlWidget.h>
#include <MeshGlobalMeshingWidget.h>
#include <MeshGroupingWidget.h>
#include <MeshImportSettingsWidget.h>
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
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataSetMapper.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPointLocator.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>


#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

constexpr int MIN_EDGE_POINTS   = 2;
constexpr int MIN_FACE_POINTS   = 3;
constexpr int MAX_MESH_DIM      = 3;
constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr double CENTER_FACTOR  = 0.5;
constexpr double DEFAULT_ALPHA  = 1.0;

static bool meshDomainKeyIsValid(const DomainKey& key)
{
    return !key.category.trimmed().isEmpty() && key.dim >= 0 && key.id != 0;
}

static const char* kMeshMapFileName = "mesh_map.json";
static const char* kMeshMshSnapshotFileName = "mesh_snapshot.msh";

// 大网格保存优化：
// 1) IIIN/DAT 网格快照只有在网格对象真正变更后才重写；
// 2) MATYPE/LAYER 来自 vtkCellData，保存工程时不再把每个单元 id 全量写入 mesh_map.json；
// 3) cell_id_to_domain_tags 是运行期反查缓存，可由 tagSets + 网格 CellData 重建，不再落盘。
static QHash<QString, unsigned long> s_savedSnapshotMTimeByPath;

static QString normalizedProjectPathKey(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

static unsigned long gridModifiedTime(vtkUnstructuredGrid* grid)
{
    return grid ? static_cast<unsigned long>(grid->GetMTime()) : 0UL;
}

static bool shouldRewriteGridSnapshot(const QString& snapshotPath, vtkUnstructuredGrid* grid)
{
    if (!grid || grid->GetNumberOfCells() <= 0)
        return false;

    if (!QFileInfo::exists(snapshotPath))
        return true;

    const QString key = normalizedProjectPathKey(snapshotPath);
    return s_savedSnapshotMTimeByPath.value(key, 0UL) != gridModifiedTime(grid);
}

static void markGridSnapshotSaved(const QString& snapshotPath, vtkUnstructuredGrid* grid)
{
    if (!grid)
        return;

    s_savedSnapshotMTimeByPath.insert(normalizedProjectPathKey(snapshotPath), gridModifiedTime(grid));
}

static bool isGridCellDataDomainTag(const MeshTagKey& key)
{
    return key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0
           || key.category.compare(QStringLiteral("LAYER"), Qt::CaseInsensitive) == 0;
}

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

static QString physicalUiNameByDim(int dim, int id)
{
    QString prefix;
    switch (dim)
    {
    case 0: prefix = QStringLiteral("点边界"); break;
    case 1: prefix = QStringLiteral("线边界"); break;
    case 2: prefix = QStringLiteral("面边界"); break;
    case 3: prefix = QStringLiteral("体域"); break;
    default: prefix = QStringLiteral("分组"); break;
    }
    return QStringLiteral("%1%2").arg(prefix).arg(id);
}

static QString normalizePhysicalUiName(int dim, int id, const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return physicalUiNameByDim(dim, id);

    static const QRegularExpression generatedNameRe(
        QStringLiteral("^(dim\\d+(_|-)?.*|phys_\\d+|point_\\d+|edge_\\d+|face_\\d+|volume_\\d+|MeshPoint_\\d+|MeshEdge_\\d+|MeshFace_\\d+|MeshVolume_\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (generatedNameRe.match(name).hasMatch())
        return physicalUiNameByDim(dim, id);

    return name;
}

static QVector<MeshIdType> meshIdsFromIntVector(const QVector<int>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (int v : values)
        out.push_back(static_cast<MeshIdType>(v));

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static QVector<MeshIdType> meshIdsFromInt64Vector(const QVector<qint64>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (qint64 v : values)
        out.push_back(static_cast<MeshIdType>(v));

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static void normalizeMeshTagSet(MeshTagSet& set)
{
    set.name = normalizePhysicalUiName(set.key.dim, set.key.id, set.name);

    std::sort(set.pointIds.begin(), set.pointIds.end());
    set.pointIds.erase(std::unique(set.pointIds.begin(), set.pointIds.end()), set.pointIds.end());

    std::sort(set.cellIds.begin(), set.cellIds.end());
    set.cellIds.erase(std::unique(set.cellIds.begin(), set.cellIds.end()), set.cellIds.end());

    std::sort(set.edgeKeys.begin(), set.edgeKeys.end());
    set.edgeKeys.erase(std::unique(set.edgeKeys.begin(), set.edgeKeys.end()), set.edgeKeys.end());

    std::sort(set.faceKeys.begin(), set.faceKeys.end());
    set.faceKeys.erase(std::unique(set.faceKeys.begin(), set.faceKeys.end()), set.faceKeys.end());
}

static void upsertPhysicalBoundarySet(QHash<MeshTagKey, MeshTagSet>& sets,
                                      const MshPickMap& map,
                                      int physId,
                                      int dim,
                                      const QVector<int>& pointIds,
                                      const QVector<qint64>& edgeKeys,
                                      const QVector<qint64>& faceKeys)
{
    if (physId <= 0 || dim < 0 || dim >= map.meshDim)
        return;
    if (pointIds.isEmpty() && edgeKeys.isEmpty() && faceKeys.isEmpty())
        return;

    MeshTagKey key;
    key.category = QStringLiteral("Physical");
    key.dim = dim;
    key.id = physId;

    MeshTagSet set = sets.value(key);
    set.key = key;
    if (set.source.trimmed().isEmpty())
        set.source = QStringLiteral("BoundaryIndex");

    if (set.name.trimmed().isEmpty())
        set.name = map.physIdToName.value(physId);

    set.pointIds += meshIdsFromIntVector(pointIds);
    set.edgeKeys += meshIdsFromInt64Vector(edgeKeys);
    set.faceKeys += meshIdsFromInt64Vector(faceKeys);

    normalizeMeshTagSet(set);
    sets.insert(key, set);
}

static QVector<MeshTagSet> sortedMeshTagSets(QHash<MeshTagKey, MeshTagSet> sets)
{
    QVector<MeshTagSet> out = sets.values().toVector();

    std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.dim != b.key.dim)
                      return a.key.dim < b.key.dim;
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });

    return out;
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


static MeshTagSet& ensurePhysicalTagSet(MshPickMap& pick,
                                        int physId,
                                        int dim,
                                        const QString& source)
{
    static MeshTagSet invalid;
    if (physId <= 0 || dim < 0 || dim > 3)
        return invalid;

    MeshTagKey key{QStringLiteral("Physical"), dim, physId};
    if (!pick.tagSets.contains(key))
    {
        MeshTagSet set;
        set.key = key;
        set.name = pick.physIdToName.value(physId, QStringLiteral("Physical=%1").arg(physId));
        set.source = source;
        pick.tagSets.insert(key, set);
    }

    MeshTagSet& set = pick.tagSets[key];
    if (set.name.trimmed().isEmpty())
        set.name = pick.physIdToName.value(physId, QStringLiteral("Physical=%1").arg(physId));
    if (set.source.trimmed().isEmpty())
        set.source = source;
    pick.physIdToName.insert(physId, set.name);
    pick.physIdToDim.insert(physId, dim);
    return set;
}

static void setPhysicalBoundaryPointIds(MshPickMap& pick,
                                        int physId,
                                        int dim,
                                        const QVector<int>& pointIds,
                                        const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, dim, source);
    if (!set.key.isValid())
        return;
    set.pointIds.clear();
    set.pointIds.reserve(pointIds.size());
    for (int pid : pointIds)
    {
        if (pid >= 0)
            set.pointIds.push_back(static_cast<MeshIdType>(pid));
    }
}

static void appendPhysicalBoundaryEdgeKey(MshPickMap& pick, int physId, qint64 edgeKey, const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, 1, source);
    if (set.key.isValid())
        set.edgeKeys.push_back(static_cast<MeshIdType>(edgeKey));
}

static void appendPhysicalBoundaryFaceKey(MshPickMap& pick, int physId, qint64 faceKey, const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, 2, source);
    if (set.key.isValid())
        set.faceKeys.push_back(static_cast<MeshIdType>(faceKey));
}

static void normalizeMeshTagSets(MshPickMap& pick)
{
    pick.pointIdToBoundaryTags.clear();

    auto dedupMeshIds = [](QVector<MeshIdType>& values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };

    for (auto it = pick.tagSets.begin(); it != pick.tagSets.end(); ++it)
    {
        MeshTagSet& set = it.value();
        dedupMeshIds(set.pointIds);
        dedupMeshIds(set.cellIds);
        dedupMeshIds(set.edgeKeys);
        dedupMeshIds(set.faceKeys);

        if (set.key.category == QStringLiteral("Physical"))
        {
            if (set.name.trimmed().isEmpty())
                set.name = pick.physIdToName.value(set.key.id, QStringLiteral("Physical=%1").arg(set.key.id));
            pick.physIdToName.insert(set.key.id, set.name);
            pick.physIdToDim.insert(set.key.id, set.key.dim);

            if (set.key.dim >= 0 && set.key.dim < pick.meshDim)
            {
                for (MeshIdType pid : set.pointIds)
                    pick.pointIdToBoundaryTags[pid].push_back(set.key);
            }
        }
    }

    for (auto it = pick.pointIdToBoundaryTags.begin(); it != pick.pointIdToBoundaryTags.end(); ++it)
    {
        auto& keys = it.value();
        std::sort(keys.begin(), keys.end(), [](const MeshTagKey& a, const MeshTagKey& b)
                  {
                      if (a.category != b.category)
                          return a.category < b.category;
                      if (a.dim != b.dim)
                          return a.dim < b.dim;
                      return a.id < b.id;
                  });
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }
}

static QHash<int, QVector<QVector<int>>> edgeNodeElemsFromKeys(
    vtkUnstructuredGrid* grid,
    const MshPickMap& pick,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<QVector<int>>> out;

    if (!grid)
        return out;

    for (const MeshTagSet& set : pick.physicalTagSets(1))
    {
        const int physId = set.key.id;

        for (qint64 edgeKey : set.edgeKeys)
        {
            const vtkIdType cid = static_cast<vtkIdType>(edgeKey >> 4);
            const int localEdgeId = static_cast<int>(edgeKey & 0xF);

            vtkCell* cell = grid->GetCell(cid);
            vtkCell* edge = cell ? cell->GetEdge(localEdgeId) : nullptr;

            if (!edge || edge->GetNumberOfPoints() < MIN_EDGE_POINTS)
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

    for (const MeshTagSet& set : pick.physicalTagSets(2))
    {
        const int physId = set.key.id;

        for (qint64 faceKey : set.faceKeys)
        {
            const vtkIdType cid = static_cast<vtkIdType>(faceKey >> 4);
            const int localFaceId = static_cast<int>(faceKey & 0xF);

            vtkCell* cell = grid->GetCell(cid);
            vtkCell* face = cell ? cell->GetFace(localFaceId) : nullptr;

            if (!face || face->GetNumberOfPoints() < MIN_FACE_POINTS)
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
    if (!grid)
        return;

    const QHash<int, int> nodeToPoint = buildNodeIdToPointId(grid);

    for (auto it = pointPhysToNodeIds.constBegin(); it != pointPhysToNodeIds.constEnd(); ++it)
        setPhysicalBoundaryPointIds(pick, it.key(), 0, nodeIdsToPointIds(it.value(), nodeToPoint), QStringLiteral("ProjectRestoreLegacy"));

    for (auto it = edgePhysToNodeIds.constBegin(); it != edgePhysToNodeIds.constEnd(); ++it)
        setPhysicalBoundaryPointIds(pick, it.key(), 1, nodeIdsToPointIds(it.value(), nodeToPoint), QStringLiteral("ProjectRestoreLegacy"));

    for (auto it = facePhysToNodeIds.constBegin(); it != facePhysToNodeIds.constEnd(); ++it)
        setPhysicalBoundaryPointIds(pick, it.key(), 2, nodeIdsToPointIds(it.value(), nodeToPoint), QStringLiteral("ProjectRestoreLegacy"));

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
            if (!edge || edge->GetNumberOfPoints() < MIN_EDGE_POINTS)
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
            if (!face || face->GetNumberOfPoints() < MIN_FACE_POINTS)
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
                appendPhysicalBoundaryEdgeKey(pick, physId, edgeIndex.value(key), QStringLiteral("ProjectRestoreLegacy"));
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
                appendPhysicalBoundaryFaceKey(pick, physId, faceIndex.value(key), QStringLiteral("ProjectRestoreLegacy"));
        }
    }

    normalizeMeshTagSets(pick);
}

static bool saveMeshMapJson(const QString& filePath,
                            const MshPickMap& pick,
                            vtkUnstructuredGrid* grid,
                            QString* errMsg)
{
    QJsonObject root;
    Q_UNUSED(grid);

    root["mesh_dim"] = pick.meshDim;
    root["phys_id_to_name"] = intStringHashToJson(pick.physIdToName);
    root["phys_id_to_dim"] = intIntHashToJson(pick.physIdToDim);

    QJsonArray tagSets;
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();

        // MATYPE/LAYER 是由网格 CellData 派生出来的运行期分组，
        // 大网格时 cellIds 会非常大，保存它们只会造成工程文件膨胀。
        // 加载工程后会通过 syncPickMapGroupingFromGrid() 从网格重新构建。
        if (isGridCellDataDomainTag(set.key))
            continue;

        tagSets.append(meshTagSetToJson(set));
    }

    root["tag_sets"] = tagSets;

    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errMsg)
            *errMsg = QString("无法写入网格映射文件：%1").arg(filePath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
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

    // cellIdToDomainTags 不再落盘。它是运行期反查缓存，
    // MeshModuleController::loadProject() 成功加载 mesh_map.json 后会调用
    // syncPickMapGroupingFromGrid() 根据 tagSets + 网格 CellData 统一重建。
    pick.cellIdToDomainTags.clear();

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

    normalizeMeshTagSets(pick);
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
        vtkDataArray* sourceOrigCellIds =
            sourceGrid->GetCellData() ? sourceGrid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
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
            {
                vtkIdType finalCellId = bestSourceCellId;
                if (sourceOrigCellIds && bestSourceCellId < sourceOrigCellIds->GetNumberOfTuples())
                    finalCellId = static_cast<vtkIdType>(sourceOrigCellIds->GetTuple1(bestSourceCellId));
                cellIds->SetValue(cid, finalCellId);
            }
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



static const QString kMeshDisplayFilterCustomKey = QStringLiteral("mesh.display.filter.custom");
static const QString kMeshDisplayFilterKetypeKey = QStringLiteral("mesh.display.filter.ketype.checked");
static const QString kMeshDisplayFilterMatypeKey = QStringLiteral("mesh.display.filter.matype.checked");
static const QString kMeshDisplayFilterLayerKey = QStringLiteral("mesh.display.filter.layer.checked");

static QSet<int> meshDisplayIntSetFromString(const QString& text)
{
    QSet<int> values;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok)
            values.insert(value);
    }
    return values;
}

static int meshDisplayTuple1i(vtkDataArray* array, vtkIdType tupleId, int defaultValue = 0)
{
    if (!array || tupleId < 0 || tupleId >= array->GetNumberOfTuples())
        return defaultValue;
    return static_cast<int>(array->GetTuple1(tupleId));
}

static int meshDisplayDimFromKetype(int ketype)
{
    switch (ketype)
    {
    case 1:
    case 2:
    case 10:
    case 21:
        return 1;
    case 3:
    case 4:
    case 5:
    case 11:
    case 12:
    case 29:
    case 31:
    case 32:
    case 33:
    case 34:
        return 2;
    case 6:
    case 9:
    case 13:
    case 17:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
        return 3;
    default:
        return 0;
    }
}

static int meshDisplayCellDimensionFast(vtkUnstructuredGrid* grid, vtkIdType cellId)
{
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return 0;

    auto* cd = grid->GetCellData();
    if (cd)
    {
        if (auto* dimArr = cd->GetArray("CELL_DIM"))
        {
            const int dim = meshDisplayTuple1i(dimArr, cellId, 0);
            if (dim > 0)
                return dim;
        }

        if (auto* ketypeArr = cd->GetArray("KETYPE"))
        {
            const int dim = meshDisplayDimFromKetype(meshDisplayTuple1i(ketypeArr, cellId, 0));
            if (dim > 0)
                return dim;
        }
    }

    vtkCell* cell = grid->GetCell(cellId);
    return cell ? cell->GetCellDimension() : 0;
}

static QColor meshDisplayCategoricalColor(int value)
{
    if (value == 0)
        return QColor::fromRgbF(0.78, 0.78, 0.78);

    const int hue = ((value * 47) % 360 + 360) % 360;
    return QColor::fromHsv(hue, 170, 220);
}

static vtkSmartPointer<vtkUnstructuredGrid> meshDisplayCopyWithFullCellIds(vtkUnstructuredGrid* grid)
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!grid)
        return out;

    out->ShallowCopy(grid);

    auto fullIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullIds->SetName("vtkOriginalCellIds");
    fullIds->SetNumberOfComponents(1);
    fullIds->SetNumberOfTuples(grid->GetNumberOfCells());

    auto* oldCellIds = grid->GetCellData() ? grid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        vtkIdType oid = cid;
        if (oldCellIds && cid < oldCellIds->GetNumberOfTuples())
            oid = static_cast<vtkIdType>(oldCellIds->GetTuple1(cid));
        fullIds->SetValue(cid, oid);
    }

    if (auto* cd = out->GetCellData())
    {
        cd->RemoveArray("vtkOriginalCellIds");
        cd->AddArray(fullIds);
    }

    auto fullPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullPointIds->SetName("vtkOriginalPointIds");
    fullPointIds->SetNumberOfComponents(1);
    fullPointIds->SetNumberOfTuples(grid->GetNumberOfPoints());

    auto* oldPointIds = grid->GetPointData() ? grid->GetPointData()->GetArray("vtkOriginalPointIds") : nullptr;
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        vtkIdType oid = pid;
        if (oldPointIds && pid < oldPointIds->GetNumberOfTuples())
            oid = static_cast<vtkIdType>(oldPointIds->GetTuple1(pid));
        fullPointIds->SetValue(pid, oid);
    }

    if (auto* pd = out->GetPointData())
    {
        pd->RemoveArray("vtkOriginalPointIds");
        pd->AddArray(fullPointIds);
    }

    return out;
}

static vtkSmartPointer<vtkUnstructuredGrid> meshDisplayExtractCells(
    vtkUnstructuredGrid* fullGrid, const vtkSmartPointer<vtkIdList>& ids)
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!fullGrid || !ids || ids->GetNumberOfIds() <= 0)
        return out;

    auto source = meshDisplayCopyWithFullCellIds(fullGrid);

    auto ex = vtkSmartPointer<vtkExtractCells>::New();
    ex->SetInputData(source);
    ex->SetCellList(ids);
    ex->Update();

    out->ShallowCopy(ex->GetOutput());
    return out;
}


static QByteArray meshDisplayFaceKey(vtkIdList* pointIds)
{
    QVector<vtkIdType> ids;
    if (pointIds)
    {
        ids.reserve(static_cast<int>(pointIds->GetNumberOfIds()));
        for (vtkIdType i = 0; i < pointIds->GetNumberOfIds(); ++i)
            ids.push_back(pointIds->GetId(i));
    }

    std::sort(ids.begin(), ids.end());

    QByteArray key;
    key.reserve(ids.size() * int(sizeof(vtkIdType)));
    for (vtkIdType id : ids)
        key.append(reinterpret_cast<const char*>(&id), int(sizeof(vtkIdType)));
    return key;
}

static vtkIdType meshDisplayOriginalCellId(vtkDataSet* grid, vtkIdType cellId)
{
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return cellId;

    auto* arr = grid->GetCellData() ? grid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
    if (arr && cellId < arr->GetNumberOfTuples())
        return static_cast<vtkIdType>(arr->GetTuple1(cellId));
    return cellId;
}

static int meshDisplayDomainValue(vtkDataSet* grid, vtkIdType cellId, const char* arrayName)
{
    auto* arr = (grid && grid->GetCellData()) ? grid->GetCellData()->GetArray(arrayName) : nullptr;
    if (!arr || cellId < 0 || cellId >= arr->GetNumberOfTuples())
        return 0;
    return static_cast<int>(arr->GetTuple1(cellId));
}

struct MeshDisplayFaceRef
{
    vtkIdType cellId = -1;          // cell id in current display source grid
    vtkIdType originalCellId = -1;  // cell id in m_importedGrid
    int originalFaceId = 15;        // 15 表示 2D 单元整体面
    int matype = 0;
    int layer = 0;
    int vtkCellType = VTK_EMPTY_CELL;
    QVector<vtkIdType> pointIds;    // 保留当前面的完整点序，包括高阶中间节点
};

// 高阶单元显示代理：vtkPolyData 的 polygon 只能按边界点顺序连线，
// 不能把高阶面上的中间节点也当作多边形顶点，否则会出现斜线/蜘蛛网。
// 这里仅取角点用于显示；真实网格、KETYPE、node_id、拾取映射仍保留在原 grid 中。
static QVector<vtkIdType> meshDisplayFaceCornerPointIds(vtkCell* face)
{
    QVector<vtkIdType> ids;
    if (!face)
        return ids;

    vtkIdList* pids = face->GetPointIds();
    if (!pids)
        return ids;

    const int pointCount = static_cast<int>(pids->GetNumberOfIds());
    if (pointCount < 3)
        return ids;

    int cornerCount = pointCount;

    switch (face->GetCellType())
    {
    case VTK_TRIANGLE:
    case VTK_QUADRATIC_TRIANGLE:
    case VTK_BIQUADRATIC_TRIANGLE:
        cornerCount = 3;
        break;

    case VTK_QUAD:
    case VTK_PIXEL:
    case VTK_QUADRATIC_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
    case VTK_BIQUADRATIC_QUAD:
        cornerCount = 4;
        break;

    default:
        // 常见兜底：
        // 6 节点面多为二次三角形；8/9 节点面多为二次四边形。
        if (pointCount == 6)
            cornerCount = 3;
        else if (pointCount == 8 || pointCount == 9)
            cornerCount = 4;
        else
        {
            const int edgeCount = face->GetNumberOfEdges();
            if (edgeCount == 3 || edgeCount == 4)
                cornerCount = edgeCount;
        }
        break;
    }

    cornerCount = std::min(cornerCount, pointCount);
    ids.reserve(cornerCount);

    for (int i = 0; i < cornerCount; ++i)
        ids.push_back(pids->GetId(i));

    return ids;
}


static QVector<QVector<vtkIdType>> meshDisplayFaceDisplayPolys(const MeshDisplayFaceRef& face)
{
    QVector<QVector<vtkIdType>> polys;
    const QVector<vtkIdType>& p = face.pointIds;
    const int n = p.size();

    if (n < 3)
        return polys;

    auto addTri = [&](int a, int b, int c)
    {
        if (a < n && b < n && c < n)
            polys.push_back(QVector<vtkIdType>{p[a], p[b], p[c]});
    };

    auto addQuad = [&](int a, int b, int c, int d)
    {
        if (a < n && b < n && c < n && d < n)
            polys.push_back(QVector<vtkIdType>{p[a], p[b], p[c], p[d]});
    };

    // 二次三角形：VTK_QUADRATIC_TRIANGLE = 0,1,2 为角点，3/4/5 为边中点。
    // 拆成 4 个一阶三角形，这样边中点之间的线也能显示出来。
    if (face.vtkCellType == VTK_QUADRATIC_TRIANGLE ||
        face.vtkCellType == VTK_BIQUADRATIC_TRIANGLE ||
        n == 6)
    {
        addTri(0, 3, 5);
        addTri(3, 1, 4);
        addTri(5, 4, 2);
        addTri(3, 4, 5);
        return polys;
    }

    // 二次四边形：VTK_QUADRATIC_QUAD = 0~3 角点，4~7 边中点。
    // 8 节点没有中心点，用 4 个角三角形 + 1 个中间四边形显示内部网格线。
    if (face.vtkCellType == VTK_QUADRATIC_QUAD || n == 8)
    {
        addTri(0, 4, 7);
        addTri(4, 1, 5);
        addTri(5, 2, 6);
        addTri(7, 6, 3);
        addQuad(4, 5, 6, 7);
        return polys;
    }

    // 九节点四边形：8 是中心点，拆成 4 个小四边形，显示效果最干净。
    if (face.vtkCellType == VTK_BIQUADRATIC_QUAD || n == 9)
    {
        addQuad(0, 4, 8, 7);
        addQuad(4, 1, 5, 8);
        addQuad(8, 5, 2, 6);
        addQuad(7, 8, 6, 3);
        return polys;
    }

    // 六节点二次线性四边形：不同求解器顺序不完全一致，保守显示为角点面，避免乱连。
    if (face.vtkCellType == VTK_QUADRATIC_LINEAR_QUAD && n >= 4)
    {
        addQuad(0, 1, 2, 3);
        return polys;
    }

    // 一阶三角/四边形，或者未知面：只用角点显示。
    if (n == 3)
        polys.push_back(p);
    else if (n >= 4)
        addQuad(0, 1, 2, 3);

    return polys;
}

static bool meshDisplayDifferentDomain(const MeshDisplayFaceRef& a,
                                       const MeshDisplayFaceRef& b,
                                       bool hasMatype,
                                       bool hasLayer)
{
    if (hasLayer && a.layer != b.layer)
        return true;
    if (hasMatype && a.matype != b.matype)
        return true;
    return false;
}

static void meshDisplayAddSurfaceFace(vtkPolyData* out,
                                      vtkCellArray* polys,
                                      vtkIdTypeArray* originalCellIds,
                                      vtkIdTypeArray* originalFaceIds,
                                      vtkIdTypeArray* originalFaceKeys,
                                      vtkCellData* sourceCellData,
                                      const MeshDisplayFaceRef& face)
{
    if (!out || !polys || !originalCellIds || !originalFaceIds || !originalFaceKeys || face.pointIds.size() < 3)
        return;

    const QVector<QVector<vtkIdType>> displayPolys = meshDisplayFaceDisplayPolys(face);
    if (displayPolys.isEmpty())
        return;

    for (const QVector<vtkIdType>& polyPts : displayPolys)
    {
        if (polyPts.size() < 3)
            continue;

        const vtkIdType newCellId = polys->GetNumberOfCells();

        // 不创建临时 vtkIdList，避免导入大网格时每个面一次堆分配。
        polys->InsertNextCell(static_cast<vtkIdType>(polyPts.size()));
        for (vtkIdType pid : polyPts)
            polys->InsertCellPoint(pid);

        if (sourceCellData && out->GetCellData())
            out->GetCellData()->CopyData(sourceCellData, face.cellId, newCellId);

        // 一个高阶面可能拆成多个显示小面，但它们都回指到同一个原始单元/原始面。
        originalCellIds->InsertNextValue(face.originalCellId);
        originalFaceIds->InsertNextValue(face.originalFaceId);
        originalFaceKeys->InsertNextValue((static_cast<vtkIdType>(face.originalCellId) << 4) |
                                          (static_cast<vtkIdType>(face.originalFaceId) & 0xF));
    }
}

static bool meshDisplayCellPassesDisplayFilter(vtkUnstructuredGrid* grid,
                                               vtkIdType cellId,
                                               bool customFilter,
                                               const QSet<int>& ketypes,
                                               const QSet<int>& matypes,
                                               const QSet<int>& layers)
{
    if (!customFilter)
        return true;
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return false;

    auto allowed = [](int value, const QSet<int>& values) -> bool
    {
        return values.isEmpty() || values.contains(value);
    };

    return allowed(meshDisplayDomainValue(grid, cellId, "KETYPE"), ketypes) &&
           allowed(meshDisplayDomainValue(grid, cellId, "MATYPE"), matypes) &&
           allowed(meshDisplayDomainValue(grid, cellId, "LAYER"), layers);
}

static vtkSmartPointer<vtkPolyData> meshDisplayBuildMappedSurface(vtkUnstructuredGrid* grid,
                                                                  bool customFilter = false,
                                                                  const QSet<int>& ketypes = QSet<int>(),
                                                                  const QSet<int>& matypes = QSet<int>(),
                                                                  const QSet<int>& layers = QSet<int>())
{
    auto out = vtkSmartPointer<vtkPolyData>::New();
    if (!grid || grid->GetNumberOfCells() <= 0 || !grid->GetPoints())
        return out;

           // 关键：直接复用显示源网格的点表。这样 PickManager 看到的 surface pid 与原网格 pid 一致，
           // 不再通过 vtkDataSetSurfaceFilter + 最近点回猜，避免点选/框选“乱窜”。
    out->SetPoints(grid->GetPoints());

    auto polys = vtkSmartPointer<vtkCellArray>::New();
    out->SetPolys(polys);

    vtkCellData* srcCellData = grid->GetCellData();
    if (srcCellData)
        out->GetCellData()->CopyAllocate(srcCellData, grid->GetNumberOfCells() * 6);

    auto originalPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalPointIds->SetName("vtkOriginalPointIds");
    originalPointIds->SetNumberOfComponents(1);
    originalPointIds->SetNumberOfTuples(grid->GetNumberOfPoints());

    auto* sourceOriginalPointIds = grid->GetPointData()
                                       ? grid->GetPointData()->GetArray("vtkOriginalPointIds")
                                       : nullptr;
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        vtkIdType oid = pid;
        if (sourceOriginalPointIds && pid < sourceOriginalPointIds->GetNumberOfTuples())
            oid = static_cast<vtkIdType>(sourceOriginalPointIds->GetTuple1(pid));
        originalPointIds->SetValue(pid, oid);
    }

    auto originalCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalCellIds->SetName("vtkOriginalCellIds");
    originalCellIds->SetNumberOfComponents(1);

    auto originalFaceIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalFaceIds->SetName("vtkOriginalFaceIds");
    originalFaceIds->SetNumberOfComponents(1);

    auto originalFaceKeys = vtkSmartPointer<vtkIdTypeArray>::New();
    originalFaceKeys->SetName("vtkOriginalFaceKeys");
    originalFaceKeys->SetNumberOfComponents(1);

    const bool hasMatype = srcCellData && srcCellData->GetArray("MATYPE");
    const bool hasLayer = srcCellData && srcCellData->GetArray("LAYER");

    QHash<QByteArray, QVector<MeshDisplayFaceRef>> faceMap;
    faceMap.reserve(static_cast<int>(grid->GetNumberOfCells() * 3));

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        if (!meshDisplayCellPassesDisplayFilter(grid, cid, customFilter, ketypes, matypes, layers))
            continue;

        const int dim = meshDisplayCellDimensionFast(grid, cid);
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;

        MeshDisplayFaceRef ref;
        ref.cellId = cid;
        ref.originalCellId = meshDisplayOriginalCellId(grid, cid);
        ref.matype = meshDisplayDomainValue(grid, cid, "MATYPE");
        ref.layer = meshDisplayDomainValue(grid, cid, "LAYER");

        if (dim == 2)
        {
            ref.originalFaceId = 15;
            ref.vtkCellType = cell->GetCellType();

            vtkIdList* pids = cell->GetPointIds();
            if (!pids || pids->GetNumberOfIds() < 3)
                continue;

            ref.pointIds.reserve(static_cast<int>(pids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                ref.pointIds.push_back(pids->GetId(i));

            meshDisplayAddSurfaceFace(out, polys, originalCellIds, originalFaceIds, originalFaceKeys, srcCellData, ref);
            continue;
        }

        if (dim != 3)
            continue;

        const int nFaces = cell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* face = cell->GetFace(fi);
            if (!face)
                continue;

            vtkIdList* pids = face->GetPointIds();
            if (!pids || pids->GetNumberOfIds() < 3)
                continue;

            MeshDisplayFaceRef one = ref;
            one.originalFaceId = fi;
            one.vtkCellType = face->GetCellType();
            one.pointIds.reserve(static_cast<int>(pids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                one.pointIds.push_back(pids->GetId(i));

            if (one.pointIds.size() < 3)
                continue;

            // faceMap 使用完整高阶面点集作为 key，用于正确识别相邻单元共享面；
            // 写入 vtkPolyData 时再拆成若干一阶显示小面，避免高阶点乱连。
            faceMap[meshDisplayFaceKey(pids)].push_back(one);
        }
    }

           // 外表面：只有一个 owner；内部材料/分层交界面：两个 owner 但 MATYPE/LAYER 不同，也需要显示和可拾取。
    for (auto it = faceMap.constBegin(); it != faceMap.constEnd(); ++it)
    {
        const QVector<MeshDisplayFaceRef>& owners = it.value();
        if (owners.isEmpty())
            continue;

        if (owners.size() == 1)
        {
            meshDisplayAddSurfaceFace(out, polys, originalCellIds, originalFaceIds, originalFaceKeys, srcCellData, owners[0]);
            continue;
        }

        for (int i = 0; i < owners.size(); ++i)
        {
            bool visibleInterface = false;
            for (int j = 0; j < owners.size(); ++j)
            {
                if (i == j)
                    continue;
                if (meshDisplayDifferentDomain(owners[i], owners[j], hasMatype, hasLayer))
                {
                    visibleInterface = true;
                    break;
                }
            }

            if (visibleInterface)
                meshDisplayAddSurfaceFace(out, polys, originalCellIds, originalFaceIds, originalFaceKeys, srcCellData, owners[i]);
        }
    }

    if (out->GetPointData())
    {
        out->GetPointData()->RemoveArray("vtkOriginalPointIds");
        out->GetPointData()->AddArray(originalPointIds);
    }

    if (out->GetCellData())
    {
        out->GetCellData()->RemoveArray("vtkOriginalCellIds");
        out->GetCellData()->RemoveArray("vtkOriginalFaceIds");
        out->GetCellData()->RemoveArray("vtkOriginalFaceKeys");
        out->GetCellData()->AddArray(originalCellIds);
        out->GetCellData()->AddArray(originalFaceIds);
        out->GetCellData()->AddArray(originalFaceKeys);
    }

    out->Modified();
    return out;
}

static void meshDisplaySetMapperInput(vtkMapper* mapper, vtkDataSet* dataSet)
{
    if (!mapper || !dataSet)
        return;

    if (auto* polyMapper = vtkPolyDataMapper::SafeDownCast(mapper))
    {
        if (auto* poly = vtkPolyData::SafeDownCast(dataSet))
            polyMapper->SetInputData(poly);
        return;
    }

    if (auto* dataSetMapper = vtkDataSetMapper::SafeDownCast(mapper))
        dataSetMapper->SetInputData(dataSet);
}

static bool meshDisplayCellMatchesFilter(vtkUnstructuredGrid* grid,
                                         vtkIdType cellId,
                                         bool customFilter,
                                         const QSet<int>& ketypes,
                                         const QSet<int>& matypes,
                                         const QSet<int>& layers)
{
    return meshDisplayCellPassesDisplayFilter(grid, cellId, customFilter, ketypes, matypes, layers);
}

static QSet<int> meshDisplayCollectCellInts(vtkDataSet* dataSet, const char* arrayName)
{
    QSet<int> values;
    auto* array = dataSet && dataSet->GetCellData() ? dataSet->GetCellData()->GetArray(arrayName) : nullptr;
    if (!array)
        return values;

    for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
        values.insert(static_cast<int>(array->GetTuple1(i)));
    return values;
}

static vtkSmartPointer<vtkLookupTable> meshDisplayBuildLookupTable(const QSet<int>& values)
{
    if (values.isEmpty())
        return nullptr;

    int minValue = std::numeric_limits<int>::max();
    int maxValue = std::numeric_limits<int>::min();
    for (int value : values)
    {
        minValue = qMin(minValue, value);
        maxValue = qMax(maxValue, value);
    }
    if (minValue > maxValue)
        return nullptr;

    const int tableSize = maxValue - minValue + 1;
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(tableSize);
    lut->SetTableRange(static_cast<double>(minValue), static_cast<double>(maxValue));
    lut->Build();

    for (int i = 0; i < tableSize; ++i)
    {
        const int value = minValue + i;
        const QColor color = values.contains(value) ? meshDisplayCategoricalColor(value)
                                                    : QColor::fromRgbF(0.78, 0.78, 0.78);
        lut->SetTableValue(i, color.redF(), color.greenF(), color.blueF(), 1.0);
    }

    return lut;
}

static void meshDisplayApplyScalarColor(vtkMapper* mapper,
                                        vtkDataSet* dataSet,
                                        const char* arrayName,
                                        vtkLookupTable* lut)
{
    if (!mapper || !dataSet || !arrayName || !lut)
        return;

    auto* cd = dataSet->GetCellData();
    if (!cd || !cd->GetArray(arrayName))
    {
        mapper->ScalarVisibilityOff();
        return;
    }

    mapper->SetScalarModeToUseCellFieldData();
    mapper->SelectColorArray(arrayName);
    mapper->SetLookupTable(lut);
    mapper->UseLookupTableScalarRangeOn();
    mapper->ScalarVisibilityOn();
}

static vtkSmartPointer<vtkUnstructuredGrid> meshDisplayBuildLineGrid(vtkUnstructuredGrid* fullGrid,
                                                                     bool customFilter,
                                                                     const QSet<int>& ketypes,
                                                                     const QSet<int>& matypes,
                                                                     const QSet<int>& layers)
{
    auto lineIds = vtkSmartPointer<vtkIdList>::New();
    if (!fullGrid)
        return vtkSmartPointer<vtkUnstructuredGrid>::New();

    const vtkIdType nCells = fullGrid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        if (!meshDisplayCellPassesDisplayFilter(fullGrid, cid, customFilter, ketypes, matypes, layers))
            continue;
        if (meshDisplayCellDimensionFast(fullGrid, cid) == 1)
            lineIds->InsertNextId(cid);
    }

    if (lineIds->GetNumberOfIds() <= 0)
        return vtkSmartPointer<vtkUnstructuredGrid>::New();

    return meshDisplayExtractCells(fullGrid, lineIds);
}

static void meshDisplayApplyGlobalState(vtkUnstructuredGrid* fullGrid,
                                        vtkActor* surfaceActor,
                                        vtkMapper* surfaceMapper,
                                        vtkActor* lineActor,
                                        vtkMapper* lineMapper)
{
    if (!fullGrid || (!surfaceMapper && !lineMapper))
        return;

    const bool customFilter = PublicVariables::instance().get(kMeshDisplayFilterCustomKey).toBool();

    const QSet<int> ketypes = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterKetypeKey).toString());
    const QSet<int> matypes = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterMatypeKey).toString());
    const QSet<int> layers = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterLayerKey).toString());

    vtkSmartPointer<vtkPolyData> viewSurface =
        meshDisplayBuildMappedSurface(fullGrid, customFilter, ketypes, matypes, layers);

    vtkSmartPointer<vtkUnstructuredGrid> viewLineGrid =
        meshDisplayBuildLineGrid(fullGrid, customFilter, ketypes, matypes, layers);

    if (surfaceMapper)
        meshDisplaySetMapperInput(surfaceMapper, viewSurface);
    if (lineMapper)
        meshDisplaySetMapperInput(lineMapper, viewLineGrid);

    if (surfaceActor)
        surfaceActor->SetVisibility(viewSurface->GetNumberOfCells() > 0 ? 1 : 0);
    if (lineActor)
        lineActor->SetVisibility(viewLineGrid->GetNumberOfCells() > 0 ? 1 : 0);

    if (surfaceMapper)
        surfaceMapper->ScalarVisibilityOff();
    if (lineMapper)
        lineMapper->ScalarVisibilityOff();

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
    m_verticalAxis = 2;
    delete m_importSettingsWidget;
    m_importSettingsWidget = new MeshImportSettingsWidget();
    m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
    m_importSettingsWidget->setSourceFileName(m_sourceFileName);
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::importRequested, this,
            &MeshModuleController::onImportGridFileDialog);
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::verticalAxisChanged, this,
            [this](int axis)
            {
                if (axis < 1 || axis > 3)
                    return;
                m_verticalAxis = axis;
                MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
            });
    m_gridNode = new ProjectNode("网格", QUuid::createUuid().toString(), m_importSettingsWidget);
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
    root["vertical_axis"] = m_verticalAxis;

    if (hasGrid)
    {
        QString err;

        if (shouldRewriteGridSnapshot(snapshotPath, m_importedGrid))
        {
            if (!MeshGridIO::instance().exportIiinFile(snapshotPath, m_importedGrid, &err, m_verticalAxis))
            {
                return false;
            }
            markGridSnapshotSaved(snapshotPath, m_importedGrid);
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

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();

    return true;
}

bool MeshModuleController::loadProject()
{
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();

    delete m_importSettingsWidget;
    m_importSettingsWidget = new MeshImportSettingsWidget();
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::importRequested, this,
            &MeshModuleController::onImportGridFileDialog);
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::verticalAxisChanged, this,
            [this](int axis)
            {
                if (axis < 1 || axis > 3)
                    return;
                m_verticalAxis = axis;
                MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
            });
    m_gridNode = new ProjectNode("网格", QUuid::createUuid().toString(), m_importSettingsWidget);
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

    const bool hasSavedVerticalAxis = root.contains("vertical_axis");
    m_verticalAxis = root.value("vertical_axis").toInt(2);
    if (m_verticalAxis < 1 || m_verticalAxis > 3)
        m_verticalAxis = 2;
    if (m_importSettingsWidget)
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);

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
        if (!hasSavedVerticalAxis)
            m_verticalAxis = MeshGridIO::verticalAxisOfGrid(m_importedGrid, m_verticalAxis);
        MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
        m_hasGrid = true;
        m_sourceFileName.clear();
        if (m_importSettingsWidget)
        {
            m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
            m_importSettingsWidget->setSourceFileName(m_sourceFileName);
        }

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
            else
            {
                // 新版 mesh_map.json 不再保存 MATYPE/LAYER 的大 cellIds，
                // 这里统一从网格 CellData 重建材料/层级分组和 cellId 反查缓存。
                syncPickMapGroupingFromGrid(m_importedGrid);
            }
        }
        else
        {
            syncPickMapGroupingFromGrid(m_importedGrid);
        }

        markGridSnapshotSaved(snapshotPath, m_importedGrid);

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

        if (kind == "MeshImportSettings")
            return nullptr;


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

        const QString title = kind == "MeshEdit" ? QStringLiteral("显示控制")
                                                   : obj.value("title").toString(kind);
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
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    QHash<MeshTagKey, MeshTagSet> sets;

           // 主入口：tagSets。这里是 UI 和业务层唯一应该依赖的边界模型。
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        MeshTagSet set = it.value();
        if (set.key.category != QStringLiteral("Physical"))
            continue;
        if (set.key.dim < 0 || set.key.dim >= map.meshDim)
            continue;
        if (dimFilter >= 0 && set.key.dim != dimFilter)
            continue;

        if (set.pointIds.isEmpty() && set.edgeKeys.isEmpty() && set.faceKeys.isEmpty())
            continue;

        normalizeMeshTagSet(set);
        sets.insert(set.key, set);
    }

           // 不再从任何旧映射兜底；Controller 对外只返回 tagSets。
    return sortedMeshTagSets(sets);
}

QVector<MeshIdType> MeshModuleController::cellIdsOfTag(const MeshTagKey& key) const
{
    if (!key.isValid())
        return {};

    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key))
    {
        QVector<MeshIdType> ids = map.tagSets.value(key).cellIds;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }
    return {};
}

QVector<MeshIdType> MeshModuleController::pointIdsOfTag(const MeshTagKey& key) const
{
    if (!key.isValid())
        return {};

    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key))
    {
        QVector<MeshIdType> ids = map.tagSets.value(key).pointIds;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    return {};
}

QString MeshModuleController::displayTextOfTag(const MeshTagKey& key) const
{
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    if (map.tagSets.contains(key))
    {
        const QString name = normalizePhysicalUiName(key.dim, key.id, map.tagSets.value(key).name);
        if (key.category == QStringLiteral("Physical"))
            return name;
        return QStringLiteral("%1：%2").arg(key.category, name);
    }

    if (key.category == QStringLiteral("Physical"))
        return normalizePhysicalUiName(key.dim, key.id, map.physIdToName.value(key.id));

    return QStringLiteral("%1=%2").arg(key.category).arg(key.id);
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

        if (set.key.id < FIRST_COMPUTABLE_MATYPE)
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
            if (id >= FIRST_COMPUTABLE_MATYPE)
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
    if (!meshDomainKeyIsValid(key))
        return out;

    const MeshTagKey tagKey{key.category, key.dim, key.id};
    for (MeshIdType cid : cellIdsOfTag(tagKey))
        out.insert(cid);
    return out;
}

QString MeshModuleController::domainDisplayText(const DomainKey& key) const
{
    if (!meshDomainKeyIsValid(key))
        return QString();
    return displayTextOfTag(MeshTagKey{key.category, key.dim, key.id});
}

void MeshModuleController::highlightDomainMatypes(const QSet<int>& matypes, int selectedMatype,
                                                  int previewMatype, int preDeleteMatype)
{
    Q_UNUSED(selectedMatype);

    if (!m_visualWidgetService)
        return;

    QList<vtkMapper*> targetMappers;
    if (m_previewSurfaceMapper)
        targetMappers.push_back(m_previewSurfaceMapper);
    if (m_previewLineMapper)
        targetMappers.push_back(m_previewLineMapper);
    if (targetMappers.isEmpty() && m_previewMapper)
        targetMappers.push_back(m_previewMapper);
    if (targetMappers.isEmpty())
        return;

    const bool hasSelection =
        !matypes.isEmpty() || previewMatype >= 0 || preDeleteMatype >= 0;

    if (!hasSelection)
    {
        // 清除材料临时高亮后，恢复全局显示控制的颜色状态。
        meshDisplayApplyGlobalState(m_importedGrid,
                                    m_previewSurfaceActor,
                                    m_previewSurfaceMapper,
                                    m_previewLineActor,
                                    m_previewLineMapper);
        PickManager::instance().updateStructure();
        m_visualWidgetService->render();
        return;
    }

    QSet<int> allMatypes = getAvailableMatypes();
    if (previewMatype >= 0)
        allMatypes.insert(previewMatype);
    if (preDeleteMatype >= 0)
        allMatypes.insert(preDeleteMatype);
    allMatypes.unite(matypes);

    int maxMatype = -1;
    for (int v : allMatypes)
        maxMatype = qMax(maxMatype, v);
    if (maxMatype < 0)
        return;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxMatype + 1);
    lut->SetTableRange(0.0, maxMatype);
    lut->Build();

    for (int i = 0; i <= maxMatype; ++i)
        lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);

    for (int matype : matypes)
    {
        if (matype >= 0 && matype <= maxMatype)
            lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
    }
    if (previewMatype >= 0 && previewMatype <= maxMatype)
        lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
    if (preDeleteMatype >= 0 && preDeleteMatype <= maxMatype)
        lut->SetTableValue(preDeleteMatype, 0.9, 0.55, 0.2, 1.0);

    for (auto* mapper : targetMappers)
    {
        if (!mapper)
            continue;
        mapper->SetScalarModeToUseCellFieldData();
        mapper->SelectColorArray("MATYPE");
        mapper->SetLookupTable(lut);
        mapper->UseLookupTableScalarRangeOn();
        mapper->ScalarVisibilityOn();
    }

    m_visualWidgetService->render();
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
    openImportSettingsPage(true);
    QTimer::singleShot(0, this, &MeshModuleController::onImportGridFileDialog);
}

void MeshModuleController::onImportGridFileDialog()
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

    importGridFile(fileName);
}

void MeshModuleController::importGridFile(const QString& fileName)
{
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
                             QString("当前只支持 .nas / .inp / .flac3d / .f3grid / .dat / .msh"));
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
    if (ext == "dat")
        m_verticalAxis = MeshGridIO::verticalAxisOfGrid(m_importedGrid, m_verticalAxis);
    else
        MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);

    if (m_importSettingsWidget)
    {
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
        m_importSettingsWidget->setSourceFileName(m_sourceFileName);
    }

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
    openImportSettingsPage(true);

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
    openGridDisplayControlPage();

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
    openGridDisplayControlPage();
    PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
    return true;
}

MshPickMap MeshModuleController::mshPickMapSolid()
{
    // 仅保留兼容旧页面的出口；不再在这里生成/修正边界规则。
    // 规则统一在 MeshGridIO::normalizePhysicalTagSets() 中完成。
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
    // 统一入口交给 MeshGridIO::setMeshMap() 规整，Controller 不再维护第二套边界规则。
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
    if (!MeshGridIO::instance().exportIiinFile(fileName, m_importedGrid, &err, m_verticalAxis))
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
    auto w = std::make_unique<QWidget>();
}

void MeshModuleController::onMeshSweep()
{
    // auto* w = new MeshSweepMeshingWidget();
    auto w = std::make_unique<QWidget>();
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
            QDir(QCoreApplication::applicationDirPath()).filePath("grid_quality");

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

        file.write(content.toUtf8());
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

void MeshModuleController::openImportSettingsPage(bool setCurrent)
{
    if (!m_gridNode)
        return;

    if (!m_importSettingsWidget)
    {
        m_importSettingsWidget = new MeshImportSettingsWidget();
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
        m_importSettingsWidget->setSourceFileName(m_sourceFileName);
        connect(m_importSettingsWidget, &MeshImportSettingsWidget::importRequested, this,
                &MeshModuleController::onImportGridFileDialog);
        connect(m_importSettingsWidget, &MeshImportSettingsWidget::verticalAxisChanged, this,
                [this](int axis)
                {
                    if (axis < 1 || axis > 3)
                        return;
                    m_verticalAxis = axis;
                    MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
                });
        m_gridNode->setPage(m_importSettingsWidget);
    }
    else
    {
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
        m_importSettingsWidget->setSourceFileName(m_sourceFileName);
        if (!m_gridNode->getPage())
            m_gridNode->setPage(m_importSettingsWidget);
    }

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(m_gridNode);
    }
}


void MeshModuleController::onMeshEdit()
{
    openGridDisplayControlPage();
}

void MeshModuleController::openGridDisplayControlPage()
{
    if (!m_gridNode)
        return;

    if (auto* existed = findDirectChildByKind(m_gridNode, "MeshEdit"))
    {
        if (m_gridEditingWidget && m_importedGrid && m_importedGrid->GetNumberOfCells() > 0)
        {
            m_gridEditingWidget->setEditableGrid(m_importedGrid, m_sourceFileName);
        }

        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existed);
        return;
    }

    auto* w = new GridEditingWidget();
    m_gridEditingWidget = w;
    if (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0)
    {
        w->setEditableGrid(m_importedGrid, m_sourceFileName);
    }

    auto* node = new ProjectNode("显示控制", QUuid::createUuid().toString(), w);
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

    static const QString kMeshSurfId = QString("mesh.imported.surface");
    static const QString kMeshLineId = QString("mesh.imported.line");
    m_visualWidgetService->removeProp(kMeshSurfId, false);
    m_visualWidgetService->removeProp(kMeshLineId, false);

           // 这里不再先 splitGridByDim / vtkExtractCells 再抽 surface。
           // surface/line 的真实输入由 meshDisplayApplyGlobalState 一次性从 m_importedGrid 构造，
           // 避免导入阶段重复拷贝网格和重复构造外表面。
    auto emptySurface = vtkSmartPointer<vtkPolyData>::New();
    auto surfaceMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    surfaceMapper->SetInputData(emptySurface);
    surfaceMapper->ScalarVisibilityOff();

    auto surfActor = vtkSmartPointer<vtkActor>::New();
    surfActor->SetMapper(surfaceMapper);
    auto* surfProp = surfActor->GetProperty();
    surfProp->SetRepresentationToSurface();
    surfProp->SetColor(0.78, 0.78, 0.78);
    surfProp->EdgeVisibilityOn();
    surfProp->SetEdgeColor(0.0, 0.0, 0.0);
    surfProp->SetLineWidth(1.0);
    m_visualWidgetService->addProp(kMeshSurfId, surfActor, QString("网格"),
                                   QString("导入网格-面/体"), true, true);

    auto emptyLine = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto lineMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    lineMapper->SetInputData(emptyLine);
    lineMapper->ScalarVisibilityOff();

    auto lineActor = vtkSmartPointer<vtkActor>::New();
    lineActor->SetMapper(lineMapper);
    auto* lineProp = lineActor->GetProperty();
    lineProp->SetColor(0.78, 0.78, 0.78);
    lineProp->SetLineWidth(2.0);
    lineProp->SetSpecular(0.0);
    lineProp->RenderLinesAsTubesOn();
    m_visualWidgetService->addProp(kMeshLineId, lineActor, QString("网格"),
                                   QString("导入网格-线"), true, true);

    m_previewSurfaceGrid = m_importedGrid;
    m_previewSurfaceActor = surfActor;
    m_previewSurfaceMapper = surfaceMapper;
    m_previewLineGrid = emptyLine;
    m_previewLineActor = lineActor;
    m_previewLineMapper = lineMapper;

    m_previewGrid = m_previewSurfaceGrid;
    m_previewActor = m_previewSurfaceActor;
    m_previewMapper = m_previewSurfaceMapper;

           // 网格导入/预览后默认只显示网格。
    m_visualWidgetService->setGroupVisible("Geometry", false, false);

           // 全局显示控制在这里统一应用。该函数会从 m_importedGrid 一次性构造：
           // 1) 只包含外表面/必要内部分界面的 surface；
           // 2) 带 vtkOriginalCellIds / vtkOriginalFaceIds / vtkOriginalFaceKeys 的拾取映射；
           // 3) 线单元显示网格。
    meshDisplayApplyGlobalState(m_importedGrid,
                                m_previewSurfaceActor,
                                m_previewSurfaceMapper,
                                m_previewLineActor,
                                m_previewLineMapper);

    if (auto* dsMapper = vtkDataSetMapper::SafeDownCast(m_previewLineMapper))
        m_previewLineGrid = vtkUnstructuredGrid::SafeDownCast(dsMapper->GetInput());

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

int MeshModuleController::verticalAxis() const
{
    return m_verticalAxis;
}

bool MeshModuleController::exportIiinDatFile(const QString& filePath, QString* errMsg)
{
    if (!m_hasGrid || !m_importedGrid)
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前无可导出的网格数据");
        return false;
    }
    MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
    return MeshGridIO::instance().exportIiinFile(filePath, m_importedGrid, errMsg, m_verticalAxis);
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

        const int dim = meshDisplayCellDimensionFast(src, cid);
        if (dim == 1)
        {
            lineIds->InsertNextId(cid);
        }
        else
        {
            nonLineIds->InsertNextId(cid);
        }
    }

    auto sourceWithIds = meshDisplayCopyWithFullCellIds(src);

    if (lineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(sourceWithIds);
        ex->SetCellList(lineIds);
        ex->Update();
        outLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }

    if (nonLineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(sourceWithIds);
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
    p->SetColor(0.78, 0.78, 0.78);  // 与导入网格默认色一致
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
    p->SetColor(0.78, 0.78, 0.78); // 与面/体网格默认颜色保持一致
    p->SetLineWidth(2.0);
    p->SetSpecular(0.0);
    p->RenderLinesAsTubesOn();

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

        meshDim = qMax(meshDim, meshDisplayCellDimensionFast(grid, cid));
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

        const int cellDim = meshDisplayCellDimensionFast(grid, cid);
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
    const bool targetIsMatype = targetArrayName.compare("MATYPE", Qt::CaseInsensitive) == 0;
    if ((targetIsMatype && startValue < FIRST_COMPUTABLE_MATYPE) ||
        (!targetIsMatype && startValue <= 0))
    {
        if (errMsg && targetIsMatype)
            *errMsg = QStringLiteral("MATYPE 起始编号必须大于等于 2，1 表示不进入计算");
        else if (errMsg)
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
        const double cx = CENTER_FACTOR * (b[0] + b[1]);
        const double cy = CENTER_FACTOR * (b[2] + b[3]);
        const double cz = CENTER_FACTOR * (b[4] + b[5]);
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
        m_importedGrid->GetCellData()->GetArray(targetArrayName.toUtf8().constData()));
    if (!targetArr)
    {
        auto newArr = vtkSmartPointer<vtkIntArray>::New();
        newArr->SetName(targetArrayName.toUtf8().constData());
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
        groupArr->SetValue(cid, QString("%1_%2").arg(groupPrefix).arg(targetValue).toUtf8().constData());
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
    const bool targetIsLayer = targetArrayName.compare("LAYER", Qt::CaseInsensitive) == 0;
    const bool targetIsMatype = targetArrayName.compare("MATYPE", Qt::CaseInsensitive) == 0;
    if ((targetIsLayer && value == 0) ||
        (targetIsMatype && value < FIRST_COMPUTABLE_MATYPE) ||
        (!targetIsLayer && !targetIsMatype && value <= 0))
    {
        if (errMsg && targetIsMatype)
            *errMsg = QStringLiteral("MATYPE 分组编号必须大于等于 2，1 表示不进入计算");
        else if (errMsg)
            *errMsg = targetIsLayer ? QString("LAYER 分组编号不能为 0")
                                    : QString("分组编号必须大于 0");
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
        const double cx = CENTER_FACTOR * (b[0] + b[1]);
        const double cy = CENTER_FACTOR * (b[2] + b[3]);
        const double cz = CENTER_FACTOR * (b[4] + b[5]);
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
