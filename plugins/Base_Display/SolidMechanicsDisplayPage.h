#ifndef SOLIDMECHANICSDISPLAYPAGE_H
#define SOLIDMECHANICSDISPLAYPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolidMechanicsDisplay;
}

class SolidMechanicsDisplayPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidMechanicsDisplayPage(QWidget* parent = nullptr);
    ~SolidMechanicsDisplayPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

  private:
    Ui::SolidMechanicsDisplay* ui;
};

#endif // SOLIDMECHANICSDISPLAYPAGE_H
