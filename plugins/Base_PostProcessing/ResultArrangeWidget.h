#pragma once

#include <BasePage.h>

namespace Ui
{
class ResultArrangeWidget;
}

class ResultArrangeWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit ResultArrangeWidget(QWidget* parent = nullptr);
    ~ResultArrangeWidget() override;

  private:
    Ui::ResultArrangeWidget* ui;
    void launchPostTool();
};
