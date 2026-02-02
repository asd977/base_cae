#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

namespace Ui { class TemperatureLoadPage; }

class TemperatureLoadPage : public BasePage
{
    Q_OBJECT
public:
    explicit TemperatureLoadPage(QWidget* parent = nullptr);
    ~TemperatureLoadPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    double alfaValue() const;
    double dtValue() const;

private:
    Ui::TemperatureLoadPage* ui = nullptr;

};
