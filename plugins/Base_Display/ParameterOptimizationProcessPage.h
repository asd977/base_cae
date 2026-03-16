#ifndef PARAMETEROPTIMIZATIONPROCESSPAGE_H
#define PARAMETEROPTIMIZATIONPROCESSPAGE_H

#include <BasePage.h>

namespace Ui
{
class ParameterOptimizationProcessPage;
}

class ParameterOptimizationProcessPage : public BasePage
{
    Q_OBJECT

  public:
    explicit ParameterOptimizationProcessPage(QWidget* parent = nullptr);
    ~ParameterOptimizationProcessPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::ParameterOptimizationProcessPage* ui;
};

#endif // PARAMETEROPTIMIZATIONPROCESSPAGE_H
