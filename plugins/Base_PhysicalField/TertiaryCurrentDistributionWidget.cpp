#include "TertiaryCurrentDistributionWidget.h"
#include "ui_TertiaryCurrentDistributionWidget.h"

TertiaryCurrentDistributionWidget::TertiaryCurrentDistributionWidget(QWidget *parent)
    : CurrentDistributionWidgetBase("TertiaryCurrentDistributionWidget", "tertiary", parent),
      ui(new Ui::TertiaryCurrentDistributionWidget)
{
    ui->setupUi(this);
    connectCommonUi();
    loadConfigFromSolver();
}

TertiaryCurrentDistributionWidget::~TertiaryCurrentDistributionWidget()
{
    delete ui;
}

QLineEdit *TertiaryCurrentDistributionWidget::meshPathLineEdit() const
{
    return ui->meshPathLineEdit;
}

QPushButton *TertiaryCurrentDistributionWidget::meshBrowseButton() const
{
    return ui->meshBrowsePushButton;
}

QLineEdit *TertiaryCurrentDistributionWidget::outputDirLineEdit() const
{
    return ui->outputDirLineEdit;
}

QPushButton *TertiaryCurrentDistributionWidget::outputBrowseButton() const
{
    return ui->outputBrowsePushButton;
}

QLineEdit *TertiaryCurrentDistributionWidget::electrolyteLineEdit() const
{
    return ui->electrolyteLineEdit;
}

QLineEdit *TertiaryCurrentDistributionWidget::electrodeLineEdit() const
{
    return ui->electrodeLineEdit;
}

QLineEdit *TertiaryCurrentDistributionWidget::insulationLineEdit() const
{
    return ui->insulationLineEdit;
}

QPushButton *TertiaryCurrentDistributionWidget::electrolyteButton() const
{
    return ui->electrolytePushButton;
}

QPushButton *TertiaryCurrentDistributionWidget::electrodeButton() const
{
    return ui->electrodeSurfacePushButton;
}

QPushButton *TertiaryCurrentDistributionWidget::insulationButton() const
{
    return ui->insulationPushButton;
}

QPushButton *TertiaryCurrentDistributionWidget::runButton() const
{
    return ui->runPushButton;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::conductivitySpin() const
{
    return ui->conductivityDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::anodePotentialSpin() const
{
    return ui->anodePotentialDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::cathodePotentialSpin() const
{
    return ui->cathodePotentialDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::exchangeCurrentSpin() const
{
    return ui->exchangeCurrentDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::anodicCoeffSpin() const
{
    return ui->anodicCoeffDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::cathodicCoeffSpin() const
{
    return ui->cathodicCoeffDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::diffusivitySpin() const
{
    return ui->diffusivityDoubleSpinBox;
}

QDoubleSpinBox *TertiaryCurrentDistributionWidget::concentrationSpin() const
{
    return ui->concentrationDoubleSpinBox;
}

