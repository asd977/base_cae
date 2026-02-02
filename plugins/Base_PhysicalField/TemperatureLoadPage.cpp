#include "TemperatureLoadPage.h"
#include "ui_TemperatureLoadPage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include "PickManager.h"

TemperatureLoadPage::TemperatureLoadPage(QWidget* parent)
    : BasePage("TemperatureLoadPage")
{
    ui = new Ui::TemperatureLoadPage;
    ui->setupUi(this);

}

TemperatureLoadPage::~TemperatureLoadPage() = default;

void TemperatureLoadPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void TemperatureLoadPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

double TemperatureLoadPage::alfaValue() const
{
    return ui->lineEditAlfa->text().toDouble();
}

double TemperatureLoadPage::dtValue() const
{
    return ui->lineEditDT->text().toDouble();
}
