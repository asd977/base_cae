#ifndef SOLVERMESHSETTINGPAGE_H
#define SOLVERMESHSETTINGPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolverMeshSettingPage;
}

class SolverMeshSettingPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolverMeshSettingPage(QWidget* parent = nullptr);
    ~SolverMeshSettingPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolverMeshSettingPage* ui;
};

#endif // SOLVERMESHSETTINGPAGE_H
