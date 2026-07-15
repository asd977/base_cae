#include "CurveSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <services/VisualWidgetService.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>

#include "PostResultAutoPrepare.h"
#include "ui_CurveSettingsWidget.h"

CurveSettingsWidget::CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessCurve), ui(new Ui::CurveSettingsWidget),
      m_visualService(visualService)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    ui->xFieldComboBox->addItem(QStringLiteral("计算步"), QStringLiteral("ISTEP"));
    ui->xFieldComboBox->addItem(QStringLiteral("迭代次数"), QStringLiteral("LOOP"));
    ui->fosCheckBox->setChecked(true);
    ui->ptotCheckBox->setChecked(true);
    ui->maxUrCheckBox->setChecked(true);
    ui->loopCheckBox->setChecked(true);

    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::onResultDirChanged);
    connect(ui->fileComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->xFieldComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->fosCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->ptotCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->maxUrCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->loopCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
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

    initResultDirectoryOptions();
    m_initialized = true;

    reloadAndShowCurve();
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
    obj["resultDir"] = ui->resultDirComboBox->currentData().toString();
    obj["fileName"] = ui->fileComboBox->currentData().toString();
    obj["xField"] = ui->xFieldComboBox->currentData().toString();
    obj["showFos"] = ui->fosCheckBox->isChecked();
    obj["showPtot"] = ui->ptotCheckBox->isChecked();
    obj["showMaxUr"] = ui->maxUrCheckBox->isChecked();
    obj["showLoop"] = ui->loopCheckBox->isChecked();
    return obj;
}

void CurveSettingsWidget::loadJson(const QJsonObject& obj)
{
    QScopedValueRollback<bool> loadingGuard(m_loadingState, true);

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
        int idx = ui->resultDirComboBox->findData(resultDir);
        if (idx < 0)
            idx = ui->resultDirComboBox->findText(resultDir);
        if (idx >= 0)
            ui->resultDirComboBox->setCurrentIndex(idx);
    }

    refreshFilesForCurrentDir();

    if (!fileName.isEmpty())
    {
        int fileIdx = ui->fileComboBox->findData(fileName);
        if (fileIdx < 0)
            fileIdx = ui->fileComboBox->findText(fileName);
        if (fileIdx >= 0)
            ui->fileComboBox->setCurrentIndex(fileIdx);
    }

    int xIdx = ui->xFieldComboBox->findData(xField);
    if (xIdx < 0)
        xIdx = ui->xFieldComboBox->findText(xField);
    if (xIdx >= 0)
        ui->xFieldComboBox->setCurrentIndex(xIdx);

    ui->fosCheckBox->setChecked(obj.value("showFos").toBool(true));
    ui->ptotCheckBox->setChecked(obj.value("showPtot").toBool(true));
    ui->maxUrCheckBox->setChecked(obj.value("showMaxUr").toBool(true));
    ui->loopCheckBox->setChecked(obj.value("showLoop").toBool(true));
}

void CurveSettingsWidget::reloadAndShowCurve()
{
    if (m_loadingState)
        return;

    const QString traceFile = selectedTraceFilePath();
    if (traceFile.isEmpty())
    {
        return;
    }

    if (!m_visualService)
    {
        return;
    }

    const QString xField = ui->xFieldComboBox->currentData().toString().isEmpty()
                               ? QStringLiteral("ISTEP")
                               : ui->xFieldComboBox->currentData().toString();
    const QStringList yFields = selectedYFields();
    QString curveErr;
    if (!m_visualService->showCurveFromTraceFile(traceFile, xField, yFields, &curveErr))
    {
        Q_UNUSED(curveErr);
    }
}

void CurveSettingsWidget::onResultDirChanged()
{
    refreshFilesForCurrentDir();
    reloadAndShowCurve();
}

void CurveSettingsWidget::initResultDirectoryOptions()
{
    const QString previousPath = ui->resultDirComboBox->currentData().toString();
    ui->resultDirComboBox->blockSignals(true);
    ui->resultDirComboBox->clear();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QDir root(projectDir.isEmpty() ? QDir::currentPath() : projectDir);
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(root.absolutePath());
    for (const PostResultAutoPrepare::ResultGroup& group : groups)
    {
        const QFileInfo trace(QDir(group.sourcePath).filePath(QStringLiteral("TRACE.TXT")));
        if (!trace.exists() || !trace.isFile() || trace.size() == 0)
            continue;
        ui->resultDirComboBox->addItem(group.displayName, group.sourcePath);
        ui->resultDirComboBox->setItemData(ui->resultDirComboBox->count() - 1,
                                           QStringLiteral("求解过程记录"), Qt::ToolTipRole);
    }

    if (ui->resultDirComboBox->count() == 0)
    {
        const QFileInfo trace(root.filePath(QStringLiteral("TRACE.TXT")));
        if (trace.exists() && trace.isFile() && trace.size() > 0)
            ui->resultDirComboBox->addItem(QStringLiteral("计算过程结果"),
                                           root.absolutePath());
    }

    int index = previousPath.isEmpty() ? -1 : ui->resultDirComboBox->findData(previousPath);
    if (index < 0 && ui->resultDirComboBox->count() > 0)
        index = 0;
    ui->resultDirComboBox->setCurrentIndex(index);
    ui->resultDirComboBox->blockSignals(false);
    refreshFilesForCurrentDir();
}

void CurveSettingsWidget::refreshFilesForCurrentDir()
{
    ui->fileComboBox->blockSignals(true);
    ui->fileComboBox->clear();

    const QString dirPath = selectedResultDirectoryPath();
    QFileInfo trace(QDir(dirPath).filePath(QStringLiteral("TRACE.TXT")));
    if (!trace.exists())
        trace = QFileInfo(QDir(dirPath).filePath(QStringLiteral("trace.txt")));
    if (trace.exists() && trace.isFile() && trace.size() > 0)
        ui->fileComboBox->addItem(QStringLiteral("计算过程曲线"), trace.absoluteFilePath());

    if (ui->fileComboBox->count() > 0)
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
