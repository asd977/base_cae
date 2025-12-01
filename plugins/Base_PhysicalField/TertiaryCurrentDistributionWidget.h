#ifndef TERTIARYCURRENTDISTRIBUTIONWIDGET_H
#define TERTIARYCURRENTDISTRIBUTIONWIDGET_H

#include "CurrentDistributionWidgetBase.h"

namespace Ui {
class TertiaryCurrentDistributionWidget;
}

class TertiaryCurrentDistributionWidget : public CurrentDistributionWidgetBase
{
    Q_OBJECT

public:
    explicit TertiaryCurrentDistributionWidget(QWidget *parent = nullptr);
    ~TertiaryCurrentDistributionWidget();

private:
    QLineEdit *meshPathLineEdit() const override;
    QPushButton *meshBrowseButton() const override;
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
    QDoubleSpinBox *diffusivitySpin() const override;
    QDoubleSpinBox *concentrationSpin() const override;

private:
    Ui::TertiaryCurrentDistributionWidget *ui;
};

#endif // TERTIARYCURRENTDISTRIBUTIONWIDGET_H
