#pragma once

#include <BasePage.h>

namespace Ui
{
class PostContourReaderWidget;
}

class PostContourReaderWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit PostContourReaderWidget(QWidget* parent = nullptr);
    ~PostContourReaderWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

  private:
    Ui::PostContourReaderWidget* ui = nullptr;
};
