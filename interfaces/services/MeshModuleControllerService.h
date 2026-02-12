#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>
class MshPickMap;
template <typename T> class vtkSmartPointer;
class vtkUnstructuredGrid;

struct MeshModuleControllerService
{
    virtual ~MeshModuleControllerService() {}
	virtual void newProject() = 0;
	virtual bool saveProject() = 0;
	virtual bool loadProject() = 0;
	virtual void closeProject() = 0;
    virtual QSet<int> getAvailableMatypes() const = 0;
    virtual void highlightMatypes(const QSet<int> &matypes,
                                  int selectedMatype,
                                  int previewMatype,
                                  int preDeleteMatype) = 0;
    virtual vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGrid() = 0;
    virtual vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridSolid() = 0;
    virtual vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridLine() = 0;
    virtual void importMshGrid(QString path) = 0;
    virtual MshPickMap mshPickMapSolid() = 0;
    virtual bool autoGroupingBySpatialBins(int binCount,
                                        const QString& targetArrayName,
                                        int startValue,
                                        const QString& groupPrefix,
                                        QString* errMsg = nullptr) = 0;
    virtual bool groupByCoordinateRange(double xmin, double xmax,
                                        double ymin, double ymax,
                                        double zmin, double zmax,
                                        const QString& targetArrayName,
                                        int value,
                                        const QString& groupName,
                                        QString* errMsg = nullptr) = 0;
};


#endif // MESH_MODULE_CONTROLLER_SERVICE_H
