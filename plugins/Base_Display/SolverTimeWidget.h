#ifndef SOLVERTIMEWIDGET_H
#define SOLVERTIMEWIDGET_H

#include <BasePage.h>

namespace Ui
{
class SolverTimeWidget;
}

class SolverTimeWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverTimeWidget(QWidget* parent = nullptr);
    ~SolverTimeWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverTimeWidget* ui;
};

#endif // SOLVERTIMEWIDGET_H
