#pragma once

#include <QSet>
#include <QString>
#include <vector>

#include <gp_Dir.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

class CadBody;
class vtkPolyData;
using GeomShapeObject = CadBody;

namespace GeometryBuildUtils
{
inline constexpr double kEps = 1.0e-7;

gp_Dir getPlaneNormal(int planeType);
gp_Dir axisDirFromType(int axisType);
gp_Dir extrudeDirFromType(int direction);
int directionTypeFromDir(const gp_Dir& dir);
QString directionNameForMessage(int direction);
bool faceNormalDirection(const TopoDS_Face& face, gp_Dir& normal);
bool isValidFaceForExtrude(const TopoDS_Face& face);
bool makeFaceFromWireForExtrude(const TopoDS_Wire& wire, TopoDS_Face& face);
bool collectExtrudableFaces(const TopoDS_Shape& source,
                            std::vector<TopoDS_Face>& faces,
                            int* suggestedDirection = nullptr,
                            QString* message = nullptr,
                            int* openEdgeCount = nullptr,
                            gp_Dir* profileNormal = nullptr);
bool collectExtrudableCurves(const TopoDS_Shape& source,
                             std::vector<TopoDS_Shape>& curves,
                             int* suggestedDirection = nullptr,
                             QString* message = nullptr,
                             gp_Dir* profileNormal = nullptr);
void hideSourceGeometry(const QString& uuid);
GeomShapeObject* validShapeObject(const QString& uuid);
QSet<int> parseEdgeSelection(const QString& text, int edgeCount, bool& ok);
QString encodeStableEdgeSelection(const TopoDS_Shape& shape, const QSet<int>& edgeIndices);
QSet<int> resolveStableEdgeSelection(const TopoDS_Shape& shape, const QString& text, bool& ok);
void registerShape(const QString& uuid, const QString& name, TopoDS_Shape* shape,
                   bool createActors = true);
void registerPreparedShape(const QString& uuid, const QString& name, TopoDS_Shape* shape,
                           vtkPolyData* surface, vtkPolyData* edges);
void removeGeometryAndDimNode(const QString& key);
} // namespace GeometryBuildUtils
