#ifndef PICKMANAGER_H
#define PICKMANAGER_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>

#include "services/VisualWidgetService.h"

// VTK
#include <vtkSmartPointer.h>
#include <vtkCallbackCommand.h>
#include <vtkCellPicker.h>
#include <vtkUnstructuredGrid.h>
#include <vtkIntArray.h>

#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>

#include <vtkDataSetMapper.h>
#include <vtkIdTypeArray.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkExtractSelection.h>

#include <vtkCellArray.h>

enum class PickMode { Node, Edge, Face };

class PickManager : public QObject
{
    Q_OBJECT
public:
    static PickManager& instance();

    // 激活某个 domain + uuid + mode（同 domain 内互斥占有）
    void activate(const QString& domain, const QString& uuid, PickMode mode);
    void deactivate(const QString& domain, const QString& uuid, PickMode mode);

    // 返回当前 domain/uuid/mode 的 id 列表
    // Node: vtkPid
    // Edge: cellId
    // Face: faceKey = (cellId<<4) | faceLocalId (2D: faceLocalId=15)
    QVector<vtkIdType> getIdList(const QString& domain, const QString& uuid, PickMode mode) const;

    void updateStructure();

    static void OnLeftButtonPress(vtkObject* caller, unsigned long, void* clientData, void*);

    static inline vtkIdType unpackFaceCellId(vtkIdType faceKey)
    {
        return (faceKey >> 4);
    }

    static inline int unpackFaceLocalId(vtkIdType faceKey)
    {
        return int(faceKey & 0xF);
    }


private:
    explicit PickManager(QObject* parent=nullptr);
    ~PickManager() override = default;

    void setupPicking();
    void teardownPicking();
    bool pickAt(int x, int y);

    // ---- node ----
    void toggleSelectNode(vtkIdType vtkPid);
    void appendSelectedPoint(vtkIdType pid);
    void removeSelectedPoint(vtkIdType pid);
    void rebuildPointsForActive(); // activate 时重建显示

    // ---- edge ----
    void toggleSelectEdge(vtkIdType cellId);
    void appendSelectedEdge(vtkIdType cellId);
    void removeSelectedEdge(vtkIdType cellId);
    void rebuildEdgesForActive(); // activate 时重建显示
    void ensureEdgePipelineReady(); // 确保 extract+mapper 连接且输入有效

    // ---- face ----
    // faceKey = packFaceKey(cellId, faceLocalId)
    void toggleSelectFace(vtkIdType faceKey);
    void rebuildFacesForActive(); // activate 时重建显示（只显示面，不显示整个 cell）
    void rebuildFacePolyForActive(); // 真正构建 face polydata
    bool computePickedFace(vtkIdType cellId, const double pickPos[3],
                           int& outFaceLocalId, QVector<vtkIdType>& outFacePids) const;

    // 显示层：根据当前 mode 决定谁可见
    void updateActorVisibility();

    // faceKey pack/unpack
    static inline vtkIdType packFaceKey(vtkIdType cellId, int faceLocalId)
    {
        return (cellId << 4) | (vtkIdType(faceLocalId) & 0xF);
    }


private:
    // ============ 互斥占有关系 ============
    // domain -> (vtkPid -> uuid)
    QHash<QString, QHash<vtkIdType, QString>> m_ownerPointByDomain;
    // domain -> (uuid -> set(vtkPid))
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_pointsByDomainUuid;

    // domain -> (cellId -> uuid)
    QHash<QString, QHash<vtkIdType, QString>> m_ownerEdgeByDomain;
    // domain -> (uuid -> set(cellId))
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_edgesByDomainUuid;

    // domain -> (faceKey -> uuid)
    QHash<QString, QHash<vtkIdType, QString>> m_ownerFaceByDomain;
    // domain -> (uuid -> set(faceKey))
    QHash<QString, QHash<QString, QSet<vtkIdType>>> m_facesByDomainUuid;

    // ============ 当前激活态 ============
    QString  m_currentDomain;
    QString  m_currentUUid;
    PickMode m_pickMode = PickMode::Node;

    VisualWidgetService* m_visual = nullptr;

    // ============ Picker ============
    vtkSmartPointer<vtkCallbackCommand> m_cbLeftPress;
    vtkSmartPointer<vtkCellPicker>      m_cellPicker;
    unsigned long                       m_leftPressTag = 0;

    // ============ Mesh ============
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    vtkSmartPointer<vtkIntArray>         m_nodeIdArr; // optional

    // ============ Node (Points) 显示 ============
    vtkSmartPointer<vtkPoints>            m_selPts;
    vtkSmartPointer<vtkPolyData>          m_selPoly;
    vtkSmartPointer<vtkVertexGlyphFilter> m_selGlyph;
    vtkSmartPointer<vtkPolyDataMapper>    m_selMapper;
    vtkSmartPointer<vtkActor>             m_selActor;

    // vtkPid -> index in m_selPts (O(1) add/remove)
    QHash<vtkIdType, int> m_pid2SelIndex;
    QVector<vtkIdType>    m_selIndex2Pid;

    // ============ Edge 显示（ExtractSelection） ============
    vtkSmartPointer<vtkIdTypeArray>      m_edgeSelIds;
    vtkSmartPointer<vtkSelectionNode>    m_edgeSelNode;
    vtkSmartPointer<vtkSelection>        m_edgeSel;
    vtkSmartPointer<vtkExtractSelection> m_edgeExtract;
    vtkSmartPointer<vtkDataSetMapper>    m_edgeMapper;
    vtkSmartPointer<vtkActor>            m_edgeActor;

    // cellId -> idx in m_edgeSelIds (O(1) add/remove)
    QHash<vtkIdType, int> m_edgeId2Idx;
    QVector<vtkIdType>    m_edgeIdx2Id;

    // ============ Face 显示（PolyData，仅高亮面） ============
    vtkSmartPointer<vtkPoints>         m_facePts;
    vtkSmartPointer<vtkCellArray>      m_facePolys;
    vtkSmartPointer<vtkPolyData>       m_facePoly;
    vtkSmartPointer<vtkPolyDataMapper> m_faceMapper;
    vtkSmartPointer<vtkActor>          m_faceActor;

};

#endif // PICKMANAGER_H
