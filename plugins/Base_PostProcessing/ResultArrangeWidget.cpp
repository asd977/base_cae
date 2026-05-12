#include "ResultArrangeWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QTextStream>

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

void ResultArrangeWidget::beforeLoad()
{
}

void ResultArrangeWidget::beforeUnload()
{
}

void ResultArrangeWidget::launchPostTool()
{
    QString resultDir = QDir::toNativeSeparators(ui->dirLineEdit->text().trimmed());
    if (resultDir.isEmpty())
    {
        QMessageBox::warning(this, tr("成果整理"), tr("请先输入成果目录。"));
        return;
    }
    if (!resultDir.endsWith("\\"))
        resultDir += "\\";

    QFile datFile("D:/IIIDIR.DAT");
    if (!datFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("成果整理"), tr("无法写入 D:/IIIDIR.DAT"));
        return;
    }
    QTextStream out(&datFile);
    QString prepDir = QDir::toNativeSeparators(
        QDir(PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString())
            .filePath("PREP"));
    if (!prepDir.endsWith("\\"))
        prepDir += "\\";

    out << "'" << resultDir << "'\n";
    out << "'1'\n";
    out << "'9.81 0.0 1'\n";
    out << "'D:\\NASGEWIN64\\'\n";
    out << "        0  IDEVICE\n";
    out << "           0  ILOG_GZKZR\n";
    out << "       0  ILOG_SOLVE\n";
    out << "'" << prepDir << "'\n";
    out << "           8\n";
    datFile.close();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString executable =
        QDir::toNativeSeparators(QDir(appDir).filePath("post/A0_FEMWINPOST/Debug/A0_FEMWINPOST.exe"));

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
