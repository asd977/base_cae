#ifndef FATIGUEINPUTWIDGET_H
#define FATIGUEINPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class FatigueInputWidget;
}

class FatigueInputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit FatigueInputWidget(QWidget* parent = nullptr);
    ~FatigueInputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::FatigueInputWidget* ui;
};

#endif // FATIGUEINPUTWIDGET_H
