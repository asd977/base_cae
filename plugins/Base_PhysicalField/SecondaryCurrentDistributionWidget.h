#ifndef SECONDARYCURRENTDISTRIBUTIONWIDGET_H
#define SECONDARYCURRENTDISTRIBUTIONWIDGET_H

#include "CurrentDistributionWidgetBase.h"

namespace Ui {
class SecondaryCurrentDistributionWidget;
}

class SecondaryCurrentDistributionWidget : public CurrentDistributionWidgetBase
{
    Q_OBJECT

public:
    explicit SecondaryCurrentDistributionWidget(QWidget *parent = nullptr);
    ~SecondaryCurrentDistributionWidget();

private:
    QLineEdit *outputDirLineEdit() const override;
    QPushButton *outputBrowseButton() const override;
    QLineEdit *electrolyteLineEdit() const override;
    QLineEdit *electrodeLineEdit() const override;
    QLineEdit *insulationLineEdit() const override;
    QPushButton *electrolyteButton() const override;
    QPushButton *electrodeButton() const override;
    QPushButton *insulationButton() const override;
    QPushButton *runButton() const override;

    QDoubleSpinBox *conductivitySpin() const override;
    QDoubleSpinBox *anodePotentialSpin() const override;
    QDoubleSpinBox *cathodePotentialSpin() const override;
    QDoubleSpinBox *exchangeCurrentSpin() const override;
    QDoubleSpinBox *anodicCoeffSpin() const override;
    QDoubleSpinBox *cathodicCoeffSpin() const override;

private:
    Ui::SecondaryCurrentDistributionWidget *ui;
};

#endif // SECONDARYCURRENTDISTRIBUTIONWIDGET_H
