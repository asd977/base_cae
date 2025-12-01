#include "GemoPickerInteractorStyle.h"
#include <vtkProperty.h>
#include <vtkPropCollection.h>
#include <vtkInteractorObserver.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkPolyData.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkDataSet.h>
#include <vtkRenderer.h>
#include <vtkPolyDataMapper.h>
#include <vtkSphereSource.h>
#include <vtkActor.h>
#include <vtkUnstructuredGrid.h>
#include <vtkDataSetMapper.h>
#include <QDebug>

GemoPickerInteractorStyle* GemoPickerInteractorStyle::New()
{
	return new GemoPickerInteractorStyle;
}

void GemoPickerInteractorStyle::setPickList(QList<vtkSmartPointer<vtkActor>> lst)
{
	// 先清除老的actor 在设置新的actor
	for (auto actor : m_actorsPickLst) 
	{
		m_cellPicker->DeletePickList(actor);
		m_pointPicker->DeletePickList(actor);
	}
	m_actorsPickLst = lst;

	for (auto actor : lst) 
	{
		if (actor)
		{
			m_cellPicker->AddPickList(actor);
			m_pointPicker->AddPickList(actor);
		}
	}
}

void GemoPickerInteractorStyle::OnLeftButtonDown()
{
	highlight();
	vtkInteractorStyleTrackballCamera::OnLeftButtonDown();
}

void GemoPickerInteractorStyle::highlight()
{
	switch(m_selectedMode)
	{
	case GUI::SelectGeomMode::SelectNone:
		break;
	case GUI::SelectGeomMode::SelectVertex:
		highlightVertex();
		break;
	case GUI::SelectGeomMode::SelectPolyLine:
		highlightLine();
		break;
	case GUI::SelectGeomMode::SelectSurface:
		highlightSurface();
		break;
	case GUI::SelectGeomMode::SelectVolume:
		highlightVolume();
		break;
	default:
		break;
	}
}

void GemoPickerInteractorStyle::highlightVolume()
{
	int* pos = this->GetInteractor()->GetEventPosition();
	m_cellPicker->Pick(pos[0], pos[1], pos[2], this->GetDefaultRenderer());
	int cellId = -1;
	int pointId = -1;
	double* pos1 = m_cellPicker->GetPickPosition();
	pointId = m_cellPicker->GetPointId();
	vtkActor* actor = m_cellPicker->GetActor();
	cellId = m_cellPicker->GetCellId();
	if (pointId == -1 || cellId == -1)
	{
		return;
	}
	emit selectGemoSolid(actor, cellId, pointId);
}

void GemoPickerInteractorStyle::highlightSurface()
{
	int* pos = this->GetInteractor()->GetEventPosition();
	m_cellPicker->Pick(pos[0], pos[1], pos[2], this->GetDefaultRenderer());
	int cellId = -1;
	int pointId = -1;
	double* pos1 = m_cellPicker->GetPickPosition();
	pointId = m_cellPicker->GetPointId();
	vtkActor* actor = m_cellPicker->GetActor();
	cellId = m_cellPicker->GetCellId();
	if (pointId == -1 || cellId == -1)
	{
		return;
	}
	emit selectGemoFace(actor, cellId, pointId);
}

void GemoPickerInteractorStyle::highlightLine()
{
	int* pos = this->GetInteractor()->GetEventPosition();
	m_cellPicker->Pick(pos[0], pos[1], pos[2], this->GetDefaultRenderer());
	int cellId = -1;
	int pointId = -1;
	double* pos1 = m_cellPicker->GetPickPosition();
	pointId = m_cellPicker->GetPointId();
	vtkActor* actor = m_cellPicker->GetActor();
	cellId = m_cellPicker->GetCellId();
	if (pointId == -1|| cellId==-1)
	{
		return;
	}
	emit selectGemoLine(actor, cellId, pointId);
}

void GemoPickerInteractorStyle::highlightVertex()
{
	int* pos = this->GetInteractor()->GetEventPosition();
	m_pointPicker->Pick(pos[0], pos[1], pos[2], this->GetDefaultRenderer());
	int cellId = -1;
	int pointId = -1;
	pointId = m_pointPicker->GetPointId();
	vtkActor* actor = m_pointPicker->GetActor();
	if (pointId == -1 ) 
	{
		return;
	}
	emit selectGemoPoint(actor, cellId, pointId);
}

void GemoPickerInteractorStyle::displayPicked()
{
	int* pos = this->GetInteractor()->GetEventPosition();
	m_cellPicker->Pick(pos[0], pos[1], pos[2], this->GetDefaultRenderer());

	vtkActor* pickedActor = m_cellPicker->GetActor();
	if (!pickedActor) return;

	vtkActorCollection* array = this->GetDefaultRenderer()->GetActors();
	if (!array) return;

	int actorNum = array->GetNumberOfItems();
	for (int i = 0; i < actorNum; i++)
	{
		vtkSmartPointer<vtkActor> currActor = vtkActor::SafeDownCast(array->GetItemAsObject(i));
		if (currActor == nullptr)
			continue;
		if (currActor != pickedActor) {
			currActor->SetVisibility(false);
		}
	}
}

void GemoPickerInteractorStyle::displayUnpicked()
{
	int* pos = this->GetInteractor()->GetEventPosition();
	m_cellPicker->Pick(pos[0], pos[1], pos[2], this->GetDefaultRenderer());

	vtkActor* pickedActor = m_cellPicker->GetActor();
	if (!pickedActor) return;

	vtkActorCollection* array = this->GetDefaultRenderer()->GetActors();
	if (!array) return;

	int actorNum = array->GetNumberOfItems();
	for (int i = 0; i < actorNum; i++)
	{
		vtkSmartPointer<vtkActor> currActor = vtkActor::SafeDownCast(array->GetItemAsObject(i));
		if (currActor == nullptr)
			continue;
		if (currActor == pickedActor) {
			currActor->SetVisibility(false);
		}
	}
}

void GemoPickerInteractorStyle::clear()
{
	m_selectedMode = GUI::SelectGeomMode::SelectNone;
	initPicker();
}

GemoPickerInteractorStyle::GemoPickerInteractorStyle()
{
	m_selectedMode = GUI::SelectGeomMode::SelectNone;
	initPicker();
}

void GemoPickerInteractorStyle::initPicker()
{
	// todo 设置pointPicker和cellPicker的相关属性
	m_pointPicker = vtkSmartPointer<vtkPointPicker>::New();
	m_pointPicker->SetTolerance(0.005);
	m_pointPicker->SetPickFromList(1);
	m_pointPicker->PickFromListOn();
	m_cellPicker = vtkSmartPointer<vtkCellPicker>::New();
	m_cellPicker->SetTolerance(0.005);
	if (m_selectedMode == GUI::SelectGeomMode::SelectPolyLine)
	{
		m_cellPicker->SetTolerance(0.0025);
	}
	m_cellPicker->SetPickFromList(1);
	m_cellPicker->PickFromListOn();
}

void GemoPickerInteractorStyle::setSelectMode(GUI::SelectGeomMode m)
{
	clear();
	m_selectedMode = m;
}