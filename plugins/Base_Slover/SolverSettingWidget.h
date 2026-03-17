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
    explicit SolverSettingWidget(QWidget *parent = nullptr);
    ~SolverSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::SolverSettingWidget *ui;
};

#endif // SOLVERSETTINGWIDGET_H
