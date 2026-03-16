#ifndef OPTIMIZATIONINPUTWIDGET_H
#define OPTIMIZATIONINPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class OptimizationInputWidget;
}

class OptimizationInputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit OptimizationInputWidget(QWidget* parent = nullptr);
    ~OptimizationInputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::OptimizationInputWidget* ui;
};

#endif // OPTIMIZATIONINPUTWIDGET_H
