#pragma once
#include <qdebug.h>
#include <QColor>
#include <qhash.h>
#include <qobject.h>
#include <geometry_global.h>
// vtk
#include <vtkActor.h>
#include <vtkAppendPolyData.h>
#include <vtkCellData.h>
#include <vtkCleanPolyData.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkKdTree.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkTriangle.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertexGlyphFilter.h>
// occ
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

class GEOMETRY_EXPORT CadBody : public QObject
{
    Q_OBJECT
  public:
    CadBody(QString uuid, TopoDS_Shape* shape, QString name = QString(),
            bool createActors = true);
    ~CadBody();

    TopoDS_Shape* getShape();

    QString getUUid()
    {
        return m_uuid;
    };
    QString getName() const
    {
        return m_name;
    }
    void setName(const QString& name)
    {
        m_name = name;
    }

    void setActor(vtkSmartPointer<vtkActor> surfActor, vtkSmartPointer<vtkActor> edgeActor);

    vtkSmartPointer<vtkActor> getSurfActor();
    vtkSmartPointer<vtkActor> getEdgeActor();
    void initActor();
    // Explicit Shape -> VTK presentation lifecycle transition for objects that
    // were restored hidden. It creates the first presentation; it is not a retry.
    bool materializePresentation();
    bool hasActors() const;

    void setDisplayColor(const QColor& color);
    QColor displayColor() const;
    bool hasDisplayColor() const;

    // 工程批量打开时使用快速显示：可见对象降低三角化精度并省略复杂边线，
    // 隐藏对象可延迟创建 Actor。真实 TopoDS_Shape 始终保留，保存/布尔/网格不受影响。
    static void setFastDisplayMode(bool enabled);
    static bool fastDisplayMode();

    struct FaceGroup
    {
        QString name;
        QString role; // "Top", "Bottom", "Sides", "Excavation", "Contact", etc.
        QVector<int> faceIndices;
    };

    void setFaceGroups(const QVector<FaceGroup>& groups) { m_faceGroups = groups; }
    QVector<FaceGroup> faceGroups() const { return m_faceGroups; }
    void addFaceGroup(const FaceGroup& group) { m_faceGroups.append(group); }

  private:
    enum class PresentationState
    {
        NotMaterialized,
        Materialized,
        Failed
    };

    TopoDS_Shape* _shape;
    QString m_uuid;
    QString m_name;
    vtkSmartPointer<vtkActor> m_surfActor;
    vtkSmartPointer<vtkActor> m_edgeActor;
    QColor m_displayColor;
    bool m_hasDisplayColor = false;
    PresentationState m_presentationState = PresentationState::NotMaterialized;
    QVector<FaceGroup> m_faceGroups;
};

using GeomShapeObject = CadBody;
