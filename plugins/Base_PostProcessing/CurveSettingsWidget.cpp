#include "CurveSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <services/VisualWidgetService.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CurveSettingsWidget::CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessCurve), m_visualService(visualService)
{
    if (parent)
        setParent(parent);

    auto* mainLayout = new QVBoxLayout(this);
    auto* row1 = new QHBoxLayout();
    auto* row2 = new QHBoxLayout();

    m_resultDirCombo = new QComboBox(this);
    m_fileCombo = new QComboBox(this);
    m_xFieldCombo = new QComboBox(this);
    m_xFieldCombo->addItems(QStringList() << "ISTEP" << "LOOP");

    m_fosCheck = new QCheckBox("FOS", this);
    m_ptotCheck = new QCheckBox("PTOT", this);
    m_maxUrCheck = new QCheckBox("MAX UR", this);
    m_loopCheck = new QCheckBox("LOOP", this);

    m_fosCheck->setChecked(true);
    m_ptotCheck->setChecked(true);
    m_maxUrCheck->setChecked(true);
    m_loopCheck->setChecked(true);

    row1->addWidget(new QLabel("结果目录", this));
    row1->addWidget(m_resultDirCombo, 1);
    row1->addWidget(new QLabel("结果文件", this));
    row1->addWidget(m_fileCombo, 1);

    row2->addWidget(new QLabel("X轴字段", this));
    row2->addWidget(m_xFieldCombo);
    row2->addWidget(new QLabel("Y轴字段", this));
    row2->addWidget(m_fosCheck);
    row2->addWidget(m_ptotCheck);
    row2->addWidget(m_maxUrCheck);
    row2->addWidget(m_loopCheck);
    row2->addStretch(1);

    auto* actionRow = new QHBoxLayout();
    m_reloadBtn = new QPushButton("加载并显示曲线", this);
    m_backTo3dBtn = new QPushButton("返回三维可视化", this);
    m_statusLabel = new QLabel("请选择目录和TRACE文件", this);
    m_statusLabel->setWordWrap(true);

    actionRow->addWidget(m_reloadBtn);
    actionRow->addWidget(m_backTo3dBtn);
    actionRow->addWidget(m_statusLabel, 1);

    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText("TRACE 文件打印输出");

    mainLayout->addLayout(row1);
    mainLayout->addLayout(row2);
    mainLayout->addLayout(actionRow);
    mainLayout->addWidget(m_logEdit, 1);

    connect(m_resultDirCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::onResultDirChanged);
    connect(m_reloadBtn, &QPushButton::clicked, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(m_backTo3dBtn, &QPushButton::clicked, this, &CurveSettingsWidget::switchBackTo3d);
}

void CurveSettingsWidget::beforeLoad()
{
    if (!m_initialized)
    {
        initResultDirectoryOptions();
        m_initialized = true;
    }
    reloadAndShowCurve();
}

void CurveSettingsWidget::beforeUnload()
{
}

QJsonObject CurveSettingsWidget::saveJson()
{
    QJsonObject obj;
    obj["resultDir"] = m_resultDirCombo->currentText();
    obj["fileName"] = m_fileCombo->currentText();
    obj["xField"] = m_xFieldCombo->currentText();
    obj["showFos"] = m_fosCheck->isChecked();
    obj["showPtot"] = m_ptotCheck->isChecked();
    obj["showMaxUr"] = m_maxUrCheck->isChecked();
    obj["showLoop"] = m_loopCheck->isChecked();
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
        const int idx = m_resultDirCombo->findText(resultDir);
        if (idx >= 0)
            m_resultDirCombo->setCurrentIndex(idx);
    }

    refreshFilesForCurrentDir();

    if (!fileName.isEmpty())
    {
        const int fileIdx = m_fileCombo->findText(fileName);
        if (fileIdx >= 0)
            m_fileCombo->setCurrentIndex(fileIdx);
    }

    const int xIdx = m_xFieldCombo->findText(xField);
    if (xIdx >= 0)
        m_xFieldCombo->setCurrentIndex(xIdx);

    m_fosCheck->setChecked(obj.value("showFos").toBool(true));
    m_ptotCheck->setChecked(obj.value("showPtot").toBool(true));
    m_maxUrCheck->setChecked(obj.value("showMaxUr").toBool(true));
    m_loopCheck->setChecked(obj.value("showLoop").toBool(true));

    reloadAndShowCurve();
}

void CurveSettingsWidget::reloadAndShowCurve()
{
    const QString traceFile = selectedTraceFilePath();
    if (traceFile.isEmpty())
    {
        m_statusLabel->setText("未找到可用 TRACE 文件");
        m_logEdit->clear();
        return;
    }

    QString printErr;
    if (!printTraceFile(traceFile, &printErr))
    {
        m_statusLabel->setText(printErr);
        return;
    }

    if (!m_visualService)
    {
        m_statusLabel->setText("VisualWidgetService 不可用");
        return;
    }

    const QString xField = m_xFieldCombo->currentText().isEmpty() ? "ISTEP" : m_xFieldCombo->currentText();
    const QStringList yFields = selectedYFields();
    QString curveErr;
    if (!m_visualService->showCurveFromTraceFile(traceFile, xField, yFields, &curveErr))
    {
        m_statusLabel->setText(curveErr.isEmpty() ? QString("曲线显示失败：%1").arg(traceFile)
                                                   : curveErr);
        return;
    }

    m_statusLabel->setText(QString("已加载：%1 | X=%2 | Y=%3")
                               .arg(traceFile, xField, yFields.join(",")));
}

void CurveSettingsWidget::switchBackTo3d()
{
    if (m_visualService)
        m_visualService->showVisualizationView();
}

void CurveSettingsWidget::onResultDirChanged()
{
    refreshFilesForCurrentDir();
}

void CurveSettingsWidget::initResultDirectoryOptions()
{
    m_resultDirCombo->blockSignals(true);
    m_resultDirCombo->clear();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir root(projectDir.isEmpty() ? QDir::currentPath() : projectDir);

    const QFileInfoList subDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dirInfo : subDirs)
    {
        if (QFile::exists(QDir(dirInfo.absoluteFilePath()).filePath("TRACE.TXT")))
            m_resultDirCombo->addItem(dirInfo.fileName(), dirInfo.absoluteFilePath());
    }

    if (m_resultDirCombo->count() == 0)
    {
        const QString fallback = root.filePath("CASE01");
        m_resultDirCombo->addItem("CASE01", fallback);
    }

    m_resultDirCombo->blockSignals(false);
    m_resultDirCombo->setCurrentIndex(0);
    refreshFilesForCurrentDir();
}

void CurveSettingsWidget::refreshFilesForCurrentDir()
{
    m_fileCombo->blockSignals(true);
    m_fileCombo->clear();

    const QString dirPath = selectedResultDirectoryPath();
    QDir dir(dirPath);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << "*.TXT" << "*.txt", QDir::Files, QDir::Name);
    for (const QFileInfo& info : files)
        m_fileCombo->addItem(info.fileName(), info.absoluteFilePath());

    const int traceIdx = m_fileCombo->findText("TRACE.TXT", Qt::MatchFixedString);
    if (traceIdx >= 0)
        m_fileCombo->setCurrentIndex(traceIdx);
    else if (m_fileCombo->count() > 0)
        m_fileCombo->setCurrentIndex(0);

    m_fileCombo->blockSignals(false);
}

QString CurveSettingsWidget::selectedResultDirectoryPath() const
{
    return m_resultDirCombo->currentData().toString();
}

QString CurveSettingsWidget::selectedTraceFilePath() const
{
    return m_fileCombo->currentData().toString();
}

bool CurveSettingsWidget::printTraceFile(const QString& filePath, QString* errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString msg = QString("TRACE 文件打开失败：%1").arg(filePath);
        if (errMsg)
            *errMsg = msg;
        return false;
    }

    m_logEdit->setPlainText(QString::fromLocal8Bit(file.readAll()));
    if (errMsg)
        errMsg->clear();
    return true;
}

QStringList CurveSettingsWidget::selectedYFields() const
{
    QStringList ys;
    if (m_fosCheck->isChecked())
        ys << "FOS";
    if (m_ptotCheck->isChecked())
        ys << "PTOT";
    if (m_maxUrCheck->isChecked())
        ys << "MAX UR";
    if (m_loopCheck->isChecked())
        ys << "LOOP";
    return ys;
}
