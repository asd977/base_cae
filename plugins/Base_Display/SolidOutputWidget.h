#ifndef SOLIDOUTPUTWIDGET_H
#define SOLIDOUTPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class SolidOutputWidget;
}

class SolidOutputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidOutputWidget(QWidget* parent = nullptr);
    ~SolidOutputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolidOutputWidget* ui;
};

#endif // SOLIDOUTPUTWIDGET_H
