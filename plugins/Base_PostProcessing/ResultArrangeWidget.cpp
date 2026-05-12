#include "ResultArrangeWidget.h"

#include <PublicConstants.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>

#include "ui_ResultArrangeWidget.h"

ResultArrangeWidget::ResultArrangeWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessResultArrange), ui(new Ui::ResultArrangeWidget)
{
    ui->setupUi(this);
    connect(ui->runButton, &QPushButton::clicked, this, &ResultArrangeWidget::launchPostTool);
}

ResultArrangeWidget::~ResultArrangeWidget()
{
    delete ui;
}

void ResultArrangeWidget::launchPostTool()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString executable =
        QDir::toNativeSeparators(QDir(appDir).filePath("post/A0_FEMWINPOST/Debug/A0_FEMWINPOST"));

    if (!QFileInfo::exists(executable))
    {
        QMessageBox::warning(this, tr("成果整理"),
                             tr("未找到可执行文件：\n%1").arg(executable));
        return;
    }

    if (!QProcess::startDetached(executable))
    {
        QMessageBox::warning(this, tr("成果整理"),
                             tr("启动失败：\n%1").arg(executable));
    }
}
