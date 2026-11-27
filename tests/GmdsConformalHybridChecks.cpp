#include <GmdsPartitionRunner.h>
#include "../plugins/Base_Mesh/tools/SMESHBridgeApi.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTemporaryDir>
#include <QTimer>
#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

static void check(bool b, const char* msg)
{
    if (!b) throw std::runtime_error(msg);
}

static QByteArray serializeBrep(const TopoDS_Shape& s)
{
    std::ostringstream o;
    BRepTools::Write(s, o, false, false, TopTools_FormatVersion_CURRENT);
    const auto str = o.str();
    return QByteArray(str.data(), qsizetype(str.size()));
}

static std::tuple<long long, long long, long long> coordKey(double x, double y, double z)
{
    return {std::llround(x * 1e4), std::llround(y * 1e4), std::llround(z * 1e4)};
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        // Build two adjacent solids sharing the Z = 2.0 interface plane:
        //   Solid A: [0..2, 0..2, 0..2] -> GMDS CurvedBlocking -> Pure Structured Hex
        //   Solid B: [0..2, 0..2, 2..4] -> SMESH Frozen Quad Interface -> Conformal Pyramid + Tet
        const TopoDS_Shape solidA = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), gp_Pnt(2, 2, 2)).Shape();
        const TopoDS_Shape solidB = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 2), gp_Pnt(2, 2, 4)).Shape();

        // Find the Z = 2.0 face index in Solid B
        TopTools_IndexedMapOfShape bFaces;
        TopExp::MapShapes(solidB, TopAbs_FACE, bFaces);
        int sharedFaceIdInB = -1;
        for (int i = 1; i <= bFaces.Extent(); ++i)
        {
            GProp_GProps fp;
            BRepGProp::SurfaceProperties(bFaces(i), fp);
            if (std::abs(fp.CentreOfMass().Z() - 2.0) < 1e-6)
            {
                sharedFaceIdInB = i;
                break;
            }
        }
        check(sharedFaceIdInB > 0, "Could not locate Z=2.0 interface face on Solid B");

        // Step 1: Run CAE_GMDSWorker in hex_mesh mode on Solid A
        GmdsPartitionRunner runner;
        QJsonObject req{
            {"mode", "hex_mesh"},
            {"divisions", 3},
            {"interfacePlane", QJsonObject{
                {"origin", QJsonArray{0.0, 0.0, 2.0}},
                {"normal", QJsonArray{0.0, 0.0, 1.0}}
            }}
        };
        QEventLoop loop;
        QVariantMap gmdsRes;
        QObject::connect(&runner, &GmdsPartitionRunner::finished, &loop, [&](const QVariantMap& r) {
            gmdsRes = r;
            loop.quit();
        });
        check(runner.start(serializeBrep(solidA), req, 6000), "Failed to start GMDS worker");
        loop.exec();
        check(gmdsRes.value("valid").toBool(), "GMDS hex_mesh failed on Solid A");

        const QVariantList jNodes = gmdsRes.value("nodes").toList();
        const QVariantList jHexes = gmdsRes.value("hexes").toList();
        const QVariantList jIQuads = gmdsRes.value("interfaceQuads").toList();
        check(jHexes.size() == 27, "Expected 3x3x3 = 27 GMDS hex cells on Solid A");
        check(jIQuads.size() == 9, "Expected 3x3 = 9 interface quads on Z=2.0");

        // Pack compact interface nodes & quads for SMESHBridge
        std::vector<CaeSMESHInterfaceNode> ifaceNodes;
        std::map<int, int> globalToLocal;
        std::vector<CaeSMESHInterfaceQuad> ifaceQuads;
        std::set<std::tuple<long long, long long, long long>> gmdsInterfaceCoordKeys;

        for (const QVariant& qv : jIQuads)
        {
            const QVariantList ql = qv.toList();
            check(ql.size() == 4, "Invalid quad");
            int loc[4];
            for (int k = 0; k < 4; ++k)
            {
                const int gid = ql[k].toInt();
                auto it = globalToLocal.find(gid);
                if (it == globalToLocal.end())
                {
                    const QVariantList pt = jNodes[gid].toList();
                    const int lid = int(ifaceNodes.size());
                    ifaceNodes.push_back({pt[0].toDouble(), pt[1].toDouble(), pt[2].toDouble()});
                    gmdsInterfaceCoordKeys.insert(coordKey(pt[0].toDouble(), pt[1].toDouble(), pt[2].toDouble()));
                    globalToLocal[gid] = lid;
                    loc[k] = lid;
                }
                else
                {
                    loc[k] = it->second;
                }
            }
            ifaceQuads.push_back({sharedFaceIdInB, loc[0], loc[1], loc[2], loc[3]});
        }

        // Step 2: Load CAE_SMESHBridge and invoke cae_smesh_conformal_tet_on_quad_interface on Solid B
        QTemporaryDir dir;
        check(dir.isValid(), "Failed to create temp dir");
        const QString brepB = dir.filePath("solidB.brep");
        const QString vtkB = dir.filePath("solidB_hybrid.vtk");
        check(BRepTools::Write(solidB, QFile::encodeName(brepB).constData()), "Write Solid B BREP");

        QLibrary lib(QDir(QCoreApplication::applicationDirPath()).filePath("plugins/CAE_SMESHBridge"));
        const bool bridgeLoaded = lib.load();
        check(bridgeLoaded, qPrintable("Failed to load CAE_SMESHBridge: " + lib.errorString()));
        using Fn = int (*)(const CaeSMESHConformalHybridRequest*, CaeSMESHConformalHybridResult*, char*, std::size_t);
        auto fn = reinterpret_cast<Fn>(lib.resolve("cae_smesh_conformal_tet_on_quad_interface"));
        check(fn != nullptr, "Could not resolve cae_smesh_conformal_tet_on_quad_interface");

        const QByteArray brepBytes = brepB.toUtf8();
        const QByteArray vtkBytes = vtkB.toUtf8();
        CaeSMESHConformalHybridRequest hreq{};
        hreq.brepPathUtf8 = brepBytes.constData();
        hreq.vtkPathUtf8 = vtkBytes.constData();
        hreq.nodes = ifaceNodes.data();
        hreq.nodeCount = std::int32_t(ifaceNodes.size());
        hreq.quads = ifaceQuads.data();
        hreq.quadCount = std::int32_t(ifaceQuads.size());
        hreq.maxSize = 0.7;
        hreq.minSize = 0.2;
        hreq.optimize = 1;

        CaeSMESHConformalHybridResult hres{};
        char errBuf[512]{};
        const int rc = fn(&hreq, &hres, errBuf, sizeof(errBuf));
        check(rc == 0, errBuf[0] ? errBuf : "cae_smesh_conformal_tet_on_quad_interface returned error");
        check(hres.interfaceQuadsInjected == 9, "Expected 9 injected interface quads");
        check(hres.pyramids >= 9, "SMESH NETGEN_3D did not generate conformal pyramids on GMDS quad interface");
        check(hres.tetrahedra > 0, "SMESH NETGEN_3D did not generate tetrahedra in Solid B");

        // Step 3: Read Solid B VTK mesh and verify 100% node-coordinate match on Z = 2.0 interface!
        auto reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
        reader->SetFileName(vtkBytes.constData());
        reader->Update();
        vtkUnstructuredGrid* gridB = reader->GetOutput();
        check(gridB && gridB->GetNumberOfCells() > 0, "Failed to read Solid B hybrid VTK");

        std::set<std::tuple<long long, long long, long long>> smeshInterfaceCoordKeys;
        int pyramidCount = 0, tetCount = 0;
        for (vtkIdType ci = 0; ci < gridB->GetNumberOfCells(); ++ci)
        {
            const int ct = gridB->GetCellType(ci);
            if (ct == VTK_PYRAMID) ++pyramidCount;
            else if (ct == VTK_TETRA) ++tetCount;
            vtkCell* cell = gridB->GetCell(ci);
            for (vtkIdType pi = 0; pi < cell->GetNumberOfPoints(); ++pi)
            {
                double xyz[3];
                gridB->GetPoint(cell->GetPointId(pi), xyz);
                if (std::abs(xyz[2] - 2.0) < 1e-4)
                    smeshInterfaceCoordKeys.insert(coordKey(xyz[0], xyz[1], xyz[2]));
            }
        }
        check(smeshInterfaceCoordKeys == gmdsInterfaceCoordKeys,
              "Interface node mismatch between GMDS Hex (Solid A) and SMESH Pyramid+Tet (Solid B)!");

        std::cout << "PASS: GMDS Hex (" << jHexes.size() << ") + SMESH Conformal Interface -> "
                  << pyramidCount << " Pyramids + " << tetCount << " Tets, "
                  << smeshInterfaceCoordKeys.size() << "/" << gmdsInterfaceCoordKeys.size()
                  << " exact shared interface nodes!\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
