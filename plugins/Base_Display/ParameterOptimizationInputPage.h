#ifndef PARAMETEROPTIMIZATIONINPUTPAGE_H
#define PARAMETEROPTIMIZATIONINPUTPAGE_H

#include <BasePage.h>

namespace Ui
{
class ParameterOptimizationInputPage;
}

class ParameterOptimizationInputPage : public BasePage
{
    Q_OBJECT

  public:
    explicit ParameterOptimizationInputPage(QWidget* parent = nullptr);
    ~ParameterOptimizationInputPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::ParameterOptimizationInputPage* ui;
};

#endif // PARAMETEROPTIMIZATIONINPUTPAGE_H
