#ifndef VISUALIZATIONSTYLEASSISTPAGE_H
#define VISUALIZATIONSTYLEASSISTPAGE_H

#include <BasePage.h>

namespace Ui
{
class VisualizationStyleAssistPage;
}

class VisualizationStyleAssistPage : public BasePage
{
    Q_OBJECT

  public:
    explicit VisualizationStyleAssistPage(QWidget* parent = nullptr);
    ~VisualizationStyleAssistPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::VisualizationStyleAssistPage* ui;
};

#endif // VISUALIZATIONSTYLEASSISTPAGE_H
