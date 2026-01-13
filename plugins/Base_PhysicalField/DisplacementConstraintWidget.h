#ifndef DISPLACEMENTCONSTRAINTWIDGET_H
#define DISPLACEMENTCONSTRAINTWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class DisplacementConstraintWidget;
}

class DisplacementConstraintWidget : public BasePage
{
    Q_OBJECT

public:
    explicit DisplacementConstraintWidget(QWidget *parent = nullptr);
    ~DisplacementConstraintWidget();
    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::DisplacementConstraintWidget *ui;
};

#endif // DISPLACEMENTCONSTRAINTWIDGET_H
