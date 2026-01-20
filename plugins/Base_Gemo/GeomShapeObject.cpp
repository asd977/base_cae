#include "GeomShapeObject.h"
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <QUuid>
#include <OccShapeToVtk.h>

GeomShapeObject::GeomShapeObject(QString uuid, TopoDS_Shape* shape)
:_shape(shape)
{
    m_uuid = uuid;
    initActor();
}

GeomShapeObject::~GeomShapeObject()
{
	if (_shape)
	{
		delete _shape;
		_shape = nullptr;
	}

}

TopoDS_Shape * GeomShapeObject::getShape()
{
	if (_shape)
	{
		return _shape;
	}
	return nullptr;
}

void GeomShapeObject::setActor(vtkSmartPointer<vtkActor> surfActor, vtkSmartPointer<vtkActor> edgeActor)
{
    m_surfActor = surfActor;
    m_edgeActor = edgeActor;
}

vtkSmartPointer<vtkActor> GeomShapeObject::getSurfActor()
{
    return m_surfActor;
}

vtkSmartPointer<vtkActor> GeomShapeObject::getEdgeActor()
{
    return m_edgeActor;
}

void GeomShapeObject::initActor()
{

    auto disp = OCC::BuildDisplayPolyData(*_shape);

    // ---- surface ----
    auto surfMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    surfMapper->SetInputData(disp.surface);
    surfMapper->ScalarVisibilityOff(); // 你要统一颜色就关掉，否则会被 TKIVtk 的 scalars/LUT 上色

    auto surfActor = vtkSmartPointer<vtkActor>::New();
    surfActor->SetMapper(surfMapper);
    auto p = surfActor->GetProperty();
    p->LightingOff();
    p->SetColor(0.85, 0.85, 0.85);
    // surfActor->GetProperty()->LightingOff();

    // surfActor->GetProperty()->SetInterpolationToPhong();

    // ---- edges ----
    auto edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    edgeMapper->SetInputData(disp.edges);
    edgeMapper->ScalarVisibilityOff(); // 关键：不关就会出现“红边”

    auto edgeActor = vtkSmartPointer<vtkActor>::New();
    edgeActor->SetMapper(edgeMapper);
    edgeActor->GetProperty()->SetLineWidth(1.0);
    edgeActor->GetProperty()->SetColor(0.0, 0.0, 0.0);

    m_surfActor = surfActor;
    m_edgeActor = edgeActor;
}
