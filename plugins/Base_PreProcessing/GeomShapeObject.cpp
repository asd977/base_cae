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


    auto surfPoly = OCC::ShapeToVtkPolyData(*shape, 0.001);

    auto surfMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    surfMapper->SetInputData(surfPoly);

    auto surfActor = vtkSmartPointer<vtkActor>::New();
    surfActor->SetMapper(surfMapper);
    auto edgePoly = OCC::ShapeEdgesToVtkLines(*shape, 0.005);

    auto edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    edgeMapper->SetInputData(edgePoly);

    auto edgeActor = vtkSmartPointer<vtkActor>::New();
    edgeActor->SetMapper(edgeMapper);
    edgeActor->GetProperty()->SetRepresentationToWireframe();
    edgeActor->GetProperty()->SetLineWidth(1);
    edgeActor->GetProperty()->SetColor(0, 0, 0);

    m_surfActor = surfActor;
    m_edgeActor = edgeActor;
    qDebug()<<"create ok "<<m_uuid;
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
