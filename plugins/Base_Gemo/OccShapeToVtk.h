#pragma once

#include <vtkSmartPointer.h>

class vtkPolyData;
class TopoDS_Shape;

namespace OCC {
struct DisplayPolyData {
  vtkSmartPointer<vtkPolyData> surface;  // 三角面 + normals（用于光照）
  vtkSmartPointer<vtkPolyData> edges;    // 仅特征边/边界边（球体一般为空）
};

// deviationCoeff：相对显示精度（越小越细，常用 0.0008~0.005）
// featureAngleDeg：锐边阈值（越小越“敏感”，常用 25~45）
// smoothShading：是否输出平滑法线（建议 true）
DisplayPolyData BuildDisplayPolyData(const TopoDS_Shape& shape,
                                     double deviationCoeff = 0.0015,
                                     double featureAngleDeg = 30.0,
                                     bool smoothShading = true);
}  // namespace OCC
