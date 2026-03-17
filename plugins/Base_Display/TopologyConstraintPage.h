#ifndef TOPOLOGYCONSTRAINTPAGE_H
#define TOPOLOGYCONSTRAINTPAGE_H

#include <BasePage.h>

namespace Ui
{
class TopologyConstraintPage;
}

class TopologyConstraintPage : public BasePage
{
    Q_OBJECT

  public:
    explicit TopologyConstraintPage(QWidget* parent = nullptr);
    ~TopologyConstraintPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::TopologyConstraintPage* ui;
};

#endif // TOPOLOGYCONSTRAINTPAGE_H
