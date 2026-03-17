#ifndef SOLVERFREQUENCYDEFINITIONPAGE_H
#define SOLVERFREQUENCYDEFINITIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolverFrequencyDefinitionPage;
}

class SolverFrequencyDefinitionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverFrequencyDefinitionPage(QWidget* parent = nullptr);
    ~SolverFrequencyDefinitionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverFrequencyDefinitionPage* ui;
};

#endif // SOLVERFREQUENCYDEFINITIONPAGE_H
