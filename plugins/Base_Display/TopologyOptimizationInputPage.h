#ifndef TOPOLOGYOPTIMIZATIONINPUTPAGE_H
#define TOPOLOGYOPTIMIZATIONINPUTPAGE_H

#include <BasePage.h>

namespace Ui
{
class TopologyOptimizationInputPage;
}

class TopologyOptimizationInputPage : public BasePage
{
    Q_OBJECT

  public:
    explicit TopologyOptimizationInputPage(QWidget* parent = nullptr);
    ~TopologyOptimizationInputPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::TopologyOptimizationInputPage* ui;
};

#endif // TOPOLOGYOPTIMIZATIONINPUTPAGE_H
