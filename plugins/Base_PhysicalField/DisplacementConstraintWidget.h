#pragma once

#include <BasePage.h>
#include <QHash>
#include <QVector>
#include <vtkObject.h>
#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;
class vtkCellPicker;
class vtkCallbackCommand;
class vtkIntArray;

class vtkPoints;
class vtkPolyData;
class vtkVertexGlyphFilter;
class vtkPolyDataMapper;
class vtkActor;

class vtkRenderer;
class vtkRenderWindowInteractor;

namespace Ui { class DisplacementConstraintWidget; }

class VisualWidgetService;

class DisplacementConstraintWidget : public BasePage
{
    Q_OBJECT
public:
    explicit DisplacementConstraintWidget(QWidget *parent = nullptr);
    ~DisplacementConstraintWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void on_clickSelectionCheckBox_checkStateChanged(const Qt::CheckState &arg1);

private:
    // picking
    void enablePicking(bool on);
    void setupPicking();
    void teardownPicking();
    bool pickAt(int x, int y);

    static void OnLeftButtonPress(vtkObject* caller,
                                  unsigned long eventId,
                                  void* clientData,
                                  void* callData);

    // selected-points rendering (single actor)
    void ensureSelectedPointsActor();
    void buildNodeIdToVtkPidMap();
    void toggleSelectNode(int nodeId, vtkIdType vtkPid);
    void addSelected(int nodeId, vtkIdType vtkPid);
    void removeSelected(int nodeId);

private:
    Ui::DisplacementConstraintWidget *ui = nullptr;

    // services & grid
    VisualWidgetService* m_visual = nullptr;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;

    // pointdata: node_id
    vtkSmartPointer<vtkIntArray> m_nodeIdArr;

    // for fast nodeId -> vtkPointId
    QHash<int, vtkIdType> m_nodeId2VtkPid;

    // picker + observer
    vtkSmartPointer<vtkCellPicker> m_picker;
    vtkSmartPointer<vtkCallbackCommand> m_cbLeftPress;
    unsigned long m_leftPressTag = 0;
    bool m_pickingEnabled = false;

    // -------- selected points: ONE actor --------
    // selected node ids (order matters for O(1) remove by swap)
    QVector<int> m_selNodeIds;          // index -> nodeId
    QHash<int, int> m_nodeId2SelIndex;  // nodeId -> index in m_selNodeIds

    vtkSmartPointer<vtkPoints> m_selPts;
    vtkSmartPointer<vtkPolyData> m_selPoly;
    vtkSmartPointer<vtkVertexGlyphFilter> m_selGlyph;
    vtkSmartPointer<vtkPolyDataMapper> m_selMapper;
    vtkSmartPointer<vtkActor> m_selActor;
};
