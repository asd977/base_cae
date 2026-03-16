#ifndef SOLIDMECHANICSSETTINGPAGE_H
#define SOLIDMECHANICSSETTINGPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolidMechanicsSettingPage;
}

class SolidMechanicsSettingPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidMechanicsSettingPage(QWidget* parent = nullptr);
    ~SolidMechanicsSettingPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolidMechanicsSettingPage* ui;
};

#endif // SOLIDMECHANICSSETTINGPAGE_H
