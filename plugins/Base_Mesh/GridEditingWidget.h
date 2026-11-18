#pragma once

#include <BasePage.h>
#include <vtkSmartPointer.h>
#include <vtkType.h>

#include <QMap>
#include <QSet>
#include <QString>
#include <QWidget>

namespace Ui
{
class GridEditingWidget;
}

class VisualWidgetService;
class QTreeWidgetItem;
class vtkActor;
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
        None = 0,
        Ketype = 1,
        Matype = 2,
        Layer = 3,
        Topology = 4,
        Region = 5
    };

    explicit GridEditingWidget(QWidget* parent = nullptr);
    ~GridEditingWidget() override;

    // 显示控制页面只修改“全局显示状态”。
    // 实际显示数据仍以 MeshModuleController 的 m_importedGrid 为唯一来源。
    void setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName = QString());
    void clearGrid();

    // 兼容旧接口：材料/开挖体高亮统一走 PickManager，不再由显示控制页接管。
    void highlightMatypes(const QSet<int>& matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype);

    void beforeLoad();
    void beforeUnload();

  private slots:
    void on_pushButtonApplyFilter_clicked();
    void on_pushButtonResetFilter_clicked();
    void on_pushButtonUncheckAll_clicked();
    void on_comboBoxColorMode_currentIndexChanged(int index);
    void onFilterTreeItemChanged(QTreeWidgetItem* item, int column);

  private:
    void clearData();

    bool cellMatchesFilter(vtkIdType cellId) const;

    void refreshMeshActors();
    void buildOrUpdateViewGrid();
    vtkSmartPointer<vtkUnstructuredGrid> extractCells(const vtkSmartPointer<vtkIdList>& ids) const;
    void setMapperInput(vtkMapper* mapper, vtkDataSet* dataSet) const;

    QMap<int, int> collectCellIntCounts(const char* arrayName) const;
    QSet<int> collectUniqueCellInts(vtkDataSet* dataSet, const char* arrayName) const;
    QSet<int> collectVisibleCellInts(const char* arrayName) const;

    void buildFilterTreeFromGrid();
    void setFilterTreeChecked(bool checked);
    void updateParentCheckState(QTreeWidgetItem* parentItem);
    void applyFilterFromTree();
    QSet<int> checkedValuesForCategory(const QString& category) const;
    QString displayTextForFilterValue(const QString& category, int value) const;

    void updateContextPresentation();
    void applyColorMode();
    void applyScalarColor(vtkMapper* mapper,
                          vtkDataSet* dataSet,
                          const char* arrayName,
                          vtkLookupTable* lut) const;
    vtkSmartPointer<vtkLookupTable> buildLookupTable(const QSet<int>& values) const;

  private:
    Ui::GridEditingWidget* ui = nullptr;
    VisualWidgetService* m_visualWidgetService = nullptr;

    vtkSmartPointer<vtkUnstructuredGrid> m_fullGrid;     // 原始完整网格副本
    vtkSmartPointer<vtkPolyData> m_viewSurface;          // 当前全局筛选后的面/体外表面
    vtkSmartPointer<vtkUnstructuredGrid> m_viewLineGrid; // 当前全局筛选后的线单元

    QString m_sourceName;

    QSet<int> m_allTopologies;
    QSet<int> m_allKetypes;
    QSet<int> m_allMatypes;
    QSet<int> m_allLayers;
    QSet<int> m_allRegions;
    QMap<int, QString> m_regionNames;

    QSet<int> m_filterTopologies; // 当前勾选保留的真实 VTK 拓扑类型
    QSet<int> m_filterKetypes;    // 当前勾选保留的求解器 KETYPE
    QSet<int> m_filterMatypes;    // 当前勾选保留的 MATYPE
    QSet<int> m_filterLayers;     // 当前勾选保留的 LAYER
    QSet<int> m_filterRegions;

    ColorMode m_colorMode = ColorMode::None;

    vtkActor* m_surfaceActor = nullptr;
    vtkActor* m_lineActor = nullptr;
    vtkMapper* m_surfaceMapper = nullptr;
    vtkMapper* m_lineMapper = nullptr;

    bool m_updatingFilterTree = false;
};
