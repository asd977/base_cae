#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <array>
#include <struct/MeshMap.h>

struct DomainKey
{
    QString category;
    int dim = -1;
    int id = 0;

    bool isValid() const
    {
        return !category.trimmed().isEmpty() && dim >= 0 && id != 0;
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

// Native SMESH local controls on selected CAD / partition subshapes.
struct EdgeControlRule
{
    QString planUuid;
    QString methodUuid;
    QString ownerTaskKind;
    QString ownerTaskUuid;
    QString edgeIds;
    QString partitionEdgeRefs;
    int divisions = 10;
    double bias = 1.0;
    double maxSize = 0.0;
};
struct FaceControlRule
{
    QString planUuid;
    QString methodUuid;
    QString ownerTaskKind;
    QString ownerTaskUuid;
    QString faceIds;
    QString partitionFaceRefs;
    double localSizeFactor = 1.0;
    bool curvatureRefine = true;
    int curvatureElementsPer2Pi = 20;
    int minimumSizeMode = -1;
    double manualMinimumSize = 0.0;
    double maxSize = 0.0;
};
struct VolumeControlRule
{
    QString planUuid;
    QString methodUuid;
    QString ownerTaskKind;
    QString ownerTaskUuid;
    QString volumeIds;
    double localSizeFactor = 1.0;
    double maxSize = 0.0;
};

struct BarElementPlacement
{
    std::array<double, 3> start{{0.0, 0.0, 0.0}};
    std::array<double, 3> end{{0.0, 0.0, 0.0}};
    std::array<double, 3> arrayOffset{{0.0, 0.0, 0.0}};
    int segmentCount = 1;
    int arrayCount = 1;
    int installationLayer = -1;
    int matype = 0;
    QString placementUuid;
};

struct BarElementRecordInfo
{
    int id = 0;
    int cellCount = 0;
    int matype = 0;
    int installationLayer = -1;
    QString placementUuid;
};

// A "domain" is an original mesh region; MATYPE and LAYER are solver attributes.
// Imported IIIN/DAT meshes deliberately map these uses to two different attributes.
enum class MeshDomainUse { MaterialRegion, ExcavationRegion };

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
    virtual QString materialGroupCategory() const = 0;
    virtual QString excavationGroupCategory() const = 0;

    // The only category resolver that business pages should call. The fallback
    // exists for legacy projects without purpose metadata, not as a per-page guess.
    QString domainCategoryForUse(MeshDomainUse use) const
    {
        const QString configured = use == MeshDomainUse::MaterialRegion
            ? materialGroupCategory() : excavationGroupCategory();
        const QVector<MeshTagSet> sets = domainTagSets();
        auto usable = [&sets](const QString& category) {
            if (category.trimmed().isEmpty()) return false;
            for (const MeshTagSet& set : sets)
                if (set.key.category.compare(category, Qt::CaseInsensitive) == 0
                    && set.key.id != 0 && !set.cellIds.isEmpty()) return true;
            return false;
        };
        if (usable(configured)) return configured;
        for (const QString& category : {QStringLiteral("REGION"), QStringLiteral("Physical")})
            if (usable(category)) return category;
        const QString legacy = use == MeshDomainUse::MaterialRegion
            ? QStringLiteral("MATYPE") : QStringLiteral("LAYER");
        return usable(legacy) ? legacy : QString();
    }

    QVector<MeshTagSet> domainTagSetsForUse(MeshDomainUse use) const
    {
        const QString category = domainCategoryForUse(use);
        QVector<MeshTagSet> result;
        for (const MeshTagSet& set : domainTagSets())
            if (set.key.category.compare(category, Qt::CaseInsensitive) == 0
                && !set.cellIds.isEmpty()) result.push_back(set);
        return result;
    }
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
    virtual int verticalAxis() const = 0;
    virtual bool exportIiinDatFile(const QString& filePath, QString* errMsg = nullptr) = 0;
    virtual bool importGeneratedSmeshMesh(const QString& vtkPath, QString* errMsg = nullptr) = 0;
    virtual MshPickMap mshPickMapSolid() = 0;
    virtual QString matypePhysicalName(int matype, int dim = 3) const = 0;
    virtual void setMshPickMapSolid(const MshPickMap& pickMap) = 0;
    virtual bool autoGroupingBySpatialBins(int binCount, const QString& targetArrayName,
                                           int startValue, const QString& groupPrefix,
                                           QString* errMsg = nullptr) = 0;
    virtual bool groupByCoordinateRange(double xmin, double xmax, double ymin, double ymax,
                                        double zmin, double zmax, const QString& targetArrayName,
                                        int value, const QString& groupName,
                                        QString* errMsg = nullptr) = 0;
    // Cell ids always refer to the full imported grid (VTK ids), not elem_id.
    // This changes exactly one solver attribute and preserves the other.
    virtual bool assignSelectedCellsToGroup(const QVector<MeshIdType>& cellIds,
                                            const QString& attribute, int groupId,
                                            const QString& displayName,
                                            QString* errMsg = nullptr) = 0;
    virtual bool assignDamConstructionLayers(const QSet<MeshIdType>& foundationCellIds,
                                             const QSet<MeshIdType>& damCellIds,
                                             int fillLayerCount,
                                             QString* errMsg = nullptr) = 0;
    virtual bool appendBarElements(const BarElementPlacement& placement,
                                   QString* errMsg = nullptr) = 0;
    virtual bool appendBarElementsBatch(const QVector<BarElementPlacement>& placements,
                                        QString* errMsg = nullptr) = 0;
    virtual bool removeBarElementRecord(int recordId, QString* errMsg = nullptr) = 0;
    virtual QVector<BarElementRecordInfo> barElementRecords() const = 0;
    virtual bool setBarPlacementMatype(const QString& placementUuid, int matype,
                                       QString* errMsg = nullptr) = 0;
    virtual void setBarPlacementDisplayMode(bool active) = 0;

    virtual void setEdgeControlRules(const QVector<EdgeControlRule>& rules) = 0;
    virtual void setFaceControlRules(const QVector<FaceControlRule>& rules) = 0;
    virtual void setVolumeControlRules(const QVector<VolumeControlRule>& rules) = 0;
    virtual QVector<EdgeControlRule> edgeControlRules() const = 0;
    virtual QVector<FaceControlRule> faceControlRules() const = 0;
    virtual QVector<VolumeControlRule> volumeControlRules() const = 0;
};

#endif // MESH_MODULE_CONTROLLER_SERVICE_H
