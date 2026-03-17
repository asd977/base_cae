#ifndef VISUALIZATIONEXPRESSIONPAGE_H
#define VISUALIZATIONEXPRESSIONPAGE_H

#include <BasePage.h>

namespace Ui
{
class VisualizationExpressionPage;
}

class VisualizationExpressionPage : public BasePage
{
    Q_OBJECT

  public:
    explicit VisualizationExpressionPage(QWidget* parent = nullptr);
    ~VisualizationExpressionPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::VisualizationExpressionPage* ui;
};

#endif // VISUALIZATIONEXPRESSIONPAGE_H
