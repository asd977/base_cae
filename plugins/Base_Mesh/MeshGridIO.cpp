#include "MeshGridIO.h"

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkFieldData.h>
#include <vtkHexahedron.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPyramid.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTetra.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWedge.h>

#include <QDebug>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QLocale>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <vector>
#include <PublicVariables.h>
#include <PublicConstants.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#else
static constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#endif

namespace
{

static const QRegularExpression kWhitespaceRe(QStringLiteral("\\s+"));

static const char* kIiinVerticalAxisArrayName = "IIIN_IVERTICAL";
static const char* kIiinObjectTypeArrayName = "IIIN_IOBJECT";
static const char* kIiinConnectivityPatternArrayName = "IIIN_CONNECTIVITY_PATTERN";
static const char* kIiinTopologySchemaVersionArrayName = "IIIN_TOPOLOGY_SCHEMA_VERSION";
constexpr int kIiinTopologySchemaVersion = 3;
constexpr int kFirstComputableMatype = 2;
constexpr int kDefaultCalculationMatype = 3;
constexpr int kDefaultConstructionLayer = 1;

// 自定义几何拓扑编号：只用于 CAE 的显示/筛选。
// 能还原为标准 Tet/Pyramid/Wedge/Hex 的退化单元全部归入对应标准拓扑；
// 不能等价为标准单元、但仍是合法闭合 Hex 塌缩体的，归入“六面体”家族，
// 底层仅借用 VTK_POLYHEDRON 表示真实边界；只有无法形成合法闭合体的才标记“异常”。
// KETYPE 仍保持求解器自己的单元族含义，不与这里混用。
constexpr int kTopologyAbnormal = -1;

struct LegacyHexTopologyInfo
{
    bool valid = false;
    bool degenerateHexFallback = false;
    int vtkCellType = VTK_EMPTY_CELL;
    int topologyType = VTK_EMPTY_CELL;     // UI/筛选使用；合法非标准塌缩体并入六面体家族
    QVector<int> localNodeSlots;           // VTK 局部节点 -> 原 IIIN 8 槽位
    QVector<QVector<int>> polyhedronFaces; // VTK_POLYHEDRON 仅作为特殊退化 Hex 的几何容器
    int patternCode = -1;                  // 原 IIIN 8 槽位 -> VTK 局部节点（每槽 3 bit）
    int uniqueCount = 0;
};

static int bitCount8(quint32 mask)
{
    int count = 0;
    while (mask)
    {
        count += int(mask & 1u);
        mask >>= 1u;
    }
    return count;
}

static QVector<quint32> sortedUniqueMasks(QVector<quint32> masks)
{
    std::sort(masks.begin(), masks.end());
    masks.erase(std::unique(masks.begin(), masks.end()), masks.end());
    return masks;
}

static QVector<quint32> collapsedLegacyHexFaceMasks(const std::array<int, 8>& labels)
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


static QVector<int> collapsedLegacyHexFace(const std::array<int, 8>& labels,
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

    // 面内部出现非相邻重复点会形成自相交/折返面，不能作为 Polyhedron 面。
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

static bool buildClosedLegacyPolyhedronFaces(const std::array<int, 8>& labels,
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

        // 同一个几何面可能由两个退化 Hex 面重合得到，只保留一次。
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

    const quint32 expectedVertexMask =
        (uniqueCount >= 32) ? 0xffffffffu : ((1u << uniqueCount) - 1u);
    if (usedVertexMask != expectedVertexMask)
        return false;

    for (auto it = edgeUseCount.constBegin(); it != edgeUseCount.constEnd(); ++it)
    {
        // VTK Polyhedron 要求闭合二维流形：每条边恰好属于两个面。
        if (it.value() != 2)
            return false;
    }

    // 对这里由一个 Hex 拓扑塌缩得到的单连通实体，Euler 特征应为 2。
    const int vertexCount = uniqueCount;
    const int edgeCount = edgeUseCount.size();
    const int faceCount = outFaces.size();
    if (vertexCount - edgeCount + faceCount != 2)
        return false;

    return true;
}

static int firstSourceSlotForLabel(const std::array<int, 8>& labels, int label)
{
    for (int slot = 0; slot < 8; ++slot)
    {
        if (labels[slot] == label)
            return slot;
    }
    return -1;
}

static int connectivityPatternCode(const std::array<int, 8>& labels,
                                   const QVector<int>& labelToLocal)
{
    if (labelToLocal.isEmpty())
        return -1;

    int code = 0;
    for (int slot = 0; slot < 8; ++slot)
    {
        const int label = labels[slot];
        if (label < 0 || label >= labelToLocal.size())
            return -1;
        const int local = labelToLocal[label];
        if (local < 0 || local > 7)
            return -1;
        code |= (local & 0x7) << (slot * 3);
    }
    return code;
}

static QVector<QVector<int>> standardFacesForVtkCell(int vtkCellType)
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

static QVector<quint32> mappedStandardFaceMasks(int vtkCellType,
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

// 老 IIIN 的 KETYPE=6 允许通过重复 8 节点槽位表达退化实体。
// 读入 CAE 后优先恢复成真正的 Tet/Pyramid/Wedge/Hex。
// 只要旧 8 槽位退化拓扑与某个标准线性实体等价，就直接归入对应标准拓扑。
// 对不能等价为标准单元、但仍能由原 Hex 六个面塌缩成合法闭合体的记录，
// 在用户语义上继续归入“六面体”，底层借用 VTK_POLYHEDRON 保存其真实面拓扑。
// 只有连闭合流形都无法形成的记录才标记为“异常”。
//
// patternCode 仅用于 IIIN 求解器专用回写。
static LegacyHexTopologyInfo analyzeLegacyHexTopology(const QVector<int>& nodeIds)
{
    LegacyHexTopologyInfo invalid;
    if (nodeIds.size() < 8)
        return invalid;

    std::array<int, 8> labels{};
    std::array<int, 8> uniqueNodeIds{};
    int uniqueCount = 0;
    quint32 cacheKey = 0;

    for (int slot = 0; slot < 8; ++slot)
    {
        const int nid = nodeIds[slot];
        if (nid <= 0)
            return invalid;

        int label = -1;
        for (int i = 0; i < uniqueCount; ++i)
        {
            if (uniqueNodeIds[i] == nid)
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
            uniqueNodeIds[uniqueCount++] = nid;
        }
        labels[slot] = label;
        cacheKey |= (quint32(label & 0x7) << (slot * 3));
    }

    static QHash<quint32, LegacyHexTopologyInfo> cache;
    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.constEnd())
        return cached.value();

    LegacyHexTopologyInfo info;
    info.uniqueCount = uniqueCount;

    // 常见旧 IIIN 退化模式走 O(1) 快速路径。
    // 例如 1099 号单元：A B C C D E F F，几何上就是两个三角形端面
    // 加三个四边形侧面的标准三棱柱，必须归入 VTK_WEDGE，而不是“异常”。
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
        QVector<int> labelToLocal(uniqueCount, -1);
        int local = 0;
        for (int slot : sourceSlots)
        {
            canonical.localNodeSlots.push_back(slot);
            const int label = labels[slot];
            if (label >= 0 && label < uniqueCount)
                labelToLocal[label] = local;
            ++local;
        }
        canonical.patternCode = connectivityPatternCode(labels, labelToLocal);
        if (canonical.patternCode < 0)
            canonical = LegacyHexTopologyInfo{};
        canonical.uniqueCount = uniqueCount;
        return canonical;
    };

    if (matchesLabels({0, 1, 2, 2, 3, 3, 3, 3}))
    {
        info = makeCanonical(VTK_TETRA, {0, 1, 2, 4});
        cache.insert(cacheKey, info);
        return info;
    }
    if (matchesLabels({0, 1, 2, 3, 4, 4, 4, 4}))
    {
        info = makeCanonical(VTK_PYRAMID, {0, 1, 2, 3, 4});
        cache.insert(cacheKey, info);
        return info;
    }
    if (matchesLabels({0, 1, 2, 2, 3, 4, 5, 5}))
    {
        info = makeCanonical(VTK_WEDGE, {0, 1, 2, 4, 5, 6});
        cache.insert(cacheKey, info);
        return info;
    }

    // 四个不同节点的三维线性实体，其几何拓扑只能是四面体。
    // 老 IIIN 中极少数 Tet 的 8 槽位重复位置并不符合常见 A B C C D D D D，
    // 例如 A B C D B B B B / A B C D A A A A。若继续按 Hex 的六个面
    // 去做“塌缩面匹配”，会因为历史槽位编码差异被误判为异常。
    // 对显示/筛选应按 4 个真实节点直接归为 Tet；原 8 槽位模式仍通过
    // IIIN_CONNECTIVITY_PATTERN 保留，Solver IIIN 回写不会改变。
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
            cache.insert(cacheKey, info);
            return info;
        }
    }

    // 其它排列/退化模式再走通用面拓扑匹配。
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

            QVector<int> labelToStd(uniqueCount, -1);
            for (int stdLocal = 0; stdLocal < uniqueCount; ++stdLocal)
            {
                const int label = stdToLabel[stdLocal];
                labelToStd[label] = stdLocal;

                const int sourceSlot = firstSourceSlotForLabel(labels, label);
                if (sourceSlot < 0)
                {
                    info = LegacyHexTopologyInfo{};
                    info.uniqueCount = uniqueCount;
                    cache.insert(cacheKey, info);
                    return info;
                }
                info.localNodeSlots[stdLocal] = sourceSlot;
            }

            info.patternCode = connectivityPatternCode(labels, labelToStd);
            if (info.patternCode < 0)
            {
                info = LegacyHexTopologyInfo{};
                info.uniqueCount = uniqueCount;
            }

            cache.insert(cacheKey, info);
            return info;
        }
    }

    // 标准单元匹配失败时，不再把重复节点直接塞回 vtkHexahedron。
    // 只要原 Hex 的六个面塌缩后仍能组成合法闭合体，就认为这是
    // “正常的退化六面体”：VTK_POLYHEDRON 仅作为几何容器，筛选仍归入“六面体”。
    QVector<QVector<int>> polyFaces;
    if (buildClosedLegacyPolyhedronFaces(labels, uniqueCount, polyFaces))
    {
        info.valid = true;
        info.degenerateHexFallback = true;
        info.vtkCellType = VTK_POLYHEDRON;
        info.topologyType = VTK_HEXAHEDRON;
        info.polyhedronFaces = polyFaces;
        info.localNodeSlots.resize(uniqueCount);

        QVector<int> labelToLocal(uniqueCount, -1);
        for (int label = 0; label < uniqueCount; ++label)
        {
            const int sourceSlot = firstSourceSlotForLabel(labels, label);
            if (sourceSlot < 0)
            {
                info = LegacyHexTopologyInfo{};
                info.uniqueCount = uniqueCount;
                cache.insert(cacheKey, info);
                return info;
            }
            // Polyhedron 的点顺序直接采用“首次出现”的 label 顺序。
            info.localNodeSlots[label] = sourceSlot;
            labelToLocal[label] = label;
        }

        info.patternCode = connectivityPatternCode(labels, labelToLocal);
        if (info.patternCode < 0)
        {
            info = LegacyHexTopologyInfo{};
            info.uniqueCount = uniqueCount;
        }

        cache.insert(cacheKey, info);
        return info;
    }

    cache.insert(cacheKey, info);
    return info;
}

static bool rebuildLegacyEightSlotConnectivity(int patternCode,
                                               const QVector<int>& vtkNodeIds,
                                               QVector<int>& outConn)
{
    if (patternCode < 0 || vtkNodeIds.isEmpty() || vtkNodeIds.size() > 8)
        return false;

    QVector<int> rebuilt;
    rebuilt.reserve(8);
    for (int slot = 0; slot < 8; ++slot)
    {
        const int local = (patternCode >> (slot * 3)) & 0x7;
        if (local < 0 || local >= vtkNodeIds.size())
            return false;
        rebuilt.push_back(vtkNodeIds[local]);
    }
    outConn = rebuilt;
    return true;
}


static bool buildOrientedPolyhedronFaceStream(vtkPoints* points,
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

            // Newell 法：对三角形/四边形都稳定，且不依赖前三点恰好不共线。
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

static bool isLinear3DSolverSolid(vtkCell* cell)
{
    if (!cell)
        return false;
    switch (cell->GetCellType())
    {
    case VTK_TETRA:
    case VTK_PYRAMID:
    case VTK_WEDGE:
    case VTK_HEXAHEDRON:
        return true;
    default:
        return false;
    }
}

// 当前求解器的混合三维网格协议仍是统一 8 节点槽位。
// 对 CAE 内部的标准线性单元，仅在 IIIN 求解器导出边界做一次适配。
static bool buildSolverEightSlotConnectivity(int vtkCellType,
                                             const QVector<int>& vtkNodeIds,
                                             QVector<int>& outConn)
{
    QVector<int> conn;
    conn.reserve(8);
    switch (vtkCellType)
    {
    case VTK_TETRA:
        if (vtkNodeIds.size() != 4)
            return false;
        conn = {vtkNodeIds[0], vtkNodeIds[1], vtkNodeIds[2], vtkNodeIds[2],
                vtkNodeIds[3], vtkNodeIds[3], vtkNodeIds[3], vtkNodeIds[3]};
        break;
    case VTK_PYRAMID:
        if (vtkNodeIds.size() != 5)
            return false;
        conn = {vtkNodeIds[0], vtkNodeIds[1], vtkNodeIds[2], vtkNodeIds[3],
                vtkNodeIds[4], vtkNodeIds[4], vtkNodeIds[4], vtkNodeIds[4]};
        break;
    case VTK_WEDGE:
        if (vtkNodeIds.size() != 6)
            return false;
        conn = {vtkNodeIds[0], vtkNodeIds[1], vtkNodeIds[2], vtkNodeIds[2],
                vtkNodeIds[3], vtkNodeIds[4], vtkNodeIds[5], vtkNodeIds[5]};
        break;
    case VTK_HEXAHEDRON:
        if (vtkNodeIds.size() != 8)
            return false;
        conn = vtkNodeIds;
        break;
    default:
        return false;
    }
    outConn = conn;
    return true;
}

static int normalizedVerticalAxis(int axis, int fallback = 2)
{
    return (axis >= 1 && axis <= 3) ? axis : fallback;
}

static inline bool parseInt(const QString& s, int& v)
{
    bool ok = false;
    v = s.toInt(&ok);
    return ok;
}

static inline bool parseDouble(const QString& s, double& v)
{
    bool ok = false;
    v = s.toDouble(&ok);
    return ok;
}

static inline MeshTagKey domainPhysicalKey(int physId, int dimension = 3)
{
    return MeshTagKey{QStringLiteral("Physical"), dimension, physId};
}

static inline MeshTagSet& ensureDomainPhysicalTagSet(MshPickMap& pickMap,
                                                     int physId,
                                                     int dimension = 3)
{
    const MeshTagKey key = domainPhysicalKey(physId, dimension);
    if (!pickMap.tagSets.contains(key))
    {
        MeshTagSet set;
        set.key = key;
        set.source = QStringLiteral("MeshGridIO");
        pickMap.tagSets.insert(key, set);
    }
    MeshTagSet& set = pickMap.tagSets[key];
    if (set.name.trimmed().isEmpty())
    {
        set.name = pickMap.physIdToName.value(physId, QStringLiteral("phys_%1").arg(physId));
    }
    return set;
}

static inline void appendDomainPhysicalCell(MshPickMap& pickMap,
                                            int physId,
                                            MeshIdType cellId,
                                            int dimension = 3)
{
    if (physId <= 0 || cellId < 0)
        return;

    MeshTagSet& set = ensureDomainPhysicalTagSet(pickMap, physId, dimension);
    set.cellIds.push_back(cellId);
    pickMap.cellIdToDomainTags[cellId].push_back(set.key);
}

// 你的“显示代理策略”搬到 IO 层：ketype -> (vtkCellType, localNodeLids) 若干片
static QVector<QPair<int, QVector<int>>> proxyPieces(int ketype)
{
    QVector<QPair<int, QVector<int>>> pieces;

    switch (ketype)
    {
    // 1D
    case 1:
    case 10:
    case 21:
        pieces.push_back({VTK_LINE, {0, 1}});
        break;

    case 2: // 3-node line: IIIN=[1,2,3] mid=2 -> VTK_QUADRATIC_EDGE=[1,3,2]
        pieces.push_back({VTK_QUADRATIC_EDGE, {0, 2, 1}});
        break;

               // 2D quad
    case 3:
        pieces.push_back({VTK_QUAD, {0, 1, 2, 3}});
        break;

    case 4: // 6-node quad -> VTK_QUADRATIC_LINEAR_QUAD
        pieces.push_back({VTK_QUADRATIC_LINEAR_QUAD, {0, 2, 3, 5, 1, 4}});
        break;

    case 5: // 8-node quad -> VTK_QUADRATIC_QUAD
        pieces.push_back({VTK_QUADRATIC_QUAD, {0, 2, 4, 6, 1, 3, 5, 7}});
        break;

    case 29: // 9-node quad -> VTK_BIQUADRATIC_QUAD
        pieces.push_back({VTK_BIQUADRATIC_QUAD, {0, 2, 4, 6, 1, 3, 5, 7, 8}});
        break;

    case 11:
        pieces.push_back({VTK_QUAD, {0, 1, 2, 3}});
        break;

    case 12: // 5-node quad (node5 center) -> split 4 tris
        pieces.push_back({VTK_TRIANGLE, {0, 1, 4}});
        pieces.push_back({VTK_TRIANGLE, {1, 2, 4}});
        pieces.push_back({VTK_TRIANGLE, {2, 3, 4}});
        pieces.push_back({VTK_TRIANGLE, {3, 0, 4}});
        break;

               // 2D tri
    case 31:
        pieces.push_back({VTK_TRIANGLE, {0, 1, 2}});
        break;

    case 32: // 6-node tri -> VTK_QUADRATIC_TRIANGLE
        pieces.push_back({VTK_QUADRATIC_TRIANGLE, {0, 2, 4, 1, 3, 5}});
        break;

    case 33: // proxy to corner tri
        pieces.push_back({VTK_TRIANGLE, {0, 3, 6}});
        break;

    case 34: // proxy to corner tri
        pieces.push_back({VTK_TRIANGLE, {0, 4, 8}});
        break;

               // 3D
    case 35:
        pieces.push_back({VTK_TETRA, {0, 1, 2, 3}});
        break;

    case 6:
    case 13:
        pieces.push_back({VTK_HEXAHEDRON, {0, 1, 2, 3, 4, 5, 6, 7}});
        break;

    case 36: // 14-node hex -> proxy to 8-corner hex
        pieces.push_back({VTK_HEXAHEDRON, {0, 1, 2, 3, 9, 10, 11, 12}});
        break;

    case 37: // 20-node hex -> VTK_QUADRATIC_HEXAHEDRON
        pieces.push_back({VTK_QUADRATIC_HEXAHEDRON,
                          {0, 2, 4, 6, 12, 14, 16, 18, 1, 3, 5, 7, 13, 15, 17, 19, 8, 9, 10, 11}});
        break;

    case 9:
    case 17:
        pieces.push_back({VTK_QUAD, {0, 1, 2, 3}});
        pieces.push_back({VTK_QUAD, {4, 5, 6, 7}});
        break;

    default:
        break;
    }

    return pieces;
}

// 导出时：vtk cell -> ketype
static bool mapVtkCellToKetypeExt(int vtkCellType, int nodeCount, int& ketype, int& expectedNodes)
{
    switch (vtkCellType)
    {
    case VTK_LINE:
        ketype = 1;
        expectedNodes = 2;
        return nodeCount == 2;
    case VTK_QUADRATIC_EDGE:
        ketype = 2;
        expectedNodes = 3;
        return nodeCount == 3;

    case VTK_TRIANGLE:
        ketype = 31;
        expectedNodes = 3;
        return nodeCount == 3;
    case VTK_QUADRATIC_TRIANGLE:
        ketype = 32;
        expectedNodes = 6;
        return nodeCount == 6;

    case VTK_QUAD:
        ketype = 3;
        expectedNodes = 4;
        return nodeCount == 4;
    case VTK_QUADRATIC_LINEAR_QUAD:
        ketype = 4;
        expectedNodes = 6;
        return nodeCount == 6;
    case VTK_QUADRATIC_QUAD:
        ketype = 5;
        expectedNodes = 8;
        return nodeCount == 8;
    case VTK_BIQUADRATIC_QUAD:
        ketype = 29;
        expectedNodes = 9;
        return nodeCount == 9;

    case VTK_TETRA:
        ketype = 35;
        expectedNodes = 4;
        return nodeCount == 4;
    case VTK_HEXAHEDRON:
        ketype = 6;
        expectedNodes = 8;
        return nodeCount == 8;
    case VTK_WEDGE:
        ketype = 38;
        expectedNodes = 6;
        return nodeCount == 6;
    case VTK_PYRAMID:
        ketype = 39;
        expectedNodes = 5;
        return nodeCount == 5;

    case VTK_QUADRATIC_HEXAHEDRON:
        ketype = 37;
        expectedNodes = 20;
        return nodeCount == 20;

    default:
        break;
    }
    return false;
}

// 导出时把 VTK 节点顺序改回 IIIN 顺序（只对你用到的高阶单元做重排）
static QVector<int> vtkToIiinOrder(int ketype)
{
    switch (ketype)
    {
    case 2:
        return {0, 2, 1}; // VTK [end0,end1,mid] -> IIIN [end0,mid,end1]
    case 4:
        return {0, 4, 1, 2, 5, 3}; // VTK QLIN_QUAD -> IIIN 6节点顺序
    case 5:
        return {0, 4, 1, 5, 2, 6, 3, 7}; // VTK QUADRATIC_QUAD -> IIIN 8节点顺序
    case 29:
        return {0, 4, 1, 5, 2, 6, 3, 7, 8}; // VTK BIQUADRATIC_QUAD -> IIIN 9节点顺序
    case 32:
        return {0, 3, 1, 4, 2, 5}; // VTK QUADRATIC_TRIANGLE -> IIIN 6节点顺序
    case 37:                       // 由你 forward map 反推的 inverse
        return {0, 8, 1, 9, 2, 10, 3, 11, 16, 17, 18, 19, 4, 12, 5, 13, 6, 14, 7, 15};
    default:
        return {}; // 线性单元不需要重排
    }
}

static double signedTetraVolume(vtkUnstructuredGrid* grid, vtkCell* cell)
{
    if (!grid || !cell || cell->GetNumberOfPoints() < 4)
        return 0.0;

    double p0[3] = {0.0, 0.0, 0.0};
    double p1[3] = {0.0, 0.0, 0.0};
    double p2[3] = {0.0, 0.0, 0.0};
    double p3[3] = {0.0, 0.0, 0.0};
    grid->GetPoint(cell->GetPointId(0), p0);
    grid->GetPoint(cell->GetPointId(1), p1);
    grid->GetPoint(cell->GetPointId(2), p2);
    grid->GetPoint(cell->GetPointId(3), p3);

    const double ux = p1[0] - p0[0];
    const double uy = p1[1] - p0[1];
    const double uz = p1[2] - p0[2];
    const double vx = p2[0] - p0[0];
    const double vy = p2[1] - p0[1];
    const double vz = p2[2] - p0[2];
    const double wx = p3[0] - p0[0];
    const double wy = p3[1] - p0[1];
    const double wz = p3[2] - p0[2];

    return (ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) +
            uz * (vx * wy - vy * wx)) /
           6.0;
}

static double hexCenterJacobianDet(vtkUnstructuredGrid* grid, vtkCell* cell,
                                   const int localOrder[8])
{
    if (!grid || !cell || !localOrder || cell->GetNumberOfPoints() < 8)
        return 0.0;

    static constexpr double kNaturalSigns[8][3] = {
        {-1.0, -1.0, -1.0}, {1.0, -1.0, -1.0}, {1.0, 1.0, -1.0},
        {-1.0, 1.0, -1.0},  {-1.0, -1.0, 1.0}, {1.0, -1.0, 1.0},
        {1.0, 1.0, 1.0},    {-1.0, 1.0, 1.0},
    };

    double jac[3][3] = {};
    for (int i = 0; i < 8; ++i)
    {
        const int lid = localOrder[i];
        if (lid < 0 || lid >= cell->GetNumberOfPoints())
            return 0.0;

        double p[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(cell->GetPointId(lid), p);
        for (int axis = 0; axis < 3; ++axis)
        {
            const double dN = kNaturalSigns[i][axis] / 8.0;
            jac[0][axis] += p[0] * dN;
            jac[1][axis] += p[1] * dN;
            jac[2][axis] += p[2] * dN;
        }
    }

    return jac[0][0] * (jac[1][1] * jac[2][2] - jac[1][2] * jac[2][1]) -
           jac[0][1] * (jac[1][0] * jac[2][2] - jac[1][2] * jac[2][0]) +
           jac[0][2] * (jac[1][0] * jac[2][1] - jac[1][1] * jac[2][0]);
}

static void orientHex8Positive(vtkUnstructuredGrid* grid, vtkCell* cell, QVector<int>& outConn)
{
    if (!grid || !cell || cell->GetCellType() != VTK_HEXAHEDRON || outConn.size() < 8)
        return;

    static constexpr int kIdentity[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const double currentDet = hexCenterJacobianDet(grid, cell, kIdentity);
    if (!(currentDet < 0.0))
        return;

    static constexpr int kFlipCandidates[3][8] = {
        {1, 0, 3, 2, 5, 4, 7, 6},
        {3, 2, 1, 0, 7, 6, 5, 4},
        {4, 5, 6, 7, 0, 1, 2, 3},
    };

    int bestCandidate = -1;
    double bestDet = currentDet;
    for (int i = 0; i < 3; ++i)
    {
        const double det = hexCenterJacobianDet(grid, cell, kFlipCandidates[i]);
        if (det > bestDet)
        {
            bestDet = det;
            bestCandidate = i;
        }
    }

    if (bestCandidate < 0 || !(bestDet > 0.0))
        return;

    const QVector<int> oldConn = outConn;
    for (int i = 0; i < 8; ++i)
        outConn[i] = oldConn[kFlipCandidates[bestCandidate][i]];
}

static vtkSmartPointer<vtkIntArray> getOrCreateCellIntArray(vtkUnstructuredGrid* g,
                                                            const char* name)
{
    auto* cd = g->GetCellData();
    auto* a = vtkIntArray::SafeDownCast(cd->GetArray(name));
    if (a)
        return a;
    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName(name);
    arr->SetNumberOfComponents(1);
    cd->AddArray(arr);
    return arr;
}

static vtkSmartPointer<vtkIntArray> getOrCreatePointIntArray(vtkUnstructuredGrid* g,
                                                             const char* name)
{
    auto* pd = g->GetPointData();
    auto* a = vtkIntArray::SafeDownCast(pd->GetArray(name));
    if (a)
        return a;
    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName(name);
    arr->SetNumberOfComponents(1);
    pd->AddArray(arr);
    return arr;
}


static QString meshDisplayNameByDim(int dim, int id)
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

static QString sanitizePhysicalDisplayName(int dim, int id, const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return meshDisplayNameByDim(dim, id);

    static const QRegularExpression generatedNameRe(
        QStringLiteral("^(dim\\d+(_|-)?.*|phys_\\d+|point_\\d+|edge_\\d+|face_\\d+|volume_\\d+|MeshPoint_\\d+|MeshEdge_\\d+|MeshFace_\\d+|MeshVolume_\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (generatedNameRe.match(name).hasMatch())
        return meshDisplayNameByDim(dim, id);

    return name;
}

static void dedupMeshIdVector(QVector<MeshIdType>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

static void dedupIntVector(QVector<int>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

static void dedupInt64Vector(QVector<qint64>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

static QVector<MeshIdType> toMeshIds(const QVector<int>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (int v : values)
        out.push_back(static_cast<MeshIdType>(v));
    dedupMeshIdVector(out);
    return out;
}

static QVector<MeshIdType> toMeshIds(const QVector<qint64>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (qint64 v : values)
        out.push_back(static_cast<MeshIdType>(v));
    dedupMeshIdVector(out);
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

    MeshTagKey key;
    key.category = QStringLiteral("Physical");
    key.dim = dim;
    key.id = physId;

    if (!pick.tagSets.contains(key))
    {
        MeshTagSet set;
        set.key = key;
        set.source = source;
        set.name = sanitizePhysicalDisplayName(dim, physId, pick.physIdToName.value(physId));
        pick.tagSets.insert(key, set);
    }

    MeshTagSet& set = pick.tagSets[key];
    if (set.name.trimmed().isEmpty())
        set.name = sanitizePhysicalDisplayName(dim, physId, pick.physIdToName.value(physId));
    if (set.source.trimmed().isEmpty())
        set.source = source;

    pick.physIdToName.insert(physId, set.name);
    pick.physIdToName.insert(physId, set.name);
    pick.physIdToDim.insert(physId, dim);

    return set;
}

static void appendPhysicalBoundaryPoint(MshPickMap& pick,
                                        int physId,
                                        int dim,
                                        int pointId,
                                        const QString& source)
{
    if (pointId < 0 || dim < 0 || dim > 2)
        return;
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, dim, source);
    if (!set.key.isValid())
        return;
    set.pointIds.push_back(static_cast<MeshIdType>(pointId));
}

static void setPhysicalBoundaryPoints(MshPickMap& pick,
                                      int physId,
                                      int dim,
                                      const QVector<int>& pointIds,
                                      const QString& source)
{
    if (dim < 0 || dim > 2)
        return;
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

static void appendPhysicalBoundaryEdgeKey(MshPickMap& pick,
                                          int physId,
                                          qint64 edgeKey,
                                          const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, 1, source);
    if (!set.key.isValid())
        return;
    set.edgeKeys.push_back(static_cast<MeshIdType>(edgeKey));
}

static void appendPhysicalBoundaryFaceKey(MshPickMap& pick,
                                          int physId,
                                          qint64 faceKey,
                                          const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, 2, source);
    if (!set.key.isValid())
        return;
    set.faceKeys.push_back(static_cast<MeshIdType>(faceKey));
}

static void normalizePhysicalTagSets(MshPickMap& pick, const QString& source)
{
    Q_UNUSED(source);

    pick.pointIdToBoundaryTags.clear();

    for (auto it = pick.tagSets.begin(); it != pick.tagSets.end(); ++it)
    {
        MeshTagSet& set = it.value();
        if (set.key.category == QStringLiteral("Physical"))
        {
            set.name = sanitizePhysicalDisplayName(set.key.dim, set.key.id, set.name);
            if (set.source.trimmed().isEmpty())
                set.source = source;
            pick.physIdToName.insert(set.key.id, set.name);
            pick.physIdToDim.insert(set.key.id, set.key.dim);
        }

        dedupMeshIdVector(set.pointIds);
        dedupMeshIdVector(set.cellIds);
        dedupMeshIdVector(set.edgeKeys);
        dedupMeshIdVector(set.faceKeys);

        if (set.key.category == QStringLiteral("Physical") && set.key.dim >= 0 && set.key.dim < pick.meshDim)
        {
            for (MeshIdType pid : set.pointIds)
                pick.pointIdToBoundaryTags[pid].push_back(set.key);
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

static int tetHexKetypeFromCell(vtkCell* cell, int fallback)
{
    if (!cell)
        return fallback;
    switch (cell->GetCellType())
    {
    case VTK_TETRA:
        return 35;
    case VTK_HEXAHEDRON:
        return 6;
    default:
        return fallback;
    }
}

static bool hasUsableDomainCategory(const MshPickMap& pick, const QString& category)
{
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.category.compare(category, Qt::CaseInsensitive) != 0)
            continue;
        if (set.key.dim != pick.meshDim || set.key.id == 0 || set.cellIds.isEmpty())
            continue;
        return true;
    }
    return false;
}

static void normalizeDomainGroupCategories(MshPickMap& pick)
{
    // 旧工程没有显式保存用途时，按真实来源标签恢复，而不是按 MATYPE 优先级猜测。
    if (pick.materialGroupCategory.trimmed().isEmpty())
    {
        if (hasUsableDomainCategory(pick, QStringLiteral("REGION")))
            pick.materialGroupCategory = QStringLiteral("REGION");
        else if (hasUsableDomainCategory(pick, QStringLiteral("Physical")))
            pick.materialGroupCategory = QStringLiteral("Physical");
        else if (hasUsableDomainCategory(pick, QStringLiteral("MATYPE")))
            pick.materialGroupCategory = QStringLiteral("MATYPE");
    }

    if (pick.excavationGroupCategory.trimmed().isEmpty())
    {
        if (hasUsableDomainCategory(pick, QStringLiteral("REGION")))
            pick.excavationGroupCategory = QStringLiteral("REGION");
        else if (hasUsableDomainCategory(pick, QStringLiteral("Physical")))
            pick.excavationGroupCategory = QStringLiteral("Physical");
        else if (hasUsableDomainCategory(pick, QStringLiteral("LAYER")))
            pick.excavationGroupCategory = QStringLiteral("LAYER");
    }
}

static bool looksLikeAnsysInpFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif

    for (int i = 0; i < 200 && !in.atEnd(); ++i)
    {
        const QString upper = in.readLine().trimmed().toUpper();
        if (upper.startsWith(QStringLiteral("NBLOCK")) ||
            upper.startsWith(QStringLiteral("EBLOCK")) ||
            upper.startsWith(QStringLiteral("/PREP7")) ||
            upper.contains(QStringLiteral("ANSYS INPUT DECK")))
        {
            return true;
        }
    }
    return false;
}

} // namespace

MeshGridIO& MeshGridIO::instance()
{
    static MeshGridIO io;
    return io;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::importFlac3D(const QString& fileName,
                                                              QString* errMsg)
{
    clearPickMap();
    m_mshPickMap.materialGroupCategory = QStringLiteral("Physical");
    m_mshPickMap.excavationGroupCategory = QStringLiteral("Physical");

    auto grid = loadFlac3dGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0)
    {
        if (errMsg)
            *errMsg = QObject::tr("无法解析 Flac3D 网格: %1").arg(fileName);
        return nullptr;
    }

    ensureDefaultArrays(grid);

    int domainDimension = 0;
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        if (vtkCell* cell = grid->GetCell(cid))
            domainDimension = qMax(domainDimension, cell->GetCellDimension());
    }
    if (domainDimension <= 0)
        domainDimension = 3;
    m_mshPickMap.meshDim = domainDimension;
    PublicVariables::instance().set(PublicConstants::kMeshDimension, domainDimension, "mesh");

    auto* cd = grid->GetCellData();
    auto* fd = grid->GetFieldData();

           // GroupName 是 vtkStringArray（vtkAbstractArray），不能用 GetArray 取
    auto* groupArr = cd ? vtkStringArray::SafeDownCast(cd->GetAbstractArray("GroupName")) : nullptr;
    auto* matypeArr = cd ? vtkIntArray::SafeDownCast(cd->GetArray("MATYPE")) : nullptr;
    auto* layerArr = cd ? vtkIntArray::SafeDownCast(cd->GetArray("LAYER")) : nullptr;
    auto* ketypeArr = cd ? vtkIntArray::SafeDownCast(cd->GetArray("KETYPE")) : nullptr;
    auto* nodeIdArr = grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    QHash<QString, int> groupToPhysId;
    int nextPhysId = kDefaultCalculationMatype;

    if (!groupArr)
    {
        qWarning() << "[Flac3D] GroupName array missing or type mismatch; MATYPE "
                      "fallback may collapse to default";
    }

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const QString groupName = (groupArr && cid < groupArr->GetNumberOfValues())
        ? QString::fromStdString(groupArr->GetValue(cid)).trimmed()
        : QString();

        if (groupName.isEmpty())
            continue;

               // 使用完整分组串（例如 slotA;slotB），避免只取第一个组导致材料分区塌缩
        const QString groupKey = groupName;
        if (groupKey.isEmpty())
            continue;

        int physId = groupToPhysId.value(groupKey, 0);
        if (physId == 0)
        {
            physId = nextPhysId++;
            groupToPhysId.insert(groupKey, physId);
            m_mshPickMap.physIdToName.insert(physId, groupKey);
            m_mshPickMap.physIdToDim.insert(physId, 3);
        }

        appendDomainPhysicalCell(m_mshPickMap, physId, static_cast<MeshIdType>(cid));
        if (matypeArr)
        {
            matypeArr->SetValue(cid, physId);
        }
        if (layerArr)
        {
            // 物理分组只用于材料/区域识别；施工层由洞室工况单独设置。
            layerArr->SetValue(cid, kDefaultConstructionLayer);
        }
        if (ketypeArr)
        {
            vtkCell* cell = grid->GetCell(cid);
            ketypeArr->SetValue(cid, tetHexKetypeFromCell(cell, ketypeArr->GetValue(cid)));
        }
    }

    if (nodeIdArr)
    {
        const vtkIdType nPts = nodeIdArr->GetNumberOfTuples();
        for (vtkIdType pid = 0; pid < nPts; ++pid)
        {
            const int vtkPid = static_cast<int>(pid);
            const int nodeId = nodeIdArr->GetValue(pid);
            m_mshPickMap.pointIdToNodeId.insert(vtkPid, nodeId);
            m_mshPickMap.nodeIdToPointId.insert(nodeId, vtkPid);
        }
    }

           // 同步一份 RegionId/RegionName，方便后续统一按区域批量赋材
    auto regionIdArr = vtkSmartPointer<vtkIntArray>::New();
    regionIdArr->SetName("RegionId");
    regionIdArr->SetNumberOfTuples(nCells);

    auto regionNameArr = vtkSmartPointer<vtkStringArray>::New();
    regionNameArr->SetName("RegionName");
    regionNameArr->SetNumberOfValues(nCells);

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        int regionId = 0;
        QString regionName;

        if (matypeArr)
        {
            regionId = matypeArr->GetValue(cid);
            regionName = m_mshPickMap.physIdToName.value(regionId);
        }
        regionIdArr->SetValue(cid, regionId);
        regionNameArr->SetValue(cid, regionName.toStdString());
    }

    if (cd)
    {
        cd->AddArray(regionIdArr);
        cd->AddArray(regionNameArr);
    }

    if (fd)
    {
        auto* zNames = vtkStringArray::SafeDownCast(fd->GetAbstractArray("ZGroupNames"));
        if (zNames)
        {
            auto bndNames = vtkSmartPointer<vtkStringArray>::New();
            auto bndDims = vtkSmartPointer<vtkIntArray>::New();
            bndNames->SetName("BndGroupNames");
            bndDims->SetName("BndGroupDims");
            for (vtkIdType i = 0; i < zNames->GetNumberOfValues(); ++i)
            {
                bndNames->InsertNextValue(zNames->GetValue(i));
                bndDims->InsertNextValue(3);
            }
            fd->AddArray(bndNames);
            fd->AddArray(bndDims);
        }
    }

    qInfo() << "[Flac3D] unique group->MATYPE count=" << groupToPhysId.size();

    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("Flac3D"));
    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::importInp(const QString& fileName, QString* errMsg)
{
    clearPickMap();
    m_mshPickMap.materialGroupCategory = QStringLiteral("Physical");
    m_mshPickMap.excavationGroupCategory = QStringLiteral("Physical");

    QString parseErr;
    auto grid = looksLikeAnsysInpFile(fileName)
                    ? loadAnsysInpGrid(fileName, &parseErr)
                    : loadAbaqusInpGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0)
    {
        if (errMsg)
        {
            *errMsg = parseErr.isEmpty()
                          ? QObject::tr("无法解析 Abaqus/ANSYS 网格: %1").arg(fileName)
                          : parseErr;
        }
        return nullptr;
    }
    ensureDefaultArrays(grid);

    int domainDimension = 0;
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        if (vtkCell* cell = grid->GetCell(cid))
            domainDimension = qMax(domainDimension, cell->GetCellDimension());
    }
    if (domainDimension <= 0)
        domainDimension = 3;
    m_mshPickMap.meshDim = domainDimension;
    PublicVariables::instance().set(PublicConstants::kMeshDimension, domainDimension, "mesh");

    auto* cd = grid->GetCellData();
    auto* matype = cd ? vtkIntArray::SafeDownCast(cd->GetArray("MATYPE")) : nullptr;
    auto* layer = cd ? vtkIntArray::SafeDownCast(cd->GetArray("LAYER")) : nullptr;
    if (matype && layer)
    {
        for (auto it = m_mshPickMap.tagSets.constBegin(); it != m_mshPickMap.tagSets.constEnd();
             ++it)
        {
            const MeshTagKey& key = it.key();
            const MeshTagSet& set = it.value();
            if (key.category != QStringLiteral("Physical") || key.dim != domainDimension ||
                key.id <= 0)
                continue;
            const int physId = key.id;
            for (MeshIdType rawCid : set.cellIds)
            {
                const vtkIdType cid = static_cast<vtkIdType>(rawCid);
                if (cid < 0 || cid >= grid->GetNumberOfCells())
                    continue;
                matype->SetValue(cid, physId);
                // 不再把 Physical ID 当作施工层。
                layer->SetValue(cid, kDefaultConstructionLayer);
            }
        }
        grid->GetCellData()->AddArray(matype);
        grid->GetCellData()->AddArray(layer);
    }
    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("AbaqusInp"));
    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::importNastran(const QString& fileName,
                                                               QString* errMsg)
{
    clearPickMap();
    m_mshPickMap.materialGroupCategory = QStringLiteral("Physical");
    m_mshPickMap.excavationGroupCategory = QStringLiteral("Physical");

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errMsg)
            *errMsg = QObject::tr("无法打开 Nastran 网格文件: %1").arg(fileName);
        return nullptr;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif

    QStringList lines;
    while (!in.atEnd())
        lines.push_back(in.readLine());

    struct NasNode
    {
        int id = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };
    struct NasElement
    {
        int id = 0;
        int propertyId = 0;
        int vtkType = VTK_EMPTY_CELL;
        int ketype = 0;
        int dimension = 0;
        QVector<int> nodeIds;
    };

    QVector<NasNode> nodes;
    QVector<NasElement> elements;
    QSet<int> nodeIdsSeen;
    QSet<int> elementIdsSeen;
    QHash<int, QString> propertyNames;
    int unsupportedElementCount = 0;
    int invalidElementCount = 0;

    auto setError = [errMsg](const QString& message)
    {
        if (errMsg)
            *errMsg = message;
    };

    auto parseNasInt = [](const QString& text, int& value) -> bool
    {
        bool ok = false;
        const qlonglong parsed = text.trimmed().toLongLong(&ok);
        if (!ok || parsed < (std::numeric_limits<int>::min)()
            || parsed > (std::numeric_limits<int>::max)())
            return false;
        value = static_cast<int>(parsed);
        return true;
    };

    auto parseNasDouble = [](QString text, double& value) -> bool
    {
        text = text.trimmed();
        if (text.isEmpty())
            return false;
        text.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);

        if (!text.contains(QLatin1Char('E'), Qt::CaseInsensitive))
        {
            for (int i = text.size() - 1; i > 0; --i)
            {
                if ((text.at(i) == QLatin1Char('+') || text.at(i) == QLatin1Char('-'))
                    && i + 1 < text.size() && text.at(i + 1).isDigit())
                {
                    text.insert(i, QLatin1Char('E'));
                    break;
                }
            }
        }

        bool ok = false;
        value = QLocale::c().toDouble(text, &ok);
        return ok && std::isfinite(value);
    };

    auto fixedDataFields = [](const QString& line, int fieldWidth) -> QStringList
    {
        QStringList fields;
        const int fieldCount = fieldWidth == 16 ? 4 : 8;
        fields.reserve(fieldCount);
        for (int field = 0; field < fieldCount; ++field)
        {
            const int pos = 8 + field * fieldWidth;
            fields.push_back(pos < line.size() ? line.mid(pos, fieldWidth).trimmed() : QString());
        }
        return fields;
    };

    auto freeDataFields = [](const QString& line) -> QStringList
    {
        QStringList fields = line.split(QLatin1Char(','), Qt::KeepEmptyParts);
        if (!fields.isEmpty())
            fields.removeFirst();
        for (QString& field : fields)
            field = field.trimmed();
        return fields;
    };

    auto isContinuationLine = [](const QString& line) -> bool
    {
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('$')))
            return false;
        if (line.contains(QLatin1Char(',')))
        {
            const QString first = line.section(QLatin1Char(','), 0, 0).trimmed();
            return first.isEmpty() || first.startsWith(QLatin1Char('+'))
                   || first.startsWith(QLatin1Char('*'));
        }
        const QString first = line.left(8).trimmed();
        return first.isEmpty() || first.startsWith(QLatin1Char('+'))
               || first.startsWith(QLatin1Char('*'));
    };

    auto parsePropertyName = [&propertyNames](const QString& line)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral("$ANSA_NAME_COMMENT;"),
                                Qt::CaseInsensitive))
            return;
        const QStringList fields = trimmed.mid(1).split(QLatin1Char(';'), Qt::KeepEmptyParts);
        if (fields.size() < 4 || fields.at(2).compare(QStringLiteral("PSOLID"),
                                                     Qt::CaseInsensitive) != 0)
            return;
        bool ok = false;
        const int propertyId = fields.at(1).trimmed().toInt(&ok);
        const QString name = fields.at(3).trimmed();
        if (ok && propertyId > 0 && !name.isEmpty())
            propertyNames.insert(propertyId, name);
    };

    auto parsedNodeIds = [&parseNasInt](const QStringList& fields, int first) -> QVector<int>
    {
        QVector<int> result;
        for (int i = first; i < fields.size(); ++i)
        {
            int nodeId = 0;
            if (parseNasInt(fields.at(i), nodeId) && nodeId > 0)
                result.push_back(nodeId);
        }
        return result;
    };

    auto appendElement = [&](const QString& rawCard, const QStringList& fields,
                             int lineNumber) -> bool
    {
        const QString card = rawCard.toUpper();
        int elementId = 0;
        int propertyId = 0;
        int nodeFieldStart = 2;
        if (fields.isEmpty() || !parseNasInt(fields.value(0), elementId) || elementId <= 0)
        {
            ++invalidElementCount;
            return true;
        }

        if (card == QStringLiteral("CONROD"))
        {
            nodeFieldStart = 1;
            parseNasInt(fields.value(3), propertyId);
        }
        else if (card == QStringLiteral("PLOTEL"))
        {
            nodeFieldStart = 1;
        }
        else if (!parseNasInt(fields.value(1), propertyId))
        {
            ++invalidElementCount;
            return true;
        }

        QVector<int> connectivity = parsedNodeIds(fields, nodeFieldStart);
        NasElement element;
        element.id = elementId;
        element.propertyId = propertyId;

        int expectedNodes = 0;
        if (card == QStringLiteral("CHEXA"))
        {
            expectedNodes = connectivity.size() >= 20 ? 20 : 8;
            element.vtkType = expectedNodes == 20 ? VTK_QUADRATIC_HEXAHEDRON : VTK_HEXAHEDRON;
            element.ketype = expectedNodes == 20 ? 37 : 6;
            element.dimension = 3;
        }
        else if (card == QStringLiteral("CTETRA"))
        {
            if (connectivity.size() >= 10)
            {
                ++unsupportedElementCount;
                return true;
            }
            expectedNodes = 4;
            element.vtkType = VTK_TETRA;
            element.ketype = 35;
            element.dimension = 3;
        }
        else if (card == QStringLiteral("CPENTA"))
        {
            if (connectivity.size() >= 15)
            {
                ++unsupportedElementCount;
                return true;
            }
            expectedNodes = 6;
            element.vtkType = VTK_WEDGE;
            element.ketype = 38;
            element.dimension = 3;
        }
        else if (card == QStringLiteral("CPYRAM"))
        {
            if (connectivity.size() >= 13)
            {
                ++unsupportedElementCount;
                return true;
            }
            expectedNodes = 5;
            element.vtkType = VTK_PYRAMID;
            element.ketype = 39;
            element.dimension = 3;
        }
        else if (card == QStringLiteral("CQUAD4"))
        {
            expectedNodes = 4;
            element.vtkType = VTK_QUAD;
            element.ketype = 3;
            element.dimension = 2;
        }
        else if (card == QStringLiteral("CQUAD8"))
        {
            expectedNodes = 8;
            element.vtkType = VTK_QUADRATIC_QUAD;
            element.ketype = 5;
            element.dimension = 2;
        }
        else if (card == QStringLiteral("CTRIA3"))
        {
            expectedNodes = 3;
            element.vtkType = VTK_TRIANGLE;
            element.ketype = 31;
            element.dimension = 2;
        }
        else if (card == QStringLiteral("CTRIA6"))
        {
            expectedNodes = 6;
            element.vtkType = VTK_QUADRATIC_TRIANGLE;
            element.ketype = 32;
            element.dimension = 2;
        }
        else if (card == QStringLiteral("CROD") || card == QStringLiteral("CBAR")
                 || card == QStringLiteral("CBEAM") || card == QStringLiteral("CONROD")
                 || card == QStringLiteral("PLOTEL"))
        {
            expectedNodes = 2;
            element.vtkType = VTK_LINE;
            element.ketype = 1;
            element.dimension = 1;
        }
        else
        {
            if (card.startsWith(QLatin1Char('C')))
                ++unsupportedElementCount;
            return true;
        }

        if (connectivity.size() < expectedNodes)
        {
            ++invalidElementCount;
            qWarning() << "[Nastran] incomplete element at line" << lineNumber << card
                       << elementId << connectivity.size() << "/" << expectedNodes;
            return true;
        }
        connectivity.resize(expectedNodes);

        if (elementIdsSeen.contains(elementId))
        {
            setError(QObject::tr("Nastran 单元号重复：%1（第 %2 行）")
                         .arg(elementId)
                         .arg(lineNumber));
            return false;
        }
        elementIdsSeen.insert(elementId);
        element.nodeIds = connectivity;
        elements.push_back(element);
        return true;
    };

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const QString line = lines.at(lineIndex);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith(QLatin1Char('$')))
        {
            parsePropertyName(line);
            continue;
        }
        if (isContinuationLine(line))
            continue;

        const bool freeFormat = line.contains(QLatin1Char(','));
        QString rawCard = freeFormat ? line.section(QLatin1Char(','), 0, 0).trimmed()
                                     : line.left(8).trimmed();
        const bool largeFormat = rawCard.endsWith(QLatin1Char('*'));
        rawCard.remove(QLatin1Char('*'));
        const QString card = rawCard.toUpper();
        if (card == QStringLiteral("ENDDATA"))
            break;

        QStringList fields = freeFormat ? freeDataFields(line)
                                        : fixedDataFields(line, largeFormat ? 16 : 8);
        while (lineIndex + 1 < lines.size() && isContinuationLine(lines.at(lineIndex + 1)))
        {
            const QString continuation = lines.at(++lineIndex);
            const QStringList continuationFields = continuation.contains(QLatin1Char(','))
                                                       ? freeDataFields(continuation)
                                                       : fixedDataFields(continuation,
                                                                         largeFormat ? 16 : 8);
            fields.append(continuationFields);
        }

        if (card == QStringLiteral("GRID"))
        {
            int nodeId = 0;
            int coordinateSystem = 0;
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (!parseNasInt(fields.value(0), nodeId) || nodeId <= 0
                || (!fields.value(1).isEmpty()
                    && !parseNasInt(fields.value(1), coordinateSystem))
                || !parseNasDouble(fields.value(2), x) || !parseNasDouble(fields.value(3), y)
                || !parseNasDouble(fields.value(4), z))
            {
                setError(QObject::tr("Nastran GRID 读取失败（第 %1 行）").arg(lineIndex + 1));
                return nullptr;
            }
            if (coordinateSystem != 0)
            {
                setError(QObject::tr("暂不支持非全局坐标系的 GRID：节点 %1 使用坐标系 %2")
                             .arg(nodeId)
                             .arg(coordinateSystem));
                return nullptr;
            }
            if (nodeIdsSeen.contains(nodeId))
            {
                setError(QObject::tr("Nastran 节点号重复：%1（第 %2 行）")
                             .arg(nodeId)
                             .arg(lineIndex + 1));
                return nullptr;
            }
            nodeIdsSeen.insert(nodeId);
            nodes.push_back({nodeId, x, y, z});
            continue;
        }

        if (!appendElement(card, fields, lineIndex + 1))
            return nullptr;
    }

    if (nodes.isEmpty())
    {
        setError(QObject::tr("Nastran 文件中没有可用的 GRID 节点"));
        return nullptr;
    }
    if (elements.isEmpty())
    {
        setError(QObject::tr("Nastran 文件中没有受支持的网格单元"));
        return nullptr;
    }

    // HyperMesh may export PID=0 while retaining component membership in
    // $HMMOVE comments. These lists can follow the elements, repeat a component,
    // and use THRU ranges spanning comment lines. Component IDs are source
    // regions. Retain source property IDs independently of solver group numbers.
    QHash<int, int> elementComponents;
    QHash<int, QString> componentNames;
    const QRegularExpression componentNameRe(
        QStringLiteral("^\\$HMNAME\\s+COMP\\s+(\\d+)\\s*\"([^\"]*)\""),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression componentMoveRe(
        QStringLiteral("^\\$HMMOVE\\s+(\\d+)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression componentDataRe(
        QStringLiteral("^\\$\\s*(?:\\d+|THRU)[\\d\\sTHRU]*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression componentTokenRe(QStringLiteral("\\d+|THRU"),
                                             QRegularExpression::CaseInsensitiveOption);
    auto orderedElementIds = elementIdsSeen.values();
    std::sort(orderedElementIds.begin(), orderedElementIds.end());
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString line = lines.at(i).trimmed();
        if (line.compare(QStringLiteral("ENDDATA"), Qt::CaseInsensitive) == 0)
            break;
        const auto name = componentNameRe.match(line);
        if (name.hasMatch())
        {
            int id = 0;
            if (parseNasInt(name.captured(1), id) && id > 0)
                componentNames.insert(id, name.captured(2).trimmed());
            continue;
        }
        const auto move = componentMoveRe.match(line);
        if (!move.hasMatch())
            continue;
        int component = 0;
        if (!parseNasInt(move.captured(1), component) || component <= 0)
            continue;
        QStringList tokens;
        while (i + 1 < lines.size() && componentDataRe.match(lines.at(i + 1).trimmed()).hasMatch())
        {
            auto matches = componentTokenRe.globalMatch(lines.at(++i));
            while (matches.hasNext())
                tokens.push_back(matches.next().captured().toUpper());
        }
        for (int token = 0; token < tokens.size(); ++token)
        {
            int first = 0, last = 0;
            if (!parseNasInt(tokens.at(token), first) || first <= 0)
            {
                setError(QObject::tr("HyperMesh 组件 %1 的单元列表格式不完整。").arg(component));
                return nullptr;
            }
            last = first;
            if (token + 1 < tokens.size() && tokens.at(token + 1) == QStringLiteral("THRU"))
            {
                if (token + 2 >= tokens.size() || !parseNasInt(tokens.at(token + 2), last)
                    || last < first)
                {
                    setError(QObject::tr("HyperMesh 组件 %1 的单元范围格式不完整。").arg(component));
                    return nullptr;
                }
                token += 2;
            }
            // Iterate existing IDs only; a large sparse THRU range must not
            // allocate or expand every integer between its endpoints.
            auto id = std::lower_bound(orderedElementIds.cbegin(), orderedElementIds.cend(), first);
            for (; id != orderedElementIds.cend() && *id <= last; ++id)
            {
                const int existing = elementComponents.value(*id);
                if (existing > 0 && existing != component)
                {
                    setError(QObject::tr("Nastran 单元 %1 被分配到多个 HyperMesh 组件。").arg(*id));
                    return nullptr;
                }
                elementComponents.insert(*id, component);
            }
        }
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();
    points->SetNumberOfPoints(nodes.size());

    QHash<int, vtkIdType> pointIdByNodeId;
    pointIdByNodeId.reserve(nodes.size() * 2);
    for (int i = 0; i < nodes.size(); ++i)
    {
        const NasNode& node = nodes.at(i);
        points->SetPoint(i, node.x, node.y, node.z);
        pointIdByNodeId.insert(node.id, static_cast<vtkIdType>(i));
        m_mshPickMap.nodeIdToPointId.insert(node.id, i);
        m_mshPickMap.pointIdToNodeId.insert(i, node.id);
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);
    grid->Allocate(elements.size());

    int meshDimension = 0;
    for (const NasElement& element : std::as_const(elements))
        meshDimension = qMax(meshDimension, element.dimension);

    QVector<int> insertedElementIndices;
    insertedElementIndices.reserve(elements.size());
    int missingNodeElementCount = 0;

    auto mapElementPointIds = [&](const NasElement& element,
                                  QVector<vtkIdType>& pointIds) -> bool
    {
        pointIds.clear();
        pointIds.reserve(element.nodeIds.size());
        for (int nodeId : element.nodeIds)
        {
            const auto it = pointIdByNodeId.constFind(nodeId);
            if (it == pointIdByNodeId.constEnd())
                return false;
            pointIds.push_back(it.value());
        }
        return true;
    };

    auto insertElement = [&](int elementIndex) -> bool
    {
        const NasElement& element = elements.at(elementIndex);
        QVector<vtkIdType> pointIds;
        if (!mapElementPointIds(element, pointIds))
        {
            ++missingNodeElementCount;
            return false;
        }
        grid->InsertNextCell(element.vtkType,
                             static_cast<vtkIdType>(pointIds.size()),
                             pointIds.constData());
        insertedElementIndices.push_back(elementIndex);
        return true;
    };

    // 先插入最高维的计算域。部分 Nastran 导出器（例如 COMSOL）还会把实体的
    // 外表面和内部界面写成 CQUAD/CTRIA。它们是边界分组，不是额外的壳计算单元。
    for (int elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
    {
        if (elements.at(elementIndex).dimension == meshDimension)
            insertElement(elementIndex);
    }

    auto pointSetKey = [](QVector<vtkIdType> pointIds) -> QString
    {
        std::sort(pointIds.begin(), pointIds.end());
        QString key;
        key.reserve(pointIds.size() * 12);
        for (vtkIdType pointId : std::as_const(pointIds))
        {
            key += QString::number(pointId);
            key += QLatin1Char(',');
        }
        return key;
    };

    QHash<QString, qint64> domainBoundaryByPoints;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        const int boundaryCount = !cell ? 0 : meshDimension == 3 ? cell->GetNumberOfFaces()
                                                                  : cell->GetNumberOfEdges();
        for (int boundaryId = 0; boundaryId < boundaryCount; ++boundaryId)
        {
            vtkCell* boundary = meshDimension == 3 ? cell->GetFace(boundaryId)
                                                    : cell->GetEdge(boundaryId);
            if (!boundary || boundary->GetNumberOfPoints() < meshDimension)
                continue;
            QVector<vtkIdType> boundaryPointIds;
            boundaryPointIds.reserve(boundary->GetNumberOfPoints());
            for (vtkIdType i = 0; i < boundary->GetNumberOfPoints(); ++i)
                boundaryPointIds.push_back(boundary->GetPointId(i));
            const qint64 boundaryKey =
                (qint64(cellId) << 4) | (qint64(boundaryId) & 0xF);
            domainBoundaryByPoints.insert(pointSetKey(boundaryPointIds), boundaryKey);

            // 同时建立高阶面的角点索引，以识别 Tet10/Hex20 上的低阶边界卡片。
            int cornerCount = boundaryPointIds.size();
            if (boundary->GetCellType() == VTK_QUADRATIC_EDGE)
                cornerCount = 2;
            else if (boundary->GetCellType() == VTK_QUADRATIC_TRIANGLE)
                cornerCount = 3;
            else if (boundary->GetCellType() == VTK_QUADRATIC_QUAD ||
                     boundary->GetCellType() == VTK_BIQUADRATIC_QUAD)
                cornerCount = 4;
            if (cornerCount < boundaryPointIds.size())
            {
                boundaryPointIds.resize(cornerCount);
                domainBoundaryByPoints.insert(pointSetKey(boundaryPointIds), boundaryKey);
            }
        }
    }

    struct BoundaryElement
    {
        int elementIndex = -1;
        qint64 entityKey = -1;
        QVector<vtkIdType> pointIds;
    };
    QVector<BoundaryElement> boundaryElements;

    // 混合维度文件中的低维卡片按格式层级保存为边界实体，不加入最高维
    // 计算单元数组。拓扑匹配仅补充其关联的域面/域边编号。
    for (int elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
    {
        const NasElement& element = elements.at(elementIndex);
        if (element.dimension == meshDimension)
            continue;

        QVector<vtkIdType> pointIds;
        if (!mapElementPointIds(element, pointIds))
        {
            ++missingNodeElementCount;
            continue;
        }
        const QString key = pointSetKey(pointIds);
        const qint64 entityKey =
            element.dimension == meshDimension - 1 && domainBoundaryByPoints.contains(key)
                ? domainBoundaryByPoints.value(key)
                : -1;
        boundaryElements.push_back({elementIndex, entityKey, pointIds});
    }

    if (grid->GetNumberOfCells() == 0)
    {
        setError(QObject::tr("Nastran 单元引用的节点不存在，未能生成有效网格"));
        return nullptr;
    }

    ensureDefaultArrays(grid);
    auto* nodeIdArray =
        vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    for (int i = 0; nodeIdArray && i < nodes.size(); ++i)
        nodeIdArray->SetValue(i, nodes.at(i).id);

    auto* matypeArray = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));
    auto* layerArray = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("LAYER"));
    auto* ketypeArray = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("KETYPE"));
    auto* elemIdArray = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    auto* partArray = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("src_part"));
    vtkNew<vtkIntArray> propertyIdArray;
    vtkNew<vtkStringArray> componentNameArray;
    if (!elementComponents.isEmpty())
    {
        propertyIdArray->SetName("nas_property_id");
        propertyIdArray->SetNumberOfTuples(grid->GetNumberOfCells());
        grid->GetCellData()->AddArray(propertyIdArray);
        componentNameArray->SetName("GroupName");
        componentNameArray->SetNumberOfValues(grid->GetNumberOfCells());
        grid->GetCellData()->AddArray(componentNameArray);
    }

    auto propertyKey = [](int dimension, int propertyId) -> quint64
    {
        return (quint64(quint32(dimension)) << 32) | quint64(quint32(propertyId));
    };
    QHash<quint64, int> propertyToMatype;
    int nextMatype = kDefaultCalculationMatype;
    for (const NasElement& element : std::as_const(elements))
    {
        const int propertyId = qMax(0, element.propertyId);
        const quint64 key = propertyKey(element.dimension, propertyId);
        if (!propertyToMatype.contains(key))
            propertyToMatype.insert(key, nextMatype++);
    }
    bool allDomainsHaveComponents = !elementComponents.isEmpty();
    for (int index : std::as_const(insertedElementIndices))
    {
        const auto& element = elements.at(index);
        if (element.dimension == meshDimension && !elementComponents.contains(element.id))
            allDomainsHaveComponents = false;
    }
    if (allDomainsHaveComponents)
    {
        m_mshPickMap.materialGroupCategory = QStringLiteral("REGION");
        m_mshPickMap.excavationGroupCategory = QStringLiteral("REGION");
    }
    QHash<quint64, int> componentBoundaryIds;
    auto componentLabel = [&](int component)
    {
        const QString label = componentNames.value(component);
        return label.isEmpty() ? QStringLiteral("组件 %1").arg(component) : label;
    };
    // PID=0 contains no usable material partition. Expose its components
    // through the existing material-group selector, without an extra UI.
    QHash<quint64, int> componentMatypes;
    QSet<int> componentBaseDimensions;
    QSet<int> ungroupedDimensions;
    for (int index : std::as_const(insertedElementIndices))
    {
        const auto& element = elements.at(index);
        if (element.propertyId == 0 && !elementComponents.contains(element.id))
            ungroupedDimensions.insert(element.dimension);
    }
    for (int index : std::as_const(insertedElementIndices))
    {
        const auto& element = elements.at(index);
        const int component = elementComponents.value(element.id);
        if (element.propertyId != 0 || component <= 0)
            continue;
        const auto key = propertyKey(element.dimension, component);
        if (componentMatypes.contains(key))
            continue;
        const int matype = componentBaseDimensions.contains(element.dimension) ||
                           ungroupedDimensions.contains(element.dimension)
            ? nextMatype++ : propertyToMatype.value(propertyKey(element.dimension, 0));
        componentBaseDimensions.insert(element.dimension);
        componentMatypes.insert(key, matype);
    }
    auto componentSet = [&](const NasElement& element) -> MeshTagSet*
    {
        const int component = elementComponents.value(element.id);
        if (component <= 0)
            return nullptr;
        const bool domain = element.dimension == meshDimension;
        int tagId = component;
        if (!domain)
        {
            const auto key = propertyKey(element.dimension, component);
            if (!componentBoundaryIds.contains(key))
                componentBoundaryIds.insert(key, nextMatype++);
            tagId = componentBoundaryIds.value(key);
        }
        const MeshTagKey key{domain ? QStringLiteral("REGION") : QStringLiteral("Physical"),
                             element.dimension, tagId};
        if (!m_mshPickMap.tagSets.contains(key))
        {
            MeshTagSet set;
            set.key = key;
            set.name = componentLabel(component);
            set.source = QStringLiteral("HyperMeshComponent");
            m_mshPickMap.tagSets.insert(key, set);
        }
        return &m_mshPickMap.tagSets[key];
    };
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        const NasElement& element =
            elements.at(insertedElementIndices.at(static_cast<int>(cellId)));
        const int propertyId = qMax(0, element.propertyId);
        const int componentId = elementComponents.value(element.id);
        const int matype = propertyId == 0 && componentId > 0
            ? componentMatypes.value(propertyKey(element.dimension, componentId))
            : propertyToMatype.value(propertyKey(element.dimension, propertyId));

        if (propertyId == 0 && componentId > 0)
        {
            auto& materialGroup = ensurePhysicalTagSet(m_mshPickMap, matype, element.dimension,
                                                        QStringLiteral("HyperMeshComponent"));
            materialGroup.name = componentLabel(componentId);
            materialGroup.cellIds.push_back(cellId);
            m_mshPickMap.groupDisplayNames[QStringLiteral("MATYPE/%1/%2")
                .arg(element.dimension).arg(matype)] = materialGroup.name;
        }

        if (matypeArray)
            matypeArray->SetValue(cellId, matype);
        if (layerArray)
            layerArray->SetValue(cellId, kDefaultConstructionLayer);
        if (ketypeArray)
            ketypeArray->SetValue(cellId, element.ketype);
        if (elemIdArray)
            elemIdArray->SetValue(cellId, element.id);
        if (partArray)
            partArray->SetValue(cellId, elementComponents.isEmpty()
                                           ? propertyId : elementComponents.value(element.id));
        if (!elementComponents.isEmpty())
        {
            propertyIdArray->SetValue(cellId, propertyId);
            const int component = elementComponents.value(element.id);
            componentNameArray->SetValue(cellId, component > 0 ? componentLabel(component).toStdString() : std::string());
        }
        if (auto* component = componentSet(element))
        {
            component->cellIds.push_back(static_cast<MeshIdType>(cellId));
            if (element.dimension == meshDimension)
                m_mshPickMap.cellIdToDomainTags[static_cast<MeshIdType>(cellId)].push_back(component->key);
            else
            {
                auto* cell = grid->GetCell(cellId);
                for (vtkIdType i = 0; cell && i < cell->GetNumberOfPoints(); ++i)
                    component->pointIds.push_back(cell->GetPointId(i));
            }
            if (propertyId == 0)
                continue;
        }

        const QString groupName =
            propertyNames.value(propertyId,
                                propertyId > 0 ? QStringLiteral("PID %1").arg(propertyId)
                                               : QStringLiteral("未分组"));
        m_mshPickMap.physIdToName.insert(matype, groupName);
        MeshTagSet& set =
            ensurePhysicalTagSet(m_mshPickMap, matype, element.dimension,
                                 QStringLiteral("Nastran"));
        set.name = groupName;
        set.cellIds.push_back(static_cast<MeshIdType>(cellId));
        if (element.dimension < meshDimension)
        {
            vtkCell* cell = grid->GetCell(cellId);
            for (vtkIdType i = 0; cell && i < cell->GetNumberOfPoints(); ++i)
                set.pointIds.push_back(static_cast<MeshIdType>(cell->GetPointId(i)));
        }
        if (element.dimension == meshDimension)
            m_mshPickMap.cellIdToDomainTags[static_cast<MeshIdType>(cellId)].push_back(set.key);
    }

    for (const BoundaryElement& boundary : std::as_const(boundaryElements))
    {
        const NasElement& element = elements.at(boundary.elementIndex);
        const int propertyId = qMax(0, element.propertyId);
        if (auto* component = componentSet(element))
        {
            for (vtkIdType pointId : boundary.pointIds)
                component->pointIds.push_back(pointId);
            if (boundary.entityKey >= 0 && element.dimension == 2)
                component->faceKeys.push_back(boundary.entityKey);
            else if (boundary.entityKey >= 0 && element.dimension == 1)
                component->edgeKeys.push_back(boundary.entityKey);
            if (propertyId == 0)
                continue;
        }
        const int matype = propertyToMatype.value(propertyKey(element.dimension, propertyId));
        const QString groupName =
            propertyNames.value(propertyId,
                                propertyId > 0 ? QStringLiteral("PID %1").arg(propertyId)
                                               : QStringLiteral("未分组"));
        m_mshPickMap.physIdToName.insert(matype, groupName);
        MeshTagSet& set =
            ensurePhysicalTagSet(m_mshPickMap, matype, element.dimension,
                                 QStringLiteral("NastranBoundary"));
        set.name = groupName;
        for (vtkIdType pointId : boundary.pointIds)
            set.pointIds.push_back(static_cast<MeshIdType>(pointId));
        if (boundary.entityKey >= 0 && element.dimension == 2)
            set.faceKeys.push_back(static_cast<MeshIdType>(boundary.entityKey));
        else if (boundary.entityKey >= 0 && element.dimension == 1)
            set.edgeKeys.push_back(static_cast<MeshIdType>(boundary.entityKey));
    }

    m_mshPickMap.meshDim = meshDimension;
    PublicVariables::instance().set(PublicConstants::kMeshDimension, meshDimension, "mesh");
    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("Nastran"));

    qInfo() << "[Nastran] imported nodes=" << nodes.size()
            << "cells=" << grid->GetNumberOfCells()
            << "boundary-elements=" << boundaryElements.size()
            << "unsupported=" << unsupportedElementCount
            << "invalid=" << invalidElementCount
            << "missing-node=" << missingNodeElementCount;
    return grid;
}

MshPickMap MeshGridIO::getMeshMap()
{
    return m_mshPickMap;
}

const MshPickMap& MeshGridIO::getMeshMapRef() const
{
    return m_mshPickMap;
}

void MeshGridIO::setMeshMap(const MshPickMap& pickMap)
{
    m_mshPickMap = pickMap;
    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("SetMeshMap"));
    normalizeDomainGroupCategories(m_mshPickMap);
}

void MeshGridIO::clearPickMap()
{
    m_mshPickMap = MshPickMap{};
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::loadIiinFile(const QString& fileName,
                                                              QString* errMsg)
{
    clearPickMap();
    m_mshPickMap.materialGroupCategory = QStringLiteral("MATYPE");
    m_mshPickMap.excavationGroupCategory = QStringLiteral("LAYER");

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (errMsg)
            *errMsg = QObject::tr("无法打开文件: %1").arg(fileName);
        return nullptr;
    }

    const QByteArray bytes = f.readAll();
    f.close();

    class FastParser
    {
      public:
        FastParser(const QByteArray& b) : m_cur(b.constData()), m_end(b.constData() + b.size()) {}

        bool readInt(int& value)
        {
            skipSpace();
            if (m_cur >= m_end)
                return false;
            char* next = nullptr;
            const long v = std::strtol(m_cur, &next, 10);
            if (next == m_cur)
                return false;
            value = static_cast<int>(v);
            m_cur = next;
            return true;
        }

        bool readDouble(double& value)
        {
            skipSpace();
            if (m_cur >= m_end)
                return false;
            char* next = nullptr;
            const double v = std::strtod(m_cur, &next);
            if (next == m_cur)
                return false;
            value = v;
            m_cur = next;
            return true;
        }

        void skipLine()
        {
            while (m_cur < m_end && *m_cur != '\n')
                ++m_cur;
            if (m_cur < m_end && *m_cur == '\n')
                ++m_cur;
        }

        bool readIntLine(QVector<int>& values)
        {
            values.clear();

            while (m_cur < m_end)
            {
                // 空行直接跳过；行内只跳过空白，不跨过换行。
                while (m_cur < m_end && (*m_cur == ' ' || *m_cur == '\t' || *m_cur == '\r'))
                    ++m_cur;
                if (m_cur < m_end && *m_cur == '\n')
                {
                    ++m_cur;
                    continue;
                }

                while (m_cur < m_end && *m_cur != '\n')
                {
                    while (m_cur < m_end && (*m_cur == ' ' || *m_cur == '\t' || *m_cur == '\r'))
                        ++m_cur;
                    if (m_cur >= m_end || *m_cur == '\n')
                        break;

                    // IIIN.DAT 正常不带注释；这里仅跳过整行注释，避免把说明文字解析成 0。
                    if (*m_cur == '#' || *m_cur == '!')
                    {
                        while (m_cur < m_end && *m_cur != '\n')
                            ++m_cur;
                        break;
                    }

                    char* next = nullptr;
                    const long v = std::strtol(m_cur, &next, 10);
                    if (next == m_cur)
                    {
                        while (m_cur < m_end && *m_cur != ' ' && *m_cur != '\t' && *m_cur != '\r' && *m_cur != '\n')
                            ++m_cur;
                        continue;
                    }

                    values.push_back(static_cast<int>(v));
                    m_cur = next;
                }

                if (m_cur < m_end && *m_cur == '\n')
                    ++m_cur;

                if (!values.isEmpty())
                    return true;
            }

            return false;
        }

      private:
        void skipSpace()
        {
            while (m_cur < m_end && (*m_cur == ' ' || *m_cur == '\t' || *m_cur == '\r' || *m_cur == '\n'))
                ++m_cur;
        }

      private:
        const char* m_cur = nullptr;
        const char* m_end = nullptr;
    };

    FastParser parser(bytes);

    int numnp = 0, madim = 3, nelem = 0, manod = 0, extra = 0;
    double am = 1.0, d1 = 0.0, d2 = 0.0, d3 = 0.0;

    if (!parser.readInt(numnp) || !parser.readInt(madim) || !parser.readInt(nelem) ||
        !parser.readInt(manod) || !parser.readInt(extra) || !parser.readDouble(am) ||
        !parser.readDouble(d1) || !parser.readDouble(d2) || !parser.readDouble(d3))
    {
        if (errMsg)
            *errMsg = QStringLiteral("首行(控制信息)读取失败");
        return nullptr;
    }

    PublicVariables::instance().set(PublicConstants::kMeshDimension, madim, "mesh");
    qInfo() << "网格维度" << madim;

    int iobject = 0, ivertical = 2;
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0, zmin = 0, zmax = 0;
    if (!parser.readInt(iobject) || !parser.readInt(ivertical) || !parser.readDouble(xmin) ||
        !parser.readDouble(xmax) || !parser.readDouble(ymin) || !parser.readDouble(ymax) ||
        !parser.readDouble(zmin) || !parser.readDouble(zmax))
    {
        if (errMsg)
            *errMsg = QStringLiteral("第二行(对象信息)读取失败");
        return nullptr;
    }
    parser.skipLine();

    if (numnp <= 0)
    {
        if (errMsg)
            *errMsg = QStringLiteral("节点数 numnp 非法");
        return nullptr;
    }
    if (manod <= 0)
        manod = 8;

    auto pts = vtkSmartPointer<vtkPoints>::New();
    pts->SetDataTypeToDouble();
    pts->SetNumberOfPoints(numnp);

    auto nodeIdArr = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArr->SetName("node_id");
    nodeIdArr->SetNumberOfComponents(1);
    nodeIdArr->SetNumberOfTuples(numnp);

    auto originalPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalPointIds->SetName("vtkOriginalPointIds");
    originalPointIds->SetNumberOfComponents(1);
    originalPointIds->SetNumberOfTuples(numnp);

    QVector<int> nodeIds;
    nodeIds.resize(numnp);

    int minNodeId = INT_MAX;
    int maxNodeId = INT_MIN;

    for (int i = 0; i < numnp; ++i)
    {
        double x = 0.0, y = 0.0, z = 0.0;
        int id = i + 1;

        if (madim <= 2)
        {
            if (!parser.readDouble(x) || !parser.readDouble(y) || !parser.readInt(id))
            {
                if (errMsg)
                    *errMsg = QStringLiteral("节点数据读取失败，位置：%1").arg(i + 1);
                return nullptr;
            }
            z = 0.0;
        }
        else
        {
            if (!parser.readDouble(x) || !parser.readDouble(y) || !parser.readDouble(z) || !parser.readInt(id))
            {
                if (errMsg)
                    *errMsg = QStringLiteral("节点数据读取失败，位置：%1").arg(i + 1);
                return nullptr;
            }
        }

        x *= am;
        y *= am;
        z *= am;

        pts->SetPoint(i, x, y, z);
        nodeIdArr->SetValue(i, id);
        originalPointIds->SetValue(i, i);
        nodeIds[i] = id;
        minNodeId = qMin(minNodeId, id);
        maxNodeId = qMax(maxNodeId, id);
    }

    QVector<vtkIdType> denseIdToPoint;
    QHash<int, vtkIdType> sparseIdToPoint;

    const qint64 idRange = (maxNodeId >= minNodeId) ? (qint64(maxNodeId) - qint64(minNodeId) + 1) : 0;
    const bool useDenseMap = (idRange > 0 && idRange <= qint64(numnp) * 4 && idRange <= 20000000);

    if (useDenseMap)
    {
        denseIdToPoint.resize(int(idRange));
        std::fill(denseIdToPoint.begin(), denseIdToPoint.end(), static_cast<vtkIdType>(-1));
        for (int i = 0; i < numnp; ++i)
            denseIdToPoint[nodeIds[i] - minNodeId] = static_cast<vtkIdType>(i);
    }
    else
    {
        sparseIdToPoint.reserve(numnp * 2);
        for (int i = 0; i < numnp; ++i)
            sparseIdToPoint.insert(nodeIds[i], static_cast<vtkIdType>(i));
    }

    auto pointIdOfNode = [&](int nid) -> vtkIdType
    {
        if (nid == 0)
            return -1;
        if (useDenseMap)
        {
            const qint64 idx = qint64(nid) - qint64(minNodeId);
            if (idx < 0 || idx >= denseIdToPoint.size())
                return -1;
            return denseIdToPoint[int(idx)];
        }
        auto it = sparseIdToPoint.constFind(nid);
        return (it == sparseIdToPoint.constEnd()) ? static_cast<vtkIdType>(-1) : it.value();
    };

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(pts);
    setVerticalAxis(grid, ivertical);
    setObjectType(grid, iobject);
    grid->GetPointData()->AddArray(nodeIdArr);
    grid->GetPointData()->AddArray(originalPointIds);

    const vtkIdType estimatedCells = nelem > 0 ? static_cast<vtkIdType>(nelem) : 1024;
    grid->Allocate(estimatedCells);

    auto matypeArr = vtkSmartPointer<vtkIntArray>::New();
    matypeArr->SetName("MATYPE");
    matypeArr->SetNumberOfComponents(1);
    matypeArr->Allocate(estimatedCells);

    auto elemIdArr = vtkSmartPointer<vtkIntArray>::New();
    elemIdArr->SetName("elem_id");
    elemIdArr->SetNumberOfComponents(1);
    elemIdArr->Allocate(estimatedCells);

    auto ketypeArr = vtkSmartPointer<vtkIntArray>::New();
    ketypeArr->SetName("KETYPE");
    ketypeArr->SetNumberOfComponents(1);
    ketypeArr->Allocate(estimatedCells);

    auto layerArr = vtkSmartPointer<vtkIntArray>::New();
    layerArr->SetName("LAYER");
    layerArr->SetNumberOfComponents(1);
    layerArr->Allocate(estimatedCells);

    auto partArr = vtkSmartPointer<vtkIntArray>::New();
    partArr->SetName("src_part");
    partArr->SetNumberOfComponents(1);
    partArr->Allocate(estimatedCells);

    auto tail1Arr = vtkSmartPointer<vtkIntArray>::New();
    tail1Arr->SetName("TAIL1");
    tail1Arr->SetNumberOfComponents(1);
    tail1Arr->Allocate(estimatedCells);

    auto tail2Arr = vtkSmartPointer<vtkIntArray>::New();
    tail2Arr->SetName("TAIL2");
    tail2Arr->SetNumberOfComponents(1);
    tail2Arr->Allocate(estimatedCells);

    auto originalCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalCellIds->SetName("vtkOriginalCellIds");
    originalCellIds->SetNumberOfComponents(1);
    originalCellIds->Allocate(estimatedCells);

    auto cellDimArr = vtkSmartPointer<vtkIntArray>::New();
    cellDimArr->SetName("CELL_DIM");
    cellDimArr->SetNumberOfComponents(1);
    cellDimArr->Allocate(estimatedCells);

    QVector<int> elemNodeIds;
    elemNodeIds.resize(manod);
    vtkIdType ptIdsBuf[32];
    QMap<vtkIdType, int> connectivityPatterns;
    QMap<vtkIdType, int> topologyOverrides;

    auto vtkTypeDimension = [](int vtkCellType) -> int
    {
        switch (vtkCellType)
        {
        case VTK_LINE:
        case VTK_QUADRATIC_EDGE:
            return 1;
        case VTK_TRIANGLE:
        case VTK_QUADRATIC_TRIANGLE:
        case VTK_QUAD:
        case VTK_QUADRATIC_LINEAR_QUAD:
        case VTK_QUADRATIC_QUAD:
        case VTK_BIQUADRATIC_QUAD:
            return 2;
        case VTK_TETRA:
        case VTK_HEXAHEDRON:
        case VTK_WEDGE:
        case VTK_PYRAMID:
        case VTK_POLYHEDRON:
        case VTK_QUADRATIC_HEXAHEDRON:
            return 3;
        default:
            return 0;
        }
    };

    auto addCellPiece = [&](int vtkCellType, const int* localNodeLids, int lidCount,
                            int elemId, int ketype, int matype, int layer, int tail1, int tail2,
                            int part) -> void
    {
        if (lidCount <= 0 || lidCount > 32)
            return;

        for (int i = 0; i < lidCount; ++i)
        {
            const int lid = localNodeLids[i];
            if (lid < 0 || lid >= elemNodeIds.size())
                return;
            const vtkIdType pid = pointIdOfNode(elemNodeIds[lid]);
            if (pid < 0)
                return;
            ptIdsBuf[i] = pid;
        }

        const vtkIdType cellId = grid->InsertNextCell(vtkCellType, static_cast<vtkIdType>(lidCount), ptIdsBuf);
        matypeArr->InsertNextValue(matype);
        elemIdArr->InsertNextValue(elemId);
        ketypeArr->InsertNextValue(ketype);
        layerArr->InsertNextValue(layer);
        partArr->InsertNextValue(part);
        tail1Arr->InsertNextValue(tail1);
        tail2Arr->InsertNextValue(tail2);
        originalCellIds->InsertNextValue(cellId);
        cellDimArr->InsertNextValue(vtkTypeDimension(vtkCellType));
    };


    auto addLegacyPolyhedronPiece = [&](const LegacyHexTopologyInfo& topology,
                                        int elemId, int ketype, int matype, int layer,
                                        int tail1, int tail2, int part) -> vtkIdType
    {
        if (!topology.valid || topology.vtkCellType != VTK_POLYHEDRON ||
            topology.localNodeSlots.size() < 4 || topology.polyhedronFaces.size() < 4)
            return -1;

        QVector<vtkIdType> pointIds;
        pointIds.reserve(topology.localNodeSlots.size());
        for (int sourceSlot : topology.localNodeSlots)
        {
            if (sourceSlot < 0 || sourceSlot >= elemNodeIds.size())
                return -1;
            const vtkIdType pid = pointIdOfNode(elemNodeIds[sourceSlot]);
            if (pid < 0)
                return -1;
            pointIds.push_back(pid);
        }

        QVector<vtkIdType> faceStream;
        if (!buildOrientedPolyhedronFaceStream(
                pts, pointIds, topology.polyhedronFaces, faceStream))
            return -1;

        const vtkIdType cellId = grid->InsertNextCell(
            VTK_POLYHEDRON,
            static_cast<vtkIdType>(pointIds.size()),
            pointIds.constData(),
            static_cast<vtkIdType>(topology.polyhedronFaces.size()),
            faceStream.constData());
        if (cellId < 0)
            return -1;

        matypeArr->InsertNextValue(matype);
        elemIdArr->InsertNextValue(elemId);
        ketypeArr->InsertNextValue(ketype);
        layerArr->InsertNextValue(layer);
        partArr->InsertNextValue(part);
        tail1Arr->InsertNextValue(tail1);
        tail2Arr->InsertNextValue(tail2);
        originalCellIds->InsertNextValue(cellId);
        cellDimArr->InsertNextValue(3);
        return cellId;
    };

    auto addPiecesByKetype = [&](int elemId, int ketype, int matype, int layer, int tail1, int tail2) -> bool
    {
        switch (ketype)
        {
        case 1:
        case 10:
        case 21:
        {
            static const int ids[] = {0, 1};
            addCellPiece(VTK_LINE, ids, 2, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 2:
        {
            static const int ids[] = {0, 2, 1};
            addCellPiece(VTK_QUADRATIC_EDGE, ids, 3, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 3:
        case 11:
        {
            static const int ids[] = {0, 1, 2, 3};
            addCellPiece(VTK_QUAD, ids, 4, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 4:
        {
            static const int ids[] = {0, 2, 3, 5, 1, 4};
            addCellPiece(VTK_QUADRATIC_LINEAR_QUAD, ids, 6, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 5:
        {
            static const int ids[] = {0, 2, 4, 6, 1, 3, 5, 7};
            addCellPiece(VTK_QUADRATIC_QUAD, ids, 8, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 29:
        {
            static const int ids[] = {0, 2, 4, 6, 1, 3, 5, 7, 8};
            addCellPiece(VTK_BIQUADRATIC_QUAD, ids, 9, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 12:
        {
            static const int a[] = {0, 1, 4};
            static const int b[] = {1, 2, 4};
            static const int c[] = {2, 3, 4};
            static const int d[] = {3, 0, 4};
            addCellPiece(VTK_TRIANGLE, a, 3, elemId, ketype, matype, layer, tail1, tail2, 0);
            addCellPiece(VTK_TRIANGLE, b, 3, elemId, ketype, matype, layer, tail1, tail2, 1);
            addCellPiece(VTK_TRIANGLE, c, 3, elemId, ketype, matype, layer, tail1, tail2, 2);
            addCellPiece(VTK_TRIANGLE, d, 3, elemId, ketype, matype, layer, tail1, tail2, 3);
            return true;
        }
        case 31:
        {
            static const int ids[] = {0, 1, 2};
            addCellPiece(VTK_TRIANGLE, ids, 3, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 32:
        {
            static const int ids[] = {0, 2, 4, 1, 3, 5};
            addCellPiece(VTK_QUADRATIC_TRIANGLE, ids, 6, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 33:
        {
            static const int ids[] = {0, 3, 6};
            addCellPiece(VTK_TRIANGLE, ids, 3, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 34:
        {
            static const int ids[] = {0, 4, 8};
            addCellPiece(VTK_TRIANGLE, ids, 3, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 35:
        {
            static const int ids[] = {0, 1, 2, 3};
            addCellPiece(VTK_TETRA, ids, 4, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 6:
        case 13:
        {
            static const int ids[] = {0, 1, 2, 3, 4, 5, 6, 7};
            addCellPiece(VTK_HEXAHEDRON, ids, 8, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 36:
        {
            static const int ids[] = {0, 1, 2, 3, 9, 10, 11, 12};
            addCellPiece(VTK_HEXAHEDRON, ids, 8, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 37:
        {
            static const int ids[] = {0, 2, 4, 6, 12, 14, 16, 18, 1, 3, 5, 7, 13, 15, 17, 19, 8, 9, 10, 11};
            addCellPiece(VTK_QUADRATIC_HEXAHEDRON, ids, 20, elemId, ketype, matype, layer, tail1, tail2, 0);
            return true;
        }
        case 9:
        case 17:
        {
            static const int a[] = {0, 1, 2, 3};
            static const int b[] = {4, 5, 6, 7};
            addCellPiece(VTK_QUAD, a, 4, elemId, ketype, matype, layer, tail1, tail2, 0);
            addCellPiece(VTK_QUAD, b, 4, elemId, ketype, matype, layer, tail1, tail2, 1);
            return true;
        }
        default:
            return false;
        }
    };

    int readElems = 0;
    int legacyTetCount = 0;
    int legacyPyramidCount = 0;
    int legacyWedgeCount = 0;
    int legacyHexCount = 0;
    int legacyCollapsedHexCount = 0;
    int legacyInvalidCount = 0;
    for (int elemIndex = 0; elemIndex < nelem; ++elemIndex)
    {
        int elemId = 0;
        if (!parser.readInt(elemId))
            break;

        for (int i = 0; i < manod; ++i)
        {
            int nid = 0;
            if (!parser.readInt(nid))
            {
                if (errMsg)
                    *errMsg = QStringLiteral("单元节点数据读取失败，单元位置：%1").arg(elemIndex + 1);
                return nullptr;
            }
            elemNodeIds[i] = nid;
        }

        int ketype = 0, matype = 0, layer = 0, tail1 = 0, tail2 = 0;
        if (!parser.readInt(ketype) || !parser.readInt(matype) || !parser.readInt(layer))
        {
            if (errMsg)
                *errMsg = QStringLiteral("单元属性数据读取失败，单元位置：%1").arg(elemIndex + 1);
            return nullptr;
        }

        if (extra >= 4 && !parser.readInt(tail1))
        {
            if (errMsg)
                *errMsg = QStringLiteral("TAIL1 读取失败，单元位置：%1").arg(elemIndex + 1);
            return nullptr;
        }
        if (extra >= 5 && !parser.readInt(tail2))
        {
            if (errMsg)
                *errMsg = QStringLiteral("TAIL2 读取失败，单元位置：%1").arg(elemIndex + 1);
            return nullptr;
        }
        for (int i = 5; i < extra; ++i)
        {
            int ignored = 0;
            if (!parser.readInt(ignored))
            {
                if (errMsg)
                    *errMsg = QStringLiteral("扩展属性读取失败，单元位置：%1").arg(elemIndex + 1);
                return nullptr;
            }
        }

        if (ketype <= 0 || ketype > 200)
            continue;

        if (ketype == 3)
        {
            int uniq[4] = {0, 0, 0, 0};
            int uniqCount = 0;
            for (int i = 0; i < qMin(manod, 4); ++i)
            {
                const int nid = elemNodeIds[i];
                if (nid == 0)
                    continue;
                bool exists = false;
                for (int j = 0; j < uniqCount; ++j)
                {
                    if (uniq[j] == nid)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists && uniqCount < 4)
                    uniq[uniqCount++] = nid;
            }
            if (uniqCount == 3)
            {
                int pattern = 0;
                for (int i = 0; i < qMin(manod, 4); ++i)
                {
                    int uniqIndex = 0;
                    while (uniqIndex < uniqCount && uniq[uniqIndex] != elemNodeIds[i])
                        ++uniqIndex;
                    if (uniqIndex >= uniqCount)
                        uniqIndex = qMin(i, uniqCount - 1);
                    pattern |= (uniqIndex & 0x3) << (i * 2);
                }

                elemNodeIds[0] = uniq[0];
                elemNodeIds[1] = uniq[1];
                elemNodeIds[2] = uniq[2];

                static const int triangleIds[] = {0, 1, 2};
                const vtkIdType cellId = grid->GetNumberOfCells();
                addCellPiece(VTK_TRIANGLE, triangleIds, 3, elemId, ketype, matype, layer,
                             tail1, tail2, 0);
                if (grid->GetNumberOfCells() > cellId)
                    connectivityPatterns.insert(cellId, pattern);
                ++readElems;
                continue;
            }
        }

        bool invalidLegacyDegenerateHex = false;

        // KETYPE=6 是老求解器的 8 槽位三维实体。历史工程会通过重复节点
        // 表达 Tet/Pyramid/Wedge 等标准退化形式；能正常退化的全部归入标准拓扑。
        // 无法归入标准拓扑但仍能形成闭合体的记录，底层可用 VTK_POLYHEDRON 保存，
        // 对用户只统一标记为“异常”。
        if (ketype == 6 && manod >= 8)
        {
            const LegacyHexTopologyInfo legacy = analyzeLegacyHexTopology(elemNodeIds);
            if (legacy.valid && !legacy.localNodeSlots.isEmpty())
            {
                vtkIdType cellId = -1;
                if (legacy.vtkCellType == VTK_POLYHEDRON)
                {
                    cellId = addLegacyPolyhedronPiece(
                        legacy, elemId, ketype, matype, layer, tail1, tail2, 0);
                }
                else
                {
                    const vtkIdType before = grid->GetNumberOfCells();
                    addCellPiece(legacy.vtkCellType, legacy.localNodeSlots.constData(),
                                 legacy.localNodeSlots.size(), elemId, ketype, matype, layer,
                                 tail1, tail2, 0);
                    if (grid->GetNumberOfCells() > before)
                        cellId = before;
                }

                if (cellId >= 0)
                {
                    connectivityPatterns.insert(cellId, legacy.patternCode);
                    if (legacy.topologyType != VTK_EMPTY_CELL &&
                        legacy.topologyType != legacy.vtkCellType)
                    {
                        topologyOverrides.insert(cellId, legacy.topologyType);
                    }

                    switch (legacy.topologyType)
                    {
                    case VTK_TETRA:
                        ++legacyTetCount;
                        break;
                    case VTK_PYRAMID:
                        ++legacyPyramidCount;
                        break;
                    case VTK_WEDGE:
                        ++legacyWedgeCount;
                        break;
                    case VTK_HEXAHEDRON:
                        ++legacyHexCount;
                        if (legacy.vtkCellType == VTK_POLYHEDRON)
                            ++legacyCollapsedHexCount;
                        break;
                    case kTopologyAbnormal:
                        break;
                    default:
                        break;
                    }
                    ++readElems;
                    continue;
                }

                // 拓扑闭合但几何构造失败：保留原 8 槽位 Hex，同时明确标记异常。
                invalidLegacyDegenerateHex = true;
                ++legacyInvalidCount;
            }
            else if (legacy.uniqueCount > 0 && legacy.uniqueCount < 8)
            {
                invalidLegacyDegenerateHex = true;
                ++legacyInvalidCount;
            }
        }

        const vtkIdType fallbackCellId = grid->GetNumberOfCells();
        if (addPiecesByKetype(elemId, ketype, matype, layer, tail1, tail2))
        {
            if (invalidLegacyDegenerateHex && grid->GetNumberOfCells() > fallbackCellId)
                topologyOverrides.insert(fallbackCellId, kTopologyAbnormal);
            ++readElems;
        }
    }

    if (legacyTetCount + legacyPyramidCount + legacyWedgeCount +
            legacyCollapsedHexCount > 0)
    {
        qInfo() << "[IIIN] legacy KETYPE=6 normalized:"
                << "tet=" << legacyTetCount
                << "pyramid=" << legacyPyramidCount
                << "wedge=" << legacyWedgeCount
                << "hex=" << legacyHexCount
                << "collapsedHex=" << legacyCollapsedHexCount;
    }
    if (legacyInvalidCount > 0)
    {
        qWarning() << "[IIIN] invalid collapsed KETYPE=6 cells="
                   << legacyInvalidCount
                   << "; these cells are kept for inspection and marked as abnormal.";
    }

    grid->GetCellData()->AddArray(matypeArr);
    grid->GetCellData()->AddArray(elemIdArr);
    grid->GetCellData()->AddArray(ketypeArr);
    grid->GetCellData()->AddArray(layerArr);
    grid->GetCellData()->AddArray(partArr);
    grid->GetCellData()->AddArray(tail1Arr);
    grid->GetCellData()->AddArray(tail2Arr);
    grid->GetCellData()->AddArray(originalCellIds);
    grid->GetCellData()->AddArray(cellDimArr);

    // 明确区分“求解器单元族 KETYPE”和“真实几何拓扑”。
    // 例如老 IIIN 中 Tet/Pyramid/Wedge/Hex 都可能仍是 KETYPE=6；
    // 筛选/着色应使用 TOPOLOGY_TYPE（值采用 VTK 标准 CellType 编号）。
    auto topologyTypeArr = vtkSmartPointer<vtkIntArray>::New();
    topologyTypeArr->SetName("TOPOLOGY_TYPE");
    topologyTypeArr->SetNumberOfComponents(1);
    topologyTypeArr->SetNumberOfTuples(grid->GetNumberOfCells());
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        const auto it = topologyOverrides.constFind(cid);
        topologyTypeArr->SetValue(
            cid, it != topologyOverrides.constEnd() ? it.value() : grid->GetCellType(cid));
    }
    grid->GetCellData()->AddArray(topologyTypeArr);

    auto connectivityPatternArr = vtkSmartPointer<vtkIntArray>::New();
    connectivityPatternArr->SetName(kIiinConnectivityPatternArrayName);
    connectivityPatternArr->SetNumberOfComponents(1);
    connectivityPatternArr->SetNumberOfTuples(grid->GetNumberOfCells());
    connectivityPatternArr->FillComponent(0, -1.0);
    for (auto it = connectivityPatterns.cbegin(); it != connectivityPatterns.cend(); ++it)
        connectivityPatternArr->SetValue(it.key(), it.value());
    grid->GetCellData()->AddArray(connectivityPatternArr);

    // 二进制 mesh_snapshot.cache.vtu 只是加速缓存。拓扑恢复算法升级后，
    // 旧 VTU 中可能还缓存着上一版的“异常/Polyhedron”分类（例如 1099 号单元）。
    // 把解析器拓扑版本写入 FieldData，工程恢复时据此判断缓存能否继续复用；
    // 版本不一致时必须回到权威 IIIN 快照重新解析一次。
    auto topologySchemaVersion = vtkSmartPointer<vtkIntArray>::New();
    topologySchemaVersion->SetName(kIiinTopologySchemaVersionArrayName);
    topologySchemaVersion->SetNumberOfComponents(1);
    topologySchemaVersion->SetNumberOfTuples(1);
    topologySchemaVersion->SetValue(0, kIiinTopologySchemaVersion);
    grid->GetFieldData()->RemoveArray(kIiinTopologySchemaVersionArrayName);
    grid->GetFieldData()->AddArray(topologySchemaVersion);

    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("IIIN"));
    return grid;
}

bool MeshGridIO::exportIiinFile(const QString& fileName, vtkUnstructuredGrid* grid,
                                QString* errMsg, int verticalAxis) const
{
    if (!grid || grid->GetNumberOfPoints() == 0 || grid->GetNumberOfCells() == 0)
    {
        if (errMsg)
            *errMsg = "网格为空，无法导出 IIIN.DAT";
        return false;
    }

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errMsg)
            *errMsg = QObject::tr("无法写入文件: %1").arg(fileName);
        return false;
    }

           // 先确保数组齐全
    ensureDefaultArrays(grid);

    auto* matypeArr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));
    auto* layerArr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("LAYER"));
    auto* tail1Arr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("TAIL1"));
    auto* tail2Arr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("TAIL2"));
    auto* ketypeArr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("KETYPE"));
    auto* connectivityPatternArr = vtkIntArray::SafeDownCast(
        grid->GetCellData()->GetArray(kIiinConnectivityPatternArrayName));
    auto* nodeIdArr = vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));

    const vtkIdType numnp = grid->GetNumberOfPoints();
    const vtkIdType totalCells = grid->GetNumberOfCells();

    double bounds[6];
    grid->GetBounds(bounds);
    double xmin = bounds[0], xmax = bounds[1], ymin = bounds[2], ymax = bounds[3], zmin = bounds[4],
        zmax = bounds[5];

    const int madim = (std::abs(zmax - zmin) < 1e-12) ? 2 : 3;
    const double am = 1.0;
    const int extra = 5;
    const double d1 = 0.0, d2 = 0.0, d3 = 0.0;
    const int iobject = objectTypeOfGrid(grid);
    const int ivertical = verticalAxisOfGrid(grid, normalizedVerticalAxis(verticalAxis));

           // 导出目标维度单元：
           // - 3D 模型导出体单元 + 线单元（锚杆/杆单元保留）
           // - 2D 模型导出面单元 + 线单元
    // MADIM describes the coordinates written below. A curved terrain surface
    // uses XYZ coordinates but still consists of 2D cells, not volume cells.
    int exportCellDim = 0;
    for (vtkIdType cid = 0; cid < totalCells; ++cid)
    {
        if (vtkCell* cell = grid->GetCell(cid))
            exportCellDim = std::max(exportCellDim, cell->GetCellDimension());
    }
    QVector<vtkIdType> exportCellIds;
    exportCellIds.reserve(static_cast<int>(totalCells));
    for (vtkIdType cid = 0; cid < totalCells; ++cid)
    {
        vtkCell* c = grid->GetCell(cid);
        if (!c)
            continue;
        const int dim = c->GetCellDimension();
        if (dim == exportCellDim || dim == 1)
            exportCellIds.push_back(cid);
    }
    if (exportCellIds.isEmpty())
    {
        if (errMsg)
            *errMsg = QObject::tr("未找到可导出的%1D单元").arg(exportCellDim);
        f.close();
        return false;
    }

    const vtkIdType ncell = static_cast<vtkIdType>(exportCellIds.size());

    // 当前求解器的 IIIN 对混合线性三维实体仍使用统一 8 节点槽位。
    // 只要存在 Hex/Wedge/Pyramid，或网格来自旧 KETYPE=6 退化实体，
    // 就在求解器导出边界统一采用该协议；CAE 内部拓扑不受影响。
    bool useSolverEightSlotSolidProtocol = false;
    if (madim >= 3)
    {
        for (vtkIdType cid : exportCellIds)
        {
            vtkCell* c = grid->GetCell(cid);
            if (!c)
                continue;

            const int storedKetype =
                (ketypeArr && cid < ketypeArr->GetNumberOfTuples()) ? ketypeArr->GetValue(cid) : 0;
            const int connectivityPattern =
                (connectivityPatternArr && cid < connectivityPatternArr->GetNumberOfTuples())
                    ? connectivityPatternArr->GetValue(cid)
                    : -1;

            const bool legacyEightSlotCell =
                storedKetype == 6 && connectivityPattern >= 0 &&
                c->GetCellDimension() == 3 &&
                (isLinear3DSolverSolid(c) || c->GetCellType() == VTK_POLYHEDRON);

            if (legacyEightSlotCell ||
                (isLinear3DSolverSolid(c) &&
                 (c->GetCellType() == VTK_HEXAHEDRON ||
                  c->GetCellType() == VTK_WEDGE ||
                  c->GetCellType() == VTK_PYRAMID)))
            {
                useSolverEightSlotSolidProtocol = true;
                break;
            }
        }
    }

           // 统计 manod（导出单元中的最大节点数）
    int manod = 0;
    QVector<int> cellKetype(static_cast<int>(ncell), 0);
    QVector<int> cellExpN(static_cast<int>(ncell), 0);

    int unsupported = 0;
    for (int oi = 0; oi < exportCellIds.size(); ++oi)
    {
        const vtkIdType cid = exportCellIds[oi];
        vtkCell* c = grid->GetCell(cid);
        if (!c)
        {
            unsupported++;
            continue;
        }
        const int storedKetype =
            (ketypeArr && cid < ketypeArr->GetNumberOfTuples()) ? ketypeArr->GetValue(cid) : 0;
        const int connectivityPattern =
            (connectivityPatternArr && cid < connectivityPatternArr->GetNumberOfTuples())
                ? connectivityPatternArr->GetValue(cid)
                : -1;

        const bool legacyKetype6Solid =
            storedKetype == 6 && connectivityPattern >= 0 &&
            c->GetCellDimension() == 3 &&
            (isLinear3DSolverSolid(c) || c->GetCellType() == VTK_POLYHEDRON);

        int k = 0, expN = 0;
        if (legacyKetype6Solid)
        {
            // 从旧 IIIN 恢复出的标准单元或 Polyhedron，写回求解器时都恢复
            // 原来的 KETYPE=6 + 8 槽位连接。
            k = 6;
            expN = 8;
        }
        else
        {
            if (!mapVtkCellToKetypeExt(c->GetCellType(),
                                       static_cast<int>(c->GetNumberOfPoints()), k, expN))
            {
                unsupported++;
                continue;
            }

            const bool solverEightSlotSolid =
                useSolverEightSlotSolidProtocol && isLinear3DSolverSolid(c);
            if (solverEightSlotSolid)
            {
                k = 6;
                expN = 8;
            }
            else if(c->GetCellType()==VTK_LINE&&storedKetype==21&&c->GetNumberOfPoints()==2)
            {
                // Beam and rod share VTK_LINE, but their native degrees of
                // freedom and section properties differ. Preserve imported beams.
                k=21;
                expN=2;
            }
            else if (c->GetCellType() == VTK_TRIANGLE && storedKetype == 3 &&
                     connectivityPattern >= 0)
            {
                k = 3;
                expN = 4;
            }
        }
        cellKetype[oi] = k;
        cellExpN[oi] = expN;
        manod = std::max(manod, expN);
    }

    if (unsupported > 0)
    {
        if (errMsg)
            *errMsg = QObject::tr("存在 %1 个不支持的单元类型，无法导出").arg(unsupported);
        f.close();
        return false;
    }

    QTextStream out(&f);
    auto iField = [](long long v, int w) -> QString
    {
        return QString("%1").arg(v, w);
    };
    auto rField = [](double v, int w, int p) -> QString
    {
        return QString::asprintf("%*.*f", w, p, v);
    };

           // header line 1
    out << iField(numnp, 10) << iField(madim, 10) << iField(ncell, 10) << iField(manod, 10)
        << iField(extra, 10) << rField(am, 16, 4) << rField(d1, 16, 4) << rField(d2, 16, 4)
        << rField(d3, 16, 4) << "\n";

           // header line 2
    out << iField(iobject, 8) << iField(ivertical, 8) << rField(xmin, 14, 4) << rField(xmax, 14, 4)
        << rField(ymin, 14, 4) << rField(ymax, 14, 4) << rField(zmin, 14, 4)
        << rField(zmax, 14, 4) << "\n";

           // nodes: 优先保留原 node_id，缺失时回退到 idx+1
    for (vtkIdType i = 0; i < numnp; ++i)
    {
        double p[3];
        grid->GetPoint(i, p);
        const int outNodeId =
            (nodeIdArr && i < nodeIdArr->GetNumberOfTuples()) ? nodeIdArr->GetValue(i)
                                                              : (static_cast<int>(i) + 1);

        out << rField(p[0], 15, 5) << rField(p[1], 15, 5);
        if (madim >= 3)
            out << rField(p[2], 15, 5);
        out << iField(outNodeId, 10) << "\n";
    }

           // elems: 优先保留原 elem_id 与 node_id（必要时按 ketype 做重排）
    for (int oi = 0; oi < exportCellIds.size(); ++oi)
    {
        const vtkIdType cid = exportCellIds[oi];
        vtkCell* c = grid->GetCell(cid);
        const int k = cellKetype[oi];
        const int expN = cellExpN[oi];

        int mat = matypeArr ? matypeArr->GetValue(cid) : kDefaultCalculationMatype;
        int lay = layerArr ? layerArr->GetValue(cid) : kDefaultConstructionLayer;
        // 杆单元在求解器协议中用负 LAYER 表示安装级次。
        // 这里同时兼容旧工程中已保存的正数杆单元层号。
        if (c->GetCellDimension() == 1 && k == 1)
            lay = -std::max(1, std::abs(lay));
        int t1 = tail1Arr ? tail1Arr->GetValue(cid) : 0;
        int t2 = tail2Arr ? tail2Arr->GetValue(cid) : 0;

               // 取 VTK 顺序的点
        QVector<int> vtkNodeIds;
        vtkNodeIds.reserve(expN);

        for (int i = 0; i < c->GetNumberOfPoints(); ++i)
        {
            vtkIdType pid = c->GetPointId(i);
            const int outNodeId =
                (nodeIdArr && pid >= 0 && pid < nodeIdArr->GetNumberOfTuples())
                    ? nodeIdArr->GetValue(pid)
                    : (static_cast<int>(pid) + 1);
            vtkNodeIds.push_back(outNodeId);
        }

        QVector<int> outConn;
        outConn.reserve(expN);
        const int connectivityPattern =
            (connectivityPatternArr && cid < connectivityPatternArr->GetNumberOfTuples())
                ? connectivityPatternArr->GetValue(cid)
                : -1;
        const int storedKetype =
            (ketypeArr && cid < ketypeArr->GetNumberOfTuples()) ? ketypeArr->GetValue(cid) : 0;
        const bool legacyKetype6Solid =
            storedKetype == 6 && connectivityPattern >= 0 &&
            c->GetCellDimension() == 3 &&
            (isLinear3DSolverSolid(c) || c->GetCellType() == VTK_POLYHEDRON);
        const bool solverEightSlotSolid =
            useSolverEightSlotSolidProtocol && isLinear3DSolverSolid(c);

        if (legacyKetype6Solid)
        {
            if (!rebuildLegacyEightSlotConnectivity(connectivityPattern, vtkNodeIds, outConn))
            {
                if (errMsg)
                    *errMsg = QObject::tr("旧 IIIN 8 槽位连接恢复失败，cell=%1").arg(cid);
                f.close();
                return false;
            }
            // 旧 IIIN round-trip：恢复原来的 8 槽位连接关系。
        }
        else if (solverEightSlotSolid)
        {
            // 新建/导入的标准混合实体只在 Solver IIIN 出口转换。
            // 纯 Tet 工程仍可走 KETYPE=35 / MANOD=4 的正常路径。
            if (c->GetCellType() == VTK_TETRA && vtkNodeIds.size() == 4 &&
                signedTetraVolume(grid, c) < 0.0)
            {
                std::swap(vtkNodeIds[1], vtkNodeIds[2]);
            }
            if (!buildSolverEightSlotConnectivity(c->GetCellType(), vtkNodeIds, outConn))
            {
                if (errMsg)
                    *errMsg = QObject::tr("无法生成求解器 8 槽位连接关系，cell=%1").arg(cid);
                f.close();
                return false;
            }
        }
        else if (k == 3 && vtkNodeIds.size() == 3 && connectivityPattern >= 0)
        {
            for (int i = 0; i < 4; ++i)
            {
                const int index = (connectivityPattern >> (i * 2)) & 0x3;
                outConn.push_back(
                    vtkNodeIds[std::min(index, static_cast<int>(vtkNodeIds.size()) - 1)]);
            }
        }
        else
        {
            while (vtkNodeIds.size() < expN)
                vtkNodeIds.push_back(0);

            const QVector<int> iiOrder = vtkToIiinOrder(k);
            if (!iiOrder.isEmpty() && iiOrder.size() == expN)
            {
                for (int idx : iiOrder)
                    outConn.push_back(vtkNodeIds[idx]);
            }
            else
            {
                outConn = vtkNodeIds;
            }
        }

               // IIIN.DAT 单元号按导出顺序从 1 开始连续编号
        if (k == 35 && c->GetCellType() == VTK_TETRA && outConn.size() >= 4 &&
            signedTetraVolume(grid, c) < 0.0)
        {
            std::swap(outConn[1], outConn[2]);
        }
        else if (k == 6 && c->GetCellType() == VTK_HEXAHEDRON && !legacyKetype6Solid)
        {
            orientHex8Positive(grid, c, outConn);
        }

        const int outElemId = oi + 1;
        out << iField(outElemId, 8);

        for (int j = 0; j < manod; ++j)
        {
            int nid = (j < outConn.size()) ? outConn[j] : 0;
            if (nid == 0 && k == 31 && !outConn.isEmpty())
                nid = outConn.last();
            out << iField(nid, 9);
        }

        out << iField(k, 5) << iField(mat, 4) << iField(lay, 4) << iField(t1, 4) << iField(t2, 4)
            << "\n";
    }

    out.flush();
    f.close();
    return true;
}

int MeshGridIO::verticalAxisOfGrid(vtkUnstructuredGrid* grid, int fallback)
{
    const int safeFallback = normalizedVerticalAxis(fallback);
    if (!grid || !grid->GetFieldData())
        return safeFallback;

    auto* axisArr = vtkIntArray::SafeDownCast(
        grid->GetFieldData()->GetArray(kIiinVerticalAxisArrayName));
    if (!axisArr || axisArr->GetNumberOfTuples() <= 0)
        return safeFallback;

    return normalizedVerticalAxis(axisArr->GetValue(0), safeFallback);
}

void MeshGridIO::setVerticalAxis(vtkUnstructuredGrid* grid, int axis)
{
    if (!grid)
        return;

    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName(kIiinVerticalAxisArrayName);
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(1);
    arr->SetValue(0, normalizedVerticalAxis(axis));
    grid->GetFieldData()->RemoveArray(kIiinVerticalAxisArrayName);
    grid->GetFieldData()->AddArray(arr);
}

int MeshGridIO::objectTypeOfGrid(vtkUnstructuredGrid* grid, int fallback)
{
    const int safeFallback = qBound(0, fallback, 8);
    if (!grid || !grid->GetFieldData())
        return safeFallback;

    auto* objectArr = vtkIntArray::SafeDownCast(
        grid->GetFieldData()->GetArray(kIiinObjectTypeArrayName));
    if (!objectArr || objectArr->GetNumberOfTuples() <= 0)
        return safeFallback;

    return qBound(0, objectArr->GetValue(0), 8);
}

void MeshGridIO::setObjectType(vtkUnstructuredGrid* grid, int objectType)
{
    if (!grid)
        return;

    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName(kIiinObjectTypeArrayName);
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(1);
    arr->SetValue(0, qBound(0, objectType, 8));
    grid->GetFieldData()->RemoveArray(kIiinObjectTypeArrayName);
    grid->GetFieldData()->AddArray(arr);
}

QString MeshGridIO::defaultIiinOutputPath(const QString& sourceFile)
{
    QFileInfo info(sourceFile);
    return QDir(info.path()).filePath(info.completeBaseName() + ".dat");
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::loadFlac3dGrid(const QString& fileName) const
{
    struct NodeData
    {
        double x, y, z;
    };

    QMap<int, NodeData> nodes;

    struct Zone
    {
        QString type;
        int id;
        QVector<int> verts;
    };
    QVector<Zone> zones;

           // 一个 zone 可能属于多个 group（同一 slot），所以用 QStringList
    QMap<int, QStringList> zoneGroups;

           // ========= 打开文件 =========
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        qWarning() << "cannot open flac3d file:" << fileName;
        return nullptr;
    }

           // ========= 一些解析工具 =========
    QSet<QString> seenSlots;
    quint32 fileMagic = 0;
    {
        QDataStream probe(&f);
        probe.setByteOrder(QDataStream::LittleEndian);
        probe >> fileMagic;
    }
    f.seek(0);

    constexpr quint32 kFlac3dBinaryMagic = 0x51F6E7E6u;
    const bool isBinaryGrid = fileMagic == kFlac3dBinaryMagic;
    if (isBinaryGrid)
    {
        auto parseBinary = [&]() -> bool
        {
            QDataStream stream(&f);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
            const qint64 fileSize = f.size();

            auto readCount = [&](qint32& count, qint64 maximum) -> bool
            {
                stream >> count;
                return stream.status() == QDataStream::Ok && count >= 0 && count <= maximum;
            };
            auto readString = [&](QString& value) -> bool
            {
                quint16 byteCount = 0;
                stream >> byteCount;
                if (stream.status() != QDataStream::Ok)
                    return false;
                QByteArray bytes;
                bytes.resize(static_cast<int>(byteCount));
                if (byteCount > 0 &&
                    stream.readRawData(bytes.data(), static_cast<int>(byteCount)) != byteCount)
                {
                    return false;
                }
                value = QString::fromUtf8(bytes);
                return true;
            };

            quint32 magic = 0;
            qint32 dimension = 0;
            qint32 nodeCount = 0;
            stream >> magic >> dimension;
            if (magic != kFlac3dBinaryMagic || dimension < 1 || dimension > 3 ||
                !readCount(nodeCount, fileSize / 28) || nodeCount == 0)
            {
                return false;
            }

            for (qint32 i = 0; i < nodeCount; ++i)
            {
                qint32 id = 0;
                NodeData node{};
                stream >> id >> node.x >> node.y >> node.z;
                if (stream.status() != QDataStream::Ok || id <= 0)
                    return false;
                nodes.insert(id, node);
            }

            qint32 zoneCount = 0;
            if (!readCount(zoneCount, fileSize / 12) || zoneCount == 0)
                return false;
            zones.reserve(zoneCount);
            for (qint32 i = 0; i < zoneCount; ++i)
            {
                qint32 id = 0;
                qint32 vertexCount = 0;
                stream >> id >> vertexCount;
                if (stream.status() != QDataStream::Ok || id <= 0 ||
                    (vertexCount != 4 && vertexCount != 5 && vertexCount != 6 &&
                     vertexCount != 8))
                {
                    return false;
                }

                Zone zone;
                zone.id = id;
                zone.type = vertexCount == 4 ? QStringLiteral("T4")
                                             : vertexCount == 5 ? QStringLiteral("P5")
                                             : vertexCount == 6 ? QStringLiteral("W6")
                                                                : QStringLiteral("B8");
                zone.verts.reserve(vertexCount);
                for (qint32 j = 0; j < vertexCount; ++j)
                {
                    qint32 nodeId = 0;
                    stream >> nodeId;
                    if (stream.status() != QDataStream::Ok || !nodes.contains(nodeId))
                        return false;
                    zone.verts.push_back(nodeId);
                }
                zones.push_back(zone);
            }

            auto readGroups = [&](bool zoneGroup) -> bool
            {
                qint32 groupCount = 0;
                if (!readCount(groupCount, fileSize / 4))
                    return false;
                for (qint32 i = 0; i < groupCount; ++i)
                {
                    QString name;
                    QString slot;
                    qint32 memberCount = 0;
                    if (!readString(name) || !readString(slot) ||
                        !readCount(memberCount, fileSize / 4))
                    {
                        return false;
                    }
                    seenSlots.insert(slot.isEmpty() ? QStringLiteral("<none>") : slot);
                    for (qint32 j = 0; j < memberCount; ++j)
                    {
                        qint32 memberId = 0;
                        stream >> memberId;
                        if (stream.status() != QDataStream::Ok)
                            return false;
                        if (zoneGroup && !name.isEmpty())
                        {
                            QStringList& groups = zoneGroups[memberId];
                            if (!groups.contains(name))
                                groups.push_back(name);
                        }
                    }
                }
                return true;
            };

            if (!readGroups(true))
                return false;

            qint32 faceCount = 0;
            if (!readCount(faceCount, fileSize / 12))
                return false;
            for (qint32 i = 0; i < faceCount; ++i)
            {
                qint32 faceId = 0;
                qint32 vertexCount = 0;
                stream >> faceId >> vertexCount;
                if (stream.status() != QDataStream::Ok || vertexCount < 3 || vertexCount > 64)
                    return false;
                for (qint32 j = 0; j < vertexCount; ++j)
                {
                    qint32 nodeId = 0;
                    stream >> nodeId;
                    if (stream.status() != QDataStream::Ok)
                        return false;
                }
            }

            return readGroups(false) && stream.status() == QDataStream::Ok;
        };

        if (!parseBinary())
        {
            qWarning() << "invalid binary flac3d grid:" << fileName;
            return nullptr;
        }
        f.close();
    }
    else
    {
        auto stripInlineComment = [](QString s) -> QString
    {
        // 处理： "305238 * FACES" 这种行内注释
        const int p = s.indexOf('*');
        if (p >= 0)
            s = s.left(p);
        return s.trimmed();
    };

    auto unquote = [](QString s) -> QString
    {
        s = s.trimmed();
        if (s.size() >= 2)
        {
            const QChar a = s.front();
            const QChar b = s.back();
            if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
                s = s.mid(1, s.size() - 2);
        }
        return s;
    };

    auto addGroup = [&](int zid, const QString& gname)
    {
        auto& lst = zoneGroups[zid];
        if (!lst.contains(gname))
            lst.push_back(gname);
    };

           // 收集所有 slot 的 ZGROUP，避免文件把分区写在非 "1" 槽位时被忽略
    const QString kWantedSlot;

    QString curGroupName;
    QString curSlot = kWantedSlot;
    bool inZGroupIds = false;
    auto parseZoneIdToken = [&](const QString& t)
    {
        // 支持： 12  或  12-20
        if (t.isEmpty())
            return;

        if (t.contains('-'))
        {
            const auto parts = t.split('-', kSkipEmptyParts);
            if (parts.size() != 2)
                return;

            bool ok1 = false, ok2 = false;
            int a = parts[0].toInt(&ok1);
            int b = parts[1].toInt(&ok2);
            if (!ok1 || !ok2)
                return;
            if (a > b)
                std::swap(a, b);

            for (int zid = a; zid <= b; ++zid)
            {
                if (!kWantedSlot.isEmpty() && curSlot != kWantedSlot)
                    continue;
                addGroup(zid, curGroupName);
            }
        }
        else
        {
            bool ok = false;
            int zid = t.toInt(&ok);
            if (!ok)
                return;
            if (!kWantedSlot.isEmpty() && curSlot != kWantedSlot)
                return;
            addGroup(zid, curGroupName);
        }
    };

           // ========= 第一遍：读 G/Z/ZGROUP =========
    QTextStream in(&f);
    while (!in.atEnd())
    {
        QString line = stripInlineComment(in.readLine());
        if (line.isEmpty())
            continue;

               // 若正在读取 ZGROUP 的 zone-id 列表（多行）
        if (inZGroupIds)
        {
            // 纯数字/范围行通常以数字开头
            if (!line.isEmpty() && line.front().isDigit())
            {
                line.replace(',', ' ');
                const QStringList tok = line.split(QRegularExpression("\\s+"), kSkipEmptyParts);
                for (const QString& t : tok)
                    parseZoneIdToken(t);
                continue;
            }
            else
            {
                // 遇到下一段命令（如 ZGROUP / G / Z / FACE...），退出列表模式
                inZGroupIds = false;
                // 不 continue，让这行走正常解析
            }
        }

        line.replace(',', ' ');
        const QStringList tok = line.split(QRegularExpression("\\s+"), kSkipEmptyParts);
        if (tok.isEmpty())
            continue;

        const QString keyword = tok[0].toUpper();

        if (keyword == "G")
        {
            if (tok.size() < 5)
                continue;
            const int id = tok[1].toInt();
            NodeData n;
            n.x = tok[2].toDouble();
            n.y = tok[3].toDouble();
            n.z = tok[4].toDouble();
            nodes[id] = n;
        }
        else if (keyword == "Z")
        {
            if (tok.size() < 4)
                continue;

            Zone z;
            z.type = tok[1];
            z.id = tok[2].toInt();

            if (z.type == "B8")
            {
                if (tok.size() < 11)
                    continue;
                for (int i = 3; i < 11; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else if (z.type == "W6")
            {
                if (tok.size() < 9)
                    continue;
                for (int i = 3; i < 9; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else if (z.type == "T4")
            {
                if (tok.size() < 7)
                    continue;
                for (int i = 3; i < 7; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else if (z.type == "P5")
            {
                if (tok.size() < 8)
                    continue;
                for (int i = 3; i < 8; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else
            {
                continue;
            }

            zones.push_back(z);
        }
        else if (keyword == "ZGROUP")
        {
            // 典型： ZGROUP "BIANPO" SLOT "1"
            if (tok.size() < 2)
                continue;

            curGroupName = unquote(tok[1]);
            curSlot.clear(); // 默认：未指定 slot

                   // 找 SLOT
            for (int i = 2; i + 1 < tok.size(); ++i)
            {
                if (tok[i].compare("SLOT", Qt::CaseInsensitive) == 0)
                {
                    curSlot = unquote(tok[i + 1]);
                    break;
                }
            }

            seenSlots.insert(curSlot.isEmpty() ? QString("<none>") : curSlot);

            qInfo() << "[Flac3D] parse ZGROUP name=" << curGroupName
                    << " slot=" << (curSlot.isEmpty() ? QString("<none>") : curSlot);

                   // 同行如果也带了 zone ids，也顺手解析（容错）
            for (int i = 2; i < tok.size(); ++i)
            {
                if (tok[i].compare("SLOT", Qt::CaseInsensitive) == 0)
                {
                    ++i;
                    continue;
                }
                if (!tok[i].isEmpty() && tok[i].front().isDigit())
                    parseZoneIdToken(tok[i]);
            }

                   // 进入“多行 zone id 列表”模式
            inZGroupIds = true;
        }
        // 其它段落（FACE GROUPS 等）这里暂不处理
    }
    f.close();
    }

    if (nodes.isEmpty() || zones.isEmpty())
    {
        qWarning() << "no nodes or zones parsed from flac3d file";
        return nullptr;
    }

           // ========= 建 vtkPoints =========
    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    QMap<int, vtkIdType> idMap;
    vtkIdType nextId = 0;

    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        const NodeData& p = it.value();
        pts->InsertNextPoint(p.x, p.y, p.z);
        idMap[it.key()] = nextId++;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(pts);
    grid->Allocate(zones.size());

    auto chooseMainAxis = [](const std::vector<double>& xs, const std::vector<double>& ys,
                             const std::vector<double>& zs) -> int
    {
        double dx =
            *std::max_element(xs.begin(), xs.end()) - *std::min_element(xs.begin(), xs.end());
        double dy =
            *std::max_element(ys.begin(), ys.end()) - *std::min_element(ys.begin(), ys.end());
        double dz =
            *std::max_element(zs.begin(), zs.end()) - *std::min_element(zs.begin(), zs.end());
        if (dx >= dy && dx >= dz)
            return 0;
        if (dy >= dx && dy >= dz)
            return 1;
        return 2;
    };

    auto sortByAngleInPlane = [](QVector<int>& idx, int axis, const std::vector<double>& xs,
                                 const std::vector<double>& ys, const std::vector<double>& zs)
    {
        double cx = 0, cy = 0, cz = 0;
        for (int i : idx)
        {
            cx += xs[i];
            cy += ys[i];
            cz += zs[i];
        }
        cx /= idx.size();
        cy /= idx.size();
        cz /= idx.size();

        struct Ang
        {
            int id;
            double ang;
        };
        QVector<Ang> arr;
        arr.reserve(idx.size());

        for (int i : idx)
        {
            double u, v;
            if (axis == 0)
            {
                u = ys[i] - cy;
                v = zs[i] - cz;
            }
            else if (axis == 1)
            {
                u = xs[i] - cx;
                v = zs[i] - cz;
            }
            else
            {
                u = xs[i] - cx;
                v = ys[i] - cy;
            }
            arr.push_back({i, std::atan2(v, u)});
        }

        std::sort(arr.begin(), arr.end(),
                  [](const Ang& a, const Ang& b)
                  {
                      return a.ang < b.ang;
                  });

        for (int k = 0; k < idx.size(); ++k)
            idx[k] = arr[k].id;
    };

    auto addHex = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 8)
            return false;

        // FLAC3D 的 B8 槽位顺序是文件格式的一部分，与单元在空间中的朝向无关。
        // 不能按坐标最长轴重新猜测顶/底面，否则弧形、径向网格中相邻单元会
        // 对同一个共享面生成不同的 VTK 拓扑，表现为折叠面和尖片。
        constexpr int flacToVtk[8] = {5, 7, 6, 3, 2, 4, 1, 0};
        auto hex = vtkSmartPointer<vtkHexahedron>::New();
        for (int vtkCorner = 0; vtkCorner < 8; ++vtkCorner)
        {
            const int sourceCorner = flacToVtk[vtkCorner];
            if (!idMap.contains(ids[sourceCorner]))
                return false;
            hex->GetPointIds()->SetId(vtkCorner, idMap.value(ids[sourceCorner]));
        }
        grid->InsertNextCell(hex->GetCellType(), hex->GetPointIds());
        return true;
    };

    auto addW6 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 6)
            return false;

        // W6 同样使用 FLAC3D 的标准槽位连接。该顺序保持两个三角面及三组
        // 对应侧边，并使转换后的 vtkWedge 保持正方向。
        constexpr int flacToVtk[6] = {0, 3, 1, 2, 5, 4};
        auto wedge = vtkSmartPointer<vtkWedge>::New();
        for (int vtkCorner = 0; vtkCorner < 6; ++vtkCorner)
        {
            const int sourceCorner = flacToVtk[vtkCorner];
            if (!idMap.contains(ids[sourceCorner]))
                return false;
            wedge->GetPointIds()->SetId(vtkCorner, idMap.value(ids[sourceCorner]));
        }
        grid->InsertNextCell(wedge->GetCellType(), wedge->GetPointIds());
        return true;
    };

    auto addT4 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 4)
            return false;

        auto tet = vtkSmartPointer<vtkTetra>::New();
        for (int i = 0; i < 4; ++i)
            tet->GetPointIds()->SetId(i, idMap[ids[i]]);

        grid->InsertNextCell(tet->GetCellType(), tet->GetPointIds());
        return true;
    };

    auto addP5 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 5)
            return false;

        struct P
        {
            vtkIdType nid;
            double x, y, z;
        };
        P p[5];

        std::vector<double> xs(5), ys(5), zs(5);
        for (int i = 0; i < 5; ++i)
        {
            const NodeData& n = nodes[ids[i]];
            p[i].nid = idMap[ids[i]];
            p[i].x = n.x;
            p[i].y = n.y;
            p[i].z = n.z;
            xs[i] = n.x;
            ys[i] = n.y;
            zs[i] = n.z;
        }

        int axis = chooseMainAxis(xs, ys, zs);

        int apex = 0;
        double best = -1e300;
        for (int i = 0; i < 5; ++i)
        {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v > best)
            {
                best = v;
                apex = i;
            }
        }

        QVector<int> baseIdx;
        baseIdx.reserve(4);
        for (int i = 0; i < 5; ++i)
            if (i != apex)
                baseIdx.push_back(i);

        sortByAngleInPlane(baseIdx, axis, xs, ys, zs);

        auto pyr = vtkSmartPointer<vtkPyramid>::New();
        for (int k = 0; k < 4; ++k)
            pyr->GetPointIds()->SetId(k, p[baseIdx[k]].nid);
        pyr->GetPointIds()->SetId(4, p[apex].nid);

        grid->InsertNextCell(pyr->GetCellType(), pyr->GetPointIds());
        return true;
    };

    qInfo() << "[Flac3D] all seen slots=" << QStringList(seenSlots.values()).join(',');

           // ========= 生成 cell 数据 + 组->cell 列表 =========
    QVector<int> cellZoneIds;
    QVector<QString> cellGroupNames;
    cellZoneIds.reserve(zones.size());
    cellGroupNames.reserve(zones.size());

           // groupName -> cellId list
    QMap<QString, QVector<vtkIdType>> groupToCells;

    for (const Zone& z : zones)
    {
        bool ok = false;
        if (z.type == "B8")
            ok = addHex(z.verts);
        else if (z.type == "W6")
            ok = addW6(z.verts);
        else if (z.type == "T4")
            ok = addT4(z.verts);
        else if (z.type == "P5")
            ok = addP5(z.verts);

        if (ok)
        {
            const vtkIdType cellId = grid->GetNumberOfCells() - 1;

            cellZoneIds.push_back(z.id);

            const QStringList groups = zoneGroups.value(z.id);
            const QString joined = groups.join(';'); // 多组时用 ; 拼起来
            cellGroupNames.push_back(joined);

            for (const QString& g : groups)
            {
                if (!g.isEmpty())
                    groupToCells[g].push_back(cellId);
            }
        }
    }

           // ========= 填 ZoneId / GroupName 到 CellData =========
    auto zoneIdArr = vtkSmartPointer<vtkIntArray>::New();
    zoneIdArr->SetName("ZoneId");
    zoneIdArr->SetNumberOfComponents(1);
    zoneIdArr->SetNumberOfTuples(cellZoneIds.size());

    auto groupArr = vtkSmartPointer<vtkStringArray>::New();
    groupArr->SetName("GroupName");
    groupArr->SetNumberOfValues(cellGroupNames.size());

    for (int i = 0; i < cellZoneIds.size(); ++i)
    {
        zoneIdArr->SetValue(i, cellZoneIds[i]);
        groupArr->SetValue(i,
                           cellGroupNames[i].toStdString()); // ✅ 你原来漏了这句
    }

    grid->GetCellData()->AddArray(zoneIdArr);
    grid->GetCellData()->AddArray(groupArr);

           // ========= 把“每个组有哪些 cellId”塞进 FieldData（可选但你要的就是这个）
           // ========= 采用打包结构：
           //   ZGroupNames[i]    = 第 i 个组名
           //   ZGroupOffsets[i]  = 该组的 cellId 起始下标
           //   ZGroupOffsets[i+1]= 结束下标（不含）
           //   ZGroupCellIds[off..end-1] = 该组的所有 cellId
    auto zGroupNames = vtkSmartPointer<vtkStringArray>::New();
    auto zGroupOffsets = vtkSmartPointer<vtkIdTypeArray>::New();
    auto zGroupCellIds = vtkSmartPointer<vtkIdTypeArray>::New();

    zGroupNames->SetName("ZGroupNames");
    zGroupOffsets->SetName("ZGroupOffsets");
    zGroupCellIds->SetName("ZGroupCellIds");

    vtkIdType off = 0;
    zGroupOffsets->InsertNextValue(off);

    for (auto it = groupToCells.begin(); it != groupToCells.end(); ++it)
    {
        const QString& gname = it.key();
        const QVector<vtkIdType>& cells = it.value();

        zGroupNames->InsertNextValue(gname.toStdString());
        for (vtkIdType cid : cells)
            zGroupCellIds->InsertNextValue(cid);

        off += static_cast<vtkIdType>(cells.size());
        zGroupOffsets->InsertNextValue(off);
    }

    grid->GetFieldData()->AddArray(zGroupNames);
    grid->GetFieldData()->AddArray(zGroupOffsets);
    grid->GetFieldData()->AddArray(zGroupCellIds);

    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::loadAbaqusInpGrid(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "Cannot open inp file:" << fileName;
        return nullptr;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif

    QMap<int, QVector3D> nodeMap;

    struct ElemBlock
    {
        int id = 0;
        QString type;
        QString elset;
        QVector<int> conn;
    };
    QVector<ElemBlock> elems;
    QHash<QString, QSet<int>> elsetToElemIds;
    QHash<QString, QSet<int>> nsetToNodeIds;

    auto nodesPerType = [](const QString& tUpper) -> int
    {
        if (tUpper.startsWith("C3D10"))
            return 10;
        if (tUpper.startsWith("C3D8"))
            return 8;
        if (tUpper.startsWith("C3D4"))
            return 4;
        if (tUpper.startsWith("C3D6"))
            return 6;
        if (tUpper.startsWith("C3D5"))
            return 5;
        if (tUpper.startsWith("S4"))
            return 4;
        if (tUpper.startsWith("S3"))
            return 3;
        if (tUpper.startsWith("T3D2") || tUpper.startsWith("B31"))
            return 2;
        if (tUpper.startsWith("T3D3"))
            return 3;
        return -1;
    };

    auto splitCsv = [](const QString& s)
    {
        return s.split(',', Qt::SkipEmptyParts);
    };

    auto appendSetIds = [](const QStringList& tok, bool generate, QSet<int>& ids)
    {
        if (generate && tok.size() >= 2)
        {
            bool okStart = false;
            bool okEnd = false;
            bool okStep = true;
            const int start = tok.value(0).trimmed().toInt(&okStart);
            const int end = tok.value(1).trimmed().toInt(&okEnd);
            int step = tok.size() >= 3 ? tok.value(2).trimmed().toInt(&okStep) : 1;
            if (okStart && okEnd && okStep && step != 0)
            {
                if (start <= end && step > 0)
                {
                    for (int id = start; id <= end; id += step)
                        ids.insert(id);
                    return;
                }
                if (start >= end && step < 0)
                {
                    for (int id = start; id >= end; id += step)
                        ids.insert(id);
                    return;
                }
            }
        }

        for (const QString& t : tok)
        {
            bool ok = false;
            const int id = t.trimmed().toInt(&ok);
            if (ok)
                ids.insert(id);
        }
    };

    bool inNode = false;
    bool inElem = false;
    bool inElset = false;
    bool inNset = false;
    QString curElemTypeUpper;
    QString curElemElset;
    QString curSetName;
    bool curSetGenerate = false;
    int expectedConn = -1;

    bool elemContinuing = false;
    int curElemId = 0;
    QVector<int> curConn;

    while (!in.atEnd())
    {
        const QString rawLine = in.readLine();
        QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith("**"))
            continue;

        if (line.startsWith('*'))
        {
            inNode = false;
            inElem = false;
            inElset = false;
            inNset = false;
            elemContinuing = false;
            curElemId = 0;
            curConn.clear();
            expectedConn = -1;
            curElemTypeUpper.clear();
            curElemElset.clear();
            curSetName.clear();
            curSetGenerate = false;

            QString upper = line.toUpper();
            auto parseHeaderValue = [&](const QString& keyUpper) -> QString
            {
                QRegularExpression re(QString(",\\s*%1\\s*=\\s*([^,\\r\\n]+)")
                                          .arg(QRegularExpression::escape(keyUpper)),
                                          QRegularExpression::CaseInsensitiveOption);
                auto m = re.match(rawLine);
                if (!m.hasMatch())
                    return QString();
                return m.captured(1).trimmed();
            };

            if (upper.startsWith("*NODE"))
            {
                inNode = true;
            }
            else if (upper.startsWith("*ELEMENT"))
            {
                inElem = true;
                curElemElset = parseHeaderValue("ELSET");

                curElemTypeUpper = parseHeaderValue("TYPE").toUpper();
                if (!curElemTypeUpper.isEmpty())
                {
                    curElemTypeUpper = curElemTypeUpper.trimmed();
                    expectedConn = nodesPerType(curElemTypeUpper);
                    if (expectedConn < 0)
                    {
                        qWarning() << "Unsupported element type:" << curElemTypeUpper;
                    }
                }
            }
            else if (upper.startsWith("*ELSET"))
            {
                inElset = true;
                curSetName = parseHeaderValue("ELSET");
                curSetGenerate = upper.contains("GENERATE");
            }
            else if (upper.startsWith("*NSET"))
            {
                inNset = true;
                curSetName = parseHeaderValue("NSET");
                curSetGenerate = upper.contains("GENERATE");
            }
            continue;
        }

        if (inNode)
        {
            QStringList tok = splitCsv(line);
            if (tok.size() >= 4)
            {
                int id = tok[0].toInt();
                double x = tok[1].toDouble();
                double y = tok[2].toDouble();
                double z = tok[3].toDouble();
                nodeMap[id] = QVector3D(x, y, z);
            }
            continue;
        }

        if (inElem && expectedConn > 0)
        {
            QStringList tok = splitCsv(line);
            if (tok.isEmpty())
                continue;

            int start = 0;
            if (!elemContinuing)
            {
                if (tok.size() < 2)
                    continue;
                curElemId = tok[0].toInt();
                start = 1;
            }

            for (int i = start; i < tok.size(); ++i)
            {
                curConn.push_back(tok[i].toInt());
            }

            if (curConn.size() < expectedConn)
            {
                elemContinuing = true;
                continue;
            }

            if (curConn.size() > expectedConn)
            {
                qWarning() << "Element has extra nodes, type=" << curElemTypeUpper
                           << " got=" << curConn.size() << " expect=" << expectedConn
                           << " truncating.";
                curConn.resize(expectedConn);
            }

            ElemBlock e;
            e.id = curElemId;
            e.type = curElemTypeUpper;
            e.elset = curElemElset.trimmed();
            e.conn = curConn;
            elems.push_back(e);
            if (!e.elset.isEmpty())
                elsetToElemIds[e.elset].insert(e.id);

            elemContinuing = false;
            curElemId = 0;
            curConn.clear();
            continue;
        }

        if (inElset && !curSetName.isEmpty())
        {
            QStringList tok = splitCsv(line);
            appendSetIds(tok, curSetGenerate, elsetToElemIds[curSetName]);
            continue;
        }

        if (inNset && !curSetName.isEmpty())
        {
            QStringList tok = splitCsv(line);
            appendSetIds(tok, curSetGenerate, nsetToNodeIds[curSetName]);
            continue;
        }
    }

    file.close();

    if (nodeMap.isEmpty() || elems.isEmpty())
    {
        qWarning() << "inp file has no nodes or elements:" << fileName;
        return nullptr;
    }

    vtkNew<vtkPoints> pts;
    pts->SetDataTypeToDouble();

    QMap<int, vtkIdType> idMap;
    vtkIdType idx = 0;
    for (auto it = nodeMap.begin(); it != nodeMap.end(); ++it)
    {
        const QVector3D& p = it.value();
        pts->InsertNextPoint(p.x(), p.y(), p.z());
        idMap[it.key()] = idx++;
        m_mshPickMap.nodeIdToPointId[it.key()] = static_cast<int>(idx - 1);
        m_mshPickMap.pointIdToNodeId[static_cast<int>(idx - 1)] = it.key();
    }

    vtkNew<vtkUnstructuredGrid> grid;
    grid->SetPoints(pts);

    auto elementDimension = [](const QString& type) -> int
    {
        if (type.startsWith("C3D"))
            return 3;
        if (type.startsWith("S"))
            return 2;
        if (type.startsWith("T3D") || type.startsWith("B31"))
            return 1;
        return 0;
    };

    int domainDimension = 0;
    QHash<int, int> elementDimensionById;
    for (const ElemBlock& e : std::as_const(elems))
    {
        const int dim = elementDimension(e.type);
        elementDimensionById.insert(e.id, dim);
        domainDimension = qMax(domainDimension, dim);
    }
    if (domainDimension <= 0)
        return nullptr;
    m_mshPickMap.meshDim = domainDimension;

    QHash<int, QVector<vtkIdType>> elemIdToCellIds;
    auto mapPointIds = [&](const QVector<int>& conn, QVector<vtkIdType>& pointIds) -> bool
    {
        pointIds.clear();
        pointIds.reserve(conn.size());
        for (int nodeId : conn)
        {
            if (!idMap.contains(nodeId))
                return false;
            pointIds.push_back(idMap.value(nodeId));
        }
        return true;
    };

    auto insertElement = [&](const ElemBlock& e) -> vtkIdType
    {
        QVector<vtkIdType> pointIds;
        if (!mapPointIds(e.conn, pointIds))
        {
            qWarning() << "Skip element: has undefined node id, type=" << e.type;
            return -1;
        }

        int vtkType = VTK_EMPTY_CELL;
        int insertedPointCount = pointIds.size();
        if (e.type.startsWith("C3D8") && pointIds.size() == 8)
            vtkType = VTK_HEXAHEDRON;
        else if (e.type.startsWith("C3D4") && pointIds.size() == 4)
            vtkType = VTK_TETRA;
        else if (e.type.startsWith("C3D10") && pointIds.size() == 10)
        {
            vtkType = VTK_TETRA;
            insertedPointCount = 4;
        }
        else if (e.type.startsWith("C3D6") && pointIds.size() == 6)
            vtkType = VTK_WEDGE;
        else if (e.type.startsWith("C3D5") && pointIds.size() == 5)
            vtkType = VTK_PYRAMID;
        else if (e.type.startsWith("S4") && pointIds.size() == 4)
            vtkType = VTK_QUAD;
        else if (e.type.startsWith("S3") && pointIds.size() == 3)
            vtkType = VTK_TRIANGLE;
        else if ((e.type.startsWith("T3D2") || e.type.startsWith("B31")) &&
                 pointIds.size() == 2)
            vtkType = VTK_LINE;
        else if (e.type.startsWith("T3D3") && pointIds.size() == 3)
            vtkType = VTK_QUADRATIC_EDGE;

        if (vtkType == VTK_EMPTY_CELL)
            return -1;
        const vtkIdType cellId =
            grid->InsertNextCell(vtkType, insertedPointCount, pointIds.constData());
        elemIdToCellIds[e.id].push_back(cellId);
        return cellId;
    };

    // 先插入最高维计算域，随后才能把 S4/S3 等面单元识别为实体边界。
    for (const ElemBlock& e : std::as_const(elems))
    {
        if (elementDimensionById.value(e.id) == domainDimension)
            insertElement(e);
    }

    auto pointSetKey = [](QVector<vtkIdType> pointIds) -> QString
    {
        std::sort(pointIds.begin(), pointIds.end());
        QString key;
        for (vtkIdType pointId : std::as_const(pointIds))
        {
            key += QString::number(pointId);
            key += QLatin1Char(',');
        }
        return key;
    };

    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    pts->GetBounds(bounds);
    const double modelSpan = qMax(qMax(bounds[1] - bounds[0], bounds[3] - bounds[2]),
                                  bounds[5] - bounds[4]);
    const double coordinateTolerance = qMax(1.0e-8, modelSpan * 1.0e-7);
    auto coordinateSetKey = [&](const QVector<vtkIdType>& pointIds) -> QString
    {
        QStringList coordinates;
        coordinates.reserve(pointIds.size());
        for (vtkIdType pointId : pointIds)
        {
            double p[3] = {0.0, 0.0, 0.0};
            pts->GetPoint(pointId, p);
            coordinates.push_back(
                QStringLiteral("%1:%2:%3")
                    .arg(qRound64(p[0] / coordinateTolerance))
                    .arg(qRound64(p[1] / coordinateTolerance))
                    .arg(qRound64(p[2] / coordinateTolerance)));
        }
        std::sort(coordinates.begin(), coordinates.end());
        return coordinates.join(QLatin1Char(';'));
    };

    struct BoundaryRef
    {
        qint64 entityKey = -1;
        QVector<vtkIdType> pointIds;
    };
    QHash<QString, BoundaryRef> domainBoundaryByNodeIds;
    QHash<QString, BoundaryRef> domainBoundaryByCoordinates;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        const int boundaryCount = !cell ? 0 : domainDimension == 3 ? cell->GetNumberOfFaces()
                                                                    : cell->GetNumberOfEdges();
        for (int boundaryId = 0; boundaryId < boundaryCount; ++boundaryId)
        {
            vtkCell* boundary = domainDimension == 3 ? cell->GetFace(boundaryId)
                                                      : cell->GetEdge(boundaryId);
            if (!boundary)
                continue;
            QVector<vtkIdType> boundaryPointIds;
            boundaryPointIds.reserve(boundary->GetNumberOfPoints());
            for (vtkIdType i = 0; i < boundary->GetNumberOfPoints(); ++i)
                boundaryPointIds.push_back(boundary->GetPointId(i));
            BoundaryRef ref;
            ref.entityKey = (qint64(cellId) << 4) | (qint64(boundaryId) & 0xF);
            ref.pointIds = boundaryPointIds;
            domainBoundaryByNodeIds.insert(pointSetKey(boundaryPointIds), ref);
            domainBoundaryByCoordinates.insert(coordinateSetKey(boundaryPointIds), ref);
        }
    }

    struct BoundaryElement
    {
        int dimension = 0;
        qint64 entityKey = -1;
        QVector<vtkIdType> pointIds;
    };
    QHash<int, BoundaryElement> boundaryElementById;
    for (const ElemBlock& e : std::as_const(elems))
    {
        const int dim = elementDimensionById.value(e.id);
        if (dim == domainDimension)
            continue;

        QVector<vtkIdType> pointIds;
        if (!mapPointIds(e.conn, pointIds))
        {
            qWarning() << "Skip element: has undefined node id, type=" << e.type;
            continue;
        }

        BoundaryRef boundaryRef;
        bool isDomainBoundary = false;
        if (dim == domainDimension - 1)
        {
            const auto nodeIt = domainBoundaryByNodeIds.constFind(pointSetKey(pointIds));
            if (nodeIt != domainBoundaryByNodeIds.constEnd())
            {
                boundaryRef = nodeIt.value();
                isDomainBoundary = true;
            }
            else
            {
                const auto coordinateIt =
                    domainBoundaryByCoordinates.constFind(coordinateSetKey(pointIds));
                if (coordinateIt != domainBoundaryByCoordinates.constEnd())
                {
                    boundaryRef = coordinateIt.value();
                    isDomainBoundary = true;
                }
            }
        }

        // 混合维度文件中的低维单元由单元类型明确表示边界实体，不加入
        // 最高维计算单元数组。匹配仅用于把边界关联回计算域的面或边。
        boundaryElementById.insert(
            e.id,
            {dim,
             isDomainBoundary ? boundaryRef.entityKey : -1,
             isDomainBoundary ? boundaryRef.pointIds : pointIds});
    }

    int nextPhysId = kDefaultCalculationMatype;
    QHash<QString, int> groupToPhysId;
    auto ensurePhysId = [&](const QString& groupName, int dim) -> int
    {
        const QString g = groupName.trimmed();
        if (g.isEmpty())
            return 0;
        const QString groupKey = QStringLiteral("%1:%2").arg(dim).arg(g);
        if (groupToPhysId.contains(groupKey))
            return groupToPhysId.value(groupKey);
        while (m_mshPickMap.physIdToName.contains(nextPhysId))
            ++nextPhysId;
        const int physId = nextPhysId++;
        groupToPhysId.insert(groupKey, physId);
        m_mshPickMap.physIdToName.insert(physId, g);
        m_mshPickMap.physIdToDim.insert(physId, dim);
        return physId;
    };

    for (auto it = elsetToElemIds.constBegin(); it != elsetToElemIds.constEnd(); ++it)
    {
        const QString& setName = it.key();
        struct GroupMembers
        {
            QSet<int> pointIds;
            QSet<qint64> cellIds;
            QSet<qint64> edgeKeys;
            QSet<qint64> faceKeys;
        };
        QMap<int, GroupMembers> membersByDimension;

        for (int elemId : it.value())
        {
            const auto boundaryIt = boundaryElementById.constFind(elemId);
            if (boundaryIt != boundaryElementById.constEnd())
            {
                const BoundaryElement& boundary = boundaryIt.value();
                GroupMembers& members = membersByDimension[boundary.dimension];
                for (vtkIdType pointId : boundary.pointIds)
                    members.pointIds.insert(static_cast<int>(pointId));
                if (boundary.entityKey >= 0 && boundary.dimension == 2)
                    members.faceKeys.insert(boundary.entityKey);
                else if (boundary.entityKey >= 0 && boundary.dimension == 1)
                    members.edgeKeys.insert(boundary.entityKey);
                continue;
            }

            const auto cids = elemIdToCellIds.value(elemId);
            for (vtkIdType cid : cids)
            {
                if (cid < 0 || cid >= grid->GetNumberOfCells())
                    continue;
                vtkCell* c = grid->GetCell(cid);
                if (!c)
                    continue;
                GroupMembers& members = membersByDimension[c->GetCellDimension()];
                members.cellIds.insert(static_cast<qint64>(cid));
                for (vtkIdType k = 0; k < c->GetNumberOfPoints(); ++k)
                    members.pointIds.insert(static_cast<int>(c->GetPointId(k)));
            }
        }

        for (auto dimIt = membersByDimension.begin(); dimIt != membersByDimension.end(); ++dimIt)
        {
            const int dim = dimIt.key();
            const GroupMembers& members = dimIt.value();
            if (members.cellIds.isEmpty() && members.pointIds.isEmpty())
                continue;
            const int physId = ensurePhysId(setName, dim);
            if (physId <= 0)
                continue;

            MeshTagSet& set = ensurePhysicalTagSet(m_mshPickMap, physId, dim,
                                                   QStringLiteral("AbaqusInpGrid"));
            set.name = setName;
            set.cellIds = members.cellIds.values().toVector();
            set.pointIds.clear();
            set.pointIds.reserve(members.pointIds.size());
            for (int pointId : members.pointIds)
                set.pointIds.push_back(static_cast<MeshIdType>(pointId));
            set.edgeKeys = members.edgeKeys.values().toVector();
            set.faceKeys = members.faceKeys.values().toVector();
            if (dim == domainDimension)
            {
                for (MeshIdType cellId : std::as_const(set.cellIds))
                    m_mshPickMap.cellIdToDomainTags[cellId].push_back(set.key);
            }
        }
    }

    for (auto it = nsetToNodeIds.constBegin(); it != nsetToNodeIds.constEnd(); ++it)
    {
        QVector<int> pids;
        pids.reserve(it.value().size());
        for (int nodeId : it.value())
        {
            auto pidIt = idMap.constFind(nodeId);
            if (pidIt == idMap.constEnd())
                continue;
            pids.push_back(static_cast<int>(pidIt.value()));
        }
        if (pids.isEmpty())
            continue;
        const int physId = ensurePhysId(it.key(), 0);
        if (physId <= 0)
            continue;
        setPhysicalBoundaryPoints(m_mshPickMap, physId, 0, pids, QStringLiteral("AbaqusInpGrid"));
    }

    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("AbaqusInpGrid"));
    qInfo() << "[Abaqus] imported nodes=" << nodeMap.size()
            << "cells=" << grid->GetNumberOfCells()
            << "boundary-elements=" << boundaryElementById.size();
    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::loadAnsysInpGrid(const QString& fileName,
                                                                  QString* errMsg)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errMsg)
            *errMsg = QObject::tr("无法打开 ANSYS 网格文件: %1").arg(fileName);
        qWarning() << "Cannot open ANSYS inp file:" << fileName;
        return nullptr;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif

    struct AnsysElem
    {
        int id = 0;
        int mat = kDefaultCalculationMatype;
        int type = 0;
        QVector<int> conn;
    };

    QMap<int, QVector3D> nodeMap;
    QVector<AnsysElem> elems;
    QVector<int> pendingElemFields;

    bool inNodeBlock = false;
    bool inElemBlock = false;
    bool skipBlockFormatLine = false;

    auto splitWhitespace = [](const QString& s)
    {
        return s.split(kWhitespaceRe, kSkipEmptyParts);
    };

    auto parsePendingElements = [&]()
    {
        while (pendingElemFields.size() >= 11)
        {
            const int nodeCount = pendingElemFields.value(8);
            if (nodeCount <= 0 || nodeCount > 64)
            {
                pendingElemFields.remove(0);
                continue;
            }

            const int required = 11 + nodeCount;
            if (pendingElemFields.size() < required)
                return;

            AnsysElem elem;
            elem.mat = pendingElemFields.value(0);
            elem.type = pendingElemFields.value(1);
            elem.id = pendingElemFields.value(10);
            for (int i = 0; i < nodeCount; ++i)
                elem.conn.push_back(pendingElemFields.value(11 + i));
            elems.push_back(elem);
            pendingElemFields.remove(0, required);
        }
    };

    while (!in.atEnd())
    {
        const QString rawLine = in.readLine();
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        const QString upper = line.toUpper();
        if (upper.startsWith(QStringLiteral("!!")) || upper.startsWith(QStringLiteral("!")))
            continue;

        if (upper.startsWith(QStringLiteral("NBLOCK")))
        {
            inNodeBlock = true;
            inElemBlock = false;
            skipBlockFormatLine = true;
            continue;
        }

        if (upper.startsWith(QStringLiteral("EBLOCK")))
        {
            inElemBlock = true;
            inNodeBlock = false;
            skipBlockFormatLine = true;
            pendingElemFields.clear();
            continue;
        }

        if (inNodeBlock)
        {
            if (skipBlockFormatLine && line.startsWith(QLatin1Char('(')))
            {
                skipBlockFormatLine = false;
                continue;
            }
            skipBlockFormatLine = false;

            if (upper.startsWith(QStringLiteral("N,")) || upper.startsWith(QStringLiteral("-1")))
            {
                inNodeBlock = false;
                continue;
            }

            const QStringList tok = splitWhitespace(line);
            if (tok.size() >= 6)
            {
                bool okId = false;
                bool okX = false;
                bool okY = false;
                bool okZ = false;
                const int id = tok.value(0).toInt(&okId);
                const double x = tok.value(tok.size() - 3).toDouble(&okX);
                const double y = tok.value(tok.size() - 2).toDouble(&okY);
                const double z = tok.value(tok.size() - 1).toDouble(&okZ);
                if (okId && okX && okY && okZ)
                    nodeMap.insert(id, QVector3D(x, y, z));
            }
            continue;
        }

        if (inElemBlock)
        {
            if (skipBlockFormatLine && line.startsWith(QLatin1Char('(')))
            {
                skipBlockFormatLine = false;
                continue;
            }
            skipBlockFormatLine = false;

            const QStringList tok = splitWhitespace(line);
            if (tok.isEmpty())
                continue;

            bool okFirst = false;
            const int first = tok.value(0).toInt(&okFirst);
            if (okFirst && first == -1)
            {
                parsePendingElements();
                pendingElemFields.clear();
                inElemBlock = false;
                continue;
            }

            for (const QString& t : tok)
            {
                bool ok = false;
                const int v = t.toInt(&ok);
                if (ok)
                    pendingElemFields.push_back(v);
            }
            parsePendingElements();
        }
    }

    parsePendingElements();
    file.close();

    if (nodeMap.isEmpty())
    {
        if (errMsg)
            *errMsg = QObject::tr("ANSYS .inp 中未找到 NBLOCK 节点块: %1").arg(fileName);
        return nullptr;
    }

    if (elems.isEmpty())
    {
        if (errMsg)
        {
            *errMsg = QObject::tr("该文件是 ANSYS/HyperMesh 输入文件，包含 %1 个节点，但没有 EBLOCK 或其他单元连接。"
                                  "没有四面体单元的节点连接关系，无法导入为网格。请重新导出包含单元的 Abaqus *ELEMENT 或 ANSYS EBLOCK 文件。")
                          .arg(nodeMap.size());
        }
        return nullptr;
    }

    vtkNew<vtkPoints> pts;
    pts->SetDataTypeToDouble();

    vtkNew<vtkIntArray> nodeIdArr;
    nodeIdArr->SetName("node_id");

    QMap<int, vtkIdType> idMap;
    vtkIdType pid = 0;
    for (auto it = nodeMap.constBegin(); it != nodeMap.constEnd(); ++it)
    {
        const QVector3D& p = it.value();
        pts->InsertNextPoint(p.x(), p.y(), p.z());
        nodeIdArr->InsertNextValue(it.key());
        idMap.insert(it.key(), pid);
        m_mshPickMap.nodeIdToPointId[it.key()] = static_cast<int>(pid);
        m_mshPickMap.pointIdToNodeId[static_cast<int>(pid)] = it.key();
        ++pid;
    }

    vtkNew<vtkUnstructuredGrid> grid;
    grid->SetPoints(pts);
    grid->GetPointData()->AddArray(nodeIdArr);

    vtkNew<vtkIntArray> matypeArr;
    matypeArr->SetName("MATYPE");
    vtkNew<vtkIntArray> layerArr;
    layerArr->SetName("LAYER");
    vtkNew<vtkIntArray> ketypeArr;
    ketypeArr->SetName("KETYPE");
    vtkNew<vtkIntArray> elemIdArr;
    elemIdArr->SetName("elem_id");

    auto allIdsValid = [&](const QVector<int>& conn) -> bool
    {
        for (int nid : conn)
            if (!idMap.contains(nid))
                return false;
        return true;
    };

    auto appendCellArrays = [&](const AnsysElem& elem, int ketype)
    {
        matypeArr->InsertNextValue(elem.mat >= kFirstComputableMatype
                                       ? elem.mat
                                       : kDefaultCalculationMatype);
        layerArr->InsertNextValue(kDefaultConstructionLayer);
        ketypeArr->InsertNextValue(ketype);
        elemIdArr->InsertNextValue(elem.id > 0 ? elem.id : static_cast<int>(grid->GetNumberOfCells()));
    };

    for (const AnsysElem& elem : elems)
    {
        if (!allIdsValid(elem.conn))
        {
            qWarning() << "Skip ANSYS element: has undefined node id, elem=" << elem.id;
            continue;
        }

        const int nodeCount = elem.conn.size();
        if (nodeCount == 4 || nodeCount == 10)
        {
            vtkIdType ids[4];
            for (int i = 0; i < 4; ++i)
                ids[i] = idMap.value(elem.conn.value(i));
            grid->InsertNextCell(VTK_TETRA, 4, ids);
            appendCellArrays(elem, 35);
        }
        else if (nodeCount == 8)
        {
            vtkIdType ids[8];
            for (int i = 0; i < 8; ++i)
                ids[i] = idMap.value(elem.conn.value(i));
            grid->InsertNextCell(VTK_HEXAHEDRON, 8, ids);
            appendCellArrays(elem, 6);
        }
        else if (nodeCount == 6)
        {
            vtkIdType ids[6];
            for (int i = 0; i < 6; ++i)
                ids[i] = idMap.value(elem.conn.value(i));
            grid->InsertNextCell(VTK_WEDGE, 6, ids);
            appendCellArrays(elem, 38);
        }
        else if (nodeCount == 5)
        {
            vtkIdType ids[5];
            for (int i = 0; i < 5; ++i)
                ids[i] = idMap.value(elem.conn.value(i));
            grid->InsertNextCell(VTK_PYRAMID, 5, ids);
            appendCellArrays(elem, 39);
        }
        else
        {
            qWarning() << "Skip unsupported ANSYS element node count:" << nodeCount
                       << "elem=" << elem.id;
        }
    }

    if (grid->GetNumberOfCells() == 0)
    {
        if (errMsg)
            *errMsg = QObject::tr("ANSYS .inp 中没有可支持的实体单元，无法导入: %1").arg(fileName);
        return nullptr;
    }

    grid->GetCellData()->AddArray(matypeArr);
    grid->GetCellData()->AddArray(layerArr);
    grid->GetCellData()->AddArray(ketypeArr);
    grid->GetCellData()->AddArray(elemIdArr);
    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("AnsysInpGrid"));
    return grid;
}

void MeshGridIO::ensureDefaultArrays(vtkUnstructuredGrid* grid) const
{
    if (!grid)
        return;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();

           // point: node_id
    {
        auto nodeId = getOrCreatePointIntArray(grid, "node_id");
        nodeId->SetNumberOfTuples(nPts);
        for (vtkIdType i = 0; i < nPts; ++i)
            nodeId->SetValue(i, static_cast<int>(i) + 1);
    }

           // cell arrays
    auto matype = getOrCreateCellIntArray(grid, "MATYPE");
    auto layer = getOrCreateCellIntArray(grid, "LAYER");
    auto ketype = getOrCreateCellIntArray(grid, "KETYPE");
    auto topologyType = getOrCreateCellIntArray(grid, "TOPOLOGY_TYPE");
    auto elemId = getOrCreateCellIntArray(grid, "elem_id");
    auto part = getOrCreateCellIntArray(grid, "src_part");
    auto tail1 = getOrCreateCellIntArray(grid, "TAIL1");
    auto tail2 = getOrCreateCellIntArray(grid, "TAIL2");

    auto resizeAndInitIfNeeded = [nCells](vtkIntArray* arr)
    {
        if (!arr)
            return;
        const bool hasExactTupleCount = (arr->GetNumberOfTuples() == nCells);
        arr->SetNumberOfTuples(nCells);

               // 只在“新建/尺寸变化”时清零，避免覆盖已正确导入的数据（如 IIIN 的
               // MATYPE/LAYER）
        if (!hasExactTupleCount)
        {
            arr->FillComponent(0, 0.0);
        }
    };

    resizeAndInitIfNeeded(matype);
    resizeAndInitIfNeeded(layer);
    resizeAndInitIfNeeded(ketype);
    resizeAndInitIfNeeded(topologyType);
    resizeAndInitIfNeeded(elemId);
    resizeAndInitIfNeeded(part);
    resizeAndInitIfNeeded(tail1);
    resizeAndInitIfNeeded(tail2);

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        // 若已有值也无所谓，这里给默认兜底
        if (matype->GetValue(cid) == 0)
            matype->SetValue(cid, kDefaultCalculationMatype);
        if (layer->GetValue(cid) == 0)
            layer->SetValue(cid, kDefaultConstructionLayer);
        if (elemId->GetValue(cid) == 0)
            elemId->SetValue(cid, static_cast<int>(cid) + 1);

        int k = 0, expN = 0;
        vtkCell* c = grid->GetCell(cid);
        // 已有 TOPOLOGY_TYPE 可能是“异常”的自定义编号，不能用
        // VTK_POLYHEDRON 覆盖掉；仅在缺省值为 0 时补真实 VTK CellType。
        if (c && topologyType->GetValue(cid) == 0)
            topologyType->SetValue(cid, c->GetCellType());
        if (c && mapVtkCellToKetypeExt(c->GetCellType(), static_cast<int>(c->GetNumberOfPoints()),
                                       k, expN))
        {
            if (ketype->GetValue(cid) == 0)
                ketype->SetValue(cid, k);
        }
        else
        {
            if (ketype->GetValue(cid) == 0)
                ketype->SetValue(cid, 0);
        }

               // src_part / tail 默认 0
               // part->GetValue(cid) == 0 ok
    }

}
