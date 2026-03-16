#ifndef ACOUSTICOUTPUTWIDGET_H
#define ACOUSTICOUTPUTWIDGET_H

#include <BasePage.h>

namespace Ui
{
class AcousticOutputWidget;
}

class AcousticOutputWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit AcousticOutputWidget(QWidget* parent = nullptr);
    ~AcousticOutputWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::AcousticOutputWidget* ui;
};

#endif // ACOUSTICOUTPUTWIDGET_H
