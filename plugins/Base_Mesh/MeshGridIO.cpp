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
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
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
constexpr int kFirstComputableMatype = 2;
constexpr int kDefaultCalculationMatype = 3;
constexpr int kDefaultConstructionLayer = 1;

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

static inline MeshTagKey domainPhysicalKey(int physId)
{
    return MeshTagKey{QStringLiteral("Physical"), 3, physId};
}

static inline MeshTagSet& ensureDomainPhysicalTagSet(MshPickMap& pickMap, int physId)
{
    const MeshTagKey key = domainPhysicalKey(physId);
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

static inline void appendDomainPhysicalCell(MshPickMap& pickMap, int physId, MeshIdType cellId)
{
    if (physId <= 0 || cellId < 0)
        return;

    MeshTagSet& set = ensureDomainPhysicalTagSet(pickMap, physId);
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
            if (key.category != QStringLiteral("Physical") || key.dim != 3 || key.id <= 0)
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

    QVector<int> insertedElementIndices;
    insertedElementIndices.reserve(elements.size());
    int missingNodeElementCount = 0;
    int meshDimension = 0;
    for (int elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
    {
        const NasElement& element = elements.at(elementIndex);
        QVector<vtkIdType> pointIds;
        pointIds.reserve(element.nodeIds.size());
        bool missingNode = false;
        for (int nodeId : element.nodeIds)
        {
            const auto it = pointIdByNodeId.constFind(nodeId);
            if (it == pointIdByNodeId.constEnd())
            {
                missingNode = true;
                break;
            }
            pointIds.push_back(it.value());
        }
        if (missingNode)
        {
            ++missingNodeElementCount;
            continue;
        }

        grid->InsertNextCell(element.vtkType,
                             static_cast<vtkIdType>(pointIds.size()),
                             pointIds.constData());
        insertedElementIndices.push_back(elementIndex);
        meshDimension = qMax(meshDimension, element.dimension);
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

    QHash<int, int> propertyToMatype;
    int nextMatype = kDefaultCalculationMatype;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        const NasElement& element =
            elements.at(insertedElementIndices.at(static_cast<int>(cellId)));
        const int propertyId = qMax(0, element.propertyId);
        if (!propertyToMatype.contains(propertyId))
            propertyToMatype.insert(propertyId, nextMatype++);
        const int matype = propertyToMatype.value(propertyId);

        if (matypeArray)
            matypeArray->SetValue(cellId, matype);
        if (layerArray)
            layerArray->SetValue(cellId, kDefaultConstructionLayer);
        if (ketypeArray)
            ketypeArray->SetValue(cellId, element.ketype);
        if (elemIdArray)
            elemIdArray->SetValue(cellId, element.id);
        if (partArray)
            partArray->SetValue(cellId, propertyId);

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

    m_mshPickMap.meshDim = meshDimension;
    PublicVariables::instance().set(PublicConstants::kMeshDimension, meshDimension, "mesh");
    normalizePhysicalTagSets(m_mshPickMap, QStringLiteral("Nastran"));

    qInfo() << "[Nastran] imported nodes=" << nodes.size()
            << "cells=" << grid->GetNumberOfCells()
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

        if (addPiecesByKetype(elemId, ketype, matype, layer, tail1, tail2))
            ++readElems;
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

    auto connectivityPatternArr = vtkSmartPointer<vtkIntArray>::New();
    connectivityPatternArr->SetName(kIiinConnectivityPatternArrayName);
    connectivityPatternArr->SetNumberOfComponents(1);
    connectivityPatternArr->SetNumberOfTuples(grid->GetNumberOfCells());
    connectivityPatternArr->FillComponent(0, -1.0);
    for (auto it = connectivityPatterns.cbegin(); it != connectivityPatterns.cend(); ++it)
        connectivityPatternArr->SetValue(it.key(), it.value());
    grid->GetCellData()->AddArray(connectivityPatternArr);

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
    const int exportCellDim = (madim >= 3) ? 3 : 2;
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
        int k = 0, expN = 0;
        if (!mapVtkCellToKetypeExt(c->GetCellType(), static_cast<int>(c->GetNumberOfPoints()), k,
                                   expN))
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
        if (c->GetCellType() == VTK_TRIANGLE && storedKetype == 3 &&
            connectivityPattern >= 0)
        {
            k = 3;
            expN = 4;
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

        if (k == 3 && vtkNodeIds.size() == 3 && connectivityPattern >= 0)
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
        else if (k == 6 && c->GetCellType() == VTK_HEXAHEDRON)
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
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "cannot open flac3d file:" << fileName;
        return nullptr;
    }

           // ========= 一些解析工具 =========
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
    QSet<QString> seenSlots;

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

        struct P
        {
            vtkIdType nid;
            double x, y, z;
        };
        P p[8];

        std::vector<double> xs(8), ys(8), zs(8);
        for (int i = 0; i < 8; ++i)
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

        double minv = 1e300, maxv = -1e300;
        for (int i = 0; i < 8; ++i)
        {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
        double mid = 0.5 * (minv + maxv);

        QVector<int> bottom, top;
        bottom.reserve(4);
        top.reserve(4);
        for (int i = 0; i < 8; ++i)
        {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v <= mid)
                bottom.push_back(i);
            else
                top.push_back(i);
        }

        if (bottom.size() != 4 || top.size() != 4)
        {
            QVector<QPair<double, int>> tmp;
            tmp.reserve(8);
            for (int i = 0; i < 8; ++i)
            {
                double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
                tmp.push_back(qMakePair(v, i));
            }
            std::sort(tmp.begin(), tmp.end(),
                      [](auto& a, auto& b)
                      {
                          return a.first < b.first;
                      });
            bottom.clear();
            top.clear();
            for (int i = 0; i < 4; ++i)
                bottom.push_back(tmp[i].second);
            for (int i = 4; i < 8; ++i)
                top.push_back(tmp[i].second);
        }

        sortByAngleInPlane(bottom, axis, xs, ys, zs);
        sortByAngleInPlane(top, axis, xs, ys, zs);

        auto hex = vtkSmartPointer<vtkHexahedron>::New();
        for (int k = 0; k < 4; ++k)
        {
            hex->GetPointIds()->SetId(k, p[bottom[k]].nid);
            hex->GetPointIds()->SetId(4 + k, p[top[k]].nid);
        }

        grid->InsertNextCell(hex->GetCellType(), hex->GetPointIds());
        return true;
    };

    auto addW6 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 6)
            return false;

        struct P
        {
            vtkIdType nid;
            double x, y, z;
        };
        P p[6];

        std::vector<double> xs(6), ys(6), zs(6);
        for (int i = 0; i < 6; ++i)
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

        double minv = 1e300, maxv = -1e300;
        for (int i = 0; i < 6; ++i)
        {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
        double mid = 0.5 * (minv + maxv);

        QVector<int> bottom, top;
        bottom.reserve(3);
        top.reserve(3);
        for (int i = 0; i < 6; ++i)
        {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v <= mid)
                bottom.push_back(i);
            else
                top.push_back(i);
        }

        if (bottom.size() != 3 || top.size() != 3)
        {
            QVector<QPair<double, int>> tmp;
            tmp.reserve(6);
            for (int i = 0; i < 6; ++i)
            {
                double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
                tmp.push_back(qMakePair(v, i));
            }
            std::sort(tmp.begin(), tmp.end(),
                      [](auto& a, auto& b)
                      {
                          return a.first < b.first;
                      });
            bottom.clear();
            top.clear();
            for (int i = 0; i < 3; ++i)
                bottom.push_back(tmp[i].second);
            for (int i = 3; i < 6; ++i)
                top.push_back(tmp[i].second);
        }

        sortByAngleInPlane(bottom, axis, xs, ys, zs);
        sortByAngleInPlane(top, axis, xs, ys, zs);

        auto wedge = vtkSmartPointer<vtkWedge>::New();
        for (int k = 0; k < 3; ++k)
        {
            wedge->GetPointIds()->SetId(k, p[bottom[k]].nid);
            wedge->GetPointIds()->SetId(3 + k, p[top[k]].nid);
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

    QHash<int, QVector<vtkIdType>> elemIdToCellIds;
    auto allIdsValid = [&](const QVector<int>& conn) -> bool
    {
        for (int nid : conn)
            if (!idMap.contains(nid))
                return false;
        return true;
    };

    for (const ElemBlock& e : elems)
    {
        if (!allIdsValid(e.conn))
        {
            qWarning() << "Skip element: has undefined node id, type=" << e.type;
            continue;
        }

        if (e.type.startsWith("C3D8") && e.conn.size() == 8)
        {
            vtkIdType ids[8];
            for (int i = 0; i < 8; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_HEXAHEDRON, 8, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
        else if (e.type.startsWith("C3D4") && e.conn.size() == 4)
        {
            vtkIdType ids[4];
            for (int i = 0; i < 4; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_TETRA, 4, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
        else if (e.type.startsWith("C3D10") && e.conn.size() == 10)
        {
            vtkIdType ids[4];
            for (int i = 0; i < 4; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_TETRA, 4, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
        else if (e.type.startsWith("C3D6") && e.conn.size() == 6)
        {
            vtkIdType ids[6];
            for (int i = 0; i < 6; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_WEDGE, 6, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
        else if (e.type.startsWith("C3D5") && e.conn.size() == 5)
        {
            vtkIdType ids[5];
            for (int i = 0; i < 5; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_PYRAMID, 5, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
        else if ((e.type.startsWith("T3D2") || e.type.startsWith("B31")) && e.conn.size() == 2)
        {
            vtkIdType ids[2];
            for (int i = 0; i < 2; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_LINE, 2, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
        else if (e.type.startsWith("T3D3") && e.conn.size() == 3)
        {
            vtkIdType ids[3];
            for (int i = 0; i < 3; ++i)
                ids[i] = idMap.value(e.conn[i]);
            const vtkIdType cid = grid->InsertNextCell(VTK_QUADRATIC_EDGE, 3, ids);
            elemIdToCellIds[e.id].push_back(cid);
        }
    }

    int nextPhysId = kDefaultCalculationMatype;
    QHash<QString, int> groupToPhysId;
    auto ensurePhysId = [&](const QString& groupName, int dim) -> int
    {
        const QString g = groupName.trimmed();
        if (g.isEmpty())
            return 0;
        if (groupToPhysId.contains(g))
            return groupToPhysId.value(g);
        while (m_mshPickMap.physIdToName.contains(nextPhysId))
            ++nextPhysId;
        const int physId = nextPhysId++;
        groupToPhysId.insert(g, physId);
        m_mshPickMap.physIdToName.insert(physId, g);
        m_mshPickMap.physIdToDim.insert(physId, dim);
        return physId;
    };

    for (auto it = elsetToElemIds.constBegin(); it != elsetToElemIds.constEnd(); ++it)
    {
        const QString& setName = it.key();
        QSet<int> allPointIds;
        QSet<qint64> allCellIds;
        int dim = 3;

        for (int elemId : it.value())
        {
            const auto cids = elemIdToCellIds.value(elemId);
            for (vtkIdType cid : cids)
            {
                if (cid < 0 || cid >= grid->GetNumberOfCells())
                    continue;
                allCellIds.insert(static_cast<qint64>(cid));
                if (auto* c = grid->GetCell(cid))
                    dim = c->GetCellDimension();
                vtkCell* c = grid->GetCell(cid);
                if (!c)
                    continue;
                for (vtkIdType k = 0; k < c->GetNumberOfPoints(); ++k)
                {
                    allPointIds.insert(static_cast<int>(c->GetPointId(k)));
                }
            }
        }
        if (allCellIds.isEmpty() && allPointIds.isEmpty())
            continue;

        const int physId = ensurePhysId(setName, dim);
        if (physId <= 0)
            continue;

        if (dim >= 3)
        {
            MeshTagSet& set = ensureDomainPhysicalTagSet(m_mshPickMap, physId);
            set.name = setName;
            set.cellIds = allCellIds.values().toVector();
            for (MeshIdType cid : set.cellIds)
                m_mshPickMap.cellIdToDomainTags[cid].push_back(set.key);
        }
        else if (dim == 2)
            setPhysicalBoundaryPoints(m_mshPickMap, physId, 2, allPointIds.values().toVector(), QStringLiteral("AbaqusInpGrid"));
        else
            setPhysicalBoundaryPoints(m_mshPickMap, physId, 1, allPointIds.values().toVector(), QStringLiteral("AbaqusInpGrid"));
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
