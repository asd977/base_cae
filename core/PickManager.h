#ifndef PICKMANAGER_H
#define PICKMANAGER_H

#include <core_global.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

// VTK
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkAreaPicker.h>
#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkCoordinate.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkExtractSelectedFrustum.h>
#include <vtkIntArray.h>
#include <vtkPlanes.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProp.h>
#include <vtkProperty2D.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertexGlyphFilter.h>

#include "services/VisualWidgetService.h"

enum class PickMode
{
    Node,
    Edge,
    Face
};
enum class GridKind
{
    Solid,
    Line
};

class CORE_EXPORT PickManager : public QObject
{
    Q_OBJECT
  public:
    static PickManager& instance();

    void activate(const QString& domain, const QString& uuid, PickMode mode);
    void deactivate(const QString& domain, const QString& uuid, PickMode mode);

    QVector<vtkIdType> getIdList(const QString& domain, const QString& uuid, PickMode mode) const;
    QVector<vtkIdType> getIdList(const QString& domain, const QString& uuid, PickMode mode,
                                 GridKind kind) const;
    QVector<vtkIdType> getSelectedPointIds(const QString& domain, const QString& uuid,
                                           GridKind kind) const;

    QHash<int, QVector<int>> getEdgeNodesByKe(const QString& domain, const QString& uuid,
                                              GridKind kind) const;
    QHash<int, QVector<int>> getFaceNodesByKe(const QString& domain, const QString& uuid,
                                              GridKind kind) const;

    void updateStructure();

    static void OnLeftButtonPress(vtkObject* caller, unsigned long, void* clientData, void*);
    static void OnLeftButtonRelease(vtkObject* caller, unsigned long, void* clientData, void*);
    static void OnMouseMove(vtkObject* caller, unsigned long, void* clientData, void*);

    // ---- face key ----
    vtkIdType packFaceKey(vtkIdType cellId, int faceLocalId) const
    {
        return (cellId << 4) | (vtkIdType(faceLocalId) & 0xF);
    }
    vtkIdType unpackFaceCellId(vtkIdType faceKey) const
    {
        return (faceKey >> 4);
    }
    int unpackFaceLocalId(vtkIdType faceKey) const
    {
        return int(faceKey & 0xF);
    }

    // ---- edge key ----
    vtkIdType packEdgeKey(vtkIdType cellId, int edgeLocalId) const
    {
        return (cellId << 4) | (vtkIdType(edgeLocalId) & 0xF);
    }
    vtkIdType unpackEdgeCellId(vtkIdType edgeKey) const
    {
        return (edgeKey >> 4);
    }
    int unpackEdgeLocalId(vtkIdType edgeKey) const
    {
        return int(edgeKey & 0xF);
    }

    vtkUnstructuredGrid* solidGrid() const
    {
        return m_gridSolid;
    }
    vtkUnstructuredGrid* lineGrid() const
    {
        return m_gridLine;
    }

    void clearSelection(const QString& domain, const QString& uuid, PickMode mode);
    void setShowBoxRect(bool on);

    void setSelectedPointIds(const QString& domain, const QString& uuid, GridKind kind,
                             const QVector<vtkIdType>& pointIds, bool replace = true);

    void setSelectedEdgeKeys(const QString& domain, const QString& uuid, GridKind kind,
                             const QVector<vtkIdType>& edgeKeys, bool replace = true);

    void setSelectedFaceKeys(const QString& domain, const QString& uuid, GridKind kind,
                             const QVector<vtkIdType>& faceKeys, bool replace = true);

  private:
    explicit PickManager(QObject* parent = nullptr);
    ~PickManager() override = default;

    void clearNodeSelection_NoRefresh(const QString& domain, const QString& uuid, GridKind kind);

    // picking
    void setupPicking();
    void teardownPicking();
    bool pickAt(int x, int y);

    // ---- node ----
    void toggleSelectNode(GridKind kind, vtkIdType pid);
    void rebuildPointsForActive();

    // ---- edge ----
    void toggleSelectEdge(GridKind kind, vtkIdType edgeKey);
    void rebuildEdgePolyForActive();
    bool computePickedEdge(vtkUnstructuredGrid* grid, vtkIdType cellId, const double pickPos[3],
                           int& outEdgeLocalId, QVector<vtkIdType>& outEdgePids) const;
    bool computePickedEdgeFromBoundary(vtkDataSet* pickedDs, vtkIdType pickedCellId,
                                       const double pickPos[3], vtkIdType& outOrigCellId,
                                       int& outOrigEdgeLocalId) const;

    // ---- face ----
    void toggleSelectFace(GridKind kind, vtkIdType faceKey);
    void rebuildFacePolyForActive();
    bool computePickedFace(vtkUnstructuredGrid* grid, vtkIdType cellId, const double pickPos[3],
                           int& outFaceLocalId, QVector<vtkIdType>& outFacePids) const;
    bool computePickedFaceFromBoundary(vtkDataSet* pickedDs, vtkIdType pickedCellId,
                                       vtkIdType& outOrigCellId, int& outOrigFaceLocalId) const;

    void updateActorVisibility();
    void refreshStructureIfNeeded();

    // line node overlay
    void rebuildLineNodeOverlayAll();

    // commit exclusive on deactivate
    void commitNodeExclusive(const QString& domain, const QString& uuid);
    void commitEdgeExclusive(const QString& domain, const QString& uuid);
    void commitFaceExclusive(const QString& domain, const QString& uuid);

    // ====== box selection ======
    enum class BoxOp
    {
        Add,
        Toggle,
        Remove
    };
    void boxSelect(int x0, int y0, int x1, int y1, BoxOp op);

    // batch modify helpers (NO rebuild / NO render)
    void setNodeSelected_NoRefresh(GridKind kind, vtkIdType pid, bool selected);
    void setEdgeSelected_NoRefresh(GridKind kind, vtkIdType edgeKey, bool selected);
    void setFaceSelected_NoRefresh(GridKind kind, vtkIdType faceKey, bool selected);

    // ====== coalesced rebuild/render ======
    enum DirtyBits : int
    {
        DirtyNone = 0,
        DirtyNode = 1,
        DirtyEdge = 2,
        DirtyFace = 4
    };
    void markDirty(int bits);
    void flushRebuild();

  private:
    // ============ active ============
    QString m_currentDomain;
    QString m_currentUUid;
    qint64 m_structureRevisionSeen = -1;
    PickMode m_pickMode = PickMode::Node;

    VisualWidgetService* m_visual = nullptr;

    // ============ observers / pickers ============
    vtkSmartPointer<vtkCallbackCommand> m_cbLeftPress;
    vtkSmartPointer<vtkCallbackCommand> m_cbLeftRelease;
    vtkSmartPointer<vtkCallbackCommand> m_cbMouseMove;

    vtkSmartPointer<vtkCellPicker> m_cellPicker;
    vtkSmartPointer<vtkAreaPicker> m_areaPicker;

    unsigned long m_leftPressTag = 0;
    unsigned long m_leftReleaseTag = 0;
    unsigned long m_mouseMoveTag = 0;

    bool m_boxing = false;
    int m_boxStart[2] = {0, 0};
    int m_boxEnd[2] = {0, 0};

    // coalesce
    int m_dirtyBits = DirtyNone;
    bool m_rebuildScheduled = false;

    // ============ mesh cache ============
    vtkUnstructuredGrid* m_gridSolid = nullptr;
    vtkUnstructuredGrid* m_gridLine = nullptr;

    vtkIntArray* m_nodeIdArrSolid = nullptr;
    vtkIntArray* m_elemIdArrSolid = nullptr;
    vtkIntArray* m_nodeIdArrLine = nullptr;
    vtkIntArray* m_elemIdArrLine = nullptr;

    vtkProp* m_solidProp = nullptr;
    vtkProp* m_lineProp = nullptr;
    vtkDataSet* m_solidPickDataSet = nullptr;
    vtkDataSet* m_linePickDataSet = nullptr;

    // ============ line node hints ============
    vtkSmartPointer<vtkPoints> m_lineNodePts;
    vtkSmartPointer<vtkPolyData> m_lineNodePoly;
    vtkSmartPointer<vtkVertexGlyphFilter> m_lineNodeGlyph;
    vtkSmartPointer<vtkPolyDataMapper> m_lineNodeMapper;
    vtkSmartPointer<vtkActor> m_lineNodeActor;
    bool m_showLineNodes = true;

    // ============ node overlay ============
    vtkSmartPointer<vtkPoints> m_selPts;
    vtkSmartPointer<vtkPolyData> m_selPoly;
    vtkSmartPointer<vtkVertexGlyphFilter> m_selGlyph;
    vtkSmartPointer<vtkPolyDataMapper> m_selMapper;
    vtkSmartPointer<vtkActor> m_selActor;

    QHash<QString, QHash<vtkIdType, QString>> m_ownerPointSolidByDomain;
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_pointsSolidByDomainUuid;
    QHash<QString, QHash<vtkIdType, QString>> m_ownerPointLineByDomain;
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_pointsLineByDomainUuid;

    QHash<QString, QHash<vtkIdType, QString>> m_prevOwnerPointSolidByDomain;
    QHash<QString, QHash<vtkIdType, QString>> m_prevOwnerPointLineByDomain;

    // ============ edge overlay ============
    vtkSmartPointer<vtkPoints> m_edgePts;
    vtkSmartPointer<vtkCellArray> m_edgeLines;
    vtkSmartPointer<vtkPolyData> m_edgePoly;
    vtkSmartPointer<vtkPolyDataMapper> m_edgeMapper;
    vtkSmartPointer<vtkActor> m_edgeActor;

    QHash<QString, QHash<vtkIdType, QString>> m_ownerEdgeSolidByDomain;
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_edgesSolidByDomainUuid;
    QHash<QString, QHash<vtkIdType, QString>> m_ownerEdgeLineByDomain;
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_edgesLineByDomainUuid;

    QHash<QString, QHash<vtkIdType, QString>> m_prevOwnerEdgeSolidByDomain;
    QHash<QString, QHash<vtkIdType, QString>> m_prevOwnerEdgeLineByDomain;

    // ============ face overlay ============
    vtkSmartPointer<vtkPoints> m_facePts;
    vtkSmartPointer<vtkCellArray> m_facePolys;
    vtkSmartPointer<vtkPolyData> m_facePoly;
    vtkSmartPointer<vtkPolyDataMapper> m_faceMapper;
    vtkSmartPointer<vtkActor> m_faceActor;

    QHash<QString, QHash<vtkIdType, QString>> m_ownerFaceSolidByDomain;
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_facesSolidByDomainUuid;
    QHash<QString, QHash<vtkIdType, QString>> m_ownerFaceLineByDomain;
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_facesLineByDomainUuid;

    QHash<QString, QHash<vtkIdType, QString>> m_prevOwnerFaceSolidByDomain;
    QHash<QString, QHash<vtkIdType, QString>> m_prevOwnerFaceLineByDomain;

    // ====== rubber band rect (2D) ======
    vtkSmartPointer<vtkPoints> m_boxPts;
    vtkSmartPointer<vtkCellArray> m_boxLines;
    vtkSmartPointer<vtkPolyData> m_boxPoly;
    vtkSmartPointer<vtkPolyDataMapper2D> m_boxMapper2D;
    vtkSmartPointer<vtkActor2D> m_boxActor2D;
    vtkSmartPointer<vtkCoordinate> m_boxCoord;

    bool m_showBoxRect = true;
    qint64 m_lastBoxRenderMs = 0;
    bool m_mouseDown = false;  // 左键按下
    bool m_dragging = false;   // 是否已进入拖拽框选
    int m_dragThresholdPx = 4; // 超过这个像素才认为在拖拽（可调）

    bool m_leftDown = false;
    int m_leftDownPos[2] = {0, 0};
    int m_clickThresholdPx = 4; // 2~6 之间自行调

    void updateBoxRectActor(int x0, int y0, int x1, int y1);
    void setBoxRectVisible(bool on);

  signals:
    void nodePicked(const QString& domain, const QString& activeUuid, GridKind kind, vtkIdType pid,
                    int nodeId, double x, double y, double z, const QString& prevUuid);
};

#endif // PICKMANAGER_H
