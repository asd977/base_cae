#include "CurveSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <services/VisualWidgetService.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSize>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyle>

#include <algorithm>

#include "PostResultAutoPrepare.h"
#include "ui_CurveSettingsWidget.h"

namespace
{
QIcon curveLegendIcon(const QColor& color)
{
    QPixmap pixmap(24, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 3.0, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(2.0, 6.0), QPointF(22.0, 6.0));
    return QIcon(pixmap);
}

void applyCurveLegend(QCheckBox* checkBox, const QColor& color)
{
    if (!checkBox)
        return;
    checkBox->setIcon(curveLegendIcon(color));
    checkBox->setIconSize(QSize(24, 12));
}
}

CurveSettingsWidget::CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessCurve), ui(new Ui::CurveSettingsWidget),
      m_visualService(visualService)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    ui->xFieldComboBox->addItem(QStringLiteral("计算步"), QStringLiteral("ISTEP"));
    ui->xFieldComboBox->addItem(QStringLiteral("迭代次数"), QStringLiteral("LOOP"));
    // 默认只显示最常用的两条曲线，避免不同量纲的 4 条曲线首次打开时挤在一起。
    ui->fosCheckBox->setChecked(true);
    ui->ptotCheckBox->setChecked(false);
    ui->maxUrCheckBox->setChecked(true);
    ui->loopCheckBox->setChecked(false);


    // 设置窗口始终显示与曲线一致的颜色说明；图表右上角图例仍由复选框单独控制。
    applyCurveLegend(ui->fosCheckBox, QColor(31, 119, 180));
    applyCurveLegend(ui->ptotCheckBox, QColor(230, 126, 34));
    applyCurveLegend(ui->maxUrCheckBox, QColor(39, 174, 96));
    applyCurveLegend(ui->loopCheckBox, QColor(192, 57, 43));

    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::onResultDirChanged);
    connect(ui->fileComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->xFieldComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]
            {
                updateLoopAvailability();
                reloadAndShowCurve();
            });
    connect(ui->fosCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->ptotCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->maxUrCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->loopCheckBox, &QCheckBox::toggled, this, &CurveSettingsWidget::reloadAndShowCurve);
    connect(ui->showLegendCheckBox, &QCheckBox::toggled, this,
            &CurveSettingsWidget::reloadAndShowCurve);

    updateLoopAvailability();
    setStatus(QStringLiteral("选择结果阶段后自动显示曲线。"));
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
    obj["showLegend"] = ui->showLegendCheckBox->isChecked();
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
    ui->ptotCheckBox->setChecked(obj.value("showPtot").toBool(false));
    ui->maxUrCheckBox->setChecked(obj.value("showMaxUr").toBool(true));
    ui->loopCheckBox->setChecked(obj.value("showLoop").toBool(false));
    ui->showLegendCheckBox->setChecked(obj.value("showLegend").toBool(false));
    updateLoopAvailability();
}

void CurveSettingsWidget::reloadAndShowCurve()
{
    if (m_loadingState)
        return;

    const QString traceFile = selectedTraceFilePath();
    if (traceFile.isEmpty())
    {
        setStatus(QStringLiteral("当前结果阶段没有可用的计算曲线数据。"), true);
        return;
    }

    if (!m_visualService)
    {
        setStatus(QStringLiteral("结果视图暂不可用，请重新打开结果页面。"), true);
        return;
    }

    const QString xField = ui->xFieldComboBox->currentData().toString().isEmpty()
                               ? QStringLiteral("ISTEP")
                               : ui->xFieldComboBox->currentData().toString();
    const QStringList yFields = selectedYFields();
    const bool hasCurveField = std::any_of(yFields.cbegin(), yFields.cend(), [](const QString& field)
    {
        return field != QStringLiteral("__SHOW_LEGEND__");
    });
    if (!hasCurveField)
    {
        setStatus(QStringLiteral("至少选择一条纵轴曲线。"), true);
        return;
    }

    QString curveErr;
    if (!m_visualService->showCurveFromTraceFile(traceFile, xField, yFields, &curveErr))
    {
        setStatus(curveErr.isEmpty() ? QStringLiteral("曲线显示失败。") : curveErr, true);
        return;
    }

    setStatus(QString());
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
    // 通过保留标记传递图例状态，不改变 VisualWidgetService 的公共接口。
    if (ui->showLegendCheckBox->isChecked())
        ys << "__SHOW_LEGEND__";
    return ys;
}


void CurveSettingsWidget::updateLoopAvailability()
{
    const bool loopIsXAxis =
        ui->xFieldComboBox->currentData().toString() == QStringLiteral("LOOP");

    if (loopIsXAxis && ui->loopCheckBox->isChecked())
    {
        const QSignalBlocker blocker(ui->loopCheckBox);
        ui->loopCheckBox->setChecked(false);
    }

    ui->loopCheckBox->setEnabled(!loopIsXAxis);
    ui->loopCheckBox->setToolTip(loopIsXAxis
                                     ? QStringLiteral("横轴已选择迭代次数，不能同时作为纵轴")
                                     : QString());
}

void CurveSettingsWidget::setStatus(const QString& text, bool error)
{
    if (!ui->statusLabel)
        return;

    ui->statusLabel->setVisible(error && !text.trimmed().isEmpty());
    ui->statusLabel->setText(error ? text : QString());
    ui->statusLabel->setProperty("error", error);
    ui->statusLabel->style()->unpolish(ui->statusLabel);
    ui->statusLabel->style()->polish(ui->statusLabel);
}
