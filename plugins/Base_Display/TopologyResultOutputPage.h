#ifndef TOPOLOGYRESULTOUTPUTPAGE_H
#define TOPOLOGYRESULTOUTPUTPAGE_H

#include <BasePage.h>

namespace Ui
{
class TopologyResultOutputPage;
}

class TopologyResultOutputPage : public BasePage
{
    Q_OBJECT

  public:
    explicit TopologyResultOutputPage(QWidget* parent = nullptr);
    ~TopologyResultOutputPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::TopologyResultOutputPage* ui;
};

#endif // TOPOLOGYRESULTOUTPUTPAGE_H
