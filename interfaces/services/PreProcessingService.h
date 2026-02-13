#ifndef PREPROCESSINGSERVICE_H
#define PREPROCESSINGSERVICE_H

#include <QStringList>
#include <QVector>

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
    virtual void generateStepFile() = 0;
    virtual QVector<qulonglong> occShapePointers() = 0;

    // dim: 1=edge, 2=face, 3=solid
    virtual QStringList listEntityIds(int dim) = 0;
    virtual bool highlightEntity(int dim, int id) = 0;
    virtual void clearEntityHighlight() = 0;
};

#endif // PREPROCESSINGSERVICE_H
