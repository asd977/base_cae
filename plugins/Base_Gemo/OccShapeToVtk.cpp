#include "OccShapeToVtk.h"

#include <algorithm>
#include <cmath>

// ===== OCC =====
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>

#include <BRepMesh_IncrementalMesh.hxx>   // ★新增：强制 OCC 侧三角化
#include <Prs3d_Drawer.hxx>
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>

// ===== VTK =====
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkNew.h>

#include <vtkTriangleFilter.h>            // ★新增：统一三角形
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

    // ---------- 1) 参数 ----------
    const double diag = ShapeDiag(shape);

    // 允许更小的 deviationCoeff（球体更需要细一点）
    deviationCoeff   = Clamp(deviationCoeff, 0.0001, 0.02);
    featureAngleDeg  = Clamp(featureAngleDeg, 5.0, 120.0);

    // 线性偏差（绝对值）：OCC 侧三角化用它更稳定
    const double linDefl = std::max(1e-6, diag * deviationCoeff);
    const double angDefl = 0.15; // 弧度，越小越细；球体推荐 0.10~0.20

    // ★关键：球体 seam 的点通常“非常接近但不完全相等”
    // 你原先 *1e-3 太保守，焊不上。这里提高到 5e-2（仍然远小于 linDefl）
    const double cleanTol = std::max(diag * 1e-10, linDefl * 5e-2);

    // ---------- 2) ★先在 OCC 侧强制三角化（避免 seam 两侧采样不一致） ----------
    // relative=true 表示 linDefl 作为相对/绝对取决于版本实现；这里我们给的是绝对值量级
    // 如果你的 OCCT 版本构造参数不匹配，就用 Set* 的形式（见下方注释替代写法）
    try
    {
        BRepMesh_IncrementalMesh mesher(shape, linDefl, /*isRelative*/ false, angDefl, /*parallel*/ true);
        (void)mesher;
    }
    catch (...)
    {
        // 即使失败也继续走 TKIVtk（不至于直接崩）
    }

    // ---------- 3) TKIVtk：Shape -> PolyData ----------
    Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
    drawer->SetDeviationCoefficient(deviationCoeff);
    drawer->SetDeviationAngle(angDefl);

    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(shape);

    // ★如果你能找到“设置 Drawer/Attributes”的正确 API，就把它打开
    // 不同版本名字不同，你任选能编译的一个：
    //
    // occShape->SetDrawer(drawer);
    // occShape->SetAttributes(drawer);
    // occShape->Attributes() = drawer;

    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    vtkNew<IVtkTools_DisplayModeFilter> dm;
    dm->SetDisplayMode(DM_Shading);
    dm->SetSmoothShading(smoothShading);
    dm->SetFaceBoundaryDraw(false);   // 不画面边界（避免 seam 线被显式画出来）
    dm->SetInputConnection(src->GetOutputPort());

    // ---------- 4) ★TriangleFilter：统一为三角形（更利于 clean / normals） ----------
    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputConnection(dm->GetOutputPort());
    tri->Update();

    // ---------- 5) ★Clean：焊接 seam（容差用 cleanTol） ----------
    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputConnection(tri->GetOutputPort());
    clean->PointMergingOn();
    clean->SetToleranceIsAbsolute(true);
    clean->SetAbsoluteTolerance(cleanTol);
    clean->Update();

    // ---------- 6) ★Normals：球体优先“连续法线”，不要拆分 ----------
    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(clean->GetOutputPort());
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();

    // ★关键：避免 seam 被当成锐边拆法线
    normals->SplittingOff();
    // 如果你坚持要 SplittingOn，也务必 180：
    // normals->SplittingOn();
    // normals->SetFeatureAngle(180.0);

    normals->Update();
    out.surface->ShallowCopy(normals->GetOutput());

    // ---------- 7) Edges：只提取特征边/边界边（球体一般不会有） ----------
    vtkNew<vtkFeatureEdges> fe;
    fe->SetInputConnection(clean->GetOutputPort());
    fe->BoundaryEdgesOn();
    fe->FeatureEdgesOn();
    fe->ManifoldEdgesOff();
    fe->NonManifoldEdgesOff();
    fe->SetFeatureAngle(featureAngleDeg);
    fe->ColoringOff();
    fe->Update();

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
