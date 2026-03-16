#ifndef SOLIDBOUNDARYWIDGET_H
#define SOLIDBOUNDARYWIDGET_H

#include <BasePage.h>

namespace Ui
{
class SolidBoundaryWidget;
}

class SolidBoundaryWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidBoundaryWidget(QWidget* parent = nullptr);
    ~SolidBoundaryWidget();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolidBoundaryWidget* ui;
};

#endif // SOLIDBOUNDARYWIDGET_H
