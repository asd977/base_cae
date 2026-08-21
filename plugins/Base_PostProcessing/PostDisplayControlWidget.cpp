#include "PostDisplayControlWidget.h"

#include "PostVisualUtils.h"
#include "ResultDisplayNames.h"
#include "ui_PostDisplayControlWidget.h"

#include <services/VisualWidgetService.h>
#include <PublicConstants.h>

#include <vtkScalarBarActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>

#include <algorithm>

PostDisplayControlWidget::PostDisplayControlWidget(VisualWidgetService* visualService,
                                                   QWidget* parent)
    : QWidget(parent), ui(new Ui::PostDisplayControlWidget), m_visualService(visualService)
{
    ui->setupUi(this);

    ui->displayModeComboBox->addItem(QStringLiteral("表面"), 0);
    ui->displayModeComboBox->addItem(QStringLiteral("表面 + 单元边线"), 1);
    ui->displayModeComboBox->addItem(QStringLiteral("线框"), 2);

    m_scalarBarDragCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_scalarBarDragCallback->SetClientData(this);
    m_scalarBarDragCallback->SetCallback(&PostDisplayControlWidget::scalarBarDragCallback);
    installScalarBarDragInteraction();

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

    connect(ui->displayModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index)
            {
                if (m_syncing || index < 0)
                    return;
                postSceneStyle().representationMode =
                    ui->displayModeComboBox->itemData(index).toInt();
                applyPostSceneRepresentation(m_visualService, false);
                notifyRefresh(QStringLiteral("representation"));
                if (m_visualService)
                    m_visualService->render();
            });

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

    connect(ui->scalarBarTitleEdit, &QLineEdit::editingFinished, this, [this]()
    {
        if (m_syncing)
            return;
        postScalarBarStyle().customTitle = ui->scalarBarTitleEdit->text().trimmed();
        applyScalarBarAppearance();
    });

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
    removeScalarBarDragInteraction();
    delete ui;
}

void PostDisplayControlWidget::syncFromState()
{
    QScopedValueRollback<bool> guard(m_syncing, true);

    const PostScalarBarStyle& bar = postScalarBarStyle();
    const PostContourStyle& contour = postContourStyle();

    ui->showResultsCheckBox->setChecked(postSceneStyle().visible);
    const int displayModeIndex = ui->displayModeComboBox->findData(postSceneStyle().representationMode);
    ui->displayModeComboBox->setCurrentIndex(displayModeIndex >= 0 ? displayModeIndex : 0);
    ui->followMeshFilterCheckBox->setChecked(postSceneStyle().followMeshFilter);
    ui->showScalarBarCheckBox->setChecked(bar.visible);
    ui->scalarBarTitleEdit->setText(bar.customTitle);
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
    {
        m_visualService->setGroupVisible(QStringLiteral("post"), postSceneStyle().visible, false);
        applyPostSceneRepresentation(m_visualService, false);
    }
    installScalarBarDragInteraction();
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
    {
        applyStandardPostScalarBar(scalarBar);
        const PostDisplayFieldContext& context = postDisplayFieldContext();
        const QString automaticTitle = context.valid
                                           ? ResultDisplayNames::scalarBarName(context.fieldName)
                                           : QString();
        const QString title = resolvedPostScalarBarTitle(automaticTitle);
        scalarBar->SetTitle(title.toUtf8().constData());
        scalarBar->Modified();
    }
    m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    if (renderNow)
        m_visualService->render();
}

void PostDisplayControlWidget::notifyRefresh(const QString& reason)
{
    notifyPostDisplaySettingsChanged(reason);
}


void PostDisplayControlWidget::installScalarBarDragInteraction()
{
    if (!m_visualService || !m_scalarBarDragCallback || m_middlePressObserverId != 0)
        return;
    vtkRenderWindowInteractor* interactor = m_visualService->interactor();
    if (!interactor)
        return;

    constexpr float priority = 1.0f;
    m_middlePressObserverId = interactor->AddObserver(
        vtkCommand::MiddleButtonPressEvent, m_scalarBarDragCallback, priority);
    m_mouseMoveObserverId = interactor->AddObserver(
        vtkCommand::MouseMoveEvent, m_scalarBarDragCallback, priority);
    m_middleReleaseObserverId = interactor->AddObserver(
        vtkCommand::MiddleButtonReleaseEvent, m_scalarBarDragCallback, priority);
}

void PostDisplayControlWidget::removeScalarBarDragInteraction()
{
    vtkRenderWindowInteractor* interactor = m_visualService ? m_visualService->interactor() : nullptr;
    if (interactor)
    {
        if (m_middlePressObserverId != 0)
            interactor->RemoveObserver(m_middlePressObserverId);
        if (m_mouseMoveObserverId != 0)
            interactor->RemoveObserver(m_mouseMoveObserverId);
        if (m_middleReleaseObserverId != 0)
            interactor->RemoveObserver(m_middleReleaseObserverId);
    }
    m_middlePressObserverId = 0;
    m_mouseMoveObserverId = 0;
    m_middleReleaseObserverId = 0;
    m_draggingScalarBar = false;
}

void PostDisplayControlWidget::scalarBarDragCallback(vtkObject*, unsigned long eventId,
                                                       void* clientData, void*)
{
    auto* self = static_cast<PostDisplayControlWidget*>(clientData);
    if (!self || !self->m_scalarBarDragCallback)
        return;
    self->m_scalarBarDragCallback->AbortFlagOff();
    if (self->handleScalarBarDragEvent(eventId))
        self->m_scalarBarDragCallback->AbortFlagOn();
}

bool PostDisplayControlWidget::handleScalarBarDragEvent(unsigned long eventId)
{
    if (!m_visualService || !isPostViewActive())
        return false;

    vtkRenderWindowInteractor* interactor = m_visualService->interactor();
    vtkRenderWindow* renderWindow = m_visualService->renderWindow();
    vtkRenderer* renderer = m_visualService->renderer();
    vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor();
    if (!interactor || !renderWindow || !renderer || !scalarBar ||
        scalarBar->GetVisibility() == 0)
    {
        m_draggingScalarBar = false;
        return false;
    }

    int* eventPosition = interactor->GetEventPosition();
    int* windowSize = renderWindow->GetSize();
    double viewport[4];
    renderer->GetViewport(viewport);
    const double viewportWidth = (viewport[2] - viewport[0]) * windowSize[0];
    const double viewportHeight = (viewport[3] - viewport[1]) * windowSize[1];
    if (viewportWidth <= 0.0 || viewportHeight <= 0.0)
        return false;

    const double localX = (eventPosition[0] - viewport[0] * windowSize[0]) / viewportWidth;
    const double localY = (eventPosition[1] - viewport[1] * windowSize[1]) / viewportHeight;
    const double* position = scalarBar->GetPosition();
    const double width = scalarBar->GetWidth();
    const double height = scalarBar->GetHeight();

    if (eventId == vtkCommand::MiddleButtonPressEvent)
    {
        constexpr double tolerance = 0.02;
        const bool hit = localX >= position[0] - tolerance &&
                         localX <= position[0] + width + tolerance &&
                         localY >= position[1] - tolerance &&
                         localY <= position[1] + height + tolerance;
        if (!hit)
            return false;
        m_draggingScalarBar = true;
        m_dragOffsetX = localX - position[0];
        m_dragOffsetY = localY - position[1];
        return true;
    }

    if (eventId == vtkCommand::MouseMoveEvent)
    {
        if (!m_draggingScalarBar)
            return false;
        const double x = qBound(0.0, localX - m_dragOffsetX, std::max(0.0, 1.0 - width));
        const double y = qBound(0.0, localY - m_dragOffsetY, std::max(0.0, 1.0 - height));
        scalarBar->SetPosition(x, y);
        PostScalarBarStyle& style = postScalarBarStyle();
        style.hasCustomPosition = true;
        style.positionX = x;
        style.positionY = y;
        scalarBar->Modified();
        renderWindow->Render();
        return true;
    }

    if (eventId == vtkCommand::MiddleButtonReleaseEvent && m_draggingScalarBar)
    {
        m_draggingScalarBar = false;
        return true;
    }
    return false;
}
