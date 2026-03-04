#pragma once

#include <QWidget>

#include "BasePage.h"
#include "PickManager.h"

namespace Ui {
class GeoStressFitFacePage;
}

class GeoStressFitFacePage : public BasePage {
  Q_OBJECT
 public:
  explicit GeoStressFitFacePage(QWidget* parent = nullptr);
  ~GeoStressFitFacePage() override;

  void beforeLoad() override;
  void beforeUnload() override;

  // -------- Part2 参数读取 --------
  QString fname() const;
  int idir() const;  // 1/2/3
  double height() const;
  double rou() const;
  double pgrout() const;
  int ii() const;

  // 用户选择：边 or 面
  bool pickEdge() const;
  bool pickFace() const;

  bool exportLeftDatFile();
 protected slots:
  void onGroupComboChanged(int idx);
  void onDimComboChanged(int idx);
 private slots:
  void onPickTargetChanged();

 private:
  Ui::GeoStressFitFacePage* ui = nullptr;
  PickMode m_activeMode =
      PickMode::Edge;  // 记录 unload 时用哪个 mode deactivate

  PickMode decidePickMode() const;
  void ensureDefaultPickTarget();
  QString fnameForExport() const;
  void refreshGroupCombo();
};
