#pragma once

#include <BasePage.h>

#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class QListWidgetItem;

namespace Ui {
class DerivedValueSettingWidget;
}

class DerivedValueSettingWidget : public BasePage {
  Q_OBJECT
 public:
  explicit DerivedValueSettingWidget(QWidget* parent = nullptr);
  ~DerivedValueSettingWidget() override;

  void beforeLoad() override;    // 首次才扫描&初始化
  void beforeUnload() override;  // 本页无需处理

 protected:
  // 让“点整行任意位置”也能切换勾选；点到复选框则走 Qt 默认逻辑
  bool eventFilter(QObject* obj, QEvent* ev) override;

 private slots:
  void refreshDatasets();
  void computeStats();
  void rebuildScalarComboForAssoc();

 private:
  // UI 辅助
  void updateSelectionInfo();
  void setItemCheckedVisual(QListWidgetItem* it, bool checked);

  // 状态恢复
  void applyState(const QVariantMap& state);

  // 扫描/数据
  void rebuildScalarCatalog();

  // ---- 内部数据结构 ----
  struct FrameItem {
    int group{};
    int order{};
    QString filePath;
  };

 private:
  Ui::DerivedValueSettingWidget* ui{};

  bool m_loadedOnce{false};
  bool m_hasPendingState{false};
  QVariantMap m_pendingState;

  QString m_dataPath;
  QVector<FrameItem> m_frames;

  // 变量名目录
  QSet<QString> m_pointArrays;
  QSet<QString> m_cellArrays;
};
