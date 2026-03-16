#ifndef SOLIDINPUTWIDGET_H
#define SOLIDINPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class SolidInputWidget;
}

class SolidInputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidInputWidget(QWidget* parent = nullptr);
    ~SolidInputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolidInputWidget* ui;
};

#endif // SOLIDINPUTWIDGET_H
