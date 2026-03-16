#ifndef SOLIDBOUNDARYCONDITIONPAGE_H
#define SOLIDBOUNDARYCONDITIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolidBoundaryConditionPage;
}

class SolidBoundaryConditionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidBoundaryConditionPage(QWidget* parent = nullptr);
    ~SolidBoundaryConditionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolidBoundaryConditionPage* ui;
};

#endif // SOLIDBOUNDARYCONDITIONPAGE_H
