#include "OccToVtkConverter.h"
#include "OccCadDisplayMeshBuilder.h"

// ===== OCC (TKIVtk) =====
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>

// ===== VTK =====
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultDeviation = 0.5;
constexpr double kDefaultAngularDeflectionDeg = 28.5;

// 兼容 fallback 也采用 FreeCAD Part::Tools::getDeflection 的参数语义，
// 避免主路径失败后突然切回另一套自定义超细 tessellation。
void ensureTriangulation(const TopoDS_Shape& shape,
                         double deviation,
                         double angularDeflectionDeg,
                         bool /*highQualityDisplay*/)
{
    if (shape.IsNull())
        return;

    deviation = deviation > 0.0 ? deviation : kDefaultDeviation;
    angularDeflectionDeg = angularDeflectionDeg > 0.0
                               ? angularDeflectionDeg
                               : kDefaultAngularDeflectionDeg;

    double linearDeflection = Precision::Confusion();
    try
    {
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        bounds.SetGap(0.0);
        if (!bounds.IsVoid())
        {
            Standard_Real xmin = 0.0;
            Standard_Real ymin = 0.0;
            Standard_Real zmin = 0.0;
            Standard_Real xmax = 0.0;
            Standard_Real ymax = 0.0;
            Standard_Real zmax = 0.0;
            bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            const double candidate =
                ((xmax - xmin) + (ymax - ymin) + (zmax - zmin)) / 300.0 * deviation;
            if (std::isfinite(candidate))
                linearDeflection = std::max(candidate, static_cast<double>(Precision::Confusion()));
        }
    }
    catch (...)
    {
        linearDeflection = Precision::Confusion();
    }

    const double angularDeflection =
        std::max(1.0, std::min(angularDeflectionDeg, 180.0)) * kPi / 180.0;

    try
    {
        BRepMesh_IncrementalMesh mesher(shape,
                                        linearDeflection,
                                        Standard_False,
                                        angularDeflection,
                                        Standard_True);
        (void)mesher;
    }
    catch (const Standard_Failure&)
    {
        // 三角化失败不应让几何对象丢失，后续仍交给 TKIVtk 尝试转换。
    }
    catch (...)
    {
    }
}

} // namespace

namespace OCC
{

DisplayPolyData BuildDisplayPolyData(const TopoDS_Shape& shape, double deviationCoeff,
                                     double featureAngleDeg, bool smoothShading, bool buildEdges)
{
    DisplayPolyData out;
    out.surface = vtkSmartPointer<vtkPolyData>::New();
    out.edges = vtkSmartPointer<vtkPolyData>::New();

    if (shape.IsNull())
        return out;

    // v27：自己的 OCC -> VTK presentation 是主路径，不再只在“小模型 + buildEdges”时启用。
    // 即使工程批量加载 buildEdges=false，也由同一个 builder 生成 Surface，只是跳过拓扑边。
    // 这样“创建几何”和“导入 STEP”不会再走两套完全不同的显示引擎。
    const Detail::CadDisplayMeshResult cadDisplay =
        Detail::BuildCadDisplayMesh(shape, deviationCoeff, featureAngleDeg, smoothShading, buildEdges);
    if (cadDisplay.success)
    {
        out.surface = cadDisplay.surface;
        out.edges = cadDisplay.edges;
        return out;
    }

    // 仅在 OCC triangulation 本身失败等异常情况下进入兼容 fallback。
    ensureTriangulation(shape, deviationCoeff, featureAngleDeg, buildEdges);

    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(shape);

    // Shape -> VTK 数据源
    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    // 面：Shading
    vtkNew<IVtkTools_DisplayModeFilter> dmSurf;
    dmSurf->SetInputConnection(src->GetOutputPort());
    dmSurf->SetDisplayMode(DM_Shading);
    dmSurf->SetSmoothShading(smoothShading);
    // When a dedicated edge actor is built, drawing face boundaries again in
    // the shading output creates double lines and depth fighting.  Fast mode
    // has no edge actor, so it keeps lightweight face boundaries instead.
    dmSurf->SetFaceBoundaryDraw(!buildEdges);
    dmSurf->Update();
    out.surface->ShallowCopy(dmSurf->GetOutput());

    // 工程批量加载/复杂对象快速显示时可以跳过独立边 actor，
    // 仍保留 Shading 的 FaceBoundary，避免成百上千个 wireframe actor 拖慢交互。
    if (buildEdges)
    {
        vtkNew<IVtkTools_DisplayModeFilter> dmEdge;
        dmEdge->SetInputConnection(src->GetOutputPort());
        dmEdge->SetDisplayMode(DM_Wireframe);

        IVtk_IdTypeMap edgeTypes;
        edgeTypes.Add(MT_FreeEdge);
        edgeTypes.Add(MT_BoundaryEdge);
        edgeTypes.Add(MT_SharedEdge);

        dmEdge->SetMeshTypesForMode(DM_Wireframe, edgeTypes);
        dmEdge->Update();
        out.edges->ShallowCopy(dmEdge->GetOutput());
    }

    return out;
}

} // namespace OCC
