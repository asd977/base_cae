#ifndef SOLVERTIMEDEFINITIONPAGE_H
#define SOLVERTIMEDEFINITIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolverTimeDefinitionPage;
}

class SolverTimeDefinitionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverTimeDefinitionPage(QWidget* parent = nullptr);
    ~SolverTimeDefinitionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverTimeDefinitionPage* ui;
};

#endif // SOLVERTIMEDEFINITIONPAGE_H
