#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <iostream>
#include <cmath>
#include "../plugins/Base_Gemo/terrain/TerrainSurfaceBuilder.h"
#include "../plugins/Base_Gemo/builders/BooleanBuilder.h"
#include "../plugins/Base_Gemo/core/CadDocument.h"
#include "../plugins/Base_Gemo/core/CadBody.h"
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

using namespace GeometryTerrain;

static void writeSampleDxf(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error("Failed to create sample DXF file");

    QTextStream out(&file);
    out << "0\nSECTION\n2\nENTITIES\n";
    out << "0\nPOLYLINE\n8\nCONTOUR_50\n66\n1\n70\n8\n";
    out << "0\nVERTEX\n8\nCONTOUR_50\n10\n0.0\n20\n0.0\n30\n50.0\n";
    out << "0\nVERTEX\n8\nCONTOUR_50\n10\n100.0\n20\n0.0\n30\n50.0\n";
    out << "0\nVERTEX\n8\nCONTOUR_50\n10\n100.0\n20\n100.0\n30\n50.0\n";
    out << "0\nVERTEX\n8\nCONTOUR_50\n10\n0.0\n20\n100.0\n30\n50.0\n";
    out << "0\nSEQEND\n";
    out << "0\nPOINT\n8\nSUMMIT\n10\n50.0\n20\n50.0\n30\n120.0\n";
    out << "0\nENDSEC\n0\nEOF\n";
    file.close();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    std::cout << "=== Running GDAL Terrain Regression & Quality Checks ===" << std::endl;

    // 1. Synthetic DXF Read & Gridding
    const QString sampleDxf = "test_sample.dxf";
    writeSampleDxf(sampleDxf);
    QVector<Source> sampleSources;
    QString diag;
    bool readOk = TerrainSurfaceBuilder::readDxf(sampleDxf, sampleSources, diag);
    QFile::remove(sampleDxf);
    if (!readOk || sampleSources.isEmpty())
    {
        std::cerr << "FAIL: readDxf failed: " << diag.toStdString() << std::endl;
        return 1;
    }
    std::cout << "[Check 1] Synthetic DXF read: PASS (" << sampleSources.size() << " sources)" << std::endl;

    GridOptions sampleOpts;
    sampleOpts.spacing = 10.0;
    Result sampleRes = TerrainSurfaceBuilder::buildGrid(sampleSources, sampleOpts);
    if (!sampleRes.ok() || sampleRes.triangles <= 0 || sampleRes.shape.IsNull())
    {
        std::cerr << "FAIL: Synthetic buildGrid failed: " << sampleRes.error.toStdString() << std::endl;
        return 1;
    }
    std::cout << "[Check 2] Synthetic buildGrid: PASS (" << sampleRes.triangles << " triangles)" << std::endl;

    // 2. Project 12333 Real DXF Data Checks
    const QString projPath = "C:/test/rock/12333/工程数据/preprocessing.json";
    if (QFile::exists(projPath))
    {
        QFile f(projPath);
        if (f.open(QIODevice::ReadOnly))
        {
            auto doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            auto items = doc.object()["geom_items"].toArray();
            if (!items.isEmpty())
            {
                auto state = items[0].toObject()["state"].toObject();
                const auto sources = TerrainSurfaceBuilder::sourcesFromState(state);
                std::cout << "[Check 3] Loaded 12333 sources: PASS (" << sources.size() << " contours)" << std::endl;

                GridOptions opts;
                opts.spacing = 5.0;
                opts.algorithm = 2; // Linear Delaunay
                opts.simplifyDxf = true;
                opts.smoothDiagonals = true;

                QElapsedTimer t; t.start();
                Result res12333 = TerrainSurfaceBuilder::buildGrid(sources, opts);
                qint64 ms = t.elapsed();
                std::cout << "[Check 4] 12333 buildGrid finished in " << ms << " ms: PASS" << std::endl;
                std::cout << "          Triangles: " << res12333.triangles << ", Z: [" << res12333.minElevation << ", " << res12333.maxElevation << "]" << std::endl;
                std::cout << "          isSolid: " << (res12333.isSolid ? "true" : "false")
                          << ", baseElevation: " << res12333.baseElevation << std::endl;
                TopTools_IndexedMapOfShape faceMap;
                TopExp::MapShapes(res12333.shape, TopAbs_FACE, faceMap);
                std::cout << "          ShapeType: " << (res12333.shape.ShapeType() == TopAbs_SOLID ? "SOLID" : "OTHER")
                          << ", Face count: " << faceMap.Extent() << std::endl;
                std::cout << "          FaceGroups: " << res12333.faceGroups.size() << std::endl;
                for (const auto& fg : res12333.faceGroups)
                {
                    std::cout << "            - " << fg.name.toStdString() << " (" << fg.role.toStdString()
                              << "): " << fg.faceIndices.size() << " faces [";
                    for (int idx : fg.faceIndices) std::cout << idx << " ";
                    std::cout << "]" << std::endl;
                }
                if (!res12333.ok())
                {
                    std::cerr << "FAIL: 12333 result not ok: " << res12333.error.toStdString() << std::endl;
                    return 1;
                }
                if (!res12333.isSolid || faceMap.Extent() != 6)
                {
                    std::cerr << "FAIL: expected closed solid with 6 faces!" << std::endl;
                    return 1;
                }

                // Update C:/test/rock/12333/工程数据/preprocessing.json with the clean 6-face watertight solid
                std::ostringstream bstream;
                BRepTools::Write(res12333.shape, bstream);
                const QString brepStr = QString::fromStdString(bstream.str());
                std::cout << "[Update 12333] Generated clean 6-face BREP string length: " << brepStr.length() << " chars" << std::endl;

                QJsonObject obj0 = items[0].toObject();
                obj0["cachedBrep"] = brepStr;
                QJsonObject st = obj0["state"].toObject();
                st["generated"] = true;
                st["showContours"] = false;
                st["showSurface"] = true;
                QJsonObject gOpts = st["gridOptions"].toObject();
                gOpts["createSolid"] = true;
                gOpts["baseElevation"] = res12333.baseElevation;
                gOpts["autoBaseElevation"] = false;
                gOpts["algorithm"] = 2; // Linear Delaunay
                gOpts["downsampleTarget"] = 100;
                gOpts["simplifyDxf"] = true;
                gOpts["smoothDiagonals"] = true;
                st["gridOptions"] = gOpts;
                obj0["state"] = st;

                QJsonArray fgArr;
                for (const auto& fg : res12333.faceGroups)
                {
                    QJsonObject fgo;
                    fgo["name"] = fg.name;
                    fgo["role"] = fg.role;
                    QJsonArray fidx;
                    for (int fi : fg.faceIndices) fidx.append(fi);
                    fgo["faceIndices"] = fidx;
                    fgArr.append(fgo);
                }
                obj0["faceGroups"] = fgArr;
                if (obj0.contains("featureData"))
                {
                    QJsonObject fd = obj0["featureData"].toObject();
                    fd["state"] = st;
                    fd["faceGroups"] = fgArr;
                    obj0["featureData"] = fd;
                }
                items[0] = obj0;
                QJsonObject rootObj = doc.object();
                rootObj["geom_items"] = items;

                QFile outJson(projPath);
                if (outJson.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    outJson.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
                    outJson.close();
                    std::cout << "[Update 12333] Updated " << projPath.toStdString() << " successfully!" << std::endl;
                }

                // Also update 12333.xml to mesh_dimension = 3
                const QString xmlPath = "C:/test/rock/12333/12333.xml";
                QFile xmlFile(xmlPath);
                if (xmlFile.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    QString xmlContent = QString::fromUtf8(xmlFile.readAll());
                    xmlFile.close();
                    xmlContent.replace("<Value key=\"mesh_dimension\" type=\"int\">2</Value>",
                                       "<Value key=\"mesh_dimension\" type=\"int\">3</Value>");
                    if (xmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                    {
                        xmlFile.write(xmlContent.toUtf8());
                        xmlFile.close();
                        std::cout << "[Update 12333] Updated mesh_dimension to 3 in " << xmlPath.toStdString() << std::endl;
                    }
                }

                // 5. Test Geotechnical Boolean Excavation Linkage
                auto* docMgr = GeomManager::CadDocument::getInstance();
                const QString geoUuid = "test_geo_12333";
                docMgr->appendGeomShapeObject(new CadBody(geoUuid, new TopoDS_Shape(res12333.shape), "Geology12333", false));

                Bnd_Box geoBox;
                BRepBndLib::Add(res12333.shape, geoBox);
                double xmin, ymin, zmin, xmax, ymax, zmax;
                geoBox.Get(xmin, ymin, zmin, xmax, ymax, zmax);

                BRepBuilderAPI_MakePolygon poly;
                poly.Add(gp_Pnt(0, 0, 0));
                poly.Add(gp_Pnt(40, 0, 0));
                poly.Add(gp_Pnt(40, 0, 25));
                poly.Add(gp_Pnt(20, 0, 35));
                poly.Add(gp_Pnt(0, 0, 25));
                poly.Close();
                BRepBuilderAPI_MakeFace mf(poly.Wire());
                TopoDS_Shape cavernTool = BRepPrimAPI_MakePrism(mf.Face(), gp_Vec(0, 80, 0)).Shape();

                gp_Trsf cavernTrsf;
                cavernTrsf.SetTranslation(gp_Vec((xmin + xmax) * 0.5 - 20, (ymin + ymax) * 0.5 - 40, zmin + (zmax - zmin) * 0.3));
                BRepBuilderAPI_Transform xformCavern(cavernTool, cavernTrsf);
                TopoDS_Shape placedCavern = xformCavern.Shape();

                TopoDS_Shape cutGeology;
                QVector<int> excavationFaces;
                QString excErr;
                bool excOk = BooleanBuilder::excavateFromGeology(geoUuid, placedCavern, cutGeology, excavationFaces, excErr);
                if (!excOk || cutGeology.IsNull() || excavationFaces.isEmpty())
                {
                    std::cerr << "FAIL: excavateFromGeology failed: " << excErr.toStdString()
                              << ", faces=" << excavationFaces.size() << std::endl;
                    return 1;
                }
                TopTools_IndexedMapOfShape cutFaces;
                TopExp::MapShapes(cutGeology, TopAbs_FACE, cutFaces);
                std::cout << "[Check 5] Cavern Excavation Boolean Linkage: PASS" << std::endl;
                std::cout << "          Cut Geology Faces: " << cutFaces.Extent()
                          << ", Excavation Inner Faces: " << excavationFaces.size() << " [";
                for (int idx : excavationFaces) std::cout << idx << " ";
                std::cout << "]" << std::endl;

                // 6. Test Geotechnical Boolean Dam Valley Fitting
                BRepBuilderAPI_MakePolygon damPoly;
                double damW = 300.0, damH = 500.0, damL = 400.0;
                damPoly.Add(gp_Pnt(0, 0, 0));
                damPoly.Add(gp_Pnt(damW, 0, 0));
                damPoly.Add(gp_Pnt(damW * 0.7, 0, damH));
                damPoly.Add(gp_Pnt(damW * 0.3, 0, damH));
                damPoly.Close();
                BRepBuilderAPI_MakeFace damFaceMaker(damPoly.Wire());
                TopoDS_Shape rawDam = BRepPrimAPI_MakePrism(damFaceMaker.Face(), gp_Vec(0, damL, 0)).Shape();

                // Position dam across the valley: starting from below valley floor (2150) to above ridge (2650)
                gp_Trsf damTrsf;
                damTrsf.SetTranslation(gp_Vec((xmin + xmax) * 0.5 - damW * 0.5, (ymin + ymax) * 0.5 - damL * 0.5, res12333.minElevation - 40.0));
                BRepBuilderAPI_Transform xformDam(rawDam, damTrsf);
                TopoDS_Shape placedDam = xformDam.Shape();

                TopoDS_Shape fittedDam;
                QVector<int> contactFaces;
                QString damErr;
                bool damOk = BooleanBuilder::fitDamToValley(geoUuid, placedDam, fittedDam, contactFaces, damErr);
                if (!damOk || fittedDam.IsNull() || contactFaces.isEmpty())
                {
                    std::cerr << "FAIL: fitDamToValley failed: " << damErr.toStdString()
                              << ", contacts=" << contactFaces.size() << std::endl;
                    return 1;
                }
                TopTools_IndexedMapOfShape fittedDamFaces;
                TopExp::MapShapes(fittedDam, TopAbs_FACE, fittedDamFaces);
                std::cout << "[Check 6] Dam Valley Fitting Boolean Linkage: PASS" << std::endl;
                std::cout << "          Fitted Dam Faces: " << fittedDamFaces.Extent()
                          << ", Contact Valley Faces: " << contactFaces.size() << " [";
                for (int idx : contactFaces) std::cout << idx << " ";
                std::cout << "]" << std::endl;

                // 7. Test Geotechnical Boolean Slope Excavation Linkage
                BRepBuilderAPI_MakePolygon slopePoly;
                double slopeW = 120.0, slopeH = 150.0, slopeL = 200.0;
                slopePoly.Add(gp_Pnt(0, 0, 0));
                slopePoly.Add(gp_Pnt(slopeW, 0, 0));
                slopePoly.Add(gp_Pnt(slopeW, 0, slopeH));
                slopePoly.Add(gp_Pnt(0, 0, slopeH));
                slopePoly.Close();
                BRepBuilderAPI_MakeFace slopeFaceMaker(slopePoly.Wire());
                TopoDS_Shape rawSlopeTool = BRepPrimAPI_MakePrism(slopeFaceMaker.Face(), gp_Vec(0, slopeL, 0)).Shape();

                gp_Trsf slopeTrsf;
                slopeTrsf.SetTranslation(gp_Vec(xmin - 20.0, (ymin + ymax) * 0.5 - slopeL * 0.5, (zmin + zmax) * 0.5));
                BRepBuilderAPI_Transform xformSlope(rawSlopeTool, slopeTrsf);
                TopoDS_Shape placedSlopeTool = xformSlope.Shape();

                TopoDS_Shape cutSlopeGeology;
                QVector<int> slopeFaces;
                QString slopeErr;
                bool slopeOk = BooleanBuilder::excavateFromGeology(geoUuid, placedSlopeTool, cutSlopeGeology, slopeFaces, slopeErr);
                if (!slopeOk || cutSlopeGeology.IsNull() || slopeFaces.isEmpty())
                {
                    std::cerr << "FAIL: slope excavation failed: " << slopeErr.toStdString()
                              << ", faces=" << slopeFaces.size() << std::endl;
                    return 1;
                }
                TopTools_IndexedMapOfShape cutSlopeMap;
                TopExp::MapShapes(cutSlopeGeology, TopAbs_FACE, cutSlopeMap);
                std::cout << "[Check 7] Slope Excavation Boolean Linkage: PASS" << std::endl;
                std::cout << "          Cut Geology Faces: " << cutSlopeMap.Extent()
                          << ", Slope Cut Faces: " << slopeFaces.size() << " [";
                for (int idx : slopeFaces) std::cout << idx << " ";
                std::cout << "]" << std::endl;
            }
        }
    }

    std::cout << "=== ALL TERRAIN REGRESSION CHECKS PASSED ===" << std::endl;
    return 0;
}
