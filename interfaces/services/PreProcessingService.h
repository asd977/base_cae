#ifndef PREPROCESSINGSERVICE_H
#define PREPROCESSINGSERVICE_H

#include <QSet>

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

    virtual QSet<int> getAvailableMatypes() const = 0;
    virtual void highlightMatypes(const QSet<int> &matypes,
                                  int selectedMatype,
                                  int previewMatype,
                                  int preDeleteMatype) = 0;
};

#endif // PREPROCESSINGSERVICE_H
