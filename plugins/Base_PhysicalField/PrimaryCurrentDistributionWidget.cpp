#include "PrimaryCurrentDistributionWidget.h"
#include "ui_PrimaryCurrentDistributionWidget.h"

PrimaryCurrentDistributionWidget::PrimaryCurrentDistributionWidget(QWidget *parent)
    : CurrentDistributionWidgetBase("PrimaryCurrentDistributionWidget", "primary", parent),
      ui(new Ui::PrimaryCurrentDistributionWidget)
{
    ui->setupUi(this);
    connectCommonUi();
    loadConfigFromSolver();
}

PrimaryCurrentDistributionWidget::~PrimaryCurrentDistributionWidget()
{
    delete ui;
}

QLineEdit *PrimaryCurrentDistributionWidget::outputDirLineEdit() const
{
    return ui->outputDirLineEdit;
}

QPushButton *PrimaryCurrentDistributionWidget::outputBrowseButton() const
{
    return ui->outputBrowsePushButton;
}

QLineEdit *PrimaryCurrentDistributionWidget::electrolyteLineEdit() const
{
    return ui->electrolyteLineEdit;
}

QLineEdit *PrimaryCurrentDistributionWidget::electrodeLineEdit() const
{
    return ui->electrodeLineEdit;
}

QLineEdit *PrimaryCurrentDistributionWidget::insulationLineEdit() const
{
    return ui->insulationLineEdit;
}

QPushButton *PrimaryCurrentDistributionWidget::electrolyteButton() const
{
    return ui->electrolytePushButton;
}

QPushButton *PrimaryCurrentDistributionWidget::electrodeButton() const
{
    return ui->electrodeSurfacePushButton;
}

QPushButton *PrimaryCurrentDistributionWidget::insulationButton() const
{
    return ui->insulationPushButton;
}

QPushButton *PrimaryCurrentDistributionWidget::runButton() const
{
    return ui->runPushButton;
}

QDoubleSpinBox *PrimaryCurrentDistributionWidget::conductivitySpin() const
{
    return ui->conductivityDoubleSpinBox;
}

QDoubleSpinBox *PrimaryCurrentDistributionWidget::anodePotentialSpin() const
{
    return ui->anodePotentialDoubleSpinBox;
}

QDoubleSpinBox *PrimaryCurrentDistributionWidget::cathodePotentialSpin() const
{
    return ui->cathodePotentialDoubleSpinBox;
}

