#pragma once

#include <QString>
#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;
class vtkPolyData;

namespace PostTecplotUtils
{
// 将求解器 IIIN 的统一 8 槽位退化实体恢复为真实 VTK 拓扑。
// Tet/Pyramid/Wedge 直接恢复为标准线性实体；少量无法直接等价为标准单元的
// 闭合退化 Hex，严格按原 8 槽位连接拆成标准 VTK_TETRA（不再把唯一点
// 当凸包重新猜连接）。这样 Cutter/Contour 与透明显示都只面对标准实体。
vtkSmartPointer<vtkUnstructuredGrid> normalizeSolverTopology(
    vtkUnstructuredGrid* source);

vtkSmartPointer<vtkUnstructuredGrid> readGrid(const QString& fileName,
                                              QString* errorMessage = nullptr);

// 以下 VTP API 仅保留旧工程兼容/清理用途。
// 当前主显示链以单一 VTU 为权威结果，域显示面在内存中由网格已有 mapped-surface
// 映射得到，不再把 surface/domains 作为主加载依赖。
QString surfaceCachePath(const QString& resultFileName);
QString domainSurfaceCachePath(const QString& resultFileName);
vtkSmartPointer<vtkPolyData> readSurface(const QString& resultFileName,
                                         QString* errorMessage = nullptr);
vtkSmartPointer<vtkPolyData> readDomainSurface(const QString& resultFileName,
                                               QString* errorMessage = nullptr);
}
