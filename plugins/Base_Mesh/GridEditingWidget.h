#pragma once

#include <BasePage.h>
#include <vtkSmartPointer.h>
#include <vtkType.h>

#include <QSet>
#include <QString>
#include <QWidget>

namespace Ui
{
class GridEditingWidget;
}

class VisualWidgetService;

class vtkActor;
class vtkDataArray;
class vtkDataSet;
class vtkIdList;
class vtkLookupTable;
class vtkMapper;
class vtkPolyData;
class vtkUnstructuredGrid;

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

    // 只负责“过滤显示 + 多颜色显示”。网格数据由外部注入。
    void setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName = QString());
    void clearGrid();

    // 材料页/分组页外部高亮 MATYPE 时调用。高亮优先级高于 comboBoxColorMode。
    void highlightMatypes(const QSet<int>& matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype);

    void beforeLoad();
    void beforeUnload();

  private slots:
    void on_pushButtonApplyFilter_clicked();
    void on_pushButtonResetFilter_clicked();
    void on_comboBoxColorMode_currentIndexChanged(int index);

  private:
    void clearData();

    bool hasFilter() const;
    bool cellMatchesFilter(vtkIdType cellId) const;

    void refreshMeshActors();
    void buildOrUpdateViewGrid();
    vtkSmartPointer<vtkUnstructuredGrid> extractCells(const vtkSmartPointer<vtkIdList>& ids) const;
    void setMapperInput(vtkMapper* mapper, vtkDataSet* dataSet) const;

    QSet<int> collectUniqueCellInts(vtkDataSet* dataSet, const char* arrayName) const;
    QSet<int> collectUniqueCellInts(const char* arrayName) const;
    QSet<int> collectVisibleCellInts(const char* arrayName) const;

    void updateFilterCombosFromGrid();
    void updateFilterSummaryLabel();

    void applyColorMode();
    void applyScalarColor(vtkMapper* mapper,
                          vtkDataSet* dataSet,
                          const char* arrayName,
                          vtkLookupTable* lut) const;
    vtkSmartPointer<vtkLookupTable> buildLookupTable(const QSet<int>& values) const;
    vtkSmartPointer<vtkLookupTable> buildMatypeHighlightLookupTable() const;
    bool hasMatypeHighlight() const;

  private:
    Ui::GridEditingWidget* ui = nullptr;
    VisualWidgetService* m_visualWidgetService = nullptr;

    vtkSmartPointer<vtkUnstructuredGrid> m_fullGrid;     // 原始完整网格
    vtkSmartPointer<vtkPolyData> m_viewSurface;          // 当前显示的面/体外表面
    vtkSmartPointer<vtkUnstructuredGrid> m_viewLineGrid; // 当前显示的线单元

    QString m_sourceName;

    QSet<int> m_filterKetypes;
    QSet<int> m_filterMatypes;
    QSet<int> m_filterLayers;

    ColorMode m_colorMode = ColorMode::None;

    QSet<int> m_highlightedMatypes;
    int m_selectedMatype = -1;
    int m_previewMatype = -1;
    int m_preDeleteMatype = -1;

    vtkActor* m_surfaceActor = nullptr;
    vtkActor* m_lineActor = nullptr;
    vtkMapper* m_surfaceMapper = nullptr;
    vtkMapper* m_lineMapper = nullptr;
};
