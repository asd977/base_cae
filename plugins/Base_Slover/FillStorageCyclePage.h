#pragma once

#include <BasePage.h>

namespace Ui
{
class FillStorageCyclePage;
}

class FillStorageCyclePage : public BasePage
{
    Q_OBJECT

  public:
    explicit FillStorageCyclePage(QWidget* parent = nullptr);
    ~FillStorageCyclePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

  private:
    Ui::FillStorageCyclePage* ui = nullptr;
};
