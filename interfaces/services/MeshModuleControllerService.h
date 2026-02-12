#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>
struct MshPickMap;
template <typename T> class vtkSmartPointer;
class vtkUnstructuredGrid;

struct EdgeControlRule
{
    QString edgeIds;
    double size = 1.0;
    int divisions = 10;
    double bias = 1.0;
    bool hardConstraint = false;
};

struct FaceControlRule
{
    QString faceIds;
    double minSize = 0.2;
    double maxSize = 1.0;
    bool curvatureRefine = true;
    int curvatureLevel = 2;
};

struct VolumeControlRule
{
    QString volumeIds;
    double minSize = 0.5;
    double maxSize = 2.0;
    double growthRate = 1.2;
    bool boundaryLayer = false;
    int layerCount = 3;
};

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
    virtual void setMshPickMapSolid(const MshPickMap& pickMap) = 0;
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

    virtual void setEdgeControlRules(const QVector<EdgeControlRule>& rules) = 0;
    virtual void setFaceControlRules(const QVector<FaceControlRule>& rules) = 0;
    virtual void setVolumeControlRules(const QVector<VolumeControlRule>& rules) = 0;
    virtual QVector<EdgeControlRule> edgeControlRules() const = 0;
    virtual QVector<FaceControlRule> faceControlRules() const = 0;
    virtual QVector<VolumeControlRule> volumeControlRules() const = 0;
};


#endif // MESH_MODULE_CONTROLLER_SERVICE_H
