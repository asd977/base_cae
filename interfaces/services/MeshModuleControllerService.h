#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <struct/MeshMap.h>

struct DomainKey
{
    QString category;
    int dim = -1;
    int id = 0;

    bool isValid() const
    {
        return !category.trimmed().isEmpty() && dim >= 0 && id > 0;
    }

    bool operator==(const DomainKey& other) const
    {
        return category == other.category && dim == other.dim && id == other.id;
    }

    bool operator!=(const DomainKey& other) const
    {
        return !(*this == other);
    }
};

inline uint qHash(const DomainKey& key, uint seed = 0)
{
    seed = qHash(key.category, seed);
    seed = qHash(key.dim, seed);
    seed = qHash(key.id, seed);
    return seed;
}

struct MshPickMap;
template <typename T> class vtkSmartPointer;
class vtkUnstructuredGrid;

struct EdgeControlRule
{
    QString edgeIds;
    int divisions = 10;
    double bias = 1.0;
};

struct FaceControlRule
{
    QString faceIds;
    double localSizeFactor = 1.0;
    bool curvatureRefine = true;
    int curvatureElementsPer2Pi = 20;
};

struct VolumeControlRule
{
    QString volumeIds;
    double localSizeFactor = 1.0;
};

struct MeshModuleControllerService
{
    virtual ~MeshModuleControllerService()
    {
    }
    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void closeProject() = 0;

    virtual QVector<MeshTagSet> domainTagSets() const = 0;
    virtual QVector<MeshTagSet> boundaryTagSets(int dimFilter = -1) const = 0;
    virtual QVector<MeshIdType> cellIdsOfTag(const MeshTagKey& key) const = 0;
    virtual QVector<MeshIdType> pointIdsOfTag(const MeshTagKey& key) const = 0;
    virtual QString displayTextOfTag(const MeshTagKey& key) const = 0;

    // legacy domain highlight API (kept while UI migration is in progress)
    virtual QSet<int> getAvailableMatypes() const = 0;
    virtual void highlightDomainMatypes(const QSet<int>& matypes, int selectedMatype,
                                        int previewMatype, int preDeleteMatype) = 0;
    virtual QVector<DomainKey> availableDomains(const QString& category) const = 0;
    virtual QSet<MeshIdType> domainCells(const DomainKey& key) const = 0;
    virtual QString domainDisplayText(const DomainKey& key) const = 0;

    virtual vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGrid() = 0;
    virtual vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridSolid() = 0;
    virtual vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridLine() = 0;
    virtual QString sourceMeshFilePath() const = 0;
    virtual bool exportIiinDatFile(const QString& filePath, QString* errMsg = nullptr) = 0;
    virtual void importMshGrid(QString path) = 0;
    virtual bool importCurrentGmshMesh(QString* errMsg = nullptr) = 0;
    virtual MshPickMap mshPickMapSolid() = 0;
    virtual QVector<MeshTagKey> domainTagsOfCell(MeshIdType cellId) const = 0;
    virtual QString matypePhysicalName(int matype, int dim = 3) const = 0;
    virtual void setMshPickMapSolid(const MshPickMap& pickMap) = 0;
    virtual bool autoGroupingBySpatialBins(int binCount, const QString& targetArrayName,
                                           int startValue, const QString& groupPrefix,
                                           QString* errMsg = nullptr) = 0;
    virtual bool groupByCoordinateRange(double xmin, double xmax, double ymin, double ymax,
                                        double zmin, double zmax, const QString& targetArrayName,
                                        int value, const QString& groupName,
                                        QString* errMsg = nullptr) = 0;

    virtual void setEdgeControlRules(const QVector<EdgeControlRule>& rules) = 0;
    virtual void setFaceControlRules(const QVector<FaceControlRule>& rules) = 0;
    virtual void setVolumeControlRules(const QVector<VolumeControlRule>& rules) = 0;
    virtual QVector<EdgeControlRule> edgeControlRules() const = 0;
    virtual QVector<FaceControlRule> faceControlRules() const = 0;
    virtual QVector<VolumeControlRule> volumeControlRules() const = 0;
};

#endif // MESH_MODULE_CONTROLLER_SERVICE_H
