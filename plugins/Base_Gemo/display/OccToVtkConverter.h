#pragma once
#include "geometry_global.h"

#include <vtkSmartPointer.h>

class vtkPolyData;
class TopoDS_Shape;

namespace OCC
{
struct DisplayPolyData
{
    vtkSmartPointer<vtkPolyData> surface; // 三角面 + normals（用于光照）
    vtkSmartPointer<vtkPolyData> edges;   // 仅特征边/边界边（球体一般为空）
};

// deviation：参考 FreeCAD Part ViewProvider 的 Deviation（百分比），默认 0.5。
// angularDeflectionDeg：显示离散角度，参考 FreeCAD 默认 28.5°。
// smoothShading：是否输出平滑法线。
// buildEdges：是否生成独立 B-Rep Edge presentation。
GEOMETRY_EXPORT DisplayPolyData BuildDisplayPolyData(const TopoDS_Shape& shape,
                                                      double deviation = 0.5,
                                                      double angularDeflectionDeg = 28.5,
                                                      bool smoothShading = true,
                                                      bool buildEdges = true);
} // namespace OCC
