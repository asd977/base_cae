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
class vtkCellPicker;
class vtkPointPicker;
class vtkLookupTable;
class vtkRenderer;
class vtkRenderWindowInteractor;
class vtkCommand;
class vtkPolyData;
class vtkDataArray;
class vtkDataSet;

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

    // ===================== 关键：完全不依赖 IiinGridModel =====================
    // 传入“可编辑网格”（内部 DeepCopy 一份作为 fullGrid）
    void setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName = QString());
    void clearGrid();

    void highlightMatypes(const QSet<int>& matypes, int selectedMatype, int previewMatype,
                          int preDeleteMatype);

    void beforeLoad();
    void beforeUnload();

  private slots:
    void on_pushButtonApplyFilter_clicked();
    void on_pushButtonResetFilter_clicked();

    void on_comboBoxColorMode_currentIndexChanged(int index);

  private:
    void clearData();

    QSet<int> parseIntSetFromString(const QString& text) const;

    // fullGrid -> viewGrid（过滤后的显示数据）
    void buildOrUpdateViewGrid();
    void setupInteractor();

    // maps
    void rebuildFullMaps();
    void rebuildViewMaps();

    vtkDataArray* pointArray(const char* name) const;
    vtkDataArray* cellArray(const char* name) const;

    bool cellMatchesFilter(vtkIdType fullCellId) const;
    QSet<int> collectUniqueCellInts(const char* arrayName) const;

    void updateFilterCombosFromGrid();
    void updateFilterSummaryLabel();

    void applyColorMode();
    vtkSmartPointer<vtkLookupTable> buildLookupTable(const QSet<int>& values) const;
    QStringList availableMeshPropIds() const;


  private:
    Ui::GridEditingWidget* ui = nullptr;

    VisualWidgetService* m_visualWidgetService = nullptr;

    // ============ 彻底剔除 IiinGridModel：只保留 VTK 网格 ============
    vtkSmartPointer<vtkUnstructuredGrid> m_fullGrid; // 权威可编辑数据
    vtkSmartPointer<vtkUnstructuredGrid> m_viewGrid; // 过滤后的显示数据（无过滤时 = fullGrid）
    vtkSmartPointer<vtkPolyData> m_viewSurface;      // 仅外表面渲染/拾取数据
    vtkSmartPointer<vtkDataSet> m_originalRenderInput; // 进入编辑时的原始渲染输入

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

    // maps
    QHash<int, vtkIdType> m_fullNodeIdToVtkId;
    QHash<int, vtkIdType> m_viewNodeIdToVtkId;
    QHash<int, vtkIdType> m_viewSurfaceNodeIdToVtkId;
    QHash<int, vtkIdType> m_fullElemIdToCellId;

    // vtk cached (view)
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkDataSetMapper> m_meshMapper;
    vtkSmartPointer<vtkActor> m_meshActor;

    // picking / interactor
    vtkRenderWindowInteractor* m_interactor = nullptr;
    vtkRenderer* m_renderer = nullptr;

    vtkSmartPointer<vtkCellPicker> m_cellPicker;
    vtkSmartPointer<vtkPointPicker> m_pointPicker;

    vtkSmartPointer<vtkCommand> m_mouseCallback;
};
