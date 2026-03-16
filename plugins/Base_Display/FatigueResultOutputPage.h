#ifndef FATIGUERESULTOUTPUTPAGE_H
#define FATIGUERESULTOUTPUTPAGE_H

#include <BasePage.h>

namespace Ui
{
class FatigueResultOutputPage;
}

class FatigueResultOutputPage : public BasePage
{
    Q_OBJECT

  public:
    explicit FatigueResultOutputPage(QWidget* parent = nullptr);
    ~FatigueResultOutputPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::FatigueResultOutputPage* ui;
};

#endif // FATIGUERESULTOUTPUTPAGE_H
