#ifndef PARAMETEROPTIMIZATIONRESULTPAGE_H
#define PARAMETEROPTIMIZATIONRESULTPAGE_H

#include <BasePage.h>

namespace Ui
{
class ParameterOptimizationResultPage;
}

class ParameterOptimizationResultPage : public BasePage
{
    Q_OBJECT

  public:
    explicit ParameterOptimizationResultPage(QWidget* parent = nullptr);
    ~ParameterOptimizationResultPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::ParameterOptimizationResultPage* ui;
};

#endif // PARAMETEROPTIMIZATIONRESULTPAGE_H
