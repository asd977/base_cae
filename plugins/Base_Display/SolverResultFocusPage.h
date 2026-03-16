#ifndef SOLVERRESULTFOCUSPAGE_H
#define SOLVERRESULTFOCUSPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolverResultFocusPage;
}

class SolverResultFocusPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverResultFocusPage(QWidget* parent = nullptr);
    ~SolverResultFocusPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverResultFocusPage* ui;
};

#endif // SOLVERRESULTFOCUSPAGE_H
