#include "PhysicalFieldSettingWidget.h"

#include <ServiceRegistry.h>
#include <services/PhysicalFieldService.h>

#include "ui_PhysicalFieldSettingWidget.h"

PhysicalFieldSettingWidget::PhysicalFieldSettingWidget(QWidget* parent)
    : ui(new Ui::PhysicalFieldSettingWidget), BasePage("PhysicalFieldSettingWidget")
{
    ui->setupUi(this);
}

PhysicalFieldSettingWidget::~PhysicalFieldSettingWidget()
{
    delete ui;
}

void PhysicalFieldSettingWidget::beforeLoad()
{
}

void PhysicalFieldSettingWidget::beforeUnload()
{
}

void PhysicalFieldSettingWidget::on_testPushButton_clicked()
{
    PhysicalFieldService* service = ServiceRegistry::instance().getService<PhysicalFieldService>();
    service->generateIIIDFile();
}

void PhysicalFieldSettingWidget::on_pushButton_clicked()
{
    PhysicalFieldService* service = ServiceRegistry::instance().getService<PhysicalFieldService>();
    service->generateIIWLFile();
}
