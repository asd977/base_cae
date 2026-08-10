#include "PostDisplayControlWidget.h"

#include "PostVisualUtils.h"
#include "ResultDisplayNames.h"
#include "ui_PostDisplayControlWidget.h"

#include <services/VisualWidgetService.h>
#include <PublicConstants.h>

#include <vtkScalarBarActor.h>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>

PostDisplayControlWidget::PostDisplayControlWidget(VisualWidgetService* visualService,
                                                   QWidget* parent)
    : QWidget(parent), ui(new Ui::PostDisplayControlWidget), m_visualService(visualService)
{
    ui->setupUi(this);

    // 分区标题由 .ui 中的 QGroupBox 提供，尺寸和滚动由外层停靠窗口统一管理。
    setProperty("visualControlPanel", true);
    setAttribute(Qt::WA_StyledBackground, true);

    ui->rangeMinSpinBox->setRange(-1.0e100, 1.0e100);
    ui->rangeMaxSpinBox->setRange(-1.0e100, 1.0e100);
    ui->rangeMinSpinBox->setDecimals(8);
    ui->rangeMaxSpinBox->setDecimals(8);
    ui->rangeMinSpinBox->setKeyboardTracking(false);
    ui->rangeMaxSpinBox->setKeyboardTracking(false);

    auto applySceneVisibility = [this](bool checked)
    {
        if (m_syncing)
            return;
        postSceneStyle().visible = checked;
        if (m_visualService)
        {
            m_visualService->setGroupVisible(QStringLiteral("post"), checked, false);
            m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
            m_visualService->render();
        }
        notifyRefresh(QStringLiteral("visibility"));
    };
    connect(ui->showResultsCheckBox, &QCheckBox::toggled, this, applySceneVisibility);

    connect(ui->followMeshFilterCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_syncing)
            return;
        postSceneStyle().followMeshFilter = checked;
        notifyRefresh(QStringLiteral("mesh-filter"));
        syncRangeControls();
    });

    connect(ui->meshFilterButton, &QPushButton::clicked, this, []()
    {
        EventManager::instance().sendEvent(PublicConstants::kActionPreprocessMeshDisplayPanel);
    });

    connect(ui->showScalarBarCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_syncing)
            return;
        postScalarBarStyle().visible = checked;
        if (m_visualService)
        {
            m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
            m_visualService->render();
        }
    });

    connect(ui->showContourCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_syncing)
            return;
        postContourStyle().enabled = checked;
        notifyRefresh(QStringLiteral("contour"));
    });

    connect(ui->autoRangeCheckBox, &QCheckBox::toggled, this, [this](bool automatic)
    {
        if (m_syncing)
            return;
        const PostDisplayFieldContext& context = postDisplayFieldContext();
        if (!context.valid)
            return;
        PostFieldRangeStyle& range = postFieldRangeStyle(
            context.fieldName, context.dataMinimum, context.dataMaximum);
        range.automatic = automatic;
        range.initialized = true;
        if (automatic)
        {
            range.minimum = context.dataMinimum;
            range.maximum = context.dataMaximum;
        }
        syncRangeControls();
        notifyRefresh(QStringLiteral("range"));
    });

    auto commitRange = [this]()
    {
        if (m_syncing)
            return;
        const PostDisplayFieldContext& context = postDisplayFieldContext();
        if (!context.valid || ui->autoRangeCheckBox->isChecked())
            return;

        double minimum = ui->rangeMinSpinBox->value();
        double maximum = ui->rangeMaxSpinBox->value();
        if (!(maximum > minimum))
        {
            minimum = context.dataMinimum;
            maximum = context.dataMaximum;
        }

        PostFieldRangeStyle& range = postFieldRangeStyle(
            context.fieldName, context.dataMinimum, context.dataMaximum);
        range.automatic = false;
        range.initialized = true;
        range.minimum = minimum;
        range.maximum = maximum;
        syncRangeControls();
        notifyRefresh(QStringLiteral("range"));
    };
    connect(ui->rangeMinSpinBox, &QDoubleSpinBox::editingFinished, this, commitRange);
    connect(ui->rangeMaxSpinBox, &QDoubleSpinBox::editingFinished, this, commitRange);

    auto connectScalarSpin = [this](QSpinBox* spin, auto setter)
    {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, setter](int value)
                {
                    if (m_syncing)
                        return;
                    setter(value);
                    applyScalarBarAppearance();
                });
    };

    connectScalarSpin(ui->titleFontSpinBox, [](int value) { postScalarBarStyle().titleFont = value; });
    connectScalarSpin(ui->labelFontSpinBox, [](int value) { postScalarBarStyle().labelFont = value; });
    connectScalarSpin(ui->precisionSpinBox, [](int value) { postScalarBarStyle().precision = value; });
    connectScalarSpin(ui->tickCountSpinBox, [](int value) { postScalarBarStyle().tickCount = value; });
    connectScalarSpin(ui->barWidthSpinBox, [](int value) { postScalarBarStyle().widthPercent = value; });
    connectScalarSpin(ui->barHeightSpinBox, [](int value) { postScalarBarStyle().heightPercent = value; });

    connect(ui->resetBarPositionButton, &QPushButton::clicked, this, [this]()
    {
        postScalarBarStyle().hasCustomPosition = false;
        applyScalarBarAppearance();
    });

    connect(ui->contourLevelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value)
            {
                if (m_syncing)
                    return;
                postContourStyle().levels = value;
                notifyRefresh(QStringLiteral("contour"));
            });
    connect(ui->contourLabelsCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_syncing)
            return;
        postContourStyle().labels = checked;
        ui->contourLabelFontSpinBox->setEnabled(
            checked && postDisplayFieldContext().supportsContourLabels);
        notifyRefresh(QStringLiteral("contour"));
    });
    connect(ui->contourLabelFontSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value)
            {
                if (m_syncing)
                    return;
                postContourStyle().labelFont = value;
                notifyRefresh(QStringLiteral("contour"));
            });

    EventManager::instance().registerHandler(
        postDisplayContextChangedEvent(), this,
        [this](const QVariantMap&)
        {
            syncRangeControls();
            applyScalarBarAppearance();
        });

    syncFromState();
}

PostDisplayControlWidget::~PostDisplayControlWidget()
{
    delete ui;
}

void PostDisplayControlWidget::syncFromState()
{
    QScopedValueRollback<bool> guard(m_syncing, true);

    const PostScalarBarStyle& bar = postScalarBarStyle();
    const PostContourStyle& contour = postContourStyle();

    ui->showResultsCheckBox->setChecked(postSceneStyle().visible);
    ui->followMeshFilterCheckBox->setChecked(postSceneStyle().followMeshFilter);
    ui->showScalarBarCheckBox->setChecked(bar.visible);
    ui->showContourCheckBox->setChecked(contour.enabled);

    ui->titleFontSpinBox->setValue(bar.titleFont);
    ui->labelFontSpinBox->setValue(bar.labelFont);
    ui->precisionSpinBox->setValue(bar.precision);
    ui->tickCountSpinBox->setValue(bar.tickCount);
    ui->barWidthSpinBox->setValue(bar.widthPercent);
    ui->barHeightSpinBox->setValue(bar.heightPercent);

    ui->contourLevelSpinBox->setValue(contour.levels);
    ui->contourLabelsCheckBox->setChecked(contour.labels);
    ui->contourLabelFontSpinBox->setValue(contour.labelFont);

    syncRangeControls();
    if (m_visualService)
        m_visualService->setGroupVisible(QStringLiteral("post"), postSceneStyle().visible, false);
    applyScalarBarAppearance(false);
}

void PostDisplayControlWidget::syncRangeControls()
{
    QScopedValueRollback<bool> guard(m_syncing, true);

    const PostDisplayFieldContext& context = postDisplayFieldContext();
    ui->rangeGroupBox->setEnabled(context.valid);
    ui->scalarBarGroupBox->setEnabled(context.valid && context.supportsScalarBar);
    ui->showScalarBarCheckBox->setEnabled(context.valid && context.supportsScalarBar);
    ui->contourGroupBox->setEnabled(context.valid && context.supportsContours);
    ui->showContourCheckBox->setEnabled(context.valid && context.supportsContours);

    const PostMeshFilterReport& filterReport = postMeshFilterReport();
    const bool showFilterStatus = postSceneStyle().followMeshFilter &&
                                  filterReport.customFilter &&
                                  !filterReport.message.isEmpty();
    ui->meshFilterStatusLabel->setVisible(showFilterStatus);
    ui->meshFilterStatusLabel->setText(showFilterStatus ? filterReport.message : QString());

    if (!context.valid)
    {
        ui->currentFieldValueLabel->setText(QStringLiteral("未选择结果"));
        ui->rangeMinSpinBox->setValue(0.0);
        ui->rangeMaxSpinBox->setValue(1.0);
        ui->autoRangeCheckBox->setChecked(true);
        ui->rangeMinSpinBox->setEnabled(false);
        ui->rangeMaxSpinBox->setEnabled(false);
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }

    ui->currentFieldValueLabel->setText(ResultDisplayNames::displayName(context.fieldName));
    PostFieldRangeStyle& range = postFieldRangeStyle(
        context.fieldName, context.dataMinimum, context.dataMaximum);
    ui->autoRangeCheckBox->setChecked(range.automatic);
    ui->rangeMinSpinBox->setValue(range.initialized ? range.minimum : context.dataMinimum);
    ui->rangeMaxSpinBox->setValue(range.initialized ? range.maximum : context.dataMaximum);
    ui->rangeMinSpinBox->setEnabled(!range.automatic);
    ui->rangeMaxSpinBox->setEnabled(!range.automatic);

    const bool labelsAvailable = context.supportsContours && context.supportsContourLabels;
    ui->contourLabelsCheckBox->setEnabled(labelsAvailable);
    ui->contourLabelFontSpinBox->setEnabled(labelsAvailable && postContourStyle().labels);
}

void PostDisplayControlWidget::applyScalarBarAppearance(bool renderNow)
{
    if (!m_visualService)
        return;
    if (vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor())
        applyStandardPostScalarBar(scalarBar);
    m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    if (renderNow)
        m_visualService->render();
}

void PostDisplayControlWidget::notifyRefresh(const QString& reason)
{
    notifyPostDisplaySettingsChanged(reason);
}
