#include "OccShapeToVtk.h"

// ===== OCC (TKIVtk) =====
#include <TopoDS_Shape.hxx>
#include <Prs3d_Drawer.hxx>

#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>

// ===== VTK =====
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkNew.h>

namespace OCC
{

DisplayPolyData BuildDisplayPolyData(const TopoDS_Shape& shape,
                                     double deviationCoeff,
                                     double deviationAngleRad,
                                     bool smoothShading)
{
    DisplayPolyData out;
    out.surface = vtkSmartPointer<vtkPolyData>::New();
    out.edges   = vtkSmartPointer<vtkPolyData>::New();

    if (shape.IsNull())
        return out;

    // 1) 设置 OCCT 三角化精度（可选，但这是“官方参数”）
    Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
    drawer->SetDeviationCoefficient(deviationCoeff);
    drawer->SetDeviationAngle(deviationAngleRad);

    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(shape);

    // 不同 OCCT 版本这里 API 名字可能不同，你用你能编译过的那个即可：
    // occShape->SetDrawer(drawer);
    // occShape->SetAttributes(drawer);
    // occShape->Attributes() = drawer;

    // 2) Shape -> VTK 数据源
    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    // 3) 面：Shading
    vtkNew<IVtkTools_DisplayModeFilter> dmSurf;
    dmSurf->SetInputConnection(src->GetOutputPort());
    dmSurf->SetDisplayMode(DM_Shading);
    dmSurf->SetSmoothShading(smoothShading);
    dmSurf->SetFaceBoundaryDraw(false); // 不画面边界（避免把 seam 当作“画出来的边”）
    dmSurf->Update();
    out.surface->ShallowCopy(dmSurf->GetOutput());

    // 4) 线：Wireframe（如果你根本不需要单独边线，可以把这一段删掉）
//    vtkNew<IVtkTools_DisplayModeFilter> dmEdge;
//    dmEdge->SetInputConnection(src->GetOutputPort());
//    dmEdge->SetDisplayMode(DM_Wireframe);
//    dmEdge->Update();
//    out.edges->ShallowCopy(dmEdge->GetOutput());

    return out;
}

} // namespace OCC
