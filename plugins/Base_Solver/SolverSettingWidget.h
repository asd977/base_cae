#ifndef SOLVERSETTINGWIDGET_H
#define SOLVERSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class SolverSettingWidget;
}

class SolverSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit SolverSettingWidget(QWidget* parent = nullptr);
    ~SolverSettingWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    void setAnalysisCounts(int seepageCount, int damCount, int slopeCount,
                           int caveCount, int blockCount);

signals:
    void createSeepageRequested();
    void createDamRequested();
    void createSlopeRequested();
    void createCaveRequested();
    void createBlockRequested();

private:
    Ui::SolverSettingWidget* ui;
};

#endif // SOLVERSETTINGWIDGET_H
