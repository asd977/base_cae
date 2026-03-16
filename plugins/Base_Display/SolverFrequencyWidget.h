#ifndef SOLVERFREQUENCYWIDGET_H
#define SOLVERFREQUENCYWIDGET_H

#include <BasePage.h>

namespace Ui
{
class SolverFrequencyWidget;
}

class SolverFrequencyWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverFrequencyWidget(QWidget* parent = nullptr);
    ~SolverFrequencyWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverFrequencyWidget* ui;
};

#endif // SOLVERFREQUENCYWIDGET_H
