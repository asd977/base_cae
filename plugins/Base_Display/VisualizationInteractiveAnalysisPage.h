#ifndef VISUALIZATIONINTERACTIVEANALYSISPAGE_H
#define VISUALIZATIONINTERACTIVEANALYSISPAGE_H

#include <BasePage.h>

namespace Ui
{
class VisualizationInteractiveAnalysisPage;
}

class VisualizationInteractiveAnalysisPage : public BasePage
{
    Q_OBJECT

  public:
    explicit VisualizationInteractiveAnalysisPage(QWidget* parent = nullptr);
    ~VisualizationInteractiveAnalysisPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::VisualizationInteractiveAnalysisPage* ui;
};

#endif // VISUALIZATIONINTERACTIVEANALYSISPAGE_H
