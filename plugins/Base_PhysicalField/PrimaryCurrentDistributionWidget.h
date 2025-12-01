#ifndef PRIMARYCURRENTDISTRIBUTIONWIDGET_H
#define PRIMARYCURRENTDISTRIBUTIONWIDGET_H

#include "CurrentDistributionWidgetBase.h"

namespace Ui {
class PrimaryCurrentDistributionWidget;
}

class PrimaryCurrentDistributionWidget : public CurrentDistributionWidgetBase
{
    Q_OBJECT

public:
    explicit PrimaryCurrentDistributionWidget(QWidget *parent = nullptr);
    ~PrimaryCurrentDistributionWidget();

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

private:
    Ui::PrimaryCurrentDistributionWidget *ui;
};

#endif // PRIMARYCURRENTDISTRIBUTIONWIDGET_H
