#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPushButton;

namespace Ui
{
class FaceLoadPage;
}

class FaceLoadPage : public BasePage
{
    Q_OBJECT
  public:
    explicit FaceLoadPage(QWidget* parent = nullptr);
    ~FaceLoadPage() override;

    void beforeLoad() override;
    void beforeUnload() override;
    double fxValue();
    double fyValue();
    double fzValue();

  private slots:
    void onGroupChanged(int index);
    void onDimChanged(int index);

  private:
    void setupBatchGroupCombo();
    void refreshGroupCombo();

  private:
    Ui::FaceLoadPage* ui = nullptr;
    QComboBox* m_groupCombo = nullptr;
};
