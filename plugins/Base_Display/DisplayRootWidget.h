#ifndef DISPLAYROOTWIDGET_H
#define DISPLAYROOTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class DisplayRootWidget;
}

class DisplayRootWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit DisplayRootWidget(QWidget* parent = nullptr);
    ~DisplayRootWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::DisplayRootWidget* ui;
};

#endif // DISPLAYROOTWIDGET_H
