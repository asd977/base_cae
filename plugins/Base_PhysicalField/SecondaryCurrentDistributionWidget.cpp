#include "SecondaryCurrentDistributionWidget.h"
#include "ui_SecondaryCurrentDistributionWidget.h"

SecondaryCurrentDistributionWidget::SecondaryCurrentDistributionWidget(QWidget *parent)
    : CurrentDistributionWidgetBase("SecondaryCurrentDistributionWidget", "secondary", parent),
      ui(new Ui::SecondaryCurrentDistributionWidget)
{
    ui->setupUi(this);
    connectCommonUi();
    loadConfigFromSolver();
}

SecondaryCurrentDistributionWidget::~SecondaryCurrentDistributionWidget()
{
    delete ui;
}

QLineEdit *SecondaryCurrentDistributionWidget::outputDirLineEdit() const
{
    return ui->outputDirLineEdit;
}

QPushButton *SecondaryCurrentDistributionWidget::outputBrowseButton() const
{
    return ui->outputBrowsePushButton;
}

QLineEdit *SecondaryCurrentDistributionWidget::electrolyteLineEdit() const
{
    return ui->electrolyteLineEdit;
}

QLineEdit *SecondaryCurrentDistributionWidget::electrodeLineEdit() const
{
    return ui->electrodeLineEdit;
}

QLineEdit *SecondaryCurrentDistributionWidget::insulationLineEdit() const
{
    return ui->insulationLineEdit;
}

QPushButton *SecondaryCurrentDistributionWidget::electrolyteButton() const
{
    return ui->electrolytePushButton;
}

QPushButton *SecondaryCurrentDistributionWidget::electrodeButton() const
{
    return ui->electrodeSurfacePushButton;
}

QPushButton *SecondaryCurrentDistributionWidget::insulationButton() const
{
    return ui->insulationPushButton;
}

QPushButton *SecondaryCurrentDistributionWidget::runButton() const
{
    return ui->runPushButton;
}

QDoubleSpinBox *SecondaryCurrentDistributionWidget::conductivitySpin() const
{
    return ui->conductivityDoubleSpinBox;
}

QDoubleSpinBox *SecondaryCurrentDistributionWidget::anodePotentialSpin() const
{
    return ui->anodePotentialDoubleSpinBox;
}

QDoubleSpinBox *SecondaryCurrentDistributionWidget::cathodePotentialSpin() const
{
    return ui->cathodePotentialDoubleSpinBox;
}

QDoubleSpinBox *SecondaryCurrentDistributionWidget::exchangeCurrentSpin() const
{
    return ui->exchangeCurrentDoubleSpinBox;
}

QDoubleSpinBox *SecondaryCurrentDistributionWidget::anodicCoeffSpin() const
{
    return ui->anodicCoeffDoubleSpinBox;
}

QDoubleSpinBox *SecondaryCurrentDistributionWidget::cathodicCoeffSpin() const
{
    return ui->cathodicCoeffDoubleSpinBox;
}

