#ifndef SOLVERVARIABLEDEFINITIONPAGE_H
#define SOLVERVARIABLEDEFINITIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolverVariableDefinitionPage;
}

class SolverVariableDefinitionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverVariableDefinitionPage(QWidget* parent = nullptr);
    ~SolverVariableDefinitionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverVariableDefinitionPage* ui;
};

#endif // SOLVERVARIABLEDEFINITIONPAGE_H
