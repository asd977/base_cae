#include "SeepageWidget.h"
#include "ui_SeepageWidget.h"

SeepageWidget::SeepageWidget(QWidget *parent) :
    BasePage("SeepageAnalysis"),
    ui(new Ui::SeepageWidget)
{
    ui->setupUi(this);
}

SeepageWidget::~SeepageWidget()
{
    delete ui;
}

void SeepageWidget::beforeLoad() {}

void SeepageWidget::beforeUnload() {}

double SeepageWidget::upstreamWaterLevel() const
{
    return ui->spinUpstreamWaterLevel->value();
}

double SeepageWidget::downstreamWaterLevel() const
{
    return ui->spinDownstreamWaterLevel->value();
}

double SeepageWidget::upstreamFlux() const
{
    return ui->spinUpstreamFlux->value();
}

double SeepageWidget::downstreamFlux() const
{
    return ui->spinDownstreamFlux->value();
}
