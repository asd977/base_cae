#ifndef PREPROCESSINGSERVICE_H
#define PREPROCESSINGSERVICE_H

#include <QList>
#include <QStringList>
#include <QVector3D>

class QString;
class vtkActor;
class vtkPolyData;
class vtkSphereSource;
class vtkUnstructuredGrid;
template <typename T> class vtkSmartPointer;
class BasePage;
struct PreProcessingService
{
    virtual ~PreProcessingService()
    {
    }
    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void closeProject() = 0;
    virtual void generateStepFile() = 0;
    // Export exactly the geometry suitable for the requested meshing dimension.
    // 2D: explicitly approved sketch regions take precedence over solids and
    // reference/open curves. 3D: the established final-solid STEP workflow.
    // On success returns an existing file path and a human-readable summary;
    // on failure returns a diagnostic and never reuses a stale output file.
    virtual bool prepareMeshingGeometry(int dimension, QString& outputPath,
                                        QString& diagnostic) = 0;

    // dim: 1=edge, 2=face, 3=solid
    virtual QStringList listEntityIds(int dim) = 0;
    virtual bool entityCenter(int dim, int id, QVector3D& center) = 0;
    // dim: 1->长度, 2->面积, 3->体积
    virtual bool entityMeasure(int dim, int id, double& measure) = 0;
    virtual QStringList entitySubshapeIds(int parentDim, int parentId, int childDim)
    { Q_UNUSED(parentDim); Q_UNUSED(parentId); Q_UNUSED(childDim); return {}; }
    virtual bool highlightEntity(int dim, int id) = 0;
    virtual bool highlightEntities(int dim, const QList<int>& ids, bool hoverPreview) = 0;
    virtual void clearEntityHoverHighlight() = 0;
    virtual void clearEntityHighlight() = 0;
};

#endif // PREPROCESSINGSERVICE_H
