#ifndef SOLIDRESULTOUTPUTPAGE_H
#define SOLIDRESULTOUTPUTPAGE_H

#include <BasePage.h>

namespace Ui
{
class SolidResultOutputPage;
}

class SolidResultOutputPage : public BasePage
{
    Q_OBJECT

  public:
    explicit SolidResultOutputPage(QWidget* parent = nullptr);
    ~SolidResultOutputPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::SolidResultOutputPage* ui;
};

#endif // SOLIDRESULTOUTPUTPAGE_H
