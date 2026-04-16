#include "CurveSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <services/VisualWidgetService.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>

#include "ui_CurveSettingsWidget.h"

CurveSettingsWidget::CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessCurve), ui(new Ui::CurveSettingsWidget),
      m_visualService(visualService)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    ui->xFieldComboBox->addItems(QStringList() << "ISTEP" << "LOOP");
    ui->fosCheckBox->setChecked(true);
    ui->ptotCheckBox->setChecked(true);
    ui->maxUrCheckBox->setChecked(true);
    ui->loopCheckBox->setChecked(true);

    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::onResultDirChanged);
    connect(ui->applyButton, &QPushButton::clicked, this, &CurveSettingsWidget::reloadAndShowCurve);
}

CurveSettingsWidget::~CurveSettingsWidget()
{
    delete ui;
}

void CurveSettingsWidget::beforeLoad()
{
    if (!m_visualService)
        return;

    m_visualService->setViewToolbarVisible(false);

    if (!m_initialized)
    {
        initResultDirectoryOptions();
        m_initialized = true;
    }
}

void CurveSettingsWidget::beforeUnload()
{
    if (!m_visualService)
        return;

    m_visualService->showVisualizationView();
    m_visualService->setViewToolbarVisible(true);
}

QJsonObject CurveSettingsWidget::saveJson()
{
    QJsonObject obj;
    obj["resultDir"] = ui->resultDirComboBox->currentText();
    obj["fileName"] = ui->fileComboBox->currentText();
    obj["xField"] = ui->xFieldComboBox->currentText();
    obj["showFos"] = ui->fosCheckBox->isChecked();
    obj["showPtot"] = ui->ptotCheckBox->isChecked();
    obj["showMaxUr"] = ui->maxUrCheckBox->isChecked();
    obj["showLoop"] = ui->loopCheckBox->isChecked();
    return obj;
}

void CurveSettingsWidget::loadJson(const QJsonObject& obj)
{
    if (!m_initialized)
    {
        initResultDirectoryOptions();
        m_initialized = true;
    }

    const QString resultDir = obj.value("resultDir").toString();
    const QString fileName = obj.value("fileName").toString();
    const QString xField = obj.value("xField").toString("ISTEP");

    if (!resultDir.isEmpty())
    {
        const int idx = ui->resultDirComboBox->findText(resultDir);
        if (idx >= 0)
            ui->resultDirComboBox->setCurrentIndex(idx);
    }

    refreshFilesForCurrentDir();

    if (!fileName.isEmpty())
    {
        const int fileIdx = ui->fileComboBox->findText(fileName);
        if (fileIdx >= 0)
            ui->fileComboBox->setCurrentIndex(fileIdx);
    }

    const int xIdx = ui->xFieldComboBox->findText(xField);
    if (xIdx >= 0)
        ui->xFieldComboBox->setCurrentIndex(xIdx);

    ui->fosCheckBox->setChecked(obj.value("showFos").toBool(true));
    ui->ptotCheckBox->setChecked(obj.value("showPtot").toBool(true));
    ui->maxUrCheckBox->setChecked(obj.value("showMaxUr").toBool(true));
    ui->loopCheckBox->setChecked(obj.value("showLoop").toBool(true));
}

void CurveSettingsWidget::reloadAndShowCurve()
{
    const QString traceFile = selectedTraceFilePath();
    if (traceFile.isEmpty())
    {
        QMessageBox::warning(this, "曲线", "未找到可用 TRACE 文件");
        return;
    }

    if (!m_visualService)
    {
        QMessageBox::warning(this, "曲线", "VisualWidgetService 不可用");
        return;
    }

    const QString xField =
        ui->xFieldComboBox->currentText().isEmpty() ? "ISTEP" : ui->xFieldComboBox->currentText();
    const QStringList yFields = selectedYFields();
    QString curveErr;
    if (!m_visualService->showCurveFromTraceFile(traceFile, xField, yFields, &curveErr))
    {
        QMessageBox::warning(this, "曲线",
                             curveErr.isEmpty() ? QString("曲线显示失败：%1").arg(traceFile)
                                                : curveErr);
    }
}

void CurveSettingsWidget::onResultDirChanged()
{
    refreshFilesForCurrentDir();
}

void CurveSettingsWidget::initResultDirectoryOptions()
{
    ui->resultDirComboBox->blockSignals(true);
    ui->resultDirComboBox->clear();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir root(projectDir.isEmpty() ? QDir::currentPath() : projectDir);

    const QFileInfoList subDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dirInfo : subDirs)
    {
        if (QFile::exists(QDir(dirInfo.absoluteFilePath()).filePath("TRACE.TXT")))
            ui->resultDirComboBox->addItem(dirInfo.fileName(), dirInfo.absoluteFilePath());
    }

    if (ui->resultDirComboBox->count() == 0)
    {
        const QString fallback = root.filePath("CASE01");
        ui->resultDirComboBox->addItem("CASE01", fallback);
    }

    ui->resultDirComboBox->blockSignals(false);
    ui->resultDirComboBox->setCurrentIndex(0);
    refreshFilesForCurrentDir();
}

void CurveSettingsWidget::refreshFilesForCurrentDir()
{
    ui->fileComboBox->blockSignals(true);
    ui->fileComboBox->clear();

    const QString dirPath = selectedResultDirectoryPath();
    QDir dir(dirPath);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << "*.TXT" << "*.txt", QDir::Files, QDir::Name);
    for (const QFileInfo& info : files)
        ui->fileComboBox->addItem(info.fileName(), info.absoluteFilePath());

    const int traceIdx = ui->fileComboBox->findText("TRACE.TXT", Qt::MatchFixedString);
    if (traceIdx >= 0)
        ui->fileComboBox->setCurrentIndex(traceIdx);
    else if (ui->fileComboBox->count() > 0)
        ui->fileComboBox->setCurrentIndex(0);

    ui->fileComboBox->blockSignals(false);
}

QString CurveSettingsWidget::selectedResultDirectoryPath() const
{
    return ui->resultDirComboBox->currentData().toString();
}

QString CurveSettingsWidget::selectedTraceFilePath() const
{
    return ui->fileComboBox->currentData().toString();
}

QStringList CurveSettingsWidget::selectedYFields() const
{
    QStringList ys;
    if (ui->fosCheckBox->isChecked())
        ys << "FOS";
    if (ui->ptotCheckBox->isChecked())
        ys << "PTOT";
    if (ui->maxUrCheckBox->isChecked())
        ys << "MAX UR";
    if (ui->loopCheckBox->isChecked())
        ys << "LOOP";
    return ys;
}
