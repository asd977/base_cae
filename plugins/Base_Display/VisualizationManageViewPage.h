#ifndef VISUALIZATIONMANAGEVIEWPAGE_H
#define VISUALIZATIONMANAGEVIEWPAGE_H

#include <BasePage.h>

namespace Ui
{
class VisualizationManageViewPage;
}

class VisualizationManageViewPage : public BasePage
{
    Q_OBJECT

  public:
    explicit VisualizationManageViewPage(QWidget* parent = nullptr);
    ~VisualizationManageViewPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::VisualizationManageViewPage* ui;
};

#endif // VISUALIZATIONMANAGEVIEWPAGE_H
