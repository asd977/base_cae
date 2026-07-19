#include "CaveExcavationWidget.h"

#include <QJsonObject>
#include <QPushButton>

#include "ui_CaveExcavationWidget.h"
#include "SolverUiLayoutHelper.h"

CaveExcavationWidget::CaveExcavationWidget(QWidget* parent)
    : BasePage("CaveExcavationAnalysis"), ui(new Ui::CaveExcavationWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    connect(ui->geoStressButton, &QPushButton::clicked, this,
            &CaveExcavationWidget::openGeoStressRequested);
    connect(ui->caveSupportButton, &QPushButton::clicked, this,
            &CaveExcavationWidget::openCaveSupportRequested);
}

CaveExcavationWidget::~CaveExcavationWidget()
{
    delete ui;
}

void CaveExcavationWidget::beforeLoad() {}
void CaveExcavationWidget::beforeUnload() {}

QJsonObject CaveExcavationWidget::saveJson()
{
    return {};
}

void CaveExcavationWidget::loadJson(const QJsonObject& obj)
{
    Q_UNUSED(obj);
}
