#include "ResultArrangeWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include "PostResultGenerator.h"
#include "ui_ResultArrangeWidget.h"

ResultArrangeWidget::ResultArrangeWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessResultArrange), ui(new Ui::ResultArrangeWidget)
{
    ui->setupUi(this);
    ui->typeComboBox->addItem(tr("自动识别"), 0);
    ui->typeComboBox->addItem(tr("均质坝/渗流"), 8);
    ui->typeComboBox->addItem(tr("边坡"), 7);
    ui->typeComboBox->addItem(tr("洞室"), 6);
    connect(ui->runButton, &QPushButton::clicked, this, &ResultArrangeWidget::launchPostTool);
}

ResultArrangeWidget::~ResultArrangeWidget()
{
    delete ui;
}

void ResultArrangeWidget::beforeLoad()
{
    if (!ui->dirLineEdit->text().trimmed().isEmpty())
        return;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    if (projectDir.trimmed().isEmpty())
        return;

    const QDir root(projectDir);
    const QStringList candidates = {
        QStringLiteral("SEEPCASE01"),     QStringLiteral("CASE01"),
        QStringLiteral("GEOSTR"),         QStringLiteral("CASE01EALL"),
        QStringLiteral("CASE01JALL"),     QStringLiteral("CASE01FILL01"),
        QStringLiteral("CASE01JACA/J03"),
    };

    for (const QString& candidate : candidates)
    {
        const QString resultDir = root.filePath(candidate);
        if (QFileInfo::exists(QDir(resultDir).filePath(QStringLiteral("IIIN.DAT"))))
        {
            ui->dirLineEdit->setText(QDir::toNativeSeparators(resultDir));
            return;
        }
    }

    ui->dirLineEdit->setText(QDir::toNativeSeparators(projectDir));
}

void ResultArrangeWidget::beforeUnload()
{
}

void ResultArrangeWidget::launchPostTool()
{
    const QString resultDir = QDir::toNativeSeparators(ui->dirLineEdit->text().trimmed());
    if (resultDir.isEmpty())
    {
        QMessageBox::warning(this, tr("成果整理"), tr("请先输入成果目录。"));
        return;
    }

    PostResultGenerator::Options options;
    options.projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    options.resultDir = resultDir;
    options.objectType = ui->typeComboBox->currentData().toInt();

    const PostResultGenerator::Result result = PostResultGenerator::generate(options);
    if (!result.ok)
    {
        QMessageBox::warning(this, tr("成果整理"), result.error);
        return;
    }

    QString message = tr("成果文件生成完成。");
    message += tr("\n\n生成文件：\n%1").arg(result.generatedFiles.join(QStringLiteral("\n")));
    if (!result.skippedFiles.isEmpty())
        message += tr("\n\n跳过：\n%1").arg(result.skippedFiles.join(QStringLiteral("\n")));
    if (!result.warnings.isEmpty())
        message += tr("\n\n警告：\n%1").arg(result.warnings.join(QStringLiteral("\n")));

    QMessageBox::information(this, tr("成果整理"), message);
}
