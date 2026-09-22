#include "ImportExportPanel.h"
#include "GeometryPanelUi.h"
#include "ui_GeometryImportWidget.h"
#include "ui_StepImportPartDialog.h"

#include <GeometryServiceAccess.h>
#include <ImportGeometryBuilder.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include "../workbench/GeometryWorkbench.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QStyle>

GeometryImportWidget::GeometryImportWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessImportGeometry),
      ui(new Ui::GeometryImportWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    connect(ui->browseButton, &QPushButton::clicked, this,
            &GeometryImportWidget::onBrowseButtonClicked);
    connect(ui->importButton, &QPushButton::clicked, this,
            &GeometryImportWidget::onImportButtonClicked);
    auto markOptionsPending = [this]
    {
        if (m_hasLoadedState)
            updateImportStatus(tr("导入设置已修改，点击“导入”后才会替换当前几何。"));
    };
    connect(ui->stepSplitModeComboBox, &QComboBox::currentIndexChanged,
            this, [markOptionsPending](int) { markOptionsPending(); });
    connect(ui->preserveColorCheckBox, &QCheckBox::toggled,
            this, [markOptionsPending](bool) { markOptionsPending(); });
    connect(ui->includeHiddenCheckBox, &QCheckBox::toggled,
            this, [markOptionsPending](bool) { markOptionsPending(); });
    connect(ui->groupAssemblyCheckBox, &QCheckBox::toggled,
            this, [markOptionsPending](bool) { markOptionsPending(); });
    GeometryPanelUi::apply(this);
    applyStepImportOptions(StepImportOptions{});
    updateStepOptionsVisibility();
}

GeometryImportWidget::~GeometryImportWidget()
{
    delete ui;
}

QJsonObject GeometryImportWidget::saveJson()
{
    // 工程文件只能记录已成功应用的导入参数，不能把浏览框中的候选路径与旧 BRep 混存。
    const StepImportOptions options = m_hasLoadedState
        ? m_lastAppliedStepOptions
        : StepImportOptions{};
    QJsonObject state;
    state[QStringLiteral("path")] = m_hasLoadedState ? m_lastImportedPath : QString();
    state[QStringLiteral("stepSplitMode")] = static_cast<int>(options.splitMode);
    state[QStringLiteral("stepPreserveColors")] = options.preserveColors;
    state[QStringLiteral("stepIncludeHidden")] = options.includeHidden;
    state[QStringLiteral("stepGroupByAssembly")] = options.groupByAssembly;
    return state;
}

void GeometryImportWidget::loadJson(const QJsonObject& state)
{
    StepImportOptions options;
    options.splitMode = state.contains(QStringLiteral("stepSplitMode"))
        ? static_cast<StepImportSplitMode>(
              state.value(QStringLiteral("stepSplitMode")).toInt(
                  static_cast<int>(StepImportSplitMode::Part)))
        : StepImportSplitMode::Body;
    options.preserveColors =
        state.value(QStringLiteral("stepPreserveColors")).toBool(true);
    options.includeHidden =
        state.value(QStringLiteral("stepIncludeHidden")).toBool(false);
    options.groupByAssembly =
        state.value(QStringLiteral("stepGroupByAssembly")).toBool(true);
    applyStepImportOptions(options);

    if (state.contains(QStringLiteral("path")))
        setFilePath(state.value(QStringLiteral("path")).toString());
    else
        updateStepOptionsVisibility();

    m_lastImportedPath = getFilePath();
    m_lastAppliedStepOptions = options;
    m_hasLoadedState = !m_lastImportedPath.trimmed().isEmpty();
}

void GeometryImportWidget::beforeLoad()
{
    if (!m_hasLoadedState || m_lastImportedPath.trimmed().isEmpty())
    {
        updateImportStatus(getFilePath().isEmpty()
                               ? QString()
                               : tr("已选择文件，点击“导入”后才会创建几何。"));
        return;
    }

    const StepImportOptions pendingOptions = currentStepImportOptions();
    const bool optionsChanged =
        pendingOptions.splitMode != m_lastAppliedStepOptions.splitMode ||
        pendingOptions.preserveColors != m_lastAppliedStepOptions.preserveColors ||
        pendingOptions.includeHidden != m_lastAppliedStepOptions.includeHidden ||
        pendingOptions.groupByAssembly != m_lastAppliedStepOptions.groupByAssembly;
    const QString pendingPath = getFilePath();
    const bool pathChanged = !pendingPath.isEmpty() &&
        QDir::cleanPath(pendingPath) != QDir::cleanPath(m_lastImportedPath);
    if (pathChanged || optionsChanged)
    {
        updateImportStatus(tr("文件或导入设置尚未应用；点击“导入”后才会替换当前几何。"));
        return;
    }

    const QFileInfo sourceInfo(m_lastImportedPath);
    if (sourceInfo.exists() && sourceInfo.isFile())
    {
        updateImportStatus(tr("已应用：%1").arg(QDir::toNativeSeparators(m_lastImportedPath)));
    }
    else
    {
        updateImportStatus(
            tr("找不到原始文件；当前仍显示工程中已保存的几何结果。重新选择文件并导入后才会替换。"),
            true);
    }
}

QString GeometryImportWidget::getFilePath() const
{
    return ui->pathLineEdit ? ui->pathLineEdit->text().trimmed() : QString();
}

void GeometryImportWidget::onImportButtonClicked()
{
    const QString filePath = getFilePath();
    if (filePath.isEmpty())
    {
        QMessageBox::information(this, tr("导入几何"), tr("请先选择几何文件。"));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const StepImportOptions newOptions = currentStepImportOptions();
    const bool hasExistingStepParts =
        !ImportGeometryBuilder::stepImportParts(m_uuid).isEmpty();
    const bool sourceChanged = m_hasLoadedState && !m_lastImportedPath.isEmpty() &&
                               QDir::cleanPath(filePath) != QDir::cleanPath(m_lastImportedPath);
    const bool structureChanged = m_hasLoadedState &&
        (newOptions.splitMode != m_lastAppliedStepOptions.splitMode ||
         newOptions.includeHidden != m_lastAppliedStepOptions.includeHidden);

    if (hasExistingStepParts && (sourceChanged || structureChanged))
    {
        const auto answer = QMessageBox::warning(
            this,
            tr("重新导入几何"),
            tr("文件或拆分方式发生了变化。重新导入会重建导入子对象，已有布尔、圆角、材料等引用可能需要重新选择。是否继续？"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }

    const QString previousImportedPath = m_lastImportedPath;
    const StepImportOptions previousOptions = m_lastAppliedStepOptions;
    const bool previouslyLoaded = m_hasLoadedState;

    // GeometryService 会在成功返回前同步提交几何历史，因此先暂存候选参数；
    // 若构建失败则完整回滚，保证历史、工程文件和当前 BRep 使用同一份已应用状态。
    m_lastImportedPath = filePath;
    m_lastAppliedStepOptions = newOptions;
    m_hasLoadedState = true;
    ImportGeometryBuilder::setStepImportOptions(m_uuid, newOptions);

    if (ui->importButton)
        ui->importButton->setEnabled(false);
    if (ui->browseButton)
        ui->browseButton->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QString errorMessage;
    const bool imported = service->recreateImportGeometry(
        m_uuid, getDisplayName(), filePath, errorMessage);

    QApplication::restoreOverrideCursor();
    if (ui->importButton)
        ui->importButton->setEnabled(true);
    if (ui->browseButton)
        ui->browseButton->setEnabled(true);

    if (!imported)
    {
        m_lastImportedPath = previousImportedPath;
        m_lastAppliedStepOptions = previousOptions;
        m_hasLoadedState = previouslyLoaded;
        if (previouslyLoaded)
            ImportGeometryBuilder::setStepImportOptions(m_uuid, previousOptions);
        else
            ImportGeometryBuilder::clearStepImportOptions(m_uuid);
        if (previouslyLoaded)
        {
            setFilePath(previousImportedPath);
            applyStepImportOptions(previousOptions);
        }
        updateImportStatus(errorMessage.isEmpty() ? tr("几何文件加载失败。") : errorMessage,
                           true);
        QMessageBox::warning(this,
                             tr("导入失败"),
                             errorMessage.isEmpty() ? tr("几何文件加载失败。") : errorMessage);
        return;
    }

    updateImportStatus(errorMessage.isEmpty() ? tr("几何导入完成。") : errorMessage);
    emit fileImported(filePath, m_uuid);
}

void GeometryImportWidget::onBrowseButtonClicked()
{
    const QString currentPath = getFilePath();
    const QString startDirectory = currentPath.isEmpty()
                                       ? QDir::homePath()
                                       : QFileInfo(currentPath).absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择几何文件"),
        startDirectory,
        tr("几何文件 (*.stp *.step *.igs *.iges *.brep *.dxf);;所有文件 (*)"));

    if (!filePath.isEmpty())
    {
        setFilePath(filePath);
        if (!m_hasLoadedState ||
            QDir::cleanPath(filePath) != QDir::cleanPath(m_lastImportedPath))
        {
            updateImportStatus(tr("已选择新文件，点击“导入”后才会替换当前几何。"));
        }
    }
}

void GeometryImportWidget::setFilePath(const QString& path)
{
    if (ui->pathLineEdit)
        ui->pathLineEdit->setText(QDir::toNativeSeparators(path));
    updateStepOptionsVisibility();
}

StepImportOptions GeometryImportWidget::currentStepImportOptions() const
{
    StepImportOptions options;
    const int index = ui && ui->stepSplitModeComboBox
        ? ui->stepSplitModeComboBox->currentIndex()
        : 0;
    switch (index)
    {
    case 1: options.splitMode = StepImportSplitMode::Body; break;
    case 2: options.splitMode = StepImportSplitMode::WholeModel; break;
    case 0:
    default: options.splitMode = StepImportSplitMode::Part; break;
    }
    options.preserveColors = !ui || !ui->preserveColorCheckBox ||
                             ui->preserveColorCheckBox->isChecked();
    options.includeHidden = ui && ui->includeHiddenCheckBox &&
                            ui->includeHiddenCheckBox->isChecked();
    options.groupByAssembly = !ui || !ui->groupAssemblyCheckBox ||
                              ui->groupAssemblyCheckBox->isChecked();
    return options;
}

void GeometryImportWidget::applyStepImportOptions(const StepImportOptions& options)
{
    if (!ui)
        return;

    int comboIndex = 0;
    if (options.splitMode == StepImportSplitMode::Body)
        comboIndex = 1;
    else if (options.splitMode == StepImportSplitMode::WholeModel)
        comboIndex = 2;

    if (ui->stepSplitModeComboBox)
        ui->stepSplitModeComboBox->setCurrentIndex(comboIndex);
    if (ui->preserveColorCheckBox)
        ui->preserveColorCheckBox->setChecked(options.preserveColors);
    if (ui->includeHiddenCheckBox)
        ui->includeHiddenCheckBox->setChecked(options.includeHidden);
    if (ui->groupAssemblyCheckBox)
        ui->groupAssemblyCheckBox->setChecked(options.groupByAssembly);
}

void GeometryImportWidget::updateStepOptionsVisibility()
{
    if (!ui || !ui->stepOptionsGroupBox)
        return;

    const QString suffix = QFileInfo(getFilePath()).suffix().toLower();
    const bool isStep = suffix == QStringLiteral("step") ||
                        suffix == QStringLiteral("stp");
    ui->stepOptionsGroupBox->setVisible(isStep);
}

void GeometryImportWidget::updateImportStatus(const QString& text, bool error)
{
    if (!ui || !ui->importStatusLabel)
        return;
    ui->importStatusLabel->setText(text);
    ui->importStatusLabel->setVisible(!text.isEmpty());
    ui->importStatusLabel->setProperty("error", error);
    ui->importStatusLabel->style()->unpolish(ui->importStatusLabel);
    ui->importStatusLabel->style()->polish(ui->importStatusLabel);
}


StepImportPartDialog::StepImportPartDialog(QWidget* parent)
    : BasePage(QStringLiteral("geom_step_import_part")),
      ui(new Ui::StepImportPartDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    GeometryPanelUi::apply(this);
    refreshText();
}

StepImportPartDialog::~StepImportPartDialog()
{
    delete ui;
}

void StepImportPartDialog::beforeLoad()
{
    refreshText();
}

QJsonObject StepImportPartDialog::saveJson()
{
    return {{QStringLiteral("importUuid"), m_importUuid},
            {QStringLiteral("partIndex"), m_partIndex},
            {QStringLiteral("sourceKey"), m_sourceKey},
            {QStringLiteral("originalName"), m_originalName},
            {QStringLiteral("assemblyPath"), m_assemblyPath},
            {QStringLiteral("colorHex"), m_colorHex}};
}

void StepImportPartDialog::loadJson(const QJsonObject& obj)
{
    m_importUuid = obj.value(QStringLiteral("importUuid")).toString();
    m_partIndex = obj.value(QStringLiteral("partIndex")).toInt(-1);
    m_sourceKey = obj.value(QStringLiteral("sourceKey")).toString();
    m_originalName = obj.value(QStringLiteral("originalName")).toString();
    m_assemblyPath = obj.value(QStringLiteral("assemblyPath")).toString();
    m_colorHex = obj.value(QStringLiteral("colorHex")).toString();
    refreshText();
}

void StepImportPartDialog::setStepPart(const QString& importUuid,
                                       int partIndex,
                                       const QString& sourceKey,
                                       const QString& originalName,
                                       const QString& assemblyPath,
                                       const QString& colorHex)
{
    m_importUuid = importUuid;
    m_partIndex = partIndex;
    m_sourceKey = sourceKey;
    m_originalName = originalName;
    m_assemblyPath = assemblyPath;
    m_colorHex = colorHex;
    refreshText();
}

void StepImportPartDialog::refreshText()
{
    if (!ui)
        return;
    if (ui->nameValueLabel)
        ui->nameValueLabel->setText(m_originalName.trimmed().isEmpty()
                                        ? tr("未命名 STEP 对象")
                                        : m_originalName);
    if (ui->pathValueLabel)
        ui->pathValueLabel->setText(m_assemblyPath.trimmed().isEmpty()
                                        ? tr("顶层对象")
                                        : m_assemblyPath);
    if (ui->colorValueLabel)
        ui->colorValueLabel->setText(m_colorHex.trimmed().isEmpty()
                                         ? tr("默认")
                                         : m_colorHex.toUpper());
    if (ui->partIndexValueLabel)
        ui->partIndexValueLabel->setText(m_partIndex < 0
                                             ? tr("-")
                                             : QString::number(m_partIndex + 1));
    if (ui->infoLabel)
    {
        QStringList technicalDetails;
        if (!m_sourceKey.trimmed().isEmpty())
            technicalDetails << tr("源键：%1").arg(m_sourceKey);
        if (!m_importUuid.trimmed().isEmpty())
            technicalDetails << tr("导入节点：%1").arg(m_importUuid);
        ui->infoLabel->setToolTip(technicalDetails.join(QLatin1Char('\n')));
    }
}
