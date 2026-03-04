#ifndef MESHGROUPINGWIDGET_H
#define MESHGROUPINGWIDGET_H

#include <BasePage.h>
#include <struct/MeshMap.h>
#include <vtkSmartPointer.h>

class vtkActor;

namespace Ui {
class MeshGroupingWidget;
}

class MeshGroupingWidget : public BasePage {
  Q_OBJECT

 public:
  explicit MeshGroupingWidget(QWidget *parent = nullptr);
  ~MeshGroupingWidget();

  void beforeLoad() override;
  void beforeUnload() override;

 private slots:
  void onAutoGrouping();
  void onRangeGrouping();
  void onRefreshGroups();
  void onToggleRangePreview(bool on);
  void onRangeValueChanged();
  void onAutoDetectBoundaries();

 private:
  void refreshGroupList();
  void refreshBoundaryGroupList();
  void detectBoundaryGroupsBySurfacePatches();
  void initializeRangeByGridBounds(bool force = false);
  void updateRangePreview(bool forceOn = false);
  void clearRangePreview(bool renderNow = true);
  QString currentTargetArrayName() const;

 private:
  Ui::MeshGroupingWidget *ui;
  vtkSmartPointer<vtkActor> m_rangePreviewActor;
  bool m_hasAutoBoundaryGroups = false;
  MshPickMap m_autoBoundaryPickMap;
};

#endif  // MESHGROUPINGWIDGET_H
