#pragma once

#include <BasePage.h>
#include <vtkSmartPointer.h>

#include <QHash>
#include <QMap>
#include <QPair>
#include <QRect>
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
class vtkActor2D;
class vtkLabeledDataMapper;
class vtkTextProperty;
class vtkCellPicker;
class vtkPointPicker;
class vtkLookupTable;
class vtkRenderer;
class vtkRenderWindowInteractor;
class vtkCommand;
class vtkPolyData;
class vtkPolyDataMapper2D;
class vtkCoordinate;
class vtkDataArray;
class vtkDataSet;

class GridEditingWidget : public BasePage
{
    Q_OBJECT
  public:
    enum class LabelSelectionMode
    {
        None,
        SurfaceNode,
        SurfaceElem,
        SurfaceBoth
    };
    enum class ColorMode
    {
        None,
        Ketype,
        Matype,
        Layer
    };

    explicit GridEditingWidget(QWidget* parent = nullptr);
    ~GridEditingWidget() override;

    // ===================== 关键：完全不依赖 IiinGridModel =====================
    // 传入“可编辑网格”（内部 DeepCopy 一份作为 fullGrid）
    void setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName = QString());
    void clearGrid();

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
    bool isLabelBoxSelectionActive() const
    {
        return m_labelSelectionMode != LabelSelectionMode::None;
    }
    bool isBoxSelecting() const
    {
        return m_isBoxSelecting;
    }

    // mouse callback -> widget
    void onPicked(long long pickedPointId, long long pickedCellId);

    void beginNodeDragFromCell(long long cellId, int displayX, int displayY);
    void dragNodeToWorldPosition(const double worldPos[3]);
    void endNodeDrag();

    void startLabelBoxSelection(int x, int y);
    void updateLabelBoxSelection(int x, int y);
    void finishLabelBoxSelection(int x, int y);

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

    void on_checkBoxEnablePicking_toggled(bool checked);
    void on_comboBoxColorMode_currentIndexChanged(int index);

    void on_pushButtonSave_clicked();

  private:
    void clearData();

    QSet<int> parseIntSetFromString(const QString& text) const;

    // fullGrid -> viewGrid（过滤后的显示数据）
    void buildOrUpdateViewGrid();
    void setupInteractor();

    void ensureLabelActorsCreated();
    void applyLabelTextStyles();

    // maps
    void rebuildFullMaps();
    void rebuildViewMaps();

    vtkDataArray* pointArray(const char* name) const;
    vtkDataArray* cellArray(const char* name) const;

    bool cellMatchesFilter(vtkIdType fullCellId) const;
    QSet<int> collectUniqueCellInts(const char* arrayName) const;

    void updateIdRangeLabels();
    void updateFilterCombosFromGrid();
    void updateFilterSummaryLabel();
    void updateSurfaceLabelSummary(const QString& text);

    void showNodeLabelsForElem(int elemId);
    void showElemLabelForElem(int elemId);

    void beginNodeDragByPointId(long long viewPointId);
    void beginNodeDragByNodeId(int nodeId);
    void beginLabelBoxSelection(LabelSelectionMode mode);
    void showSurfaceLabelsInRect(const QRect& rect, LabelSelectionMode mode);

    void ensureSelectionBoxActorCreated();
    void updateSelectionBoxActor(const QRect& rect);

    void applyColorMode();
    vtkSmartPointer<vtkLookupTable> buildLookupTable(const QSet<int>& values) const;
    QStringList availableMeshPropIds() const;

    // editing helpers
    void moveNodeById(int nodeId, const double* newX, const double* newY, const double* newZ);
    void deleteElemById(int elemId);

    bool saveAsVtu(const QString& filePath);

    int maxSurfaceLabelCount() const
    {
        return 300;
    } // 可改 UI 可配

  private:
    Ui::GridEditingWidget* ui = nullptr;

    VisualWidgetService* m_visualWidgetService = nullptr;

    // ============ 彻底剔除 IiinGridModel：只保留 VTK 网格 ============
    vtkSmartPointer<vtkUnstructuredGrid> m_fullGrid; // 权威可编辑数据
    vtkSmartPointer<vtkUnstructuredGrid> m_viewGrid; // 过滤后的显示数据（无过滤时 = fullGrid）
    vtkSmartPointer<vtkPolyData> m_viewSurface;      // 仅外表面渲染/拾取数据

    QString m_sourceName; // 保存/溯源用
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

    // maps
    QHash<int, vtkIdType> m_fullNodeIdToVtkId;
    QHash<int, vtkIdType> m_viewNodeIdToVtkId;
    QHash<int, vtkIdType> m_viewSurfaceNodeIdToVtkId;
    QHash<int, vtkIdType> m_fullElemIdToCellId;

    // vtk cached (view)
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkDataSetMapper> m_meshMapper;
    vtkSmartPointer<vtkActor> m_meshActor;

    // labels
    vtkSmartPointer<vtkLabeledDataMapper> m_nodeLabelMapper;
    vtkSmartPointer<vtkActor2D> m_nodeLabelActor;
    vtkSmartPointer<vtkTextProperty> m_nodeLabelTextProperty;

    vtkSmartPointer<vtkLabeledDataMapper> m_elemLabelMapper;
    vtkSmartPointer<vtkActor2D> m_elemLabelActor;
    vtkSmartPointer<vtkTextProperty> m_elemLabelTextProperty;

    // selection box
    vtkSmartPointer<vtkPolyData> m_boxSelectionPoly;
    vtkSmartPointer<vtkPolyDataMapper2D> m_boxSelectionMapper;
    vtkSmartPointer<vtkCoordinate> m_boxSelectionCoord;
    vtkSmartPointer<vtkActor2D> m_boxSelectionActor;

    // picking / interactor
    vtkRenderWindowInteractor* m_interactor = nullptr;
    vtkRenderer* m_renderer = nullptr;

    vtkSmartPointer<vtkCellPicker> m_cellPicker;
    vtkSmartPointer<vtkPointPicker> m_pointPicker;

    vtkSmartPointer<vtkCommand> m_mouseCallback;

    bool m_enableMousePicking = false;

    // label box selection
    LabelSelectionMode m_labelSelectionMode = LabelSelectionMode::None;
    bool m_isBoxSelecting = false;
    int m_boxStart[2] = {0, 0};
    int m_boxEnd[2] = {0, 0};

    // color mode
    ColorMode m_colorMode = ColorMode::None;

    // drag state
    bool m_isDragging = false;
    long long m_dragViewPointId = -1;
    int m_dragNodeId = -1;
    double m_dragStartX = 0, m_dragStartY = 0, m_dragStartZ = 0;
};
