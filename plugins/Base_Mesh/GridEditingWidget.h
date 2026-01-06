#pragma once

#include <QWidget>
#include <QSet>
#include <QHash>
#include <QString>
#include <QStringList>
#include <BasePage.h>

#include <vtkSmartPointer.h>

#include "IiinGridModel.h"

namespace Ui { class GridEditingWidget; }

class VisualWidgetService;

class vtkPoints;
class vtkUnstructuredGrid;
class vtkDataSetMapper;
class vtkActor;
class vtkActor2D;
class vtkLabeledDataMapper;
class vtkTextProperty;
class vtkCellPicker;
class vtkPointPicker;
class vtkLookupTable;
class vtkRenderer;
class vtkRenderWindowInteractor;
class vtkCommand;

class GridEditingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit GridEditingWidget(QWidget *parent = nullptr);
    ~GridEditingWidget() override;

    void setModel(const IiinGridModel &model);
    void clearIiinGrid();
    void highlightMatypes(const QSet<int> &matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype);

    bool isMousePickingEnabled() const { return m_enableMousePicking; }
    bool isDraggingNode() const { return m_isDragging; }

    // mouse callback -> widget
    void onPicked(long long pickedPointId, long long pickedCellId);

    void beginNodeDragFromCell(long long cellId, int displayX, int displayY);
    void dragNodeToWorldPosition(const double worldPos[3]);
    void endNodeDrag();

    void beforeLoad();
    void beforeUnload();


private slots:
    void on_pushButtonApplyFilter_clicked();
    void on_pushButtonResetFilter_clicked();

    void on_pushButtonShowNodeLabels_clicked();
    void on_pushButtonShowElemLabel_clicked();
    void on_pushButtonShowSurfaceLabels_clicked();

    void on_pushButtonMoveNode_clicked();
    void on_pushButtonDeleteElem_clicked();

    void on_horizontalSliderOpacity_valueChanged(int value);
    void on_checkBoxShowInternalGrid_toggled(bool checked);
    void on_checkBoxEnablePicking_toggled(bool checked);

    void on_pushButtonSave_clicked();

private:
    void clearData();

    QSet<int> parseIntSetFromString(const QString &text) const;
    int expectedNodeCountForKetype(int ketype) const;
    bool elemMatchesFilter(const Elem &e) const;

    void buildOrUpdateGrid();
    void setupInteractor();

    void ensureLabelActorsCreated();
    void applyLabelTextStyles();

    const Elem* findElemById(int elemId) const;
    Elem*       findElemById(int elemId);

    int nodeIndexById(int nodeId) const;

    void showNodeLabelsForElem(int elemId);
    void showElemLabelForElem(int elemId);
    void showSurfaceLabels();

    void moveNode(int nodeId, double *newX, double *newY, double *newZ);
    void deleteElemById(int elemId);

    void updateIdRangeLabels();
    void updateFilterCombosFromModel();
    void updateFilterSummaryLabel();
    void updateSurfaceLabelSummary(const QString &text);

    void beginNodeDragByPointId(long long pointId);

    int maxSurfaceLabelCount() const { return 300; } // 你想做成 UI 可配也行

private:
    Ui::GridEditingWidget *ui = nullptr;

    VisualWidgetService *m_visualWidgetService = nullptr;

    // 数据
    IiinGridModel m_model;
    QString m_fileName;
    bool m_dirty = false;

    // filter
    QSet<int> m_filterKetypes;
    QSet<int> m_filterMatypes;
    QSet<int> m_filterLayers;

    // highlight
    QSet<int> m_highlightedMatypes;
    int m_highlightedMatype = -1;
    vtkSmartPointer<vtkLookupTable> m_matypeLookupTable;

    // id ranges
    int m_minNodeId = 0, m_maxNodeId = 0;
    int m_minElemId = 0, m_maxElemId = 0;

    // vtk cached
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;

    vtkSmartPointer<vtkDataSetMapper> m_meshMapper;
    vtkSmartPointer<vtkActor>         m_meshActor;

    // labels
    vtkSmartPointer<vtkLabeledDataMapper> m_nodeLabelMapper;
    vtkSmartPointer<vtkActor2D>           m_nodeLabelActor;
    vtkSmartPointer<vtkTextProperty>      m_nodeLabelTextProperty;

    vtkSmartPointer<vtkLabeledDataMapper> m_elemLabelMapper;
    vtkSmartPointer<vtkActor2D>           m_elemLabelActor;
    vtkSmartPointer<vtkTextProperty>      m_elemLabelTextProperty;

    // picking / interactor
    vtkRenderWindowInteractor* m_interactor = nullptr;
    vtkRenderer*               m_renderer   = nullptr;

    vtkSmartPointer<vtkCellPicker>  m_cellPicker;
    vtkSmartPointer<vtkPointPicker> m_pointPicker;

    vtkSmartPointer<vtkCommand> m_mouseCallback;

    bool m_enableMousePicking = false;

    // drag state
    bool      m_isDragging = false;
    long long m_dragVtkPointId = -1;
    int       m_dragNodeId = -1;
    double    m_dragStartX = 0, m_dragStartY = 0, m_dragStartZ = 0;

    // node id -> vtk point id
    QHash<int, vtkIdType> m_nodeIdToVtkId;
};
