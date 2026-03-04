#include "OccShapeToVtk.h"

// ===== OCC (TKIVtk) =====
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <Prs3d_Drawer.hxx>
#include <TopoDS_Shape.hxx>

// ===== VTK =====
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace OCC
{

DisplayPolyData BuildDisplayPolyData(const TopoDS_Shape& shape, double deviationCoeff,
                                     double deviationAngleRad, bool smoothShading)
{
    DisplayPolyData out;
    out.surface = vtkSmartPointer<vtkPolyData>::New();
    out.edges = vtkSmartPointer<vtkPolyData>::New();

    if (shape.IsNull())
        return out;

    // 1) 设置 OCCT 三角化精度
    Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
    drawer->SetDeviationCoefficient(deviationCoeff);
    drawer->SetDeviationAngle(deviationAngleRad);

    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(shape);

    // 2) Shape -> VTK 数据源
    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    // 3) 面：Shading
    vtkNew<IVtkTools_DisplayModeFilter> dmSurf;
    dmSurf->SetInputConnection(src->GetOutputPort());
    dmSurf->SetDisplayMode(DM_Shading);
    dmSurf->SetSmoothShading(smoothShading);
    dmSurf->SetFaceBoundaryDraw(true);
    dmSurf->Update();
    out.surface->ShallowCopy(dmSurf->GetOutput());

    // 4) 边
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

    return out;
}

} // namespace OCC
