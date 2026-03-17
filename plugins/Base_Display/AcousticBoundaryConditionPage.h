#ifndef ACOUSTICBOUNDARYCONDITIONPAGE_H
#define ACOUSTICBOUNDARYCONDITIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class AcousticBoundaryConditionPage;
}

class AcousticBoundaryConditionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit AcousticBoundaryConditionPage(QWidget* parent = nullptr);
    ~AcousticBoundaryConditionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::AcousticBoundaryConditionPage* ui;
};

#endif // ACOUSTICBOUNDARYCONDITIONPAGE_H
