#pragma once

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

class TopoDS_Shape;

namespace OCC
{
namespace Detail
{

struct CadDisplayMeshResult
{
    vtkSmartPointer<vtkPolyData> surface;
    vtkSmartPointer<vtkPolyData> edges;
    bool success = false;
};

// OCC -> VTK CAD presentation builder，数据组织参考 FreeCAD PartGui：
// - 整个 Shape 先 Clean，再单次 BRepMesh_IncrementalMesh；
// - Face 直接读取 Poly_Triangulation；
// - Face-owned Edge 直接读取同一 triangulation 的 PolygonOnTriangulation；
// - 真正 free edge 使用与 FreeCAD Part::Tools::polygonOfEdge 等价的 Polygon3D 路径；
// - 不再自行递归采样 BRepAdaptor_Curve；
// - Surface 与 Edge 共用同一个 vtkPoints；
// - OCC_FaceId / OCC_EdgeId 使用 TopExp 稳定索引；
// - buildEdges=false 时仍使用同一 surface presentation，只跳过拓扑边构建。
CadDisplayMeshResult BuildCadDisplayMesh(const TopoDS_Shape& shape,
                                         double deviationCoeff,
                                         double featureAngleDeg,
                                         bool smoothShading,
                                         bool buildEdges = true);

} // namespace Detail
} // namespace OCC
