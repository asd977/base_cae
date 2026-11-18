#pragma once

#include "BasePage.h"
#include "LoadPageState.h"

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QComboBox;

namespace Ui
{
class TemperatureLoadPage;
}

class TemperatureLoadPage : public BasePage
{
    Q_OBJECT
  public:
    CAE_LOAD_PAGE_STATE("lineEditAlfa","lineEditDT")
    explicit TemperatureLoadPage(QWidget* parent = nullptr);
    ~TemperatureLoadPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    double alfaValue() const;
    double dtValue() const;

  private slots:
    void onGroupChanged(int index);
    void onDimChanged(int index);

  private:
    void setupBatchGroupCombo();
    void refreshGroupCombo();

  private:
    Ui::TemperatureLoadPage* ui = nullptr;
    QComboBox* m_groupCombo = nullptr;
};
