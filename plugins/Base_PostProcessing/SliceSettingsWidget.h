#pragma once

#include <BasePage.h>
#include <vtkSmartPointer.h>

#include <QString>
#include <QVector>
#include <QWidget>

class VisualWidgetService;
class vtkUnstructuredGrid;
class vtkPlane;
class vtkActor;
class vtkPolyDataMapper;
class vtkPlaneSource;
class vtkCutter;
class vtkLookupTable;

namespace Ui {
class SliceSettingsWidget;
}

class SliceSettingsWidget : public BasePage {
  Q_OBJECT
 public:
  explicit SliceSettingsWidget(VisualWidgetService* postProcessingWidget,
                               QWidget* parent = nullptr);
  ~SliceSettingsWidget();

  // 页面生命周期：外部切换页时调用
  void beforeLoad();    // 首次真正加载；其后仅把上次显示的内容挂回
  void beforeUnload();  // 只移除 actor，不销毁对象（状态保留）

 private slots:
  // 文件/标量/方向
  void on_stepComboBox_currentIndexChanged(int index);
  void on_scalarComboBox_currentIndexChanged(int index);
  void on_planeComboBox_currentIndexChanged(int index);

  // 单切面预览（紫色大截面），仅在“单切面模式”才预览
  void showCutPlane(int dummy = 0);

  // 确认：按当前模式（单/多）生成结果
  void on_confirmButton_clicked();

  // 多切面/单切面互斥切换
  void on_multiSliceGroupBox_toggled(bool checked);

  void on_singleSliceGroupBox_toggled(bool checked);

 private:
  /* ---- 内部助手 ---- */
  void loadFileName(QString dataPath);
  void readTecplot(const QString& fileName);

  // 整体云图
  void updateNephogram(const QString& scalar);

  // 单切面：按当前 m_plane 生成切面云图（仅在确认时调用）
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

 private:
  Ui::SliceSettingsWidget* ui{};
  VisualWidgetService* m_visualService{};

  bool m_loadedOnce{false};
  QString m_dataPath;

  // 网格 & 范围
  vtkSmartPointer<vtkUnstructuredGrid> m_grid;
  double m_XRange[2]{0, 0}, m_YRange[2]{0, 0}, m_ZRange[2]{0, 0};

  // 整体云图
  vtkSmartPointer<vtkActor> m_actor;

  // 单切面：预览平面（紫色）+ 结果面
  vtkSmartPointer<vtkPlane> m_plane;
  vtkSmartPointer<vtkPlaneSource> m_slicePlaneSource;
  vtkSmartPointer<vtkPolyDataMapper> m_slicePlaneMapper;
  vtkSmartPointer<vtkActor> m_slicePlaneActor;  // 预览
  vtkSmartPointer<vtkActor> m_sliceActor;       // 结果

  // 多切面：一组结果（确认后生成）
  QVector<vtkSmartPointer<vtkPlane>> m_multiPlanes;
  QVector<vtkSmartPointer<vtkCutter>> m_multiCutters;
  QVector<vtkSmartPointer<vtkPolyDataMapper>> m_multiMappers;
  QVector<vtkSmartPointer<vtkActor>> m_multiActors;

  // 显示状态（用于 beforeLoad 挂回）
  bool m_singleDisplayed{false};  // 正在显示单切面结果
  bool m_multiDisplayed{false};   // 正在显示多切面结果
};
