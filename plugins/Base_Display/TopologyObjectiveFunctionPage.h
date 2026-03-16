#ifndef TOPOLOGYOBJECTIVEFUNCTIONPAGE_H
#define TOPOLOGYOBJECTIVEFUNCTIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class TopologyObjectiveFunctionPage;
}

class TopologyObjectiveFunctionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit TopologyObjectiveFunctionPage(QWidget* parent = nullptr);
    ~TopologyObjectiveFunctionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::TopologyObjectiveFunctionPage* ui;
};

#endif // TOPOLOGYOBJECTIVEFUNCTIONPAGE_H
