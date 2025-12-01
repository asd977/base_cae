#ifndef PREPROCESSINGSERVICE_H
#define PREPROCESSINGSERVICE_H

#include <QStringList>



class QString;
class vtkActor;
class vtkPolyData;
class vtkSphereSource;
class vtkUnstructuredGrid;
template <typename T> class vtkSmartPointer;
class BasePage;
struct PreProcessingService
{
    virtual ~PreProcessingService() {}
	virtual void newProject() = 0;
	virtual bool saveProject() = 0;
	virtual bool loadProject() = 0;
	virtual void closeProject() = 0;
	virtual bool getGridIsDraw() = 0;
};

#endif // PREPROCESSINGSERVICE_H
