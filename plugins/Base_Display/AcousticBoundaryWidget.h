#ifndef ACOUSTICBOUNDARYWIDGET_H
#define ACOUSTICBOUNDARYWIDGET_H

#include <BasePage.h>

namespace Ui
{
class AcousticBoundaryWidget;
}

class AcousticBoundaryWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit AcousticBoundaryWidget(QWidget* parent = nullptr);
    ~AcousticBoundaryWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::AcousticBoundaryWidget* ui;
};

#endif // ACOUSTICBOUNDARYWIDGET_H
