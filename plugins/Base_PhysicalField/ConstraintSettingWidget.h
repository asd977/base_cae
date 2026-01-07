#ifndef CONSTRAINTSETTINGWIDGET_H
#define CONSTRAINTSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ConstraintSettingWidget;
}

class ConstraintSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ConstraintSettingWidget(QWidget *parent = nullptr);
    ~ConstraintSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::ConstraintSettingWidget *ui;
};

#endif // CONSTRAINTSETTINGWIDGET_H
