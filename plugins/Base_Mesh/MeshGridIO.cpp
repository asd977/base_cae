#include "MeshGridIO.h"

#include <algorithm>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <vector>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector3D>
#include <QDebug>

#include <vtkCell.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <vtkSmartPointer.h>
#include <vtkHexahedron.h>
#include <vtkNew.h>
#include <vtkWedge.h>
#include <vtkTetra.h>
#include <vtkPyramid.h>
#include <vtkIntArray.h>
#include <vtkStringArray.h>
#include <vtkCellData.h>
#include <vtkPointData.h>
#include <vtkFieldData.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#else
static constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#endif

namespace {

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

// 你的“显示代理策略”搬到 IO 层：ketype -> (vtkCellType, localNodeLids) 若干片
static QVector<QPair<int, QVector<int>>> proxyPieces(int ketype)
{
    QVector<QPair<int, QVector<int>>> pieces;

    switch (ketype)
    {
    // 1D
    case 1: case 10: case 21:
        pieces.push_back({VTK_LINE, {0,1}});
        break;

    case 2: // 3-node line: IIIN=[1,2,3] mid=2 -> VTK_QUADRATIC_EDGE=[1,3,2]
        pieces.push_back({VTK_QUADRATIC_EDGE, {0,2,1}});
        break;

    // 2D quad
    case 3:
        pieces.push_back({VTK_QUAD, {0,1,2,3}});
        break;

    case 4: // 6-node quad -> VTK_QUADRATIC_LINEAR_QUAD
        pieces.push_back({VTK_QUADRATIC_LINEAR_QUAD, {0,2,3,5,1,4}});
        break;

    case 5: // 8-node quad -> VTK_QUADRATIC_QUAD
        pieces.push_back({VTK_QUADRATIC_QUAD, {0,2,4,6,1,3,5,7}});
        break;

    case 29: // 9-node quad -> VTK_BIQUADRATIC_QUAD
        pieces.push_back({VTK_BIQUADRATIC_QUAD, {0,2,4,6,1,3,5,7,8}});
        break;

    case 11:
        pieces.push_back({VTK_QUAD, {0,1,2,3}});
        break;

    case 12: // 5-node quad (node5 center) -> split 4 tris
        pieces.push_back({VTK_TRIANGLE, {0,1,4}});
        pieces.push_back({VTK_TRIANGLE, {1,2,4}});
        pieces.push_back({VTK_TRIANGLE, {2,3,4}});
        pieces.push_back({VTK_TRIANGLE, {3,0,4}});
        break;

    // 2D tri
    case 31:
        pieces.push_back({VTK_TRIANGLE, {0,1,2}});
        break;

    case 32: // 6-node tri -> VTK_QUADRATIC_TRIANGLE
        pieces.push_back({VTK_QUADRATIC_TRIANGLE, {0,2,4,1,3,5}});
        break;

    case 33: // proxy to corner tri
        pieces.push_back({VTK_TRIANGLE, {0,3,6}});
        break;

    case 34: // proxy to corner tri
        pieces.push_back({VTK_TRIANGLE, {0,4,8}});
        break;

    // 3D
    case 35:
        pieces.push_back({VTK_TETRA, {0,1,2,3}});
        break;

    case 6:
    case 13:
        pieces.push_back({VTK_HEXAHEDRON, {0,1,2,3,4,5,6,7}});
        break;

    case 36: // 14-node hex -> proxy to 8-corner hex
        pieces.push_back({VTK_HEXAHEDRON, {0,1,2,3,9,10,11,12}});
        break;

    case 37: // 20-node hex -> VTK_QUADRATIC_HEXAHEDRON
        pieces.push_back({VTK_QUADRATIC_HEXAHEDRON,
                          {0,2,4,6,12,14,16,18,
                           1,3,5,7,
                           13,15,17,19,
                           8,9,10,11}});
        break;

    case 9:
    case 17:
        pieces.push_back({VTK_QUAD, {0,1,2,3}});
        pieces.push_back({VTK_QUAD, {4,5,6,7}});
        break;

    default:
        break;
    }

    return pieces;
}

// 导出时：vtk cell -> ketype
static bool mapVtkCellToKetypeExt(int vtkCellType, int nodeCount, int &ketype, int &expectedNodes)
{
    switch (vtkCellType) {
    case VTK_LINE:                ketype = 1;  expectedNodes = 2;  return nodeCount == 2;
    case VTK_QUADRATIC_EDGE:      ketype = 2;  expectedNodes = 3;  return nodeCount == 3;

    case VTK_TRIANGLE:            ketype = 31; expectedNodes = 3;  return nodeCount == 3;
    case VTK_QUADRATIC_TRIANGLE:  ketype = 32; expectedNodes = 6;  return nodeCount == 6;

    case VTK_QUAD:                ketype = 3;  expectedNodes = 4;  return nodeCount == 4;
    case VTK_QUADRATIC_LINEAR_QUAD: ketype = 4; expectedNodes = 6; return nodeCount == 6;
    case VTK_QUADRATIC_QUAD:      ketype = 5;  expectedNodes = 8;  return nodeCount == 8;
    case VTK_BIQUADRATIC_QUAD:    ketype = 29; expectedNodes = 9;  return nodeCount == 9;

    case VTK_TETRA:               ketype = 35; expectedNodes = 4;  return nodeCount == 4;
    case VTK_HEXAHEDRON:          ketype = 6;  expectedNodes = 8;  return nodeCount == 8;
    case VTK_WEDGE:               ketype = 38; expectedNodes = 6;  return nodeCount == 6;
    case VTK_PYRAMID:             ketype = 39; expectedNodes = 5;  return nodeCount == 5;

    case VTK_QUADRATIC_HEXAHEDRON: ketype = 37; expectedNodes = 20; return nodeCount == 20;

    default:
        break;
    }
    return false;
}

// 导出时把 VTK 节点顺序改回 IIIN 顺序（只对你用到的高阶单元做重排）
static QVector<int> vtkToIiinOrder(int ketype)
{
    switch (ketype) {
    case 2:  return {0,2,1};                 // VTK [end0,end1,mid] -> IIIN [end0,mid,end1]
    case 4:  return {0,4,1,2,5,3};           // VTK QLIN_QUAD -> IIIN 6节点顺序
    case 5:  return {0,4,1,5,2,6,3,7};       // VTK QUADRATIC_QUAD -> IIIN 8节点顺序
    case 29: return {0,4,1,5,2,6,3,7,8};     // VTK BIQUADRATIC_QUAD -> IIIN 9节点顺序
    case 32: return {0,3,1,4,2,5};           // VTK QUADRATIC_TRIANGLE -> IIIN 6节点顺序
    case 37: // 由你 forward map 反推的 inverse
        return {0,8,1,9,2,10,3,11,16,17,18,19,4,12,5,13,6,14,7,15};
    default:
        return {}; // 线性单元不需要重排
    }
}

static vtkSmartPointer<vtkIntArray> getOrCreateCellIntArray(vtkUnstructuredGrid* g, const char* name)
{
    auto* cd = g->GetCellData();
    auto* a = vtkIntArray::SafeDownCast(cd->GetArray(name));
    if (a) return a;
    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName(name);
    arr->SetNumberOfComponents(1);
    cd->AddArray(arr);
    return arr;
}

static vtkSmartPointer<vtkIntArray> getOrCreatePointIntArray(vtkUnstructuredGrid* g, const char* name)
{
    auto* pd = g->GetPointData();
    auto* a = vtkIntArray::SafeDownCast(pd->GetArray(name));
    if (a) return a;
    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName(name);
    arr->SetNumberOfComponents(1);
    pd->AddArray(arr);
    return arr;
}

static int tetHexKetypeFromCell(vtkCell* cell, int fallback)
{
    if (!cell) return fallback;
    switch (cell->GetCellType()) {
    case VTK_TETRA:
        return 35;
    case VTK_HEXAHEDRON:
        return 6;
    default:
        return fallback;
    }
}

} // namespace

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::importFlac3D(const QString &fileName, QString *errMsg)
{
    clearPickMap();

    auto grid = loadFlac3dGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0) {
        if (errMsg) *errMsg = QObject::tr("无法解析 Flac3D 网格: %1").arg(fileName);
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
    auto* nodeIdArr = grid->GetPointData() ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id")) : nullptr;

    QHash<QString, int> groupToPhysId;
    int nextPhysId = 1;

    if (!groupArr) {
        qWarning() << "[Flac3D] GroupName array missing or type mismatch; MATYPE fallback may collapse to default";
    }

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        const QString groupName =
            (groupArr && cid < groupArr->GetNumberOfValues())
            ? QString::fromStdString(groupArr->GetValue(cid)).trimmed()
            : QString();

        if (groupName.isEmpty()) continue;

        // 使用完整分组串（例如 slotA;slotB），避免只取第一个组导致材料分区塌缩
        const QString groupKey = groupName;
        if (groupKey.isEmpty()) continue;

        int physId = groupToPhysId.value(groupKey, 0);
        if (physId == 0) {
            physId = nextPhysId++;
            groupToPhysId.insert(groupKey, physId);
            m_mshPickMap.physIdToName.insert(physId, groupKey);
            m_mshPickMap.physIdToDim.insert(physId, 3);
        }

        m_mshPickMap.volPhysToCellIds[physId].push_back(static_cast<qint64>(cid));
        if (matypeArr) {
            matypeArr->SetValue(cid, physId);
        }
        if (layerArr) {
            layerArr->SetValue(cid, physId);
        }
        if (ketypeArr) {
            vtkCell* cell = grid->GetCell(cid);
            ketypeArr->SetValue(cid, tetHexKetypeFromCell(cell, ketypeArr->GetValue(cid)));
        }
    }

    if (nodeIdArr) {
        const vtkIdType nPts = nodeIdArr->GetNumberOfTuples();
        for (vtkIdType pid = 0; pid < nPts; ++pid) {
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

    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        int regionId = 0;
        QString regionName;

        if (matypeArr) {
            regionId = matypeArr->GetValue(cid);
            regionName = m_mshPickMap.physIdToName.value(regionId);
        }
        regionIdArr->SetValue(cid, regionId);
        regionNameArr->SetValue(cid, regionName.toStdString());
    }

    if (cd) {
        cd->AddArray(regionIdArr);
        cd->AddArray(regionNameArr);
    }

    if (fd) {
        auto* zNames = vtkStringArray::SafeDownCast(fd->GetAbstractArray("ZGroupNames"));
        if (zNames) {
            auto bndNames = vtkSmartPointer<vtkStringArray>::New();
            auto bndDims = vtkSmartPointer<vtkIntArray>::New();
            bndNames->SetName("BndGroupNames");
            bndDims->SetName("BndGroupDims");
            for (vtkIdType i = 0; i < zNames->GetNumberOfValues(); ++i) {
                bndNames->InsertNextValue(zNames->GetValue(i));
                bndDims->InsertNextValue(3);
            }
            fd->AddArray(bndNames);
            fd->AddArray(bndDims);
        }
    }

    if (matypeArr) {
        grid->GetCellData()->SetScalars(matypeArr);
    }

    qInfo() << "[Flac3D] unique group->MATYPE count=" << groupToPhysId.size();

    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::importInp(const QString &fileName, QString *errMsg)
{
    clearPickMap();

    auto grid = loadAbaqusInpGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0) {
        if (errMsg) *errMsg = QObject::tr("无法解析 Abaqus 网格: %1").arg(fileName);
        return nullptr;
    }
    ensureDefaultArrays(grid);
    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid>
MeshGridIO::loadMshFile(const QString &fileName, QString *errMsg)
{
    auto fail = [&](const QString& s) -> vtkSmartPointer<vtkUnstructuredGrid> {
        if (errMsg) *errMsg = s;
        qWarning() << "[loadMshFile]" << s;
        return nullptr;
    };
    if (errMsg) errMsg->clear();

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QString("cannot open msh file: %1").arg(fileName));
    }

    QTextStream in(&f);
    in.setLocale(QLocale::c());

    auto stripInlineComment = [](QString s) -> QString {
        const int p = s.indexOf('*'); // 兼容你之前写法
        if (p >= 0) s = s.left(p);
        return s.trimmed();
    };

    auto unquote = [](QString s) -> QString {
        s = s.trimmed();
        if (s.size() >= 2) {
            const QChar a = s.front(), b = s.back();
            if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
                s = s.mid(1, s.size() - 2);
        }
        return s;
    };

    clearPickMap();

    // ------------------ 结果：grid + pickMap ------------------
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();

    MshPickMap pick; // 本次读入生成的映射（最后写回 m_mshPickMap）

    // ------------------ Nodes ------------------
    auto pts = vtkSmartPointer<vtkPoints>::New();
    pts->SetDataTypeToDouble();

    // msh nodeId -> vtk pointId
    QHash<int, vtkIdType> nodeIdToVtkId;
    nodeIdToVtkId.reserve(4096);

    bool haveNodes = false;
    bool haveElements = false;

    // ------------------ 临时：边界元素角点(用于后处理构造 edgeKey/faceKey) ------------------
    // physId -> 多个边界元素，每个元素是“角点 vtk pointId 列表”
    QHash<int, QVector<QVector<int>>> tmpEdgeElems; // 每个元素2点
    QHash<int, QVector<QVector<int>>> tmpFaceElems; // 每个元素3点或4点

    // ------------------ helper：physName fallback ------------------
    auto physNameOf = [&](int physId) -> QString {
        QString name = pick.physIdToName.value(physId);
        if (name.isEmpty() && physId != 0) name = QString("phys_%1").arg(physId);
        return name;
    };

    // ------------------ helper：msh nodeId -> vtk pointId(int) ------------------
    auto toVtkPidInt = [&](int mshNid) -> int {
        auto it = nodeIdToVtkId.find(mshNid);
        if (it == nodeIdToVtkId.end()) return -1;
        const vtkIdType pid = it.value();
        if (pid > std::numeric_limits<int>::max()) return -1;
        return int(pid);
    };

    // ------------------ helper：vector 去重（int / qint64） ------------------
    auto dedupIntVec = [](QVector<int>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    auto dedupI64Vec = [](QVector<qint64>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };

    // ------------------ 读文件主循环 ------------------
    while (!in.atEnd())
    {
        QString line = stripInlineComment(in.readLine());
        if (line.isEmpty()) continue;

        // ---- MeshFormat ----
        if (line == "$MeshFormat")
        {
            if (in.atEnd()) return fail("unexpected EOF after $MeshFormat");
            const QString fmt = stripInlineComment(in.readLine());
            const auto tok = fmt.split(QRegularExpression("\\s+"), kSkipEmptyParts);
            if (tok.size() < 3) return fail("invalid MeshFormat line");

            // 2.2 0 8
            const int fileType = tok[1].toInt(); // 0 ascii, 1 binary

            while (!in.atEnd()) {
                const QString t = stripInlineComment(in.readLine());
                if (t == "$EndMeshFormat") break;
            }
            if (fileType != 0) {
                return fail(QString("only ASCII msh is supported (fileType=%1)").arg(fileType));
            }
            continue;
        }

        // ---- PhysicalNames ----
        if (line == "$PhysicalNames")
        {
            if (in.atEnd()) return fail("unexpected EOF after $PhysicalNames");
            const int n = stripInlineComment(in.readLine()).toInt();
            if (n < 0) return fail("invalid $PhysicalNames count");

            QRegularExpression re("^\\s*(\\d+)\\s+(\\d+)\\s+\"(.*)\"\\s*$");
            for (int i = 0; i < n; ++i)
            {
                if (in.atEnd()) return fail("unexpected EOF in $PhysicalNames");
                const QString s = stripInlineComment(in.readLine());

                auto m = re.match(s);
                if (m.hasMatch()) {
                    const int dim = m.captured(1).toInt();
                    const int tag = m.captured(2).toInt();
                    const QString name = m.captured(3);
                    pick.physIdToDim[tag]  = dim;
                    pick.physIdToName[tag] = name;
                } else {
                    const auto tok = s.split(QRegularExpression("\\s+"), kSkipEmptyParts);
                    if (tok.size() >= 3) {
                        const int dim = tok[0].toInt();
                        const int tag = tok[1].toInt();
                        QString name = tok.mid(2).join(' ');
                        name = unquote(name);
                        pick.physIdToDim[tag]  = dim;
                        pick.physIdToName[tag] = name;
                    }
                }
            }

            while (!in.atEnd()) {
                const QString t = stripInlineComment(in.readLine());
                if (t == "$EndPhysicalNames") break;
            }
            continue;
        }

        // ---- Nodes (msh2.2) ----
        if (line == "$Nodes")
        {
            if (in.atEnd()) return fail("unexpected EOF after $Nodes");
            const int nNodes = stripInlineComment(in.readLine()).toInt();
            if (nNodes <= 0) return fail("invalid $Nodes count");

            pts->Allocate(nNodes);

            for (int i = 0; i < nNodes; ++i)
            {
                if (in.atEnd()) return fail("unexpected EOF in $Nodes");
                const QString s = stripInlineComment(in.readLine());
                const auto tok = s.split(QRegularExpression("\\s+"), kSkipEmptyParts);
                if (tok.size() < 4) return fail("invalid node line");

                bool okId=false, okX=false, okY=false, okZ=false;
                const int nid = tok[0].toInt(&okId);
                const double x = tok[1].toDouble(&okX);
                const double y = tok[2].toDouble(&okY);
                const double z = tok[3].toDouble(&okZ);
                if (!okId || !okX || !okY || !okZ) return fail("failed to parse node");

                const vtkIdType vid = pts->InsertNextPoint(x, y, z);
                nodeIdToVtkId.insert(nid, vid);

                // 写入 pick 的双向映射（int）
                if (vid > std::numeric_limits<int>::max())
                    return fail("vtk pointId exceeds int range (too many points)");
                pick.nodeIdToPointId.insert(nid, int(vid));
                pick.pointIdToNodeId.insert(int(vid), nid);
            }

            while (!in.atEnd()) {
                const QString t = stripInlineComment(in.readLine());
                if (t == "$EndNodes") break;
            }

            grid->SetPoints(pts);
            haveNodes = true;
            continue;
        }

        // ---- Elements (msh2.2) ----
        if (line == "$Elements")
        {
            if (!haveNodes) return fail("$Elements appears before $Nodes (unsupported order)");
            if (in.atEnd()) return fail("unexpected EOF after $Elements");

            const int nElem = stripInlineComment(in.readLine()).toInt();
            if (nElem <= 0) return fail("invalid $Elements count");

            grid->Allocate(nElem);

            for (int i = 0; i < nElem; ++i)
            {
                if (in.atEnd()) return fail("unexpected EOF in $Elements");

                const QString s = stripInlineComment(in.readLine());
                const auto tok = s.split(QRegularExpression("\\s+"), kSkipEmptyParts);
                if (tok.size() < 3) return fail("invalid element line");

                int p = 0;
                bool okE=false, okT=false, okNT=false;
                const int elemId   = tok[p++].toInt(&okE);
                const int elemType = tok[p++].toInt(&okT);
                const int numTags  = tok[p++].toInt(&okNT);
                if (!okE || !okT || !okNT) return fail("failed to parse element header");
                if (numTags < 0) return fail("invalid numTags");
                if (tok.size() < 3 + numTags) return fail("element line too short for tags");

                QVector<int> tags;
                tags.reserve(numTags);
                for (int k = 0; k < numTags; ++k) {
                    bool ok=false;
                    tags.push_back(tok[p++].toInt(&ok));
                    if (!ok) return fail("failed to parse element tag");
                }

                const int physicalId = (numTags >= 1 ? tags[0] : 0);
                // const int entityId = (numTags >= 2 ? tags[1] : 0); // 你不需要就不存

                QVector<int> nodeIds;
                nodeIds.reserve(tok.size() - p);
                for (; p < tok.size(); ++p) {
                    bool ok=false;
                    const int nid = tok[p].toInt(&ok);
                    if (ok) nodeIds.push_back(nid);
                }

                // ========== 体单元：导入 tet/hex/wedge/pyramid（高阶取角点）==========
                int vtkType = -1;
                int needN = 0;

                if (elemType == 4 || elemType == 11) {        // tetra4 / tetra10
                    vtkType = VTK_TETRA;
                    needN   = 4;
                } else if (elemType == 5 || elemType == 17 || elemType == 12) { // hexa8 / hexa20 / hexa27
                    vtkType = VTK_HEXAHEDRON;
                    needN   = 8;
                } else if (elemType == 6 || elemType == 13 || elemType == 18) { // prism6 / prism18 / prism15
                    vtkType = VTK_WEDGE;
                    needN   = 6;
                } else if (elemType == 7 || elemType == 14 || elemType == 19) { // pyramid5 / pyramid14 / pyramid13
                    vtkType = VTK_PYRAMID;
                    needN   = 5;
                }

                if (vtkType != -1)
                {
                    if (nodeIds.size() < needN) {
                        return fail(QString("volume element %1 type %2 has too few nodes (%3)")
                                    .arg(elemId).arg(elemType).arg(nodeIds.size()));
                    }

                    vtkIdType ptIds[8]; // tet最多4，hex最多8
                    for (int k = 0; k < needN; ++k)
                    {
                        const int nid = nodeIds[k];
                        auto it = nodeIdToVtkId.find(nid);
                        if (it == nodeIdToVtkId.end()) {
                            return fail(QString("element %1 references unknown node id %2").arg(elemId).arg(nid));
                        }
                        ptIds[k] = it.value();
                    }

                    const vtkIdType cellId = grid->InsertNextCell(vtkType, needN, ptIds);

                    // 体分组：physicalId -> cellIds
                    pick.volPhysToCellIds[physicalId].push_back(qint64(cellId));
                }
                else
                {
                    // ========== 非体：点/线/面（只用于构造批量选的映射）==========
                    // 点单元 type=15：只存 pointId
                    if (elemType == 15)
                    {
                        if (nodeIds.size() >= 1) {
                            const int pid = toVtkPidInt(nodeIds[0]);
                            if (pid >= 0) pick.bndPointPhysToPointIds[physicalId].push_back(pid);
                        }
                        continue;
                    }

                    // 线：line2(1) / line3(8) => 取端点两个角点
                    if (elemType == 1 || elemType == 8)
                    {
                        if (nodeIds.size() >= 2)
                        {
                            const int p0 = toVtkPidInt(nodeIds[0]);
                            const int p1 = toVtkPidInt(nodeIds[1]);
                            if (p0 >= 0 && p1 >= 0) {
                                pick.bndEdgePhysToPointIds[physicalId].push_back(p0);
                                pick.bndEdgePhysToPointIds[physicalId].push_back(p1);
                                tmpEdgeElems[physicalId].push_back(QVector<int>{p0, p1});
                            }
                        }
                        continue;
                    }

                    // 三角面：tri3(2) / tri6(9) => 取前三个角点
                    if (elemType == 2 || elemType == 9)
                    {
                        if (nodeIds.size() >= 3)
                        {
                            const int p0 = toVtkPidInt(nodeIds[0]);
                            const int p1 = toVtkPidInt(nodeIds[1]);
                            const int p2 = toVtkPidInt(nodeIds[2]);
                            if (p0 >= 0 && p1 >= 0 && p2 >= 0) {
                                pick.bndFacePhysToPointIds[physicalId].push_back(p0);
                                pick.bndFacePhysToPointIds[physicalId].push_back(p1);
                                pick.bndFacePhysToPointIds[physicalId].push_back(p2);
                                tmpFaceElems[physicalId].push_back(QVector<int>{p0, p1, p2});
                            }
                        }
                        continue;
                    }

                    // 四边形面：quad4(3) / quad9(10) => 取前四个角点
                    if (elemType == 3 || elemType == 10)
                    {
                        if (nodeIds.size() >= 4)
                        {
                            const int p0 = toVtkPidInt(nodeIds[0]);
                            const int p1 = toVtkPidInt(nodeIds[1]);
                            const int p2 = toVtkPidInt(nodeIds[2]);
                            const int p3 = toVtkPidInt(nodeIds[3]);
                            if (p0 >= 0 && p1 >= 0 && p2 >= 0 && p3 >= 0) {
                                pick.bndFacePhysToPointIds[physicalId].push_back(p0);
                                pick.bndFacePhysToPointIds[physicalId].push_back(p1);
                                pick.bndFacePhysToPointIds[physicalId].push_back(p2);
                                pick.bndFacePhysToPointIds[physicalId].push_back(p3);
                                tmpFaceElems[physicalId].push_back(QVector<int>{p0, p1, p2, p3});
                            }
                        }
                        continue;
                    }

                    // 其他类型先忽略（需要再扩展）
                }
            }

            while (!in.atEnd()) {
                const QString t = stripInlineComment(in.readLine());
                if (t == "$EndElements") break;
            }

            haveElements = true;
            continue;
        }

        // 其它段落：$NodeData/$ElementData/... 忽略
    }

    f.close();

    if (!haveNodes) {
        return fail("no $Nodes section found (only msh2.2 supported in this loader)");
    }
    if (!haveElements || grid->GetNumberOfCells() == 0) {
        return fail("no volume cells imported (missing $Elements or no supported 3D elements found)");
    }

    // ======================= 后处理：去重点集 =======================
    for (auto it = pick.bndPointPhysToPointIds.begin(); it != pick.bndPointPhysToPointIds.end(); ++it)
        dedupIntVec(it.value());
    for (auto it = pick.bndEdgePhysToPointIds.begin(); it != pick.bndEdgePhysToPointIds.end(); ++it)
        dedupIntVec(it.value());
    for (auto it = pick.bndFacePhysToPointIds.begin(); it != pick.bndFacePhysToPointIds.end(); ++it)
        dedupIntVec(it.value());

    for (auto it = pick.volPhysToCellIds.begin(); it != pick.volPhysToCellIds.end(); ++it)
        dedupI64Vec(it.value());

    // ======================= 后处理：构造 edgeKey / faceKey =======================
    // edgeKey = (cellId<<4) | (edgeLocalId & 0xF)
    // faceKey = (cellId<<4) | (faceLocalId & 0xF)

    auto pack2 = [](int a, int b) -> quint64 {
        if (a > b) std::swap(a, b);
        return (quint64(quint32(a)) << 32) | quint64(quint32(b));
    };

    auto packN_sorted = [](const QVector<int>& pids) -> QByteArray {
        QVector<int> v = pids;
        std::sort(v.begin(), v.end());
        QByteArray key;
        key.resize(v.size() * int(sizeof(int)));
        memcpy(key.data(), v.constData(), size_t(key.size()));
        return key;
    };

    QHash<quint64, qint64>   edgeIndex; // (pidA,pidB) -> edgeKey
    QHash<QByteArray, qint64> faceIndex; // (sorted pids...) -> faceKey

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell) continue;

        // ---- edges ----
        const int nEdges = cell->GetNumberOfEdges();
        for (int ei = 0; ei < nEdges; ++ei)
        {
            vtkCell* e = cell->GetEdge(ei);
            if (!e) continue;
            vtkIdList* ids = e->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2) continue;

            const vtkIdType a64 = ids->GetId(0);
            const vtkIdType b64 = ids->GetId(1);
            if (a64 > std::numeric_limits<int>::max() || b64 > std::numeric_limits<int>::max())
                return fail("vtk pointId exceeds int range when building edge index");

            const int a = int(a64);
            const int b = int(b64);

            const quint64 k = pack2(a, b);
            const qint64 edgeKey = (qint64(cid) << 4) | (qint64(ei) & 0xF);

            if (!edgeIndex.contains(k))
                edgeIndex.insert(k, edgeKey);
        }

        // ---- faces ----
        const int nFaces = cell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* fcell = cell->GetFace(fi);
            if (!fcell) continue;
            vtkIdList* ids = fcell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3) continue;

            QVector<int> fpids;
            fpids.reserve(int(ids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                const vtkIdType pid64 = ids->GetId(i);
                if (pid64 > std::numeric_limits<int>::max())
                    return fail("vtk pointId exceeds int range when building face index");
                fpids.push_back(int(pid64));
            }

            const QByteArray k = packN_sorted(fpids);
            const qint64 faceKey = (qint64(cid) << 4) | (qint64(fi) & 0xF);

            if (!faceIndex.contains(k))
                faceIndex.insert(k, faceKey);
        }
    }

    // 用 msh 的边界线元素(2点) -> edgeIndex 查 edgeKey
    for (auto it = tmpEdgeElems.begin(); it != tmpEdgeElems.end(); ++it)
    {
        const int physId = it.key();
        const auto& elems = it.value();

        auto& out = pick.bndEdgePhysToEdgeKeys[physId];
        out.reserve(elems.size());

        for (const auto& e2 : elems)
        {
            if (e2.size() != 2) continue;
            const quint64 k = pack2(e2[0], e2[1]);
            auto hit = edgeIndex.find(k);
            if (hit != edgeIndex.end())
                out.push_back(hit.value());
        }
        dedupI64Vec(out);
    }

    // 用 msh 的边界面元素(3/4点) -> faceIndex 查 faceKey
    for (auto it = tmpFaceElems.begin(); it != tmpFaceElems.end(); ++it)
    {
        const int physId = it.key();
        const auto& elems = it.value();

        auto& out = pick.bndFacePhysToFaceKeys[physId];
        out.reserve(elems.size());

        for (const auto& fN : elems)
        {
            if (fN.size() < 3) continue;
            const QByteArray k = packN_sorted(fN);
            auto hit = faceIndex.find(k);
            if (hit != faceIndex.end())
                out.push_back(hit.value());
        }
        dedupI64Vec(out);
    }

    // ======================= PhysicalNames 缺失时：给 physId 兜底名/维度 =======================
    // （很多 msh 可能没写 $PhysicalNames，但 elements 里仍然有 physicalId）
    // 这里尽量补全名字（phys_#），维度补不全就保持缺省
    for (auto it = pick.volPhysToCellIds.begin(); it != pick.volPhysToCellIds.end(); ++it) {
        const int physId = it.key();
        if (!pick.physIdToName.contains(physId))
            pick.physIdToName.insert(physId, physNameOf(physId));
        if (!pick.physIdToDim.contains(physId))
            pick.physIdToDim.insert(physId, 3);
    }
    for (auto it = pick.bndPointPhysToPointIds.begin(); it != pick.bndPointPhysToPointIds.end(); ++it) {
        const int physId = it.key();
        if (!pick.physIdToName.contains(physId))
            pick.physIdToName.insert(physId, physNameOf(physId));
        if (!pick.physIdToDim.contains(physId))
            pick.physIdToDim.insert(physId, 0);
    }
    for (auto it = pick.bndEdgePhysToPointIds.begin(); it != pick.bndEdgePhysToPointIds.end(); ++it) {
        const int physId = it.key();
        if (!pick.physIdToName.contains(physId))
            pick.physIdToName.insert(physId, physNameOf(physId));
        if (!pick.physIdToDim.contains(physId))
            pick.physIdToDim.insert(physId, 1);
    }
    for (auto it = pick.bndFacePhysToPointIds.begin(); it != pick.bndFacePhysToPointIds.end(); ++it) {
        const int physId = it.key();
        if (!pick.physIdToName.contains(physId))
            pick.physIdToName.insert(physId, physNameOf(physId));
        if (!pick.physIdToDim.contains(physId))
            pick.physIdToDim.insert(physId, 2);
    }

    // 写回成员：供外部 DisplacementConstraintWidget / FaceLoadPage 等查询
    m_mshPickMap = pick;

    // 同步 RegionId/RegionName，且将 MATYPE 直接映射到体 physicalId（便于材料批量设置）
    auto* cd = grid->GetCellData();

    auto ensureCellIntArray = [cd, nCells](const char* name, int defaultValue) {
        auto* arr = vtkIntArray::SafeDownCast(cd->GetArray(name));
        if (!arr) {
            auto holder = vtkSmartPointer<vtkIntArray>::New();
            holder->SetName(name);
            holder->SetNumberOfComponents(1);
            holder->SetNumberOfTuples(nCells);
            holder->FillComponent(0, static_cast<double>(defaultValue));
            cd->AddArray(holder);
            arr = vtkIntArray::SafeDownCast(cd->GetArray(name));
        } else if (arr->GetNumberOfTuples() != nCells) {
            arr->SetNumberOfTuples(nCells);
            arr->FillComponent(0, static_cast<double>(defaultValue));
        }
        return arr;
    };

    auto* matype = ensureCellIntArray("MATYPE", 1);
    auto* layer = ensureCellIntArray("LAYER", 1);
    auto* ketype = ensureCellIntArray("KETYPE", 0);

    auto regionId = vtkSmartPointer<vtkIntArray>::New();
    regionId->SetName("RegionId");
    regionId->SetNumberOfTuples(nCells);

    auto regionName = vtkSmartPointer<vtkStringArray>::New();
    regionName->SetName("RegionName");
    regionName->SetNumberOfValues(nCells);

    QHash<vtkIdType, int> cellToPhys;
    for (auto it = pick.volPhysToCellIds.begin(); it != pick.volPhysToCellIds.end(); ++it) {
        const int physId = it.key();
        const auto& cids = it.value();
        for (qint64 c : cids) {
            if (c >= 0) cellToPhys.insert(static_cast<vtkIdType>(c), physId);
        }
    }

    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        const int physId = cellToPhys.value(cid, 0);
        const int mappedMatype = (physId > 0 ? physId : 1);
        matype->SetValue(cid, mappedMatype);
        if (layer) {
            layer->SetValue(cid, mappedMatype);
        }
        if (ketype) {
            vtkCell* cell = grid->GetCell(cid);
            ketype->SetValue(cid, tetHexKetypeFromCell(cell, ketype->GetValue(cid)));
        }
        regionId->SetValue(cid, physId);
        regionName->SetValue(cid, pick.physIdToName.value(physId).toStdString());
    }

    cd->AddArray(regionId);
    cd->AddArray(regionName);
    cd->SetScalars(matype);

    grid->Squeeze();
    return grid;
}

MeshGridIO& MeshGridIO::instance()
{
    static MeshGridIO s;
    return s;
}

MshPickMap MeshGridIO::getMeshMap()
{
    return m_mshPickMap;
}

void MeshGridIO::setMeshMap(const MshPickMap& pickMap)
{
    m_mshPickMap = pickMap;
}

void MeshGridIO::clearPickMap()
{
    m_mshPickMap = MshPickMap{};
}

vtkSmartPointer<vtkUnstructuredGrid> MeshGridIO::loadIiinFile(const QString &fileName, QString *errMsg)
{
    clearPickMap();

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errMsg) *errMsg = QObject::tr("无法打开文件: %1").arg(fileName);
        return nullptr;
    }

    QTextStream in(&f);

    int numnp=0, madim=3, nelem=0, manod=0, extra=0;
    double am=1.0, d1=0.0,d2=0.0,d3=0.0;

    in >> numnp >> madim >> nelem >> manod >> extra >> am >> d1 >> d2 >> d3;
    if (in.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = "首行(控制信息)读取失败";
        return nullptr;
    }

    int iobject=0, ivertical=2;
    double xmin=0,xmax=0,ymin=0,ymax=0,zmin=0,zmax=0;
    in >> iobject >> ivertical >> xmin >> xmax >> ymin >> ymax >> zmin >> zmax;
    if (in.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = "第二行(对象信息)读取失败";
        return nullptr;
    }
    in.readLine(); // 丢弃第二行尾部

    if (numnp <= 0) {
        if (errMsg) *errMsg = "节点数 numnp 非法";
        return nullptr;
    }
    if (manod <= 0) manod = 8; // 容错

    // -------- nodes --------
    struct NodeRec { double x,y,z; int id; };
    QVector<NodeRec> nodes;
    nodes.reserve(numnp);

    while (!in.atEnd() && nodes.size() < numnp) {
        QString raw = in.readLine();
        if (raw.isNull()) break;
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        QStringList parts = line.simplified().split(' ', kSkipEmptyParts);
        if (parts.size() < 3) continue;

        NodeRec n{};
        bool ok = true;

        if (parts.size() >= 4) {
            ok &= parseDouble(parts[0], n.x);
            ok &= parseDouble(parts[1], n.y);
            ok &= parseDouble(parts[2], n.z);
            ok &= parseInt(parts[3], n.id);
        } else {
            if (madim == 2) {
                ok &= parseDouble(parts[0], n.x);
                ok &= parseDouble(parts[1], n.y);
                n.z = 0.0;
                ok &= parseInt(parts[2], n.id);
            } else {
                ok &= parseDouble(parts[0], n.x);
                ok &= parseDouble(parts[1], n.y);
                ok &= parseDouble(parts[2], n.z);
                n.id = nodes.size() + 1;
            }
        }
        if (!ok) continue;

        n.x *= am; n.y *= am; n.z *= am;
        nodes.push_back(n);
    }

    if (nodes.size() != numnp) {
        // 容错：不强制失败，但提醒
        qWarning() << "[IIIN] node count mismatch, header=" << numnp << "read=" << nodes.size();
        numnp = nodes.size();
    }

    // points + node_id
    auto pts = vtkSmartPointer<vtkPoints>::New();
    pts->SetNumberOfPoints(numnp);

    auto nodeIdArr = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArr->SetName("node_id");
    nodeIdArr->SetNumberOfComponents(1);
    nodeIdArr->SetNumberOfTuples(numnp);

    // id->pointIndex
    QHash<int, vtkIdType> id2p;
    id2p.reserve(numnp * 2);

    for (int i = 0; i < numnp; ++i) {
        pts->SetPoint(i, nodes[i].x, nodes[i].y, nodes[i].z);
        nodeIdArr->SetValue(i, nodes[i].id);
        id2p.insert(nodes[i].id, static_cast<vtkIdType>(i));
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(pts);
    grid->GetPointData()->AddArray(nodeIdArr);

    // cell arrays
    auto matypeArr = vtkSmartPointer<vtkIntArray>::New();
    matypeArr->SetName("MATYPE"); matypeArr->SetNumberOfComponents(1);

    auto elemIdArr = vtkSmartPointer<vtkIntArray>::New();
    elemIdArr->SetName("elem_id"); elemIdArr->SetNumberOfComponents(1);

    auto ketypeArr = vtkSmartPointer<vtkIntArray>::New();
    ketypeArr->SetName("KETYPE"); ketypeArr->SetNumberOfComponents(1);

    auto layerArr = vtkSmartPointer<vtkIntArray>::New();
    layerArr->SetName("LAYER"); layerArr->SetNumberOfComponents(1);

    auto partArr = vtkSmartPointer<vtkIntArray>::New();
    partArr->SetName("src_part"); partArr->SetNumberOfComponents(1);

    auto tail1Arr = vtkSmartPointer<vtkIntArray>::New();
    tail1Arr->SetName("TAIL1"); tail1Arr->SetNumberOfComponents(1);

    auto tail2Arr = vtkSmartPointer<vtkIntArray>::New();
    tail2Arr->SetName("TAIL2"); tail2Arr->SetNumberOfComponents(1);

    // -------- elems --------
    struct ElemRec {
        int id=0;
        QVector<int> nodeIds; // 0..manod-1
        int ketype=0, matype=0, layer=0, tail1=0, tail2=0;
    };

    auto addCellPiece = [&](int vtkCellType,
            const QVector<int>& localNodeLids,
            const ElemRec& e,
            int part)->void
    {
        QVector<vtkIdType> ptIds;
        ptIds.reserve(localNodeLids.size());

        for (int lid : localNodeLids) {
            if (lid < 0 || lid >= e.nodeIds.size()) return;
            int nid = e.nodeIds[lid];
            if (nid == 0) return;

            auto it = id2p.find(nid);
            if (it == id2p.end()) return;
            ptIds.push_back(it.value());
        }

        if (ptIds.size() != localNodeLids.size()) return;

        grid->InsertNextCell(vtkCellType,
                             static_cast<vtkIdType>(ptIds.size()),
                             ptIds.data());

        matypeArr->InsertNextValue(e.matype);
        elemIdArr->InsertNextValue(e.id);
        ketypeArr->InsertNextValue(e.ketype);
        layerArr ->InsertNextValue(e.layer);
        partArr  ->InsertNextValue(part);
        tail1Arr ->InsertNextValue(e.tail1);
        tail2Arr ->InsertNextValue(e.tail2);
    };

    int readElems = 0;
    while (!in.atEnd()) {
        if (nelem > 0 && readElems >= nelem) break;

        QString raw = in.readLine();
        if (raw.isNull()) break;
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        QStringList parts = line.simplified().split(' ', kSkipEmptyParts);
        if (parts.size() < 4) continue;

        int elemId = 0;
        if (!parseInt(parts[0], elemId)) continue;

        auto tryParse = [&](bool withTail, ElemRec& e)->bool {
            const int n = parts.size();
            int ket=0, mat=0, lay=0, t1=0, t2=0;
            int nodeCount = 0;

            if (withTail) {
                if (n < 6) return false;
                if (!parseInt(parts[n-5], ket)) return false;
                if (!parseInt(parts[n-4], mat)) return false;
                if (!parseInt(parts[n-3], lay)) return false;
                if (!parseInt(parts[n-2], t1))  return false;
                if (!parseInt(parts[n-1], t2))  return false;
                nodeCount = n - 1 - 5;
            } else {
                if (!parseInt(parts[n-3], ket)) return false;
                if (!parseInt(parts[n-2], mat)) return false;
                if (!parseInt(parts[n-1], lay)) return false;
                t1 = t2 = 0;
                nodeCount = n - 1 - 3;
            }

            if (ket <= 0 || ket > 200) return false;
            if (nodeCount < 0 || nodeCount > manod) return false;

            e.id = elemId;
            e.ketype = ket;
            e.matype = mat;
            e.layer  = lay;
            e.tail1  = t1;
            e.tail2  = t2;
            e.nodeIds.clear();
            e.nodeIds.resize(manod);

            for (int i = 0; i < nodeCount; ++i) {
                int nid = 0;
                if (!parseInt(parts[1 + i], nid)) return false;
                e.nodeIds[i] = nid;
            }
            return true;
        };

        ElemRec e;
        bool ok = tryParse(true, e);
        if (!ok) ok = tryParse(false, e);
        if (!ok) continue;

        auto pieces = proxyPieces(e.ketype);
        if (pieces.isEmpty()) continue;

        for (int pi = 0; pi < pieces.size(); ++pi) {
            addCellPiece(pieces[pi].first, pieces[pi].second, e, pi);
        }

        ++readElems;
    }

    grid->GetCellData()->AddArray(matypeArr);
    grid->GetCellData()->SetScalars(matypeArr);

    grid->GetCellData()->AddArray(elemIdArr);
    grid->GetCellData()->AddArray(ketypeArr);
    grid->GetCellData()->AddArray(layerArr);
    grid->GetCellData()->AddArray(partArr);
    grid->GetCellData()->AddArray(tail1Arr);
    grid->GetCellData()->AddArray(tail2Arr);

    ensureDefaultArrays(grid);
    return grid;
}

bool MeshGridIO::exportIiinFile(const QString &fileName, vtkUnstructuredGrid *grid, QString *errMsg) const
{
    if (!grid || grid->GetNumberOfPoints() == 0 || grid->GetNumberOfCells() == 0) {
        if (errMsg) *errMsg = "网格为空，无法导出 IIIN.DAT";
        return false;
    }

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errMsg) *errMsg = QObject::tr("无法写入文件: %1").arg(fileName);
        return false;
    }

    // 先确保数组齐全
    ensureDefaultArrays(grid);

    auto* matypeArr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));
    auto* layerArr  = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("LAYER"));
    auto* tail1Arr  = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("TAIL1"));
    auto* tail2Arr  = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("TAIL2"));

    const vtkIdType numnp = grid->GetNumberOfPoints();
    const vtkIdType ncell = grid->GetNumberOfCells();

    double bounds[6];
    grid->GetBounds(bounds);
    double xmin=bounds[0], xmax=bounds[1], ymin=bounds[2], ymax=bounds[3], zmin=bounds[4], zmax=bounds[5];

    const int madim = (std::abs(zmax - zmin) < 1e-12) ? 2 : 3;
    const double am = 1.0;
    const int extra = 0;
    const double d1=0.0,d2=0.0,d3=0.0;
    const int iobject=0, ivertical=2;

    // 统计 manod（全局最大节点数）
    int manod = 0;
    QVector<int> cellKetype(ncell, 0);
    QVector<int> cellExpN(ncell, 0);

    int unsupported = 0;
    for (vtkIdType cid = 0; cid < ncell; ++cid) {
        vtkCell* c = grid->GetCell(cid);
        if (!c) { unsupported++; continue; }
        int k=0, expN=0;
        if (!mapVtkCellToKetypeExt(c->GetCellType(), static_cast<int>(c->GetNumberOfPoints()), k, expN)) {
            unsupported++;
            continue;
        }
        cellKetype[cid] = k;
        cellExpN[cid]   = expN;
        manod = std::max(manod, expN);
    }

    if (unsupported > 0) {
        if (errMsg) *errMsg = QObject::tr("存在 %1 个不支持的单元类型，无法导出").arg(unsupported);
        f.close();
        return false;
    }

    QTextStream out(&f);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);

    // header line 1
    out << numnp << " " << madim << " " << ncell << " " << manod << " "
        << extra << " " << am << " " << d1 << " " << d2 << " " << d3 << "\n";

    // header line 2
    out << iobject << " " << ivertical << " "
        << xmin << " " << xmax << " "
        << ymin << " " << ymax << " "
        << zmin << " " << zmax << "\n";

    // nodes: 这里统一按点序号写 nodeId=idx+1（最稳，不依赖 pointData node_id）
    for (vtkIdType i = 0; i < numnp; ++i) {
        double p[3]; grid->GetPoint(i, p);
        out << p[0] << " " << p[1] << " " << p[2] << " " << (static_cast<int>(i) + 1) << "\n";
    }

    // elems: elemId=cid+1；nodeId=pointId+1（必要时按 ketype 做重排）
    for (vtkIdType cid = 0; cid < ncell; ++cid) {
        vtkCell* c = grid->GetCell(cid);
        const int k = cellKetype[cid];
        const int expN = cellExpN[cid];

        int mat = matypeArr ? matypeArr->GetValue(cid) : 1;
        int lay = layerArr  ? layerArr->GetValue(cid)  : 1;
        int t1  = tail1Arr  ? tail1Arr->GetValue(cid)  : 0;
        int t2  = tail2Arr  ? tail2Arr->GetValue(cid)  : 0;

        // 取 VTK 顺序的点
        QVector<int> vtkNodeIds;
        vtkNodeIds.reserve(expN);

        for (int i = 0; i < c->GetNumberOfPoints(); ++i) {
            vtkIdType pid = c->GetPointId(i);
            vtkNodeIds.push_back(static_cast<int>(pid) + 1);
        }

        // 如果 expN > 实际点数（一般不会），补 0
        while (vtkNodeIds.size() < expN) vtkNodeIds.push_back(0);

        // 重排到 IIIN 顺序
        QVector<int> iiOrder = vtkToIiinOrder(k);
        QVector<int> outConn;
        outConn.reserve(expN);

        if (!iiOrder.isEmpty() && iiOrder.size() == expN) {
            for (int idx : iiOrder) outConn.push_back(vtkNodeIds[idx]);
        } else {
            outConn = vtkNodeIds; // 线性或无需重排
        }

        // 写出：id + manod 个节点(不足补0) + ketype matype layer tail1 tail2
        out << (static_cast<int>(cid) + 1);

        for (int j = 0; j < manod; ++j) {
            int nid = (j < outConn.size()) ? outConn[j] : 0;
            out << " " << nid;
        }

        out << " " << k << " " << mat << " " << lay << " " << t1 << " " << t2 << "\n";
    }

    out.flush();
    f.close();
    return true;
}

QString MeshGridIO::defaultIiinOutputPath(const QString &sourceFile)
{
    QFileInfo info(sourceFile);
    return info.path() + "/" + info.completeBaseName() + ".dat";
}


vtkSmartPointer<vtkUnstructuredGrid>
MeshGridIO::loadFlac3dGrid(const QString &fileName) const
{
    struct NodeData {
        double x, y, z;
    };

    QMap<int, NodeData> nodes;

    struct Zone {
        QString type;
        int id;
        QVector<int> verts;
    };
    QVector<Zone> zones;

    // 一个 zone 可能属于多个 group（同一 slot），所以用 QStringList
    QMap<int, QStringList> zoneGroups;

    // ========= 打开文件 =========
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "cannot open flac3d file:" << fileName;
        return nullptr;
    }

    // ========= 一些解析工具 =========
    auto stripInlineComment = [](QString s) -> QString {
        // 处理： "305238 * FACES" 这种行内注释
        const int p = s.indexOf('*');
        if (p >= 0) s = s.left(p);
        return s.trimmed();
    };

    auto unquote = [](QString s) -> QString {
        s = s.trimmed();
        if (s.size() >= 2) {
            const QChar a = s.front();
            const QChar b = s.back();
            if ((a == '"'  && b == '"') || (a == '\'' && b == '\''))
                s = s.mid(1, s.size() - 2);
        }
        return s;
    };

    auto addGroup = [&](int zid, const QString& gname) {
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

    auto parseZoneIdToken = [&](const QString& t) {
        // 支持： 12  或  12-20
        if (t.isEmpty()) return;

        if (t.contains('-')) {
            const auto parts = t.split('-', kSkipEmptyParts);
            if (parts.size() != 2) return;

            bool ok1 = false, ok2 = false;
            int a = parts[0].toInt(&ok1);
            int b = parts[1].toInt(&ok2);
            if (!ok1 || !ok2) return;
            if (a > b) std::swap(a, b);

            for (int zid = a; zid <= b; ++zid) {
                if (!kWantedSlot.isEmpty() && curSlot != kWantedSlot) continue;
                addGroup(zid, curGroupName);
            }
        } else {
            bool ok = false;
            int zid = t.toInt(&ok);
            if (!ok) return;
            if (!kWantedSlot.isEmpty() && curSlot != kWantedSlot) return;
            addGroup(zid, curGroupName);
        }
    };

    // ========= 第一遍：读 G/Z/ZGROUP =========
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = stripInlineComment(in.readLine());
        if (line.isEmpty()) continue;

        // 若正在读取 ZGROUP 的 zone-id 列表（多行）
        if (inZGroupIds) {
            // 纯数字/范围行通常以数字开头
            if (!line.isEmpty() && line.front().isDigit()) {
                line.replace(',', ' ');
                const QStringList tok = line.split(QRegularExpression("\\s+"), kSkipEmptyParts);
                for (const QString& t : tok) parseZoneIdToken(t);
                continue;
            } else {
                // 遇到下一段命令（如 ZGROUP / G / Z / FACE...），退出列表模式
                inZGroupIds = false;
                // 不 continue，让这行走正常解析
            }
        }

        line.replace(',', ' ');
        const QStringList tok = line.split(QRegularExpression("\\s+"), kSkipEmptyParts);
        if (tok.isEmpty()) continue;

        const QString keyword = tok[0].toUpper();

        if (keyword == "G") {
            if (tok.size() < 5) continue;
            const int id = tok[1].toInt();
            NodeData n;
            n.x = tok[2].toDouble();
            n.y = tok[3].toDouble();
            n.z = tok[4].toDouble();
            nodes[id] = n;
        }
        else if (keyword == "Z") {
            if (tok.size() < 4) continue;

            Zone z;
            z.type = tok[1];
            z.id   = tok[2].toInt();

            if (z.type == "B8") {
                if (tok.size() < 11) continue;
                for (int i = 3; i < 11; ++i) z.verts.push_back(tok[i].toInt());
            } else if (z.type == "W6") {
                if (tok.size() < 9) continue;
                for (int i = 3; i < 9; ++i) z.verts.push_back(tok[i].toInt());
            } else if (z.type == "T4") {
                if (tok.size() < 7) continue;
                for (int i = 3; i < 7; ++i) z.verts.push_back(tok[i].toInt());
            } else if (z.type == "P5") {
                if (tok.size() < 8) continue;
                for (int i = 3; i < 8; ++i) z.verts.push_back(tok[i].toInt());
            } else {
                continue;
            }

            zones.push_back(z);
        }
        else if (keyword == "ZGROUP") {
            // 典型： ZGROUP "BIANPO" SLOT "1"
            if (tok.size() < 2) continue;

            curGroupName = unquote(tok[1]);
            curSlot.clear(); // 默认：未指定 slot

            // 找 SLOT
            for (int i = 2; i + 1 < tok.size(); ++i) {
                if (tok[i].compare("SLOT", Qt::CaseInsensitive) == 0) {
                    curSlot = unquote(tok[i + 1]);
                    break;
                }
            }

            seenSlots.insert(curSlot.isEmpty() ? QStringLiteral("<none>") : curSlot);

            qInfo() << "[Flac3D] parse ZGROUP name=" << curGroupName << " slot=" << (curSlot.isEmpty() ? QStringLiteral("<none>") : curSlot);

            // 同行如果也带了 zone ids，也顺手解析（容错）
            for (int i = 2; i < tok.size(); ++i) {
                if (tok[i].compare("SLOT", Qt::CaseInsensitive) == 0) { ++i; continue; }
                if (!tok[i].isEmpty() && tok[i].front().isDigit())
                    parseZoneIdToken(tok[i]);
            }

            // 进入“多行 zone id 列表”模式
            inZGroupIds = true;
        }
        // 其它段落（FACE GROUPS 等）这里暂不处理
    }
    f.close();

    if (nodes.isEmpty() || zones.isEmpty()) {
        qWarning() << "no nodes or zones parsed from flac3d file";
        return nullptr;
    }

    // ========= 建 vtkPoints =========
    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    QMap<int, vtkIdType> idMap;
    vtkIdType nextId = 0;

    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        const NodeData &p = it.value();
        pts->InsertNextPoint(p.x, p.y, p.z);
        idMap[it.key()] = nextId++;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid =
            vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(pts);
    grid->Allocate(zones.size());

    auto chooseMainAxis = [](const std::vector<double>& xs,
            const std::vector<double>& ys,
            const std::vector<double>& zs) -> int
    {
        double dx = *std::max_element(xs.begin(), xs.end()) - *std::min_element(xs.begin(), xs.end());
        double dy = *std::max_element(ys.begin(), ys.end()) - *std::min_element(ys.begin(), ys.end());
        double dz = *std::max_element(zs.begin(), zs.end()) - *std::min_element(zs.begin(), zs.end());
        if (dx >= dy && dx >= dz) return 0;
        if (dy >= dx && dy >= dz) return 1;
        return 2;
    };

    auto sortByAngleInPlane = [](QVector<int>& idx,
            int axis,
            const std::vector<double>& xs,
            const std::vector<double>& ys,
            const std::vector<double>& zs)
    {
        double cx = 0, cy = 0, cz = 0;
        for (int i : idx) { cx += xs[i]; cy += ys[i]; cz += zs[i]; }
        cx /= idx.size(); cy /= idx.size(); cz /= idx.size();

        struct Ang { int id; double ang; };
        QVector<Ang> arr;
        arr.reserve(idx.size());

        for (int i : idx) {
            double u, v;
            if (axis == 0)      { u = ys[i] - cy; v = zs[i] - cz; }
            else if (axis == 1) { u = xs[i] - cx; v = zs[i] - cz; }
            else                { u = xs[i] - cx; v = ys[i] - cy; }
            arr.push_back({i, std::atan2(v, u)});
        }

        std::sort(arr.begin(), arr.end(),
                  [](const Ang& a, const Ang& b){ return a.ang < b.ang; });

        for (int k = 0; k < idx.size(); ++k) idx[k] = arr[k].id;
    };

    auto addHex = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 8) return false;

        struct P { vtkIdType nid; double x,y,z; };
        P p[8];

        std::vector<double> xs(8), ys(8), zs(8);
        for (int i = 0; i < 8; ++i) {
            const NodeData &n = nodes[ ids[i] ];
            p[i].nid = idMap[ ids[i] ];
            p[i].x = n.x; p[i].y = n.y; p[i].z = n.z;
            xs[i] = n.x; ys[i] = n.y; zs[i] = n.z;
        }

        int axis = chooseMainAxis(xs, ys, zs);

        double minv = 1e300, maxv = -1e300;
        for (int i = 0; i < 8; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
        double mid = 0.5 * (minv + maxv);

        QVector<int> bottom, top;
        bottom.reserve(4); top.reserve(4);
        for (int i = 0; i < 8; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v <= mid) bottom.push_back(i);
            else          top.push_back(i);
        }

        if (bottom.size() != 4 || top.size() != 4) {
            QVector<QPair<double,int>> tmp;
            tmp.reserve(8);
            for (int i = 0; i < 8; ++i) {
                double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
                tmp.push_back(qMakePair(v, i));
            }
            std::sort(tmp.begin(), tmp.end(),
                      [](auto &a, auto &b){ return a.first < b.first; });
            bottom.clear(); top.clear();
            for (int i = 0; i < 4; ++i) bottom.push_back(tmp[i].second);
            for (int i = 4; i < 8; ++i) top.push_back(tmp[i].second);
        }

        sortByAngleInPlane(bottom, axis, xs, ys, zs);
        sortByAngleInPlane(top,    axis, xs, ys, zs);

        auto hex = vtkSmartPointer<vtkHexahedron>::New();
        for (int k = 0; k < 4; ++k) {
            hex->GetPointIds()->SetId(k,     p[ bottom[k] ].nid);
            hex->GetPointIds()->SetId(4 + k, p[ top[k]    ].nid);
        }

        grid->InsertNextCell(hex->GetCellType(), hex->GetPointIds());
        return true;
    };

    auto addW6 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 6) return false;

        struct P { vtkIdType nid; double x,y,z; };
        P p[6];

        std::vector<double> xs(6), ys(6), zs(6);
        for (int i = 0; i < 6; ++i) {
            const NodeData &n = nodes[ ids[i] ];
            p[i].nid = idMap[ ids[i] ];
            p[i].x = n.x; p[i].y = n.y; p[i].z = n.z;
            xs[i] = n.x; ys[i] = n.y; zs[i] = n.z;
        }

        int axis = chooseMainAxis(xs, ys, zs);

        double minv = 1e300, maxv = -1e300;
        for (int i = 0; i < 6; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
        double mid = 0.5 * (minv + maxv);

        QVector<int> bottom, top;
        bottom.reserve(3); top.reserve(3);
        for (int i = 0; i < 6; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v <= mid) bottom.push_back(i);
            else          top.push_back(i);
        }

        if (bottom.size() != 3 || top.size() != 3) {
            QVector<QPair<double,int>> tmp;
            tmp.reserve(6);
            for (int i = 0; i < 6; ++i) {
                double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
                tmp.push_back(qMakePair(v, i));
            }
            std::sort(tmp.begin(), tmp.end(),
                      [](auto &a, auto &b){ return a.first < b.first; });
            bottom.clear(); top.clear();
            for (int i = 0; i < 3; ++i) bottom.push_back(tmp[i].second);
            for (int i = 3; i < 6; ++i) top.push_back(tmp[i].second);
        }

        sortByAngleInPlane(bottom, axis, xs, ys, zs);
        sortByAngleInPlane(top,    axis, xs, ys, zs);

        auto wedge = vtkSmartPointer<vtkWedge>::New();
        for (int k = 0; k < 3; ++k) {
            wedge->GetPointIds()->SetId(k,   p[ bottom[k] ].nid);
            wedge->GetPointIds()->SetId(3+k, p[ top[k]    ].nid);
        }

        grid->InsertNextCell(wedge->GetCellType(), wedge->GetPointIds());
        return true;
    };

    auto addT4 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 4) return false;

        auto tet = vtkSmartPointer<vtkTetra>::New();
        for (int i = 0; i < 4; ++i)
            tet->GetPointIds()->SetId(i, idMap[ ids[i] ]);

        grid->InsertNextCell(tet->GetCellType(), tet->GetPointIds());
        return true;
    };

    auto addP5 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 5) return false;

        struct P { vtkIdType nid; double x,y,z; };
        P p[5];

        std::vector<double> xs(5), ys(5), zs(5);
        for (int i = 0; i < 5; ++i) {
            const NodeData &n = nodes[ ids[i] ];
            p[i].nid = idMap[ ids[i] ];
            p[i].x = n.x; p[i].y = n.y; p[i].z = n.z;
            xs[i] = n.x; ys[i] = n.y; zs[i] = n.z;
        }

        int axis = chooseMainAxis(xs, ys, zs);

        int apex = 0;
        double best = -1e300;
        for (int i = 0; i < 5; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v > best) { best = v; apex = i; }
        }

        QVector<int> baseIdx;
        baseIdx.reserve(4);
        for (int i = 0; i < 5; ++i)
            if (i != apex) baseIdx.push_back(i);

        sortByAngleInPlane(baseIdx, axis, xs, ys, zs);

        auto pyr = vtkSmartPointer<vtkPyramid>::New();
        for (int k = 0; k < 4; ++k)
            pyr->GetPointIds()->SetId(k, p[ baseIdx[k] ].nid);
        pyr->GetPointIds()->SetId(4, p[apex].nid);

        grid->InsertNextCell(pyr->GetCellType(), pyr->GetPointIds());
        return true;
    };

    qInfo() << "[Flac3D] all seen slots=" << QStringList(seenSlots.values()).join(',');

    // ========= 生成 cell 数据 + 组->cell 列表 =========
    QVector<int>     cellZoneIds;
    QVector<QString> cellGroupNames;
    cellZoneIds.reserve(zones.size());
    cellGroupNames.reserve(zones.size());

    // groupName -> cellId list
    QMap<QString, QVector<vtkIdType>> groupToCells;

    for (const Zone &z : zones) {
        bool ok = false;
        if (z.type == "B8")      ok = addHex(z.verts);
        else if (z.type == "W6") ok = addW6(z.verts);
        else if (z.type == "T4") ok = addT4(z.verts);
        else if (z.type == "P5") ok = addP5(z.verts);

        if (ok) {
            const vtkIdType cellId = grid->GetNumberOfCells() - 1;

            cellZoneIds.push_back(z.id);

            const QStringList groups = zoneGroups.value(z.id);
            const QString joined = groups.join(';'); // 多组时用 ; 拼起来
            cellGroupNames.push_back(joined);

            for (const QString& g : groups) {
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

    for (int i = 0; i < cellZoneIds.size(); ++i) {
        zoneIdArr->SetValue(i, cellZoneIds[i]);
        groupArr->SetValue(i, cellGroupNames[i].toStdString()); // ✅ 你原来漏了这句
    }

    grid->GetCellData()->AddArray(zoneIdArr);
    grid->GetCellData()->AddArray(groupArr);

    // ========= 把“每个组有哪些 cellId”塞进 FieldData（可选但你要的就是这个） =========
    // 采用打包结构：
    //   ZGroupNames[i]    = 第 i 个组名
    //   ZGroupOffsets[i]  = 该组的 cellId 起始下标
    //   ZGroupOffsets[i+1]= 结束下标（不含）
    //   ZGroupCellIds[off..end-1] = 该组的所有 cellId
    auto zGroupNames   = vtkSmartPointer<vtkStringArray>::New();
    auto zGroupOffsets = vtkSmartPointer<vtkIdTypeArray>::New();
    auto zGroupCellIds = vtkSmartPointer<vtkIdTypeArray>::New();

    zGroupNames->SetName("ZGroupNames");
    zGroupOffsets->SetName("ZGroupOffsets");
    zGroupCellIds->SetName("ZGroupCellIds");

    vtkIdType off = 0;
    zGroupOffsets->InsertNextValue(off);

    for (auto it = groupToCells.begin(); it != groupToCells.end(); ++it) {
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

vtkSmartPointer<vtkUnstructuredGrid>
MeshGridIO::loadAbaqusInpGrid(const QString &fileName) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open inp file:" << fileName;
        return nullptr;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif

    QMap<int, QVector3D> nodeMap;

    struct ElemBlock { QString type; QVector<int> conn; };
    QVector<ElemBlock> elems;

    auto nodesPerType = [](const QString& tUpper) -> int {
        if (tUpper.startsWith("C3D8")) return 8;
        if (tUpper.startsWith("C3D4")) return 4;
        if (tUpper.startsWith("C3D6")) return 6;
        if (tUpper.startsWith("C3D5")) return 5;
        return -1;
    };

    auto splitCsv = [](const QString& s) {
        return s.split(',', QString::SkipEmptyParts);
    };

    bool inNode = false;
    bool inElem = false;
    QString curElemTypeUpper;
    int expectedConn = -1;

    bool elemContinuing = false;
    QVector<int> curConn;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("**")) continue;

        if (line.startsWith('*')) {
            inNode = false;
            inElem = false;
            elemContinuing = false;
            curConn.clear();
            expectedConn = -1;
            curElemTypeUpper.clear();

            QString upper = line.toUpper();

            if (upper.startsWith("*NODE")) {
                inNode = true;
            } else if (upper.startsWith("*ELEMENT")) {
                inElem = true;

                int pos = upper.indexOf("TYPE=");
                if (pos != -1) {
                    curElemTypeUpper = upper.mid(pos + 5).trimmed();
                    int comma = curElemTypeUpper.indexOf(',');
                    if (comma != -1) curElemTypeUpper = curElemTypeUpper.left(comma).trimmed();
                    expectedConn = nodesPerType(curElemTypeUpper);
                    if (expectedConn < 0) {
                        qWarning() << "Unsupported element type:" << curElemTypeUpper;
                    }
                }
            }
            continue;
        }

        if (inNode) {
            QStringList tok = splitCsv(line);
            if (tok.size() >= 4) {
                int id = tok[0].toInt();
                double x = tok[1].toDouble();
                double y = tok[2].toDouble();
                double z = tok[3].toDouble();
                nodeMap[id] = QVector3D(x, y, z);
            }
            continue;
        }

        if (inElem && expectedConn > 0) {
            QStringList tok = splitCsv(line);
            if (tok.isEmpty()) continue;

            int start = 0;
            if (!elemContinuing) {
                if (tok.size() < 2) continue;
                start = 1;
            }

            for (int i = start; i < tok.size(); ++i) {
                curConn.push_back(tok[i].toInt());
            }

            if (curConn.size() < expectedConn) {
                elemContinuing = true;
                continue;
            }

            if (curConn.size() > expectedConn) {
                qWarning() << "Element has extra nodes, type=" << curElemTypeUpper
                           << " got=" << curConn.size() << " expect=" << expectedConn
                           << " truncating.";
                curConn.resize(expectedConn);
            }

            ElemBlock e;
            e.type = curElemTypeUpper;
            e.conn = curConn;
            elems.push_back(e);

            elemContinuing = false;
            curConn.clear();
            continue;
        }
    }

    file.close();

    if (nodeMap.isEmpty() || elems.isEmpty()) {
        qWarning() << "inp file has no nodes or elements:" << fileName;
        return nullptr;
    }

    vtkNew<vtkPoints> pts;
    pts->SetDataTypeToDouble();

    QMap<int, vtkIdType> idMap;
    vtkIdType idx = 0;
    for (auto it = nodeMap.begin(); it != nodeMap.end(); ++it) {
        const QVector3D &p = it.value();
        pts->InsertNextPoint(p.x(), p.y(), p.z());
        idMap[it.key()] = idx++;
    }

    vtkNew<vtkUnstructuredGrid> grid;
    grid->SetPoints(pts);

    auto allIdsValid = [&](const QVector<int>& conn) -> bool {
        for (int nid : conn) if (!idMap.contains(nid)) return false;
        return true;
    };

    for (const ElemBlock &e : elems) {
        if (!allIdsValid(e.conn)) {
            qWarning() << "Skip element: has undefined node id, type=" << e.type;
            continue;
        }

        if (e.type.startsWith("C3D8") && e.conn.size() == 8) {
            vtkIdType ids[8];
            for (int i = 0; i < 8; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_HEXAHEDRON, 8, ids);
        } else if (e.type.startsWith("C3D4") && e.conn.size() == 4) {
            vtkIdType ids[4];
            for (int i = 0; i < 4; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_TETRA, 4, ids);
        } else if (e.type.startsWith("C3D6") && e.conn.size() == 6) {
            vtkIdType ids[6];
            for (int i = 0; i < 6; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_WEDGE, 6, ids);
        } else if (e.type.startsWith("C3D5") && e.conn.size() == 5) {
            vtkIdType ids[5];
            for (int i = 0; i < 5; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_PYRAMID, 5, ids);
        }
    }

    return grid;
}

void MeshGridIO::ensureDefaultArrays(vtkUnstructuredGrid *grid) const
{
    if (!grid) return;

    const vtkIdType nPts   = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();

    // point: node_id
    {
        auto nodeId = getOrCreatePointIntArray(grid, "node_id");
        nodeId->SetNumberOfTuples(nPts);
        for (vtkIdType i = 0; i < nPts; ++i) nodeId->SetValue(i, static_cast<int>(i) + 1);
    }

    // cell arrays
    auto matype = getOrCreateCellIntArray(grid, "MATYPE");
    auto layer  = getOrCreateCellIntArray(grid, "LAYER");
    auto ketype = getOrCreateCellIntArray(grid, "KETYPE");
    auto elemId = getOrCreateCellIntArray(grid, "elem_id");
    auto part   = getOrCreateCellIntArray(grid, "src_part");
    auto tail1  = getOrCreateCellIntArray(grid, "TAIL1");
    auto tail2  = getOrCreateCellIntArray(grid, "TAIL2");

    auto resizeAndInitIfNeeded = [nCells](vtkIntArray* arr) {
        if (!arr) return;
        const bool hasExactTupleCount = (arr->GetNumberOfTuples() == nCells);
        arr->SetNumberOfTuples(nCells);

        // 只在“新建/尺寸变化”时清零，避免覆盖已正确导入的数据（如 IIIN 的 MATYPE/LAYER）
        if (!hasExactTupleCount) {
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

    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        // 若已有值也无所谓，这里给默认兜底
        if (matype->GetValue(cid) == 0) matype->SetValue(cid, 1);
        if (layer ->GetValue(cid) == 0) layer ->SetValue(cid, 1);
        if (elemId->GetValue(cid) == 0) elemId->SetValue(cid, static_cast<int>(cid) + 1);

        int k = 0, expN = 0;
        vtkCell* c = grid->GetCell(cid);
        if (c && mapVtkCellToKetypeExt(c->GetCellType(), static_cast<int>(c->GetNumberOfPoints()), k, expN)) {
            if (ketype->GetValue(cid) == 0) ketype->SetValue(cid, k);
        } else {
            if (ketype->GetValue(cid) == 0) ketype->SetValue(cid, 0);
        }

        // src_part / tail 默认 0
        // part->GetValue(cid) == 0 ok
    }

    // 默认把 MATYPE 作为 scalars（你高亮逻辑用到）
    grid->GetCellData()->SetScalars(matype);
}
