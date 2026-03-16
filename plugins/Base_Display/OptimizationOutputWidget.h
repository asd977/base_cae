#ifndef OPTIMIZATIONOUTPUTWIDGET_H
#define OPTIMIZATIONOUTPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class OptimizationOutputWidget;
}

class OptimizationOutputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit OptimizationOutputWidget(QWidget* parent = nullptr);
    ~OptimizationOutputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::OptimizationOutputWidget* ui;
};

#endif // OPTIMIZATIONOUTPUTWIDGET_H
