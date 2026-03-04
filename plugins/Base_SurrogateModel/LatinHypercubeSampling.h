#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

class QDoubleSpinBox;
class QSpinBox;
class QStackedWidget;
class QTableWidgetItem;

namespace Ui {
class LatinHypercubeSampling;
}

/* ================= 命名空间：LHS ================= */
namespace LHS {

enum class Dist { Normal = 0, Uniform = 1, RandInt = 2, Gamma = 3 };

struct RowConfig {
  QString name;
  Dist dist{Dist::Normal};
  // Normal
  double mu{0.0}, sigma{1.0};
  // Uniform
  double umin{0.0}, umax{1.0};
  // randint
  int rmin{0}, rmax{10};
  // gamma
  double gshape{1.0}, gloc{0.0}, gscale{1.0};
};

// 助手（声明）
QString distName(Dist d);
int distIndex(Dist d);
Dist distFromIndex(int idx);

}  // namespace LHS
/* ================================================= */

class LatinHypercubeSampling : public BasePage {
  Q_OBJECT
 public:
  explicit LatinHypercubeSampling(QWidget* parent = nullptr);
  ~LatinHypercubeSampling() override;

  void beforeLoad() override;    // 仅首次初始化
  void beforeUnload() override;  // 无需处理

 private slots:
  // UI 操作
  void on_test_clicked();
  void onRowSelectionChanged(int currentRow, int currentCol, int prevRow,
                             int prevCol);
  void onCellNameEdited(QTableWidgetItem* item);
  void onDistComboChanged(int idx);

  // 右侧参数面板值变化
  void onNormalChanged();
  void onUniformChanged();
  void onRandIntChanged();
  void onGammaChanged();

  // 可选按钮（若 .ui 中提供）
  void on_addRowButton_clicked();
  void on_removeRowButton_clicked();

  // 表格切换页（辅助）
  void onDistributionPageSwitchByRow(int row);

 protected:
  bool eventFilter(QObject* obj,
                   QEvent* ev) override;  // 支持 Insert / Delete 快捷键

 private:
  /* ---------- 内部工具 ---------- */
  void ensureOnceInit();       // 构建右侧面板
  void ensureOneRowIfEmpty();  // 无行时新增一行
  void addRow(const LHS::RowConfig& rc = LHS::RowConfig());
  void removeCurrentRow();
  void rebuildRowWidgets(int row);  // 给某行装载下拉框，连接信号
  void updateComboRowProperties();  // 删除/插入后，重置每个 combo 的 row 属性
  void syncPanelFromRow(int row);   // 将 row 配置显示到右侧面板
  void syncRowFromPanel(int row);   // 将右侧面板参数写回 row 配置
  LHS::RowConfig& rowCfg(int row);
  const LHS::RowConfig& rowCfg(int row) const;

  // 导出 JSON
  QJsonObject toJson() const;

 private:
  Ui::LatinHypercubeSampling* ui{};

  bool m_initedOnce{false};
  QVector<LHS::RowConfig> m_rows;  // 与表格行一一对应

  // 右侧参数面板（运行时创建）
  QStackedWidget* m_stack{};
  // Normal
  QDoubleSpinBox *m_mu{}, *m_sigma{};
  // Uniform
  QDoubleSpinBox *m_umin{}, *m_umax{};
  // RandInt
  QSpinBox *m_rmin{}, *m_rmax{};
  // Gamma
  QDoubleSpinBox *m_gshape{}, *m_gloc{}, *m_gscale{};
  void runPythonLHS_File();
};
