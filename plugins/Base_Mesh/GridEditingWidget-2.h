#pragma once

#include <BasePage.h>
#include <vtkSmartPointer.h>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace Ui
{
class GridEditingWidget;
}

class VisualWidgetService;

class vtkPoints;
class vtkUnstructuredGrid;
class vtkDataSetMapper;
class vtkActor;
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
    enum class ColorMode
    {
        None,
        Ketype,
        Matype,
        Layer
    };

    explicit GridEditingWidget(QWidget* parent = nullptr);
    ~GridEditingWidget() override;

    void setModel(const IiinGridModel& model);
    void clearIiinGrid();
    void highlightMatypes(const QSet<int>& matypes, int selectedMatype, int previewMatype,
                          int preDeleteMatype);

    bool isMousePickingEnabled() const
    {
        return m_enableMousePicking;
    }
    bool isDraggingNode() const
    {
        return m_isDragging;
    }

    // mouse callback -> widget
    void onPicked(long long pickedPointId, long long pickedCellId);

    void beginNodeDragFromCell(long long cellId, int displayX, int displayY);
    void dragNodeToWorldPosition(const double worldPos[3]);
    void endNodeDrag();

    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void on_pushButtonApplyFilter_clicked();
    void on_pushButtonResetFilter_clicked();

    void on_pushButtonMoveNode_clicked();
    void on_pushButtonDeleteElem_clicked();

    void on_horizontalSliderOpacity_valueChanged(int value);
    void on_checkBoxShowInternalGrid_toggled(bool checked);
    void on_checkBoxEnablePicking_toggled(bool checked);
    void on_comboBoxColorMode_currentIndexChanged(int index);

    void on_pushButtonSave_clicked();

  private:
    void clearData();

    QSet<int> parseIntSetFromString(const QString& text) const;
    int expectedNodeCountForKetype(int ketype) const;
    bool elemMatchesFilter(const Elem& e) const;

    void buildOrUpdateGrid();
    void setupInteractor();

    const Elem* findElemById(int elemId) const;
    Elem* findElemById(int elemId);

    int nodeIndexById(int nodeId) const;

    void moveNode(int nodeId, double* newX, double* newY, double* newZ);
    void deleteElemById(int elemId);

    void updateIdRangeLabels();
    void updateFilterCombosFromModel();
    void updateFilterSummaryLabel();

    void beginNodeDragByPointId(long long pointId);
    void applyColorMode();
    vtkSmartPointer<vtkLookupTable> buildLookupTable(const QSet<int>& values) const;
    QStringList availableMeshPropIds() const;

  private:
    Ui::GridEditingWidget* ui = nullptr;

    VisualWidgetService* m_visualWidgetService = nullptr;

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
    vtkSmartPointer<vtkActor> m_meshActor;

    // picking / interactor
    vtkRenderWindowInteractor* m_interactor = nullptr;
    vtkRenderer* m_renderer = nullptr;

    vtkSmartPointer<vtkCellPicker> m_cellPicker;
    vtkSmartPointer<vtkPointPicker> m_pointPicker;

    vtkSmartPointer<vtkCommand> m_mouseCallback;

    bool m_enableMousePicking = false;

    // color mode
    ColorMode m_colorMode = ColorMode::None;

    // drag state
    bool m_isDragging = false;
    long long m_dragVtkPointId = -1;
    int m_dragNodeId = -1;
    double m_dragStartX = 0, m_dragStartY = 0, m_dragStartZ = 0;

    // node id -> vtk point id
    QHash<int, vtkIdType> m_nodeIdToVtkId;
};
