#include "OccShapeToVtk.h"

#include <algorithm>
#include <cmath>

// ===== OCC =====
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>

#include <Prs3d_Drawer.hxx>
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>

// ===== VTK =====
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkNew.h>

#include <vtkCleanPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkFeatureEdges.h>

namespace OCC
{
namespace
{
static double Clamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(v, hi));
}

static double ShapeDiag(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid())
        return 1.0;

    double xMin, yMin, zMin, xMax, yMax, zMax;
    box.Get(xMin, yMin, zMin, xMax, yMax, zMax);

    const double dx = xMax - xMin;
    const double dy = yMax - yMin;
    const double dz = zMax - zMin;
    const double diag = std::sqrt(dx*dx + dy*dy + dz*dz);
    return (diag > 0.0) ? diag : 1.0;
}
} // namespace

DisplayPolyData BuildDisplayPolyData(const TopoDS_Shape& shape,
                                     double deviationCoeff,
                                     double featureAngleDeg,
                                     bool smoothShading)
{
    DisplayPolyData out;
    out.surface = vtkSmartPointer<vtkPolyData>::New();
    out.edges   = vtkSmartPointer<vtkPolyData>::New();

    if (shape.IsNull())
        return out;

    // ---------- 1) 计算参数 ----------
    const double diag = ShapeDiag(shape);

    // deviationCoeff 是“相对精度”（SizeOfObject * deviationCoeff）
    // 文档解释：相对偏差 = 物体尺寸 * DeviationCoefficient :contentReference[oaicite:2]{index=2}
    deviationCoeff = Clamp(deviationCoeff, 0.0003, 0.02);
    featureAngleDeg = Clamp(featureAngleDeg, 5.0, 80.0);

    // Clean 容差：用于焊接 seam/重复点（对球体 seam 很关键）
    // 取相对偏差的 1e-3~1e-2 比较稳；并设置一个极小下限
    const double cleanTol = std::max(diag * 1e-9, (diag * deviationCoeff) * 1e-3);

    // ---------- 2) TKIVtk：Shape -> PolyData ----------
    // (a) drawer 控制三角化质量（相对偏差 + 角度）
    Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
    drawer->SetDeviationCoefficient(deviationCoeff);
    drawer->SetDeviationAngle(0.25); // 弧度，越小越细；一般 0.15~0.35

    // (b) IVtkOCC_Shape 包装 OCC shape
    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(shape);
    // 不同 OCCT 版本对 Attributes() 可写性略不同；下面这句大多版本可用：
    // 如果你这里编译不过，就删掉（仍然能跑，只是用默认精度）。
    //occShape->Attributes() = drawer;

    // (c) DataSource
    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    // ---------- 3) DisplayModeFilter：只要 Shading 输出 ----------
    vtkNew<IVtkTools_DisplayModeFilter> dm;
    dm->SetDisplayMode(DM_Shading);
    dm->SetSmoothShading(smoothShading);     // DM_Shading 时输出顶点法线 :contentReference[oaicite:3]{index=3}
    dm->SetFaceBoundaryDraw(false);          // 关键：不画“面边界”（球体 seam 线从这里最常见）
    dm->SetInputConnection(src->GetOutputPort());

    // ---------- 4) Clean：焊接 seam/重复点，减少球体褶皱/虚线 ----------
    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputConnection(dm->GetOutputPort());
    clean->PointMergingOn();
    clean->SetToleranceIsAbsolute(true);
    clean->SetAbsoluteTolerance(cleanTol);
    clean->Update();

    // ---------- 5) Normals：让光照更稳定（球体褶皱很多时候是法线不连续/不一致） ----------
    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(clean->GetOutputPort());
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();

    // 既保留硬边（立方体不发圆），又不影响球体（球体夹角都很小）
    normals->SplittingOn();
    normals->SetFeatureAngle(45.0);

    normals->Update();

    // 把输出拷贝出来（避免 pipeline 对象析构后悬空）
    out.surface->ShallowCopy(normals->GetOutput());

    // ---------- 6) Edges：不要用“全部线框”，只提取“特征边/边界边” ----------
    // 这样球体不会出一堆乱线/虚线；圆柱/立方体等硬边仍然清晰
    vtkNew<vtkFeatureEdges> fe;
    fe->SetInputConnection(clean->GetOutputPort());

    fe->BoundaryEdgesOn();     // 边界边
    fe->FeatureEdgesOn();      // 特征锐边
    fe->ManifoldEdgesOff();
    fe->NonManifoldEdgesOff();

    fe->SetFeatureAngle(featureAngleDeg);

    // 关键：关掉 Coloring，避免生成 edge type 标量导致“边变红”
    fe->ColoringOff();

    fe->Update();

    // 线也 clean 一下（去重）
    vtkNew<vtkCleanPolyData> cleanEdges;
    cleanEdges->SetInputConnection(fe->GetOutputPort());
    cleanEdges->PointMergingOn();
    cleanEdges->SetToleranceIsAbsolute(true);
    cleanEdges->SetAbsoluteTolerance(cleanTol);
    cleanEdges->Update();

    out.edges->ShallowCopy(cleanEdges->GetOutput());
    return out;
}

} // namespace OCC
