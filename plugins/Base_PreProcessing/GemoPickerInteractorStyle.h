#ifndef GEMOPICKERINTERACTORSTYLE_H
#define GEMOPICKERINTERACTORSTYLE_H

#include <vtkSmartPointer.h>
#include <vtkPointPicker.h>
#include <vtkCellPicker.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <QList>
#include <QVector>
#include <QObject>
#include "GemoPickerType.h"

class GemoPickerInteractorStyle : public QObject, public vtkInteractorStyleTrackballCamera
{
	Q_OBJECT
public:
	static GemoPickerInteractorStyle* New();
	vtkTypeMacro(GemoPickerInteractorStyle, vtkInteractorStyleTrackballCamera);

	void setPickList(QList<vtkSmartPointer<vtkActor>> lst);
	void clear();
	void highlight();
	void setSelectMode(GUI::SelectGeomMode m);

	vtkSmartPointer<vtkCellPicker> getCellPicker() { return m_cellPicker; }
	vtkSmartPointer<vtkPointPicker> getPointPicker() { return m_pointPicker; }
signals:
	void selectGemoPoint(vtkActor* actor, int cellIndex, int pointIndex);
	void selectGemoLine(vtkActor* actor, int cellIndex, int pointIndex);
	void selectGemoFace(vtkActor* actor, int cellIndex, int pointIndex);
	void selectGemoSolid(vtkActor* actor, int cellIndex, int pointIndex);
private:
	GemoPickerInteractorStyle();
	~GemoPickerInteractorStyle() {}
	void initPicker();
	void OnLeftButtonDown();
	//highlight
	void highlightVolume();
	void highlightSurface();
	void highlightLine();
	void highlightVertex();

	//display
	void displayPicked();
	void displayUnpicked();

	vtkSmartPointer<vtkPointPicker> m_pointPicker;
	vtkSmartPointer<vtkCellPicker> m_cellPicker;
	QList<vtkSmartPointer<vtkActor>> m_actorsPickLst;
	GUI::SelectGeomMode m_selectedMode{ GUI::SelectGeomMode::SelectNone };

};



#endif // GEMOPICKERINTERACTORSTYLE_H