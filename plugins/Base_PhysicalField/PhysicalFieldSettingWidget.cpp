#include "PhysicalFieldSettingWidget.h"

#include <ServiceRegistry.h>
#include <services/PhysicalFieldService.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>

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

void PhysicalFieldSettingWidget::on_importIIIDButton_clicked()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString defaultDir = projectDir.isEmpty()
                                   ? QDir::homePath()
                                   : SolverFilePaths::projectInputDirectory(projectDir);
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入已有约束"), defaultDir,
        QStringLiteral("约束数据 (*.DAT *.dat);;所有文件 (*)"));
    if (filePath.isEmpty())
        return;

    PhysicalFieldService* service = ServiceRegistry::instance().getService<PhysicalFieldService>();
    if (!service)
    {
        QMessageBox::warning(this, QStringLiteral("导入已有约束"),
                             QStringLiteral("约束与荷载服务不可用。"));
        return;
    }

    QString errorMessage;
    if (!service->importIIIDFile(filePath, &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("导入已有约束"), errorMessage);
        return;
    }

    QMessageBox::information(this, QStringLiteral("导入已有约束"),
                             QStringLiteral("约束数据导入完成，已自动创建位移约束。"));
}
