#pragma once

#include <vtkType.h> // vtkIdType

#include <QString>

#include "BasePage.h"

class PickManager;

namespace Ui
{
class GeoStressPointPage;
}

enum class GridKind; // 你工程里已有这个枚举

class GeoStressPointPage : public BasePage
{
    Q_OBJECT
  public:
    explicit GeoStressPointPage(QWidget* parent = nullptr);
    ~GeoStressPointPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    // ===== 参数获取：直接从 ui 读 =====
    double sxValue() const;
    double syValue() const;
    double szValue() const;
    double sxyValue() const;
    double sxzValue() const;
    double syzValue() const;

  private slots:
    void onDimChanged(int index);
    void onGroupChanged(int index);

    // ✅ 注意：这里的参数列表必须和 PickManager::nodePicked(...) 完全一致
    void onNodePicked(const QString& domain, const QString& activeUuid, GridKind kind,
                      vtkIdType pidOrCellId, int nodeId, double x, double y, double z,
                      const QString& prevUuid);

  private:
    void setupBatchGroupCombo();
    void refreshGroupCombo();
    void applyDimensionUi(int dim);
    void refreshPrevOwnerTitle(const QString& prevUuid);

  private:
    Ui::GeoStressPointPage* ui = nullptr;
    int m_dim = 3;
};
