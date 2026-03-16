#ifndef FATIGUEINPUTSETTINGPAGE_H
#define FATIGUEINPUTSETTINGPAGE_H

#include <BasePage.h>

namespace Ui
{
class FatigueInputSettingPage;
}

class FatigueInputSettingPage : public BasePage
{
    Q_OBJECT

  public:
    explicit FatigueInputSettingPage(QWidget* parent = nullptr);
    ~FatigueInputSettingPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::FatigueInputSettingPage* ui;
};

#endif // FATIGUEINPUTSETTINGPAGE_H
