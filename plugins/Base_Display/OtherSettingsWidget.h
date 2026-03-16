#ifndef OTHERSETTINGSWIDGET_H
#define OTHERSETTINGSWIDGET_H

#include <BasePage.h>

namespace Ui
{
class OtherSettingsWidget;
}

class OtherSettingsWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit OtherSettingsWidget(QWidget* parent = nullptr);
    ~OtherSettingsWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::OtherSettingsWidget* ui;
};

#endif // OTHERSETTINGSWIDGET_H
