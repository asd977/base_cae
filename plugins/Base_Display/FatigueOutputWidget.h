#ifndef FATIGUEOUTPUTWIDGET_H
#define FATIGUEOUTPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class FatigueOutputWidget;
}

class FatigueOutputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit FatigueOutputWidget(QWidget* parent = nullptr);
    ~FatigueOutputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::FatigueOutputWidget* ui;
};

#endif // FATIGUEOUTPUTWIDGET_H
