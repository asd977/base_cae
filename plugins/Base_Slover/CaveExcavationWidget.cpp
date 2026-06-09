#include "CaveExcavationWidget.h"

#include <QJsonObject>

#include "ui_CaveExcavationWidget.h"

CaveExcavationWidget::CaveExcavationWidget(QWidget* parent)
    : BasePage("CaveExcavationAnalysis"), ui(new Ui::CaveExcavationWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("QWidget#CaveExcavationWidget { background: white; }");
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
