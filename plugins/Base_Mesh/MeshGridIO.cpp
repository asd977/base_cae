#include "MeshGridIO.h"

#include <algorithm>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <vector>
#include <QRegExp>
#include <QStringConverter>
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

namespace {

bool mapVtkCellToKetype(int vtkCellType, int nodeCount, int &ketype, int &expectedNodes)
{
    switch (vtkCellType) {
    case VTK_HEXAHEDRON:
        ketype = 6;
        expectedNodes = 8;
        return nodeCount == 8;
    case VTK_TETRA:
        ketype = 35;
        expectedNodes = 4;
        return nodeCount == 4;
    case VTK_WEDGE:
        ketype = 38;
        expectedNodes = 6;
        return nodeCount == 6;
    case VTK_PYRAMID:
        ketype = 39;
        expectedNodes = 5;
        return nodeCount == 5;
    case VTK_TRIANGLE:
        ketype = 31;
        expectedNodes = 3;
        return nodeCount == 3;
    case VTK_QUAD:
        ketype = 3;
        expectedNodes = 4;
        return nodeCount == 4;
    case VTK_LINE:
        ketype = 1;
        expectedNodes = 2;
        return nodeCount == 2;
    default:
        break;
    }

    return false;
}

} // namespace

bool MeshGridIO::loadIiinFile(const QString &fileName, IiinGridModel &model, QString *errMsg) const
{
    return model.loadFromFile(fileName, errMsg);
}

bool MeshGridIO::importFlac3D(const QString &fileName, IiinGridModel &model, QString *errMsg) const
{
    vtkSmartPointer<vtkUnstructuredGrid> grid = loadFlac3dGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0) {
        if (errMsg) *errMsg = QObject::tr("无法解析 Flac3D 网格: %1").arg(fileName);
        return false;
    }

    return populateModelFromGrid(grid, defaultIiinOutputPath(fileName), model, errMsg);
}

bool MeshGridIO::importInp(const QString &fileName, IiinGridModel &model, QString *errMsg) const
{
    vtkSmartPointer<vtkUnstructuredGrid> grid = loadAbaqusInpGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0) {
        if (errMsg) *errMsg = QObject::tr("无法解析 Abaqus 网格: %1").arg(fileName);
        return false;
    }

    return populateModelFromGrid(grid, defaultIiinOutputPath(fileName), model, errMsg);
}

bool MeshGridIO::exportIiinFile(const QString &fileName, const IiinGridModel &model, QString *errMsg) const
{
    return model.saveToFile(fileName, errMsg);
}

QString MeshGridIO::defaultIiinOutputPath(const QString &sourceFile)
{
    QFileInfo info(sourceFile);
    return info.path() + "/" + info.completeBaseName() + ".dat";
}

bool MeshGridIO::populateModelFromGrid(vtkUnstructuredGrid *grid,
                                       const QString &outputFileName,
                                       IiinGridModel &model,
                                       QString *errMsg) const
{
    if (!grid || grid->GetNumberOfPoints() == 0 || grid->GetNumberOfCells() == 0) {
        if (errMsg) *errMsg = QObject::tr("网格为空，无法转换为 IIIN.DAT");
        return false;
    }

    const vtkIdType pointCount = grid->GetNumberOfPoints();
    QVector<Node> nodes;
    nodes.reserve(static_cast<int>(pointCount));

    double xmin = 0.0, xmax = 0.0;
    double ymin = 0.0, ymax = 0.0;
    double zmin = 0.0, zmax = 0.0;

    for (vtkIdType i = 0; i < pointCount; ++i) {
        double p[3];
        grid->GetPoint(i, p);

        Node node;
        node.id = static_cast<int>(i) + 1;
        node.x = p[0];
        node.y = p[1];
        node.z = p[2];
        nodes.push_back(node);

        if (i == 0) {
            xmin = xmax = p[0];
            ymin = ymax = p[1];
            zmin = zmax = p[2];
        } else {
            xmin = std::min(xmin, p[0]);
            xmax = std::max(xmax, p[0]);
            ymin = std::min(ymin, p[1]);
            ymax = std::max(ymax, p[1]);
            zmin = std::min(zmin, p[2]);
            zmax = std::max(zmax, p[2]);
        }
    }

    QVector<Elem> elems;
    elems.reserve(static_cast<int>(grid->GetNumberOfCells()));
    int manod = 0;

    for (vtkIdType i = 0; i < grid->GetNumberOfCells(); ++i) {
        vtkCell *cell = grid->GetCell(i);
        if (!cell) continue;

        const int nodeCount = static_cast<int>(cell->GetNumberOfPoints());
        int ketype = 0;
        int expectedNodes = 0;
        if (!mapVtkCellToKetype(cell->GetCellType(), nodeCount, ketype, expectedNodes)) {
            qWarning() << "[MeshGridIO] Unsupported cell type" << cell->GetCellType()
                       << "with" << nodeCount << "points, skipping.";
            continue;
        }

        Elem elem;
        elem.id = elems.size() + 1;
        elem.ketype = ketype;
        elem.matype = 1;
        elem.layer = 1;
        elem.nodeIds.reserve(expectedNodes);

        for (int j = 0; j < nodeCount; ++j) {
            vtkIdType pointId = cell->GetPointId(j);
            elem.nodeIds.push_back(static_cast<int>(pointId) + 1);
        }

        manod = std::max(manod, nodeCount);
        elems.push_back(elem);
    }

    if (elems.isEmpty()) {
        if (errMsg) *errMsg = QObject::tr("未能转换任何单元为 IIIN.DAT");
        return false;
    }

    int madim = (std::abs(zmax - zmin) < 1e-12) ? 2 : 3;
    const int iobject = 0;
    const int ivertical = 2;
    const double am = 1.0;

    return model.setFromData(nodes,
                             elems,
                             madim,
                             manod,
                             am,
                             iobject,
                             ivertical,
                             xmin, xmax,
                             ymin, ymax,
                             zmin, zmax,
                             outputFileName,
                             errMsg);
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

    QMap<int, QString> zoneGroups;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "cannot open flac3d file:" << fileName;
        return nullptr;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith('*'))
            continue;

        line.replace(',', ' ');
        QStringList tok = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
        if (tok.isEmpty())
            continue;

        if (tok[0] == "G") {
            if (tok.size() < 5)
                continue;
            int id = tok[1].toInt();
            NodeData n;
            n.x = tok[2].toDouble();
            n.y = tok[3].toDouble();
            n.z = tok[4].toDouble();
            nodes[id] = n;
        } else if (tok[0] == "Z") {
            if (tok.size() < 4)
                continue;

            Zone z;
            z.type = tok[1];
            z.id   = tok[2].toInt();

            if (z.type == "B8") {
                if (tok.size() < 11) continue;
                for (int i = 3; i < 11; ++i)
                    z.verts.push_back(tok[i].toInt());
            } else if (z.type == "W6") {
                if (tok.size() < 9) continue;
                for (int i = 3; i < 9; ++i)
                    z.verts.push_back(tok[i].toInt());
            } else if (z.type == "T4") {
                if (tok.size() < 7) continue;
                for (int i = 3; i < 7; ++i)
                    z.verts.push_back(tok[i].toInt());
            } else if (z.type == "P5") {
                if (tok.size() < 8) continue;
                for (int i = 3; i < 8; ++i)
                    z.verts.push_back(tok[i].toInt());
            } else {
                continue;
            }

            zones.push_back(z);
        } else if (tok[0] == "ZGROUP") {
            if (tok.size() < 3)
                continue;

            QString groupName = tok[1];

            for (int i = 2; i < tok.size(); ++i) {
                QString t = tok[i];
                if (t.contains('-')) {
                    const auto parts = t.split('-');
                    if (parts.size() != 2) continue;
                    bool ok1 = false, ok2 = false;
                    int a = parts[0].toInt(&ok1);
                    int b = parts[1].toInt(&ok2);
                    if (!ok1 || !ok2) continue;
                    if (a > b) std::swap(a, b);
                    for (int zid = a; zid <= b; ++zid) {
                        zoneGroups[zid] = groupName;
                    }
                } else {
                    bool ok = false;
                    int zid = t.toInt(&ok);
                    if (ok)
                        zoneGroups[zid] = groupName;
                }
            }
        }
    }
    f.close();

    if (nodes.isEmpty() || zones.isEmpty()) {
        qWarning() << "no nodes or zones parsed from flac3d file";
        return nullptr;
    }

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
        for (int i : idx) {
            cx += xs[i]; cy += ys[i]; cz += zs[i];
        }
        cx /= idx.size();
        cy /= idx.size();
        cz /= idx.size();

        struct Ang {
            int id;
            double ang;
        };
        QVector<Ang> arr;
        arr.reserve(idx.size());

        for (int i : idx) {
            double u, v;
            if (axis == 0) {
                u = ys[i] - cy;
                v = zs[i] - cz;
            } else if (axis == 1) {
                u = xs[i] - cx;
                v = zs[i] - cz;
            } else {
                u = xs[i] - cx;
                v = ys[i] - cy;
            }
            double ang = std::atan2(v, u);
            arr.push_back({i, ang});
        }

        std::sort(arr.begin(), arr.end(),
                  [](const Ang& a, const Ang& b){ return a.ang < b.ang; });

        for (int k = 0; k < idx.size(); ++k)
            idx[k] = arr[k].id;
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

    QVector<int>   cellZoneIds;
    QVector<QString> cellGroupNames;
    cellZoneIds.reserve(zones.size());
    cellGroupNames.reserve(zones.size());

    for (const Zone &z : zones) {
        bool ok = false;
        if (z.type == "B8")
            ok = addHex(z.verts);
        else if (z.type == "W6")
            ok = addW6(z.verts);
        else if (z.type == "T4")
            ok = addT4(z.verts);
        else if (z.type == "P5")
            ok = addP5(z.verts);

        if (ok) {
            cellZoneIds.push_back(z.id);
            cellGroupNames.push_back(zoneGroups.value(z.id));
        }
    }

    vtkIdType nCells = grid->GetNumberOfCells();
    if (nCells != cellZoneIds.size()) {
        qWarning() << "[importFlac3D] cell count mismatch:"
                   << nCells << "vs" << cellZoneIds.size();
    }

    auto zoneIdArr = vtkSmartPointer<vtkIntArray>::New();
    zoneIdArr->SetName("ZoneId");
    zoneIdArr->SetNumberOfComponents(1);
    zoneIdArr->SetNumberOfTuples(cellZoneIds.size());

    auto groupArr = vtkSmartPointer<vtkStringArray>::New();
    groupArr->SetName("GroupName");
    groupArr->SetNumberOfValues(cellGroupNames.size());

    for (int i = 0; i < cellZoneIds.size(); ++i) {
        zoneIdArr->SetValue(i, cellZoneIds[i]);
    }

    grid->GetCellData()->AddArray(zoneIdArr);
    grid->GetCellData()->AddArray(groupArr);

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
