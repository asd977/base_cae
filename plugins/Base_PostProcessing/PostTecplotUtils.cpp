#include "PostTecplotUtils.h"

#include <vtkAppendFilter.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkConvexPointSet.h>
#include <vtkDataArray.h>
#include <vtkDataObjectTreeIterator.h>
#include <vtkFieldData.h>
#include <vtkIntArray.h>
#include <vtkDataSet.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridReader.h>
#include <vtkXMLPolyDataReader.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTemporaryFile>
#include <QWaitCondition>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <initializer_list>
#include <limits>
#include <vector>

namespace
{
constexpr int kTopologyAbnormal = -1;
constexpr char kNormalizedTopologyMarker[] = "CAE_POST_TOPOLOGY_NORMALIZED";


struct SurfaceCacheEntry
{
    qint64 size = -1;
    qint64 modifiedMs = -1;
    quint64 accessSerial = 0;
    vtkSmartPointer<vtkPolyData> surface;
};

QHash<QString, SurfaceCacheEntry>& surfaceCache()
{
    static QHash<QString, SurfaceCacheEntry> cache;
    return cache;
}

QSet<QString>& surfaceLoadsInFlight()
{
    static QSet<QString> files;
    return files;
}

struct GridCacheEntry
{
    qint64 size = -1;
    qint64 modifiedMs = -1;
    quint64 accessSerial = 0;
    vtkSmartPointer<vtkUnstructuredGrid> grid;
};

QMutex& gridCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, GridCacheEntry>& gridCache()
{
    static QHash<QString, GridCacheEntry> cache;
    return cache;
}

quint64& gridCacheSerial()
{
    static quint64 serial = 0;
    return serial;
}

quint64& surfaceCacheSerial()
{
    static quint64 serial = 0;
    return serial;
}

QSet<QString>& gridLoadsInFlight()
{
    static QSet<QString> loads;
    return loads;
}

QWaitCondition& gridLoadWaitCondition()
{
    static QWaitCondition condition;
    return condition;
}

class GridLoadGuard
{
  public:
    explicit GridLoadGuard(const QString& key) : m_key(key) {}
    ~GridLoadGuard()
    {
        if (m_key.isEmpty())
            return;
        QMutexLocker locker(&gridCacheMutex());
        gridLoadsInFlight().remove(m_key);
        gridLoadWaitCondition().wakeAll();
    }

    GridLoadGuard(const GridLoadGuard&) = delete;
    GridLoadGuard& operator=(const GridLoadGuard&) = delete;

  private:
    QString m_key;
};

vtkSmartPointer<vtkUnstructuredGrid> shallowGridCopy(vtkUnstructuredGrid* source)
{
    if (!source)
        return nullptr;
    auto copy = vtkSmartPointer<vtkUnstructuredGrid>::New();
    copy->ShallowCopy(source);
    return copy;
}

bool triangulatePointSetToTetrahedra(const QVector<vtkIdType>& pointIds,
                                     vtkPoints* sourcePoints,
                                     QVector<QVector<vtkIdType>>& tetrahedra)
{
    tetrahedra.clear();
    if (!sourcePoints || pointIds.size() < 4)
        return false;

    auto convex = vtkSmartPointer<vtkConvexPointSet>::New();
    convex->GetPointIds()->SetNumberOfIds(pointIds.size());
    convex->GetPoints()->SetNumberOfPoints(pointIds.size());
    for (int i = 0; i < pointIds.size(); ++i)
    {
        const vtkIdType pid = pointIds[i];
        if (pid < 0 || pid >= sourcePoints->GetNumberOfPoints())
            return false;
        double p[3]{0.0, 0.0, 0.0};
        sourcePoints->GetPoint(pid, p);
        convex->GetPointIds()->SetId(i, pid);
        convex->GetPoints()->SetPoint(i, p);
    }

    auto ids = vtkSmartPointer<vtkIdList>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    if (!convex->Triangulate(0, ids, points) ||
        ids->GetNumberOfIds() < 4 ||
        ids->GetNumberOfIds() % 4 != 0)
        return false;

    tetrahedra.reserve(static_cast<int>(ids->GetNumberOfIds() / 4));
    for (vtkIdType i = 0; i + 3 < ids->GetNumberOfIds(); i += 4)
    {
        QVector<vtkIdType> tet{
            ids->GetId(i), ids->GetId(i + 1), ids->GetId(i + 2), ids->GetId(i + 3)};
        QSet<vtkIdType> unique;
        for (vtkIdType pid : tet)
            unique.insert(pid);
        if (unique.size() != 4)
            continue;
        tetrahedra.push_back(tet);
    }
    return !tetrahedra.isEmpty();
}


// 对求解器统一 8 槽位 BRICK 的非标准退化形式，按原 Hex 参考拓扑的
// 0-6 体对角线做固定六四面体分解。与“把唯一点当凸包重新三角化”相比，
// 这种方式始终尊重原 8 个槽位的连接关系，不会把非凸/塌缩体错误连成跨域尖片。
// 重复节点或近零体积 Tet 会被直接丢弃。
bool tetrahedralizeCollapsedHexSlots(vtkCell* cell, vtkPoints* sourcePoints,
                                      QVector<QVector<vtkIdType>>& tetrahedra,
                                      int* rejectedDegenerate = nullptr)
{
    tetrahedra.clear();
    if (rejectedDegenerate)
        *rejectedDegenerate = 0;
    if (!cell || !sourcePoints || cell->GetNumberOfPoints() != 8)
        return false;

    static constexpr int kTetSlots[6][4] = {
        {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
        {0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6},
    };

    double bounds[6] = {
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
    };
    for (vtkIdType i = 0; i < 8; ++i)
    {
        double p[3] = {0.0, 0.0, 0.0};
        sourcePoints->GetPoint(cell->GetPointId(i), p);
        bounds[0] = std::min(bounds[0], p[0]);
        bounds[1] = std::max(bounds[1], p[0]);
        bounds[2] = std::min(bounds[2], p[1]);
        bounds[3] = std::max(bounds[3], p[1]);
        bounds[4] = std::min(bounds[4], p[2]);
        bounds[5] = std::max(bounds[5], p[2]);
    }
    const double dx = bounds[1] - bounds[0];
    const double dy = bounds[3] - bounds[2];
    const double dz = bounds[5] - bounds[4];
    const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double volume6Tolerance =
        std::max(diagonal * diagonal * diagonal * 1.0e-12, 1.0e-24);

    QSet<QByteArray> emitted;
    tetrahedra.reserve(6);
    // Qt 将 slots 定义为关键字宏，局部变量不能命名为 slots。
    for (const auto& slotIds : kTetSlots)
    {
        QVector<vtkIdType> tet{
            cell->GetPointId(slotIds[0]), cell->GetPointId(slotIds[1]),
            cell->GetPointId(slotIds[2]), cell->GetPointId(slotIds[3])};

        QSet<vtkIdType> unique;
        for (vtkIdType pid : tet)
            unique.insert(pid);
        if (unique.size() != 4)
        {
            if (rejectedDegenerate)
                ++(*rejectedDegenerate);
            continue;
        }

        double a[3], b[3], c[3], d[3];
        sourcePoints->GetPoint(tet[0], a);
        sourcePoints->GetPoint(tet[1], b);
        sourcePoints->GetPoint(tet[2], c);
        sourcePoints->GetPoint(tet[3], d);
        const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double ad[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]};
        const double cross[3] = {
            ac[1] * ad[2] - ac[2] * ad[1],
            ac[2] * ad[0] - ac[0] * ad[2],
            ac[0] * ad[1] - ac[1] * ad[0],
        };
        const double volume6 =
            ab[0] * cross[0] + ab[1] * cross[1] + ab[2] * cross[2];
        if (!std::isfinite(volume6) || std::abs(volume6) <= volume6Tolerance)
        {
            if (rejectedDegenerate)
                ++(*rejectedDegenerate);
            continue;
        }

        // 统一为正向 Tet，避免 Cutter/Contour 对倒置小体产生不稳定结果。
        if (volume6 < 0.0)
            std::swap(tet[1], tet[2]);

        QVector<vtkIdType> sorted = tet;
        std::sort(sorted.begin(), sorted.end());
        QByteArray key;
        key.reserve(int(sizeof(vtkIdType)) * 4);
        for (vtkIdType pid : sorted)
            key.append(reinterpret_cast<const char*>(&pid), int(sizeof(vtkIdType)));
        if (emitted.contains(key))
            continue;
        emitted.insert(key);
        tetrahedra.push_back(tet);
    }

    return !tetrahedra.isEmpty();
}


struct LegacyHexTopologyInfo
{
    bool valid = false;
    bool degenerateHexFallback = false;
    int vtkCellType = VTK_EMPTY_CELL;
    int topologyType = VTK_EMPTY_CELL;      // 后处理筛选使用；合法非标准塌缩体归入六面体家族
    QVector<int> localNodeSlots;            // 标准 VTK 局部节点 -> Tecplot BRICK 的 8 个槽位
    QVector<QVector<int>> polyhedronFaces;  // 仅用于判断非标准退化 Hex 是否仍构成合法闭合体
    int uniqueCount = 0;
};

int bitCount8(quint32 mask)
{
    int count = 0;
    while (mask)
    {
        count += int(mask & 1u);
        mask >>= 1u;
    }
    return count;
}

QVector<quint32> sortedUniqueMasks(QVector<quint32> masks)
{
    std::sort(masks.begin(), masks.end());
    masks.erase(std::unique(masks.begin(), masks.end()), masks.end());
    return masks;
}

QVector<quint32> collapsedLegacyHexFaceMasks(const std::array<int, 8>& labels)
{
    static constexpr int kHexFaces[6][4] = {
        {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0},
    };

    QVector<quint32> faces;
    faces.reserve(6);
    for (const auto& face : kHexFaces)
    {
        quint32 mask = 0;
        for (int slot : face)
            mask |= (1u << labels[slot]);
        if (bitCount8(mask) >= 3)
            faces.push_back(mask);
    }
    return sortedUniqueMasks(faces);
}


QVector<int> collapsedLegacyHexFace(const std::array<int, 8>& labels,
                                    const int face[4])
{
    QVector<int> ordered;
    ordered.reserve(4);
    for (int i = 0; i < 4; ++i)
    {
        const int label = labels[face[i]];
        if (ordered.isEmpty() || ordered.last() != label)
            ordered.push_back(label);
    }

    if (ordered.size() > 1 && ordered.first() == ordered.last())
        ordered.removeLast();

    QSet<int> seen;
    for (int label : ordered)
    {
        if (seen.contains(label))
            return QVector<int>{-1}; // 非相邻重复：非法折返面
        seen.insert(label);
    }

    if (ordered.size() < 3)
        return {};
    return ordered;
}

bool buildClosedLegacyPolyhedronFaces(const std::array<int, 8>& labels,
                                      int uniqueCount,
                                      QVector<QVector<int>>& outFaces)
{
    outFaces.clear();
    if (uniqueCount < 4 || uniqueCount > 8)
        return false;

    static constexpr int kHexFaces[6][4] = {
        {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0},
    };

    QSet<quint32> faceMasks;
    quint32 usedVertexMask = 0;
    QHash<int, int> edgeUseCount;

    for (const auto& rawFace : kHexFaces)
    {
        QVector<int> face = collapsedLegacyHexFace(labels, rawFace);
        if (face.size() == 1 && face.first() < 0)
            return false;
        if (face.size() < 3)
            continue;

        quint32 mask = 0;
        for (int label : face)
        {
            if (label < 0 || label >= uniqueCount)
                return false;
            mask |= (1u << label);
        }

        if (faceMasks.contains(mask))
            continue;
        faceMasks.insert(mask);
        usedVertexMask |= mask;

        for (int i = 0; i < face.size(); ++i)
        {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            if (a == b)
                return false;
            const int lo = qMin(a, b);
            const int hi = qMax(a, b);
            const int edgeKey = (lo << 4) | hi;
            edgeUseCount[edgeKey] = edgeUseCount.value(edgeKey) + 1;
        }

        outFaces.push_back(face);
    }

    if (outFaces.size() < 4)
        return false;

    const quint32 expectedVertexMask = (1u << uniqueCount) - 1u;
    if (usedVertexMask != expectedVertexMask)
        return false;

    for (auto it = edgeUseCount.constBegin(); it != edgeUseCount.constEnd(); ++it)
    {
        if (it.value() != 2)
            return false;
    }

    return uniqueCount - edgeUseCount.size() + outFaces.size() == 2;
}

int firstSourceSlotForLabel(const std::array<int, 8>& labels, int label)
{
    for (int slot = 0; slot < 8; ++slot)
    {
        if (labels[slot] == label)
            return slot;
    }
    return -1;
}

bool buildOrientedPolyhedronFaceStream(vtkPoints* points,
                                       const QVector<vtkIdType>& pointIds,
                                       const QVector<QVector<int>>& localFaces,
                                       QVector<vtkIdType>& faceStream)
{
    faceStream.clear();
    if (!points || pointIds.size() < 4 || localFaces.size() < 4)
        return false;

    double cellCenter[3] = {0.0, 0.0, 0.0};
    for (vtkIdType pid : pointIds)
    {
        double p[3] = {0.0, 0.0, 0.0};
        points->GetPoint(pid, p);
        cellCenter[0] += p[0];
        cellCenter[1] += p[1];
        cellCenter[2] += p[2];
    }
    const double invPointCount = 1.0 / static_cast<double>(pointIds.size());
    cellCenter[0] *= invPointCount;
    cellCenter[1] *= invPointCount;
    cellCenter[2] *= invPointCount;

    faceStream.reserve(localFaces.size() * 5);
    for (QVector<int> face : localFaces)
    {
        if (face.size() < 3)
            return false;

        double faceCenter[3] = {0.0, 0.0, 0.0};
        double normal[3] = {0.0, 0.0, 0.0};

        for (int i = 0; i < face.size(); ++i)
        {
            const int localA = face[i];
            const int localB = face[(i + 1) % face.size()];
            if (localA < 0 || localA >= pointIds.size() ||
                localB < 0 || localB >= pointIds.size())
                return false;

            double a[3] = {0.0, 0.0, 0.0};
            double b[3] = {0.0, 0.0, 0.0};
            points->GetPoint(pointIds[localA], a);
            points->GetPoint(pointIds[localB], b);

            faceCenter[0] += a[0];
            faceCenter[1] += a[1];
            faceCenter[2] += a[2];

            normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
            normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
            normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }

        const double invFacePointCount = 1.0 / static_cast<double>(face.size());
        faceCenter[0] *= invFacePointCount;
        faceCenter[1] *= invFacePointCount;
        faceCenter[2] *= invFacePointCount;

        const double n2 =
            normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
        if (n2 <= 1.0e-24)
            return false;

        const double outward[3] = {
            faceCenter[0] - cellCenter[0],
            faceCenter[1] - cellCenter[1],
            faceCenter[2] - cellCenter[2],
        };
        const double dot =
            normal[0] * outward[0] + normal[1] * outward[1] + normal[2] * outward[2];
        if (dot < 0.0)
            std::reverse(face.begin(), face.end());

        faceStream.push_back(static_cast<vtkIdType>(face.size()));
        for (int local : face)
            faceStream.push_back(pointIds[local]);
    }

    return true;
}

void addTopologyTypeArray(vtkUnstructuredGrid* grid,
                          const QVector<int>* topologyValues = nullptr)
{
    if (!grid || !grid->GetCellData())
        return;

    auto topologyType = vtkSmartPointer<vtkIntArray>::New();
    topologyType->SetName("TOPOLOGY_TYPE");
    topologyType->SetNumberOfComponents(1);
    topologyType->SetNumberOfTuples(grid->GetNumberOfCells());
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        int value = grid->GetCellType(cid);
        if (topologyValues && cid < topologyValues->size() &&
            topologyValues->at(static_cast<int>(cid)) != VTK_EMPTY_CELL)
        {
            value = topologyValues->at(static_cast<int>(cid));
        }
        topologyType->SetValue(cid, value);
    }

    grid->GetCellData()->RemoveArray("TOPOLOGY_TYPE");
    grid->GetCellData()->AddArray(topologyType);
}

QVector<QVector<int>> standardFacesForVtkCell(int vtkCellType)
{
    switch (vtkCellType)
    {
    case VTK_TETRA:
        return {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    case VTK_PYRAMID:
        return {{0, 1, 2, 3}, {0, 4, 1}, {1, 4, 2}, {2, 4, 3}, {3, 4, 0}};
    case VTK_WEDGE:
        return {{0, 2, 1}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {2, 0, 3, 5}};
    case VTK_HEXAHEDRON:
        return {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 4, 5, 1},
                {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
    default:
        return {};
    }
}

QVector<quint32> mappedStandardFaceMasks(int vtkCellType,
                                         const std::vector<int>& stdToLabel)
{
    QVector<quint32> masks;
    const auto faces = standardFacesForVtkCell(vtkCellType);
    masks.reserve(faces.size());
    for (const QVector<int>& face : faces)
    {
        quint32 mask = 0;
        for (int local : face)
        {
            if (local < 0 || local >= static_cast<int>(stdToLabel.size()))
                return {};
            mask |= (1u << stdToLabel[local]);
        }
        masks.push_back(mask);
    }
    return sortedUniqueMasks(masks);
}

// 求解器/后处理 Tecplot 缓存把 Tet/Pyramid/Wedge 等退化写成 8 节点 BRICK。
// 能还原为标准线性实体的全部归入对应标准拓扑；不能等价为标准单元、
// 但原 Hex 六个面塌缩后仍是合法闭合体的，继续归入“六面体”家族。
// 这类非标准闭合退化体不再直接保存为 Polyhedron/ConvexPointSet。
// 规范化阶段会把它拆成标准 VTK_TETRA，保证 Cutter/Contour 只处理标准实体；
// 只有非法塌缩才标记为“异常”。
LegacyHexTopologyInfo analyzeLegacyHexTopology(vtkCell* cell)
{
    LegacyHexTopologyInfo invalid;
    if (!cell || cell->GetCellType() != VTK_HEXAHEDRON ||
        cell->GetNumberOfPoints() != 8)
        return invalid;

    std::array<vtkIdType, 8> uniquePointIds{};
    std::array<int, 8> labels{};
    int uniqueCount = 0;
    quint32 cacheKey = 0;

    for (int slot = 0; slot < 8; ++slot)
    {
        const vtkIdType pointId = cell->GetPointId(slot);
        if (pointId < 0)
            return invalid;

        int label = -1;
        for (int i = 0; i < uniqueCount; ++i)
        {
            if (uniquePointIds[i] == pointId)
            {
                label = i;
                break;
            }
        }
        if (label < 0)
        {
            if (uniqueCount >= 8)
                return invalid;
            label = uniqueCount;
            uniquePointIds[uniqueCount++] = pointId;
        }
        labels[slot] = label;
        cacheKey |= (quint32(label & 0x7) << (slot * 3));
    }

    static QHash<quint32, LegacyHexTopologyInfo> cache;
    static QMutex cacheMutex;
    {
        QMutexLocker locker(&cacheMutex);
        const auto cached = cache.constFind(cacheKey);
        if (cached != cache.constEnd())
            return cached.value();
    }

    LegacyHexTopologyInfo info;
    info.uniqueCount = uniqueCount;
    auto storeCached = [&](const LegacyHexTopologyInfo& value)
    {
        QMutexLocker locker(&cacheMutex);
        cache.insert(cacheKey, value);
    };

    // 常见旧 BRICK 退化模式直接识别，避免通用排列匹配的开销和歧义。
    // A B C C D E F F = 标准三棱柱（两个三角形端面）。
    auto matchesLabels = [&](std::initializer_list<int> pattern) -> bool
    {
        if (static_cast<int>(pattern.size()) != 8)
            return false;
        int i = 0;
        for (int value : pattern)
        {
            if (labels[i++] != value)
                return false;
        }
        return true;
    };

    auto makeCanonical = [&](int vtkType, std::initializer_list<int> sourceSlots) -> LegacyHexTopologyInfo
    {
        LegacyHexTopologyInfo canonical;
        canonical.valid = true;
        canonical.uniqueCount = uniqueCount;
        canonical.vtkCellType = vtkType;
        canonical.topologyType = vtkType;
        canonical.localNodeSlots.reserve(static_cast<int>(sourceSlots.size()));
        for (int slot : sourceSlots)
            canonical.localNodeSlots.push_back(slot);
        return canonical;
    };

    if (matchesLabels({0, 1, 2, 2, 3, 3, 3, 3}))
    {
        info = makeCanonical(VTK_TETRA, {0, 1, 2, 4});
        storeCached(info);
        return info;
    }
    if (matchesLabels({0, 1, 2, 3, 4, 4, 4, 4}))
    {
        info = makeCanonical(VTK_PYRAMID, {0, 1, 2, 3, 4});
        storeCached(info);
        return info;
    }
    if (matchesLabels({0, 1, 2, 2, 3, 4, 5, 5}))
    {
        info = makeCanonical(VTK_WEDGE, {0, 1, 2, 4, 5, 6});
        storeCached(info);
        return info;
    }

    // 与 IIIN 导入保持一致：只要 BRICK 中实际只有 4 个不同节点，
    // 对后处理几何就直接恢复成四面体。老结果文件中存在少量非典型
    // 8 槽位重复方式，不能因为重复位置不同就误标成“异常”。
    if (uniqueCount == 4)
    {
        std::array<int, 4> firstSlots = {-1, -1, -1, -1};
        for (int slot = 0; slot < 8; ++slot)
        {
            const int label = labels[slot];
            if (label >= 0 && label < 4 && firstSlots[label] < 0)
                firstSlots[label] = slot;
        }

        if (firstSlots[0] >= 0 && firstSlots[1] >= 0 &&
            firstSlots[2] >= 0 && firstSlots[3] >= 0)
        {
            info = makeCanonical(
                VTK_TETRA,
                {firstSlots[0], firstSlots[1], firstSlots[2], firstSlots[3]});
            storeCached(info);
            return info;
        }
    }

    int targetType = VTK_EMPTY_CELL;
    if (uniqueCount == 4)
        targetType = VTK_TETRA;
    else if (uniqueCount == 5)
        targetType = VTK_PYRAMID;
    else if (uniqueCount == 6)
        targetType = VTK_WEDGE;
    else if (uniqueCount == 8)
        targetType = VTK_HEXAHEDRON;

    if (targetType != VTK_EMPTY_CELL)
    {
        std::vector<int> stdToLabel(uniqueCount);
        for (int i = 0; i < uniqueCount; ++i)
            stdToLabel[i] = i;

        const QVector<quint32> collapsedFaces = collapsedLegacyHexFaceMasks(labels);
        bool matched = false;
        do
        {
            if (mappedStandardFaceMasks(targetType, stdToLabel) == collapsedFaces)
            {
                matched = true;
                break;
            }
        } while (std::next_permutation(stdToLabel.begin(), stdToLabel.end()));

        if (matched)
        {
            info.valid = true;
            info.vtkCellType = targetType;
            info.topologyType = targetType;
            info.localNodeSlots.resize(uniqueCount);
            for (int stdLocal = 0; stdLocal < uniqueCount; ++stdLocal)
            {
                const int label = stdToLabel[stdLocal];
                const int sourceSlot = firstSourceSlotForLabel(labels, label);
                if (sourceSlot < 0)
                {
                    info = LegacyHexTopologyInfo{};
                    info.uniqueCount = uniqueCount;
                    storeCached(info);
                    return info;
                }
                info.localNodeSlots[stdLocal] = sourceSlot;
            }

            storeCached(info);
            return info;
        }
    }

    QVector<QVector<int>> polyFaces;
    if (buildClosedLegacyPolyhedronFaces(labels, uniqueCount, polyFaces))
    {
        info.valid = true;
        info.degenerateHexFallback = true;
        // 这类非标准闭合退化体不再交给 ConvexPointSet/Polyhedron 猜拓扑。
        // 仅标记为“按原 8 槽位 Hex 连接做固定 Tet 分解”，真正写入结果网格时
        // 仍然只产生标准 VTK_TETRA。
        info.vtkCellType = VTK_HEXAHEDRON;
        info.topologyType = VTK_HEXAHEDRON;
        info.localNodeSlots.resize(uniqueCount);
        for (int label = 0; label < uniqueCount; ++label)
        {
            const int sourceSlot = firstSourceSlotForLabel(labels, label);
            if (sourceSlot < 0)
            {
                info = LegacyHexTopologyInfo{};
                info.uniqueCount = uniqueCount;
                storeCached(info);
                return info;
            }
            info.localNodeSlots[label] = sourceSlot;
        }

        storeCached(info);
        return info;
    }

    storeCached(info);
    return info;
}

int normalizedTopologyMarkerVersion(vtkUnstructuredGrid* grid)
{
    if (!grid || !grid->GetFieldData())
        return 0;
    vtkDataArray* marker = grid->GetFieldData()->GetArray(kNormalizedTopologyMarker);
    if (!marker || marker->GetNumberOfTuples() <= 0)
        return 0;
    return qMax(0, static_cast<int>(std::llround(marker->GetTuple1(0))));
}

vtkSmartPointer<vtkUnstructuredGrid> normalizeLegacyDegenerateBricks(
    vtkUnstructuredGrid* source)
{
    if (!source)
        return nullptr;

    if (source->GetNumberOfCells() <= 0)
    {
        auto emptyCopy = shallowGridCopy(source);
        addTopologyTypeArray(emptyCopy);
        return emptyCopy;
    }

    vtkDataArray* ketypeArray = source->GetCellData()
                                    ? source->GetCellData()->GetArray("KETYPE")
                                    : nullptr;

    const vtkIdType cellCount = source->GetNumberOfCells();
    QVector<LegacyHexTopologyInfo> topology;
    topology.resize(static_cast<int>(cellCount));

    vtkIdType tetraCount = 0;
    vtkIdType pyramidCount = 0;
    vtkIdType wedgeCount = 0;
    vtkIdType convexFallbackCount = 0;
    vtkIdType invalidCount = 0;
    QVector<int> sourceTopologyValues(static_cast<int>(cellCount), VTK_EMPTY_CELL);

    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        vtkCell* cell = source->GetCell(cid);
        if (cell)
            sourceTopologyValues[static_cast<int>(cid)] = cell->GetCellType();
        if (!cell || cell->GetCellType() != VTK_HEXAHEDRON ||
            cell->GetNumberOfPoints() != 8)
            continue;

        // 当前自动生成的 Tecplot 结果会携带 KETYPE。存在该字段时只处理
        // 求解器 KETYPE=6 的实体，避免碰到其他特殊单元族。
        if (ketypeArray && cid < ketypeArray->GetNumberOfTuples() &&
            static_cast<int>(std::llround(ketypeArray->GetTuple1(cid))) != 6)
            continue;

        QSet<vtkIdType> uniqueIds;
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
            uniqueIds.insert(cell->GetPointId(i));

        // 真正 8 个不同节点的普通 Hex 不需要规范化。
        if (uniqueIds.size() >= 8)
            continue;

        const LegacyHexTopologyInfo info = analyzeLegacyHexTopology(cell);
        topology[static_cast<int>(cid)] = info;
        if (!info.valid)
        {
            sourceTopologyValues[static_cast<int>(cid)] = kTopologyAbnormal;
            ++invalidCount;
            continue;
        }

        sourceTopologyValues[static_cast<int>(cid)] = info.topologyType;
        if (info.topologyType == VTK_TETRA)
            ++tetraCount;
        else if (info.topologyType == VTK_PYRAMID)
            ++pyramidCount;
        else if (info.topologyType == VTK_WEDGE)
            ++wedgeCount;
        else if (info.degenerateHexFallback)
            ++convexFallbackCount;
    }

    const vtkIdType convertedCount =
        tetraCount + pyramidCount + wedgeCount + convexFallbackCount;
    // invalidCount 也必须触发网格重建。旧逻辑在“全部异常单元都无法直接归类”时
    // 返回浅拷贝，导致重复节点 Hex 原样进入 VTK，最终显示成碎片/尖片。
    if (convertedCount + invalidCount <= 0)
    {
        auto copy = shallowGridCopy(source);
        addTopologyTypeArray(copy, &sourceTopologyValues);
        if (invalidCount > 0)
        {
            qWarning() << "[Post] invalid legacy degenerate BRICK cells="
                       << invalidCount;
        }
        return copy;
    }

    auto normalized = vtkSmartPointer<vtkUnstructuredGrid>::New();
    normalized->SetPoints(source->GetPoints());
    normalized->GetPointData()->ShallowCopy(source->GetPointData());
    normalized->GetFieldData()->ShallowCopy(source->GetFieldData());

    // 非标准闭合退化体只占极少数，但每个可能拆成多个 Tet。
    // 预留少量增长空间，避免每插入一个 Tet 都触发 CellData 扩容。
    const vtkIdType reserveCells =
        cellCount + std::max<vtkIdType>(convexFallbackCount * 6, 0);
    normalized->Allocate(reserveCells);
    normalized->GetCellData()->CopyAllocate(source->GetCellData(), reserveCells);
    QVector<int> normalizedTopologyValues;
    normalizedTopologyValues.reserve(static_cast<int>(reserveCells));

    vtkIdType fallbackTetCount = 0;
    vtkIdType fallbackTriangulationFailed = 0;
    vtkIdType recoveredAbnormalBodies = 0;
    vtkIdType recoveredAbnormalTets = 0;
    vtkIdType droppedAbnormalBodies = 0;

    auto appendCell = [&](int vtkType, const QVector<vtkIdType>& ids,
                          vtkIdType sourceCellId, int topologyValue) -> vtkIdType
    {
        if (ids.isEmpty())
            return -1;
        const vtkIdType newCellId = normalized->InsertNextCell(
            vtkType, static_cast<vtkIdType>(ids.size()), ids.constData());
        if (newCellId < 0)
            return -1;

        normalized->GetCellData()->CopyData(source->GetCellData(), sourceCellId, newCellId);
        while (normalizedTopologyValues.size() <= newCellId)
            normalizedTopologyValues.push_back(VTK_EMPTY_CELL);
        normalizedTopologyValues[static_cast<int>(newCellId)] = topologyValue;
        return newCellId;
    };

    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        vtkCell* cell = source->GetCell(cid);
        if (!cell)
            continue;

        const LegacyHexTopologyInfo& info = topology[static_cast<int>(cid)];
        const int topologyValue = sourceTopologyValues[static_cast<int>(cid)];

        const bool abnormalLegacyBrick = topologyValue == kTopologyAbnormal;
        if ((info.valid && info.degenerateHexFallback) || abnormalLegacyBrick)
        {
            QVector<QVector<vtkIdType>> tetrahedra;
            int rejectedDegenerateTets = 0;
            if (tetrahedralizeCollapsedHexSlots(
                    cell, source->GetPoints(), tetrahedra, &rejectedDegenerateTets))
            {
                vtkIdType insertedForBody = 0;
                const int emittedTopology =
                    abnormalLegacyBrick ? VTK_TETRA : info.topologyType;
                for (const QVector<vtkIdType>& tet : tetrahedra)
                {
                    if (appendCell(VTK_TETRA, tet, cid, emittedTopology) >= 0)
                    {
                        ++fallbackTetCount;
                        ++insertedForBody;
                        if (abnormalLegacyBrick)
                            ++recoveredAbnormalTets;
                    }
                }
                if (insertedForBody > 0)
                {
                    if (abnormalLegacyBrick)
                        ++recoveredAbnormalBodies;
                    continue;
                }
            }

            // 无法从原 Hex 槽位得到任何有效体积 Tet，说明连接本身已经几何塌缩。
            // 不再把重复节点 BRICK 原样写回 VTK；跳过该异常显示单元并记录数量。
            Q_UNUSED(rejectedDegenerateTets);
            ++fallbackTriangulationFailed;
            if (abnormalLegacyBrick)
                ++droppedAbnormalBodies;
            continue;
        }

        if (info.valid && info.vtkCellType != VTK_HEXAHEDRON &&
            !info.degenerateHexFallback)
        {
            QVector<vtkIdType> ids;
            ids.reserve(info.localNodeSlots.size());
            for (int sourceSlot : info.localNodeSlots)
                ids.push_back(cell->GetPointId(sourceSlot));
            if (appendCell(info.vtkCellType, ids, cid, topologyValue) >= 0)
                continue;
        }

        const vtkIdType numberOfPoints = cell->GetNumberOfPoints();
        QVector<vtkIdType> ids;
        ids.reserve(static_cast<int>(numberOfPoints));
        for (vtkIdType i = 0; i < numberOfPoints; ++i)
            ids.push_back(cell->GetPointId(i));
        appendCell(cell->GetCellType(), ids, cid, topologyValue);
    }

    normalized->Squeeze();
    addTopologyTypeArray(normalized, &normalizedTopologyValues);

    qInfo() << "[Post] legacy degenerate BRICK normalized:"
            << "tet=" << tetraCount
            << "pyramid=" << pyramidCount
            << "wedge=" << wedgeCount
            << "fallbackBodies=" << convexFallbackCount
            << "fallbackTets=" << fallbackTetCount
            << "fallbackTriangulationFailed=" << fallbackTriangulationFailed
            << "recoveredAbnormalBodies=" << recoveredAbnormalBodies
            << "recoveredAbnormalTets=" << recoveredAbnormalTets
            << "droppedAbnormalBodies=" << droppedAbnormalBodies
            << "invalid=" << invalidCount;
    return normalized;
}

void trimGridCacheLocked()
{
    constexpr int maxEntries = 8;
    auto& cache = gridCache();
    while (cache.size() > maxEntries)
    {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it)
        {
            if (oldest == cache.end() || it->accessSerial < oldest->accessSerial)
                oldest = it;
        }
        if (oldest == cache.end())
            break;
        cache.erase(oldest);
    }
}

void trimSurfaceCacheLocked()
{
    constexpr int maxEntries = 8;
    auto& cache = surfaceCache();
    while (cache.size() > maxEntries)
    {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it)
        {
            if (oldest == cache.end() || it->accessSerial < oldest->accessSerial)
                oldest = it;
        }
        if (oldest == cache.end())
            break;
        cache.erase(oldest);
    }
}

bool isAsciiPath(const QString& path)
{
    for (const QChar ch : path)
    {
        if (ch.unicode() > 0x7f)
            return false;
    }
    return true;
}

QString shortPathName(const QString& path)
{
#ifdef Q_OS_WIN
    const DWORD length = GetShortPathNameW(
        reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, 0);
    if (length == 0)
        return {};

    std::wstring buffer(static_cast<size_t>(length), L'\0');
    const DWORD written = GetShortPathNameW(
        reinterpret_cast<LPCWSTR>(path.utf16()), buffer.data(), length);
    if (written == 0 || written >= length)
        return {};
    buffer.resize(written);
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(buffer.size()));
#else
    Q_UNUSED(path);
    return {};
#endif
}

QString temporaryDirectory()
{
    const QStringList candidates = {
        QDir::tempPath(),
        QCoreApplication::applicationDirPath()
    };
    for (const QString& candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (isAsciiPath(candidate) && info.isDir() && info.isWritable())
            return QDir(candidate).absolutePath();
    }
    return QDir::tempPath();
}

bool copyToTemporaryFile(const QString& sourcePath, QTemporaryFile& target,
                         QString* errorMessage)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法读取结果文件：%1").arg(sourcePath);
        return false;
    }
    if (!target.open())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建后处理临时文件。");
        return false;
    }

    constexpr qint64 chunkSize = 1024 * 1024;
    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(chunkSize);
        if (chunk.isEmpty() && source.error() != QFile::NoError)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("读取结果文件失败：%1").arg(sourcePath);
            return false;
        }
        if (target.write(chunk) != chunk.size())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("写入后处理临时文件失败。");
            return false;
        }
    }
    target.flush();
    target.close();
    return true;
}
}

namespace PostTecplotUtils
{
vtkSmartPointer<vtkUnstructuredGrid> tetrahedralizeLegacyFallbackCells(
    vtkUnstructuredGrid* source)
{
    if (!source)
        return nullptr;

    const vtkIdType cellCount = source->GetNumberOfCells();
    vtkIdType fallbackCount = 0;
    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        const int type = source->GetCellType(cid);
        if (type == VTK_POLYHEDRON || type == VTK_CONVEX_POINT_SET)
            ++fallbackCount;
    }
    if (fallbackCount <= 0)
        return shallowGridCopy(source);

    auto converted = vtkSmartPointer<vtkUnstructuredGrid>::New();
    converted->SetPoints(source->GetPoints());
    converted->GetPointData()->ShallowCopy(source->GetPointData());
    converted->GetFieldData()->ShallowCopy(source->GetFieldData());

    const vtkIdType reserveCells =
        cellCount + std::max<vtkIdType>(fallbackCount * 6, 0);
    converted->Allocate(reserveCells);
    converted->GetCellData()->CopyAllocate(source->GetCellData(), reserveCells);

    vtkIdType convertedBodies = 0;
    vtkIdType generatedTets = 0;
    vtkIdType failedBodies = 0;

    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        vtkCell* cell = source->GetCell(cid);
        if (!cell)
            continue;

        const int type = cell->GetCellType();
        if (type == VTK_POLYHEDRON || type == VTK_CONVEX_POINT_SET)
        {
            QVector<vtkIdType> uniqueIds;
            uniqueIds.reserve(static_cast<int>(cell->GetNumberOfPoints()));
            QSet<vtkIdType> seen;
            for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
            {
                const vtkIdType pid = cell->GetPointId(i);
                if (pid >= 0 && !seen.contains(pid))
                {
                    seen.insert(pid);
                    uniqueIds.push_back(pid);
                }
            }

            QVector<QVector<vtkIdType>> tetrahedra;
            if (triangulatePointSetToTetrahedra(
                    uniqueIds, source->GetPoints(), tetrahedra))
            {
                for (const QVector<vtkIdType>& tet : tetrahedra)
                {
                    const vtkIdType newCellId = converted->InsertNextCell(
                        VTK_TETRA, 4, tet.constData());
                    if (newCellId >= 0)
                    {
                        converted->GetCellData()->CopyData(
                            source->GetCellData(), cid, newCellId);
                        ++generatedTets;
                    }
                }
                ++convertedBodies;
                continue;
            }

            ++failedBodies;
        }

        QVector<vtkIdType> ids;
        ids.reserve(static_cast<int>(cell->GetNumberOfPoints()));
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
            ids.push_back(cell->GetPointId(i));
        const vtkIdType newCellId = converted->InsertNextCell(
            type, static_cast<vtkIdType>(ids.size()), ids.constData());
        if (newCellId >= 0)
            converted->GetCellData()->CopyData(source->GetCellData(), cid, newCellId);
    }

    converted->Squeeze();
    qInfo() << "[Post] legacy fallback cells tetrahedralized:"
            << "bodies=" << convertedBodies
            << "tets=" << generatedTets
            << "failed=" << failedBodies;
    return converted;
}

vtkSmartPointer<vtkUnstructuredGrid> normalizeSolverTopology(
    vtkUnstructuredGrid* source)
{
    auto normalized = normalizeLegacyDegenerateBricks(source);
    return tetrahedralizeLegacyFallbackCells(normalized);
}

vtkSmartPointer<vtkUnstructuredGrid> readGrid(const QString& fileName,
                                              QString* errorMessage)
{
    const QString sourcePath = QDir::fromNativeSeparators(fileName.trimmed());
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("结果文件不存在：%1").arg(fileName);
        return nullptr;
    }

    const QString cacheKey = sourceInfo.canonicalFilePath().isEmpty()
                                 ? sourceInfo.absoluteFilePath()
                                 : sourceInfo.canonicalFilePath();
    const qint64 fileSize = sourceInfo.size();
    const qint64 modifiedMs = sourceInfo.lastModified().toMSecsSinceEpoch();
    {
        QMutexLocker locker(&gridCacheMutex());
        auto it = gridCache().find(cacheKey);
        if (it != gridCache().end() && it->size == fileSize &&
            it->modifiedMs == modifiedMs && it->grid)
        {
            it->accessSerial = ++gridCacheSerial();
            if (errorMessage)
                errorMessage->clear();
            return shallowGridCopy(it->grid);
        }

        // 后台预读和前台进入结果页可能同时请求同一文件；只允许一次实际读取，
        // 其余请求等待并直接复用内存网格。
        while (gridLoadsInFlight().contains(cacheKey))
        {
            gridLoadWaitCondition().wait(&gridCacheMutex());
            it = gridCache().find(cacheKey);
            if (it != gridCache().end() && it->size == fileSize &&
                it->modifiedMs == modifiedMs && it->grid)
            {
                it->accessSerial = ++gridCacheSerial();
                if (errorMessage)
                    errorMessage->clear();
                return shallowGridCopy(it->grid);
            }
        }
        gridLoadsInFlight().insert(cacheKey);
    }
    GridLoadGuard loadGuard(cacheKey);

    const bool vtuInput =
        sourceInfo.suffix().compare(QStringLiteral("vtu"), Qt::CaseInsensitive) == 0;

    QString vtkPath = sourcePath;
    std::unique_ptr<QTemporaryFile> temporaryFile;
    if (!isAsciiPath(vtkPath))
    {
        const QString sourceShortPath = shortPathName(sourcePath);
        if (!sourceShortPath.isEmpty() && isAsciiPath(sourceShortPath))
            vtkPath = sourceShortPath;
        else
        {
            const QString suffix = vtuInput ? QStringLiteral(".vtu")
                                            : QStringLiteral(".tec");
            temporaryFile = std::make_unique<QTemporaryFile>(
                QDir(temporaryDirectory()).filePath(
                    QStringLiteral("cae_post_XXXXXX") + suffix));
            temporaryFile->setAutoRemove(true);
            if (!copyToTemporaryFile(sourcePath, *temporaryFile, errorMessage))
                return nullptr;
            vtkPath = temporaryFile->fileName();
        }
        if (!isAsciiPath(vtkPath))
        {
            const QString shortPath = shortPathName(vtkPath);
            if (!shortPath.isEmpty())
                vtkPath = shortPath;
        }
    }

    if (!isAsciiPath(vtkPath))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法为结果文件创建兼容的读取路径。");
        return nullptr;
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    const QByteArray encodedPath = vtkPath.toLocal8Bit();

    if (vtuInput)
    {
        auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
        reader->SetFileName(encodedPath.constData());
        reader->Update();
        vtkUnstructuredGrid* output = reader->GetOutput();
        if (!output || output->GetNumberOfPoints() <= 0 ||
            output->GetNumberOfCells() <= 0)
        {
            if (errorMessage)
                *errorMessage =
                    QStringLiteral("VTU 结果文件中没有可读取的网格数据：%1").arg(fileName);
            return nullptr;
        }
        grid->ShallowCopy(output);
    }
    else
    {
        auto reader = vtkSmartPointer<vtkTecplotReader>::New();
        reader->SetFileName(encodedPath.constData());
        reader->Update();

        vtkMultiBlockDataSet* output = reader->GetOutput();
        if (!output || output->GetNumberOfBlocks() == 0)
        {
            if (errorMessage)
                *errorMessage =
                    QStringLiteral("结果文件中没有可读取的网格数据：%1").arg(fileName);
            return nullptr;
        }

        auto appender = vtkSmartPointer<vtkAppendFilter>::New();
        auto iterator =
            vtkSmartPointer<vtkDataObjectTreeIterator>::Take(output->NewTreeIterator());
        iterator->VisitOnlyLeavesOn();
        iterator->SkipEmptyNodesOn();
        int dataSetCount = 0;
        vtkDataSet* firstDataSet = nullptr;
        for (iterator->InitTraversal(); !iterator->IsDoneWithTraversal();
             iterator->GoToNextItem())
        {
            if (auto* dataSet =
                    vtkDataSet::SafeDownCast(iterator->GetCurrentDataObject()))
            {
                if (!firstDataSet)
                    firstDataSet = dataSet;
                appender->AddInputData(dataSet);
                ++dataSetCount;
            }
        }
        if (dataSetCount == 0)
        {
            if (errorMessage)
                *errorMessage =
                    QStringLiteral("结果文件中没有可显示的网格分区：%1").arg(fileName);
            return nullptr;
        }

        if (dataSetCount == 1)
        {
            if (auto* singleGrid =
                    vtkUnstructuredGrid::SafeDownCast(firstDataSet))
            {
                grid->ShallowCopy(singleGrid);
            }
            else
            {
                appender->Update();
                grid->ShallowCopy(appender->GetOutput());
            }
        }
        else
        {
            appender->Update();
            grid->ShallowCopy(appender->GetOutput());
        }
    }

    if (!grid || grid->GetNumberOfPoints() == 0 || grid->GetNumberOfCells() == 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("结果网格为空：%1").arg(fileName);
        return nullptr;
    }

    // marker >= 4 的新缓存已经保证所有重复节点 Hex 都被改写为标准单元
    // 或在零体积时丢弃。marker 2/3 仍可能保留“无法直接归类”的重复节点 Hex，
    // 因此升级后只需兼容扫描一次；新写出的 marker 4 继续走快路径。
    const int topologyMarkerVersion = normalizedTopologyMarkerVersion(grid);
    if (topologyMarkerVersion <= 0)
    {
        // Tecplot/更老 VTU：可能仍是 8 槽位退化 BRICK，需要完整规范化。
        grid = normalizeSolverTopology(grid);
    }
    else if (topologyMarkerVersion == 1)
    {
        // V20 已经把常规 Tet/Pyramid/Wedge 恢复完成，只可能残留早期
        // Polyhedron/ConvexPointSet 兜底。不要再逐单元做一遍退化 Hex 分析。
        grid = tetrahedralizeLegacyFallbackCells(grid);
    }
    else if (topologyMarkerVersion < 4)
    {
        grid = normalizeSolverTopology(grid);
    }

    {
        QMutexLocker locker(&gridCacheMutex());
        GridCacheEntry entry;
        entry.size = fileSize;
        entry.modifiedMs = modifiedMs;
        entry.accessSerial = ++gridCacheSerial();
        entry.grid = grid;
        gridCache().insert(cacheKey, entry);
        trimGridCacheLocked();
    }

    if (errorMessage)
        errorMessage->clear();
    return shallowGridCopy(grid);
}

QString surfaceCachePath(const QString& resultFileName)
{
    const QFileInfo info(QDir::fromNativeSeparators(resultFileName.trimmed()));
    if (info.suffix().compare(QStringLiteral("vtu"), Qt::CaseInsensitive) != 0)
        return {};
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".surface.vtp"));
}

QString domainSurfaceCachePath(const QString& resultFileName)
{
    const QFileInfo info(QDir::fromNativeSeparators(resultFileName.trimmed()));
    if (info.suffix().compare(QStringLiteral("vtu"), Qt::CaseInsensitive) != 0)
        return {};
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".domains.vtp"));
}

vtkSmartPointer<vtkPolyData> readSurfaceFile(const QString& surfacePath,
                                             QString* errorMessage)
{
    const QFileInfo sourceInfo(surfacePath);
    if (surfacePath.isEmpty() || !sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage)
            errorMessage->clear();
        return nullptr;
    }

    const QString cacheKey = sourceInfo.canonicalFilePath().isEmpty()
                                 ? sourceInfo.absoluteFilePath()
                                 : sourceInfo.canonicalFilePath();
    const qint64 fileSize = sourceInfo.size();
    const qint64 modifiedMs = sourceInfo.lastModified().toMSecsSinceEpoch();

    {
        QMutexLocker locker(&gridCacheMutex());
        auto it = surfaceCache().find(cacheKey);
        if (it != surfaceCache().end() && it->size == fileSize &&
            it->modifiedMs == modifiedMs && it->surface)
        {
            it->accessSerial = ++surfaceCacheSerial();
            auto copy = vtkSmartPointer<vtkPolyData>::New();
            copy->ShallowCopy(it->surface);
            if (errorMessage)
                errorMessage->clear();
            return copy;
        }
        while (surfaceLoadsInFlight().contains(cacheKey))
        {
            gridLoadWaitCondition().wait(&gridCacheMutex());
            it = surfaceCache().find(cacheKey);
            if (it != surfaceCache().end() && it->size == fileSize &&
                it->modifiedMs == modifiedMs && it->surface)
            {
                it->accessSerial = ++surfaceCacheSerial();
                auto copy = vtkSmartPointer<vtkPolyData>::New();
                copy->ShallowCopy(it->surface);
                if (errorMessage)
                    errorMessage->clear();
                return copy;
            }
        }
        surfaceLoadsInFlight().insert(cacheKey);
    }

    struct SurfaceLoadGuard
    {
        QString key;
        ~SurfaceLoadGuard()
        {
            QMutexLocker locker(&gridCacheMutex());
            surfaceLoadsInFlight().remove(key);
            gridLoadWaitCondition().wakeAll();
        }
    } guard{cacheKey};

    QString vtkPath = surfacePath;
    std::unique_ptr<QTemporaryFile> temporaryFile;
    if (!isAsciiPath(vtkPath))
    {
        const QString sourceShortPath = shortPathName(surfacePath);
        if (!sourceShortPath.isEmpty() && isAsciiPath(sourceShortPath))
            vtkPath = sourceShortPath;
        else
        {
            temporaryFile = std::make_unique<QTemporaryFile>(
                QDir(temporaryDirectory()).filePath(QStringLiteral("cae_post_surface_XXXXXX.vtp")));
            temporaryFile->setAutoRemove(true);
            if (!copyToTemporaryFile(surfacePath, *temporaryFile, errorMessage))
                return nullptr;
            vtkPath = temporaryFile->fileName();
        }
    }

    auto reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    const QByteArray encodedPath = vtkPath.toLocal8Bit();
    reader->SetFileName(encodedPath.constData());
    reader->Update();
    vtkPolyData* output = reader->GetOutput();
    if (!output || output->GetNumberOfPoints() <= 0 || output->GetNumberOfCells() <= 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("后处理表面缓存为空：%1").arg(surfacePath);
        return nullptr;
    }

    auto surface = vtkSmartPointer<vtkPolyData>::New();
    surface->ShallowCopy(output);
    {
        QMutexLocker locker(&gridCacheMutex());
        SurfaceCacheEntry entry;
        entry.size = fileSize;
        entry.modifiedMs = modifiedMs;
        entry.accessSerial = ++surfaceCacheSerial();
        entry.surface = surface;
        surfaceCache().insert(cacheKey, entry);
        trimSurfaceCacheLocked();
    }
    if (errorMessage)
        errorMessage->clear();
    auto copy = vtkSmartPointer<vtkPolyData>::New();
    copy->ShallowCopy(surface);
    return copy;
}

vtkSmartPointer<vtkPolyData> readSurface(const QString& resultFileName,
                                         QString* errorMessage)
{
    return readSurfaceFile(surfaceCachePath(resultFileName), errorMessage);
}

vtkSmartPointer<vtkPolyData> readDomainSurface(const QString& resultFileName,
                                               QString* errorMessage)
{
    return readSurfaceFile(domainSurfaceCachePath(resultFileName), errorMessage);
}

}
