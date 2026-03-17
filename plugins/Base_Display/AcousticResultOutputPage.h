#ifndef ACOUSTICRESULTOUTPUTPAGE_H
#define ACOUSTICRESULTOUTPUTPAGE_H

#include <BasePage.h>

namespace Ui
{
class AcousticResultOutputPage;
}

class AcousticResultOutputPage : public BasePage
{
    Q_OBJECT

  public:
    explicit AcousticResultOutputPage(QWidget* parent = nullptr);
    ~AcousticResultOutputPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::AcousticResultOutputPage* ui;
};

#endif // ACOUSTICRESULTOUTPUTPAGE_H
