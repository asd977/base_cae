// OccShapeToVtk.cpp
#include "OccShapeToVtk.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>

#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <qDebug>
#include <BRepBndLib.hxx>

namespace OCC
{
vtkSmartPointer<vtkPolyData> ShapeToVtkPolyData(const TopoDS_Shape& shape,
                                                double deflection)
{
    vtkSmartPointer<vtkPolyData> polyData =
        vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints> points =
        vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> polys =
        vtkSmartPointer<vtkCellArray>::New();

    if (shape.IsNull())
        return polyData;

    // 三角化
    BRepMesh_IncrementalMesh mesher(shape, deflection);

    TopExp_Explorer exp;
    for (exp.Init(shape, TopAbs_FACE); exp.More(); exp.Next())
    {
        TopoDS_Face face = TopoDS::Face(exp.Current());

        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull())
            continue;

        gp_Trsf trsf = loc.Transformation();

        // 记录当前 face 的起始点索引
        vtkIdType baseId = points->GetNumberOfPoints();

        // 1) 插点：用 NbNodes() + Node(i)
        const Standard_Integer nbNodes = tri->NbNodes();
        for (Standard_Integer i = 1; i <= nbNodes; ++i)
        {
            gp_Pnt p = tri->Node(i);  // 注意：Node(i)
            p.Transform(trsf);
            points->InsertNextPoint(p.X(), p.Y(), p.Z());
        }

        // 2) 插三角形：用 NbTriangles() + Triangle(i)
        const Standard_Integer nbTris = tri->NbTriangles();
        for (Standard_Integer i = 1; i <= nbTris; ++i)
        {
            Poly_Triangle t = tri->Triangle(i);  // Triangle(i)
            Standard_Integer n1, n2, n3;
            t.Get(n1, n2, n3);

            vtkIdType ids[3];

            if (face.Orientation() == TopAbs_REVERSED)
            {
                ids[0] = baseId + (n3 - 1);
                ids[1] = baseId + (n2 - 1);
                ids[2] = baseId + (n1 - 1);
            }
            else
            {
                ids[0] = baseId + (n1 - 1);
                ids[1] = baseId + (n2 - 1);
                ids[2] = baseId + (n3 - 1);
            }

            polys->InsertNextCell(3, ids);
        }
    }

    polyData->SetPoints(points);
    polyData->SetPolys(polys);
    return polyData;
}

void TriangulateOneShape(const TopoDS_Shape& shape, double factor)
{
    if (shape.IsNull())
        return;

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xMin, xMax, yMin, yMax, zMin, zMax;
    box.Get(xMin, yMin, zMin, xMax, yMax, zMax);

    double sizeFactor = std::sqrt( std::pow(xMax - xMin, 2.0)
                                  + std::pow(yMax - yMin, 2.0)
                                  + std::pow(zMax - zMin, 2.0) );
    double lineDef = factor * sizeFactor;

    IMeshTools_Parameters params;
    params.Deflection = lineDef;

    BRepMesh_IncrementalMesh mesh(shape, params);
}

vtkSmartPointer<vtkPolyData> ShapeEdgesToVtkLines(const TopoDS_Shape& shape,
                                                  double factor)
{
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    auto points   = vtkSmartPointer<vtkPoints>::New();
    auto lines    = vtkSmartPointer<vtkCellArray>::New();

    if (shape.IsNull())
        return polyData;

    TopExp_Explorer exp;
    for (exp.Init(shape, TopAbs_EDGE); exp.More(); exp.Next())
    {
        TopoDS_Edge edge = TopoDS::Edge(exp.Current());

        // 关键：对“当前这条边”单独三角化
        TriangulateOneShape(edge, factor);

        TopLoc_Location loc;
        Handle(Poly_Polygon3D) poly = BRep_Tool::Polygon3D(edge, loc);
        if (poly.IsNull())
            continue;

        const TColgp_Array1OfPnt& nodes = poly->Nodes();
        const Standard_Integer lower = nodes.Lower();
        const Standard_Integer upper = nodes.Upper();
        const Standard_Integer nPts  = upper - lower + 1;
        if (nPts < 2)
            continue;

        gp_Trsf trsf = loc.Transformation();
        vtkIdType baseId = points->GetNumberOfPoints();

        // 1) 插点
        for (Standard_Integer i = lower; i <= upper; ++i)
        {
            gp_Pnt p = nodes.Value(i);
            p.Transform(trsf);
            points->InsertNextPoint(p.X(), p.Y(), p.Z());
        }

        // 2) 插线段
        for (Standard_Integer i = 0; i < nPts - 1; ++i)
        {
            vtkIdType ids[2];
            ids[0] = baseId + i;
            ids[1] = baseId + i + 1;
            lines->InsertNextCell(2, ids);
        }
    }

    polyData->SetPoints(points);
    polyData->SetLines(lines);

    return polyData;
}
}
