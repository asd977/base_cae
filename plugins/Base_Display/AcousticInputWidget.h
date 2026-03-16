#ifndef ACOUSTICINPUTWIDGET_H
#define ACOUSTICINPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class AcousticInputWidget;
}

class AcousticInputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit AcousticInputWidget(QWidget* parent = nullptr);
    ~AcousticInputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::AcousticInputWidget* ui;
};

#endif // ACOUSTICINPUTWIDGET_H
