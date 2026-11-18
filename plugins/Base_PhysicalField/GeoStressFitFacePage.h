#pragma once

#include <QWidget>

#include "BasePage.h"
#include "LoadPageState.h"
#include "PickManager.h"

class QCheckBox;
class QLabel;

namespace Ui
{
class GeoStressFitFacePage;
}

class GeoStressFitFacePage : public BasePage
{
    Q_OBJECT
  public:
    CAE_LOAD_PAGE_STATE("editFname","spinII","spinRou","spinHeight","spinPgrout","comboIdir","radionButtonFace","radionButtonEdge")
    explicit GeoStressFitFacePage(QWidget* parent = nullptr);
    ~GeoStressFitFacePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    // -------- Part2 参数读取 --------
    QString fname() const;
    int idir() const; // 1/2/3
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
    void onFacePicked(const QString& domain, const QString& activeUuid, GridKind kind,
                      vtkIdType faceKey, bool selected);

  private:
    Ui::GeoStressFitFacePage* ui = nullptr;
    PickMode m_activeMode = PickMode::Edge; // 记录 unload 时用哪个 mode deactivate
    QCheckBox* m_wholeFaceCheckBox = nullptr;
    QLabel* m_selectionStatusLabel = nullptr;

    PickMode decidePickMode() const;
    void ensureDefaultPickTarget();
    QString fnameForExport() const;
    void refreshGroupCombo();
    void refreshSelectionSummary();
};
