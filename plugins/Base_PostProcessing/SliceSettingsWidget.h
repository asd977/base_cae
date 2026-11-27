#pragma once

#include <BasePage.h>
#include <vtkActor2D.h>
#include <vtkSmartPointer.h>

#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class VisualWidgetService;
class vtkUnstructuredGrid;
class vtkPolyData;
class vtkPlane;
class vtkActor;
class vtkPolyDataMapper;
class vtkCutter;
class vtkLookupTable;
class vtkCellDataToPointData;

namespace Ui
{
class SliceSettingsWidget;
}

class SliceSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit SliceSettingsWidget(VisualWidgetService* postProcessingWidget,
                                 QWidget* parent = nullptr);
    ~SliceSettingsWidget();

    // 页面生命周期：外部切换页时调用
    void beforeLoad() override;   // 首次真正加载；其后仅把上次显示的内容挂回
    void beforeUnload() override; // 只移除 actor，不销毁对象（状态保留）
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    // 文件/标量/方向
    void on_stepComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);
    void on_resultTimeComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_planeComboBox_currentIndexChanged(int index);

    // 单切面位置变化后直接更新真实切面结果。
    void showCutPlane(int dummy = 0);

    // 多切面由按钮显式生成；单切面调用时仅做兼容刷新。
    void on_confirmButton_clicked();

    void on_singleSliceRadioButton_toggled(bool checked);
    void on_slicePositionSpin_valueChanged(double value);

  private:
    /* ---- 内部助手 ---- */
    void loadFileName(QString dataPath);
    void refreshResultGroups();
    void readTecplot(const QString& fileName);

    // 整体云图
    void updateNephogram(const QString& scalar);

    // 单切面：按当前 m_plane 生成切面云图。
    void buildSingleSliceResult();

    // 多切面：按数量/端点/方向等距生成（仅在确认时调用）
    void buildMultiSliceResult();

    // 多切面：移除但不清空数据结构（为了切换模式或切换步时只显示整体）
    void hideMultiSlicesFromScene();
    // 多切面：完全清空（用于切步后丢弃旧结果）
    void clearMultiSlicesAll();

    // 统一显隐：把“当前要显示的内容”挂到场景（根据
    // m_singleDisplayed/m_multiDisplayed）
    void reattachAccordingToDisplayState();
    void detachAllFromScene();
    void applyPendingState();
    void updatePositionControls(double normalizedPosition);
    double currentNormalizedPosition() const;
    bool isSingleMode() const;

  private:
    Ui::SliceSettingsWidget* ui{};
    VisualWidgetService* m_visualService{};

    bool m_loadedOnce{false};
    bool m_suppressRender{false};
    QString m_loadedFilePath;
    qint64 m_loadedFileSize{-1};
    qint64 m_loadedFileModified{-1};
    QString m_contextCacheKey;
    QString m_surfaceCacheKey;
    vtkSmartPointer<vtkPolyData> m_cachedSurface;
    void updateContextActors(vtkUnstructuredGrid* displayGrid);
    bool m_hasPendingState{false};
    QVariantMap m_pendingState;
    QString m_dataPath;

    // 网格 & 范围
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    vtkSmartPointer<vtkCellDataToPointData> m_cellToPoint;
    double m_XRange[2]{0, 0}, m_YRange[2]{0, 0}, m_ZRange[2]{0, 0};

    // 整体云图
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkActor> m_contextActor;
    vtkSmartPointer<vtkActor> m_contextOutlineActor;

    // 单切面结果
    vtkSmartPointer<vtkPlane> m_plane;
    vtkSmartPointer<vtkActor> m_sliceActor;
    vtkSmartPointer<vtkActor> m_cutIndicatorActor;
    vtkSmartPointer<vtkActor> m_sliceContourActor;
    vtkSmartPointer<vtkActor2D> m_sliceContourLabelActor;

    // 多切面：一组结果（确认后生成）
    QVector<vtkSmartPointer<vtkPlane>> m_multiPlanes;
    QVector<vtkSmartPointer<vtkCutter>> m_multiCutters;
    QVector<vtkSmartPointer<vtkPolyDataMapper>> m_multiMappers;
    QVector<vtkSmartPointer<vtkActor>> m_multiActors;
    QVector<vtkSmartPointer<vtkActor>> m_multiCutIndicatorActors;
    QVector<vtkSmartPointer<vtkActor>> m_multiContourActors;
    QVector<vtkSmartPointer<vtkActor2D>> m_multiContourLabelActors;

    // 显示状态（用于 beforeLoad 挂回）
    bool m_singleDisplayed{false}; // 正在显示单切面结果
    bool m_multiDisplayed{false};  // 正在显示多切面结果
};
