#include "PostDisplayControlWidget.h"

#include "PostVisualUtils.h"
#include "CompactDoubleSpinBox.h"
#include "ResultDisplayNames.h"
#include "PostScientificText.h"
#include "ui_PostDisplayControlWidget.h"

#include <EventManager.h>
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
#include <QHBoxLayout>
#include <QLabel>
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

    m_meshEdgeSettings=new QWidget(ui->visibilityGroupBox);
    m_meshEdgeForm=new QFormLayout(m_meshEdgeSettings);
    m_meshEdgeForm->setContentsMargins(0,0,0,0);
    m_meshEdgeWidth=new CompactDoubleSpinBox(m_meshEdgeSettings);
    m_meshEdgeWidth->setObjectName(QStringLiteral("postMeshEdgeWidth"));
    m_meshEdgeWidth->setRange(1,5);m_meshEdgeWidth->setDecimals(1);m_meshEdgeWidth->setSingleStep(.5);
    m_meshEdgeWidth->setSuffix(QStringLiteral(" px"));m_meshEdgeWidth->setKeyboardTracking(false);
    m_meshEdgeOpacity=new QSpinBox(m_meshEdgeSettings);
    m_meshEdgeOpacity->setObjectName(QStringLiteral("postMeshEdgeOpacity"));
    m_meshEdgeOpacity->setRange(0,100);m_meshEdgeOpacity->setSingleStep(5);
    m_meshEdgeOpacity->setSuffix(QStringLiteral(" %"));m_meshEdgeOpacity->setKeyboardTracking(false);
    m_meshEdgeOpacity->setToolTip(QStringLiteral("仅控制单元边线；0% 不显示，100% 完全不透明，不改变云图表面的透明度。"));
    m_meshEdgeForm->addRow(QStringLiteral("边线粗细"),m_meshEdgeWidth);
    m_meshEdgeForm->addRow(QStringLiteral("边线不透明度"),m_meshEdgeOpacity);
    ui->visibilityLayout->insertWidget(2,m_meshEdgeSettings);

    const auto updateMeshEdges=[this] {
        m_meshEdgeSettings->setVisible(postSceneStyle().representationMode!=0);
        m_meshEdgeForm->setRowVisible(m_meshEdgeOpacity,postSceneStyle().representationMode==1);
    };
    connect(m_meshEdgeWidth,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this](double value) {
        if(m_syncing)return;postSceneStyle().meshEdgeWidth=value;applyPostSceneRepresentation(m_visualService);
    });
    connect(m_meshEdgeOpacity,QOverload<int>::of(&QSpinBox::valueChanged),this,[this](int value) {
        if(m_syncing)return;postSceneStyle().meshEdgeOpacity=value/100.0;applyPostSceneRepresentation(m_visualService);
    });

    // Retain the advanced native options for internal diagnostics, outside the
    // user-facing layout. Project loading always restores the agreed defaults.
    auto* advanced=new QWidget(this);
    advanced->setObjectName(QStringLiteral("postSurfaceAdvancedOptions"));
    advanced->hide();
    m_showOutlineCheckBox=new QCheckBox(advanced);
    m_showOutlineCheckBox->setObjectName(QStringLiteral("postSurfaceOutline"));
    m_surfaceInterpolationComboBox=new QComboBox(advanced);
    m_surfaceInterpolationComboBox->setObjectName(QStringLiteral("postSurfaceInterpolation"));
    m_surfaceInterpolationComboBox->addItem(QStringLiteral("Gouraud"),1);
    m_surfaceInterpolationComboBox->addItem(QStringLiteral("Flat"),0);
    m_scalarInterpolationCheckBox=new QCheckBox(advanced);
    m_scalarInterpolationCheckBox->setObjectName(QStringLiteral("postScalarInterpolation"));
    m_computeNormalsCheckBox=new QCheckBox(advanced);
    m_computeNormalsCheckBox->setObjectName(QStringLiteral("postComputePointNormals"));
    m_splitSharpEdgesCheckBox=new QCheckBox(advanced);
    m_splitSharpEdgesCheckBox->setObjectName(QStringLiteral("postSplitSharpEdges"));
    m_normalsFeatureAngleSpinBox=new CompactDoubleSpinBox(advanced);
    m_normalsFeatureAngleSpinBox->setObjectName(QStringLiteral("postNormalsFeatureAngle"));
    m_normalsFeatureAngleSpinBox->setRange(0,180);
    m_normalsFeatureAngleSpinBox->setDecimals(1);
    m_normalsFeatureAngleSpinBox->setSuffix(QStringLiteral("°"));
    m_normalsFeatureAngleSpinBox->setKeyboardTracking(false);
    m_reverseColorsCheckBox = new QCheckBox(QStringLiteral("反转颜色"), ui->scalarBarGroupBox);
    ui->scalarBarFormLayout->insertRow(1, m_reverseColorsCheckBox);
    m_tickModeComboBox = new QComboBox(ui->scalarBarGroupBox);
    m_tickModeComboBox->addItem(QStringLiteral("自动刻度"), 0);
    m_tickModeComboBox->addItem(QStringLiteral("等分数量"), 1);
    m_tickModeComboBox->addItem(QStringLiteral("固定步长"), 2);
    ui->scalarBarFormLayout->insertRow(2, QStringLiteral("刻度模式"), m_tickModeComboBox);
    m_tickStepSpinBox = new CompactDoubleSpinBox(ui->scalarBarGroupBox);
    m_tickStepSpinBox->setRange(1.0e-12, 1.0e100);
    m_tickStepSpinBox->setDecimals(12);
    m_tickStepSpinBox->setKeyboardTracking(false);
    ui->scalarBarFormLayout->insertRow(3, QStringLiteral("刻度步长"), m_tickStepSpinBox);

    m_thresholdGroup = new QGroupBox(QStringLiteral("阈值透明显示（云图）"), this);
    m_thresholdGroup->setObjectName(QStringLiteral("postThresholdGroup"));
    auto* thresholdForm = new QFormLayout(m_thresholdGroup);
    m_thresholdEnabled = new QCheckBox(QStringLiteral("低于阈值的区域透明显示"), m_thresholdGroup);
    m_thresholdEnabled->setObjectName(QStringLiteral("postThresholdEnabled"));
    m_thresholdValue = new CompactDoubleSpinBox(m_thresholdGroup);
    m_thresholdValue->setObjectName(QStringLiteral("postThresholdValue"));
    m_thresholdValue->setRange(-1.0e100, 1.0e100);
    m_thresholdValue->setDecimals(12);
    m_thresholdValue->setKeyboardTracking(false);
    m_belowTransparency = new QSpinBox(m_thresholdGroup);
    m_belowTransparency->setObjectName(QStringLiteral("postBelowTransparency"));
    m_belowTransparency->setRange(0, 100);
    m_belowTransparency->setSuffix(QStringLiteral(" %"));
    m_belowTransparency->setToolTip(QStringLiteral("0% 为不透明，100% 为完全透明；大于或等于阈值的区域保持原云图。阈值单位与当前变量一致。"));
    thresholdForm->addRow(m_thresholdEnabled);
    thresholdForm->addRow(QStringLiteral("结果阈值"), m_thresholdValue);
    thresholdForm->addRow(QStringLiteral("阈值以下透明度"), m_belowTransparency);
    ui->verticalLayout->insertWidget(2, m_thresholdGroup);
    auto commitThreshold = [this]() {
        const auto& context = postDisplayFieldContext();
        if (m_syncing || !context.valid || !context.supportsThreshold) return;
        auto& style = postFieldDisplayStyle(context.fieldName);
        style.thresholdEnabled = m_thresholdEnabled->isChecked();
        style.threshold = m_thresholdValue->value();
        style.belowTransparency = m_belowTransparency->value();
        m_thresholdValue->setEnabled(style.thresholdEnabled);
        m_belowTransparency->setEnabled(style.thresholdEnabled);
        notifyRefresh(QStringLiteral("threshold"));
    };
    connect(m_thresholdEnabled, &QCheckBox::toggled, this, commitThreshold);
    connect(m_thresholdValue, &QDoubleSpinBox::editingFinished, this, commitThreshold);
    connect(m_belowTransparency, &QSpinBox::valueChanged, this, commitThreshold);

    m_contourModeComboBox = new QComboBox(ui->contourGroupBox);
    m_contourModeComboBox->addItem(QStringLiteral("叠加云图"), 0);
    m_contourModeComboBox->addItem(QStringLiteral("黑白等值线"), 1);
    ui->contourFormLayout->insertRow(1, QStringLiteral("显示方式"), m_contourModeComboBox);

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

    connect(ui->showBrandingCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_syncing || !m_visualService)
            return;
        m_visualService->setBrandingVisible(checked);
    });

    connect(ui->displayModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this,updateMeshEdges](int index)
            {
                if (m_syncing || index < 0)
                    return;
                postSceneStyle().representationMode =
                    ui->displayModeComboBox->itemData(index).toInt();
                applyPostSceneRepresentation(m_visualService, false);
                m_showOutlineCheckBox->setEnabled(false);
                updateMeshEdges();
                notifyRefresh(QStringLiteral("representation"));
                if (m_visualService)
                    m_visualService->render();
            });

    connect(m_showOutlineCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (m_syncing)
            return;
        Q_UNUSED(enabled);
        const QSignalBlocker blocker(m_showOutlineCheckBox);
        m_showOutlineCheckBox->setChecked(false);
    });
    connect(m_surfaceInterpolationComboBox,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index){
        if(m_syncing || index<0)return;
        postSceneStyle().surfaceInterpolation=m_surfaceInterpolationComboBox->itemData(index).toInt();
        notifyRefresh(QStringLiteral("surface-lighting"));
    });
    connect(m_scalarInterpolationCheckBox,&QCheckBox::toggled,this,[this](bool enabled){
        if(m_syncing)return;
        postSceneStyle().interpolateScalarsBeforeMapping=enabled;
        notifyRefresh(QStringLiteral("scalar-interpolation"));
    });
    const auto refreshNormals=[this]{
        m_splitSharpEdgesCheckBox->setEnabled(postSceneStyle().computePointNormals);
        m_normalsFeatureAngleSpinBox->setEnabled(postSceneStyle().computePointNormals && postSceneStyle().splitSharpEdges);
        notifyRefresh(QStringLiteral("surface-normals"));
    };
    connect(m_computeNormalsCheckBox,&QCheckBox::toggled,this,[this,refreshNormals](bool enabled){
        if(m_syncing)return;postSceneStyle().computePointNormals=enabled;refreshNormals();
    });
    connect(m_splitSharpEdgesCheckBox,&QCheckBox::toggled,this,[this,refreshNormals](bool enabled){
        if(m_syncing)return;postSceneStyle().splitSharpEdges=enabled;refreshNormals();
    });
    connect(m_normalsFeatureAngleSpinBox,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,refreshNormals](double angle){
        if(m_syncing)return;postSceneStyle().normalsFeatureAngle=angle;refreshNormals();
    });

    connect(m_tickModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index)
    {
        if (m_syncing || index < 0)
            return;
        postScalarBarStyle().tickMode = m_tickModeComboBox->itemData(index).toInt();
        ui->tickCountSpinBox->setEnabled(postScalarBarStyle().tickMode != 2);
        m_tickStepSpinBox->setEnabled(postScalarBarStyle().tickMode == 2);
        if (postScalarBarStyle().tickMode == 2 && m_visualService)
        {
            if (auto* bar = m_visualService->scalarBarActor())
            {
                if (auto* lut = bar->GetLookupTable())
                {
                    const double* limits = lut->GetRange();
                    const double span = limits[1] - limits[0];
                    // The old default 1.0 produces only one/no label when
                    // the field has a magnitude around 1e-4.
                    if (std::isfinite(span) && span > 0.0 &&
                        postScalarBarStyle().tickStep > span)
                    {
                        const double suggested = suggestedPostScalarTickStep(
                            limits[0], limits[1], ui->tickCountSpinBox->value());
                        const QSignalBlocker blocker(m_tickStepSpinBox);
                        m_tickStepSpinBox->setValue(suggested);
                        postScalarBarStyle().tickStep = m_tickStepSpinBox->value();
                    }
                }
            }
        }
        applyScalarBarAppearance();
    });
    connect(m_tickStepSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double step)
    {
        if (m_syncing)
            return;
        postScalarBarStyle().tickStep = step;
        applyScalarBarAppearance();
    });

    connect(m_reverseColorsCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_syncing)
            return;
        postScalarBarStyle().reverseColors = checked;
        notifyRefresh(QStringLiteral("color-map"));
    });

    connect(m_contourModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index)
            {
                if (m_syncing || index < 0)
                    return;
                postContourStyle().monochrome =
                    m_contourModeComboBox->itemData(index).toInt() == 1;
                notifyRefresh(QStringLiteral("contour"));
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
            applyScalarBarAppearance(false);
        });
    EventManager::instance().registerHandler(
        PublicConstants::kEventBrandingVisibilityChanged, this,
        [this](const QVariantMap& data)
        {
            const QSignalBlocker blocker(ui->showBrandingCheckBox);
            ui->showBrandingCheckBox->setChecked(
                data.value(QStringLiteral("visible"), true).toBool());
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
    ui->showBrandingCheckBox->setChecked(
        !m_visualService || m_visualService->isBrandingVisible());
    const int displayModeIndex = ui->displayModeComboBox->findData(postSceneStyle().representationMode);
    ui->displayModeComboBox->setCurrentIndex(displayModeIndex >= 0 ? displayModeIndex : 0);
    ui->followMeshFilterCheckBox->setChecked(postSceneStyle().followMeshFilter);
    ui->showScalarBarCheckBox->setChecked(bar.visible);
    ui->scalarBarTitleEdit->setText(bar.customTitle);
    m_reverseColorsCheckBox->setChecked(bar.reverseColors);
    m_meshEdgeWidth->setValue(postSceneStyle().meshEdgeWidth);
    m_meshEdgeOpacity->setValue(qRound(postSceneStyle().meshEdgeOpacity*100));
    m_meshEdgeSettings->setVisible(postSceneStyle().representationMode!=0);
    m_meshEdgeForm->setRowVisible(m_meshEdgeOpacity,postSceneStyle().representationMode==1);
    m_showOutlineCheckBox->setChecked(postSceneStyle().showSurfaceOutline);
    m_showOutlineCheckBox->setEnabled(false);
    m_surfaceInterpolationComboBox->setCurrentIndex(m_surfaceInterpolationComboBox->findData(postSceneStyle().surfaceInterpolation));
    m_scalarInterpolationCheckBox->setChecked(postSceneStyle().interpolateScalarsBeforeMapping);
    m_computeNormalsCheckBox->setChecked(postSceneStyle().computePointNormals);
    m_splitSharpEdgesCheckBox->setChecked(postSceneStyle().splitSharpEdges);
    m_splitSharpEdgesCheckBox->setEnabled(postSceneStyle().computePointNormals);
    m_normalsFeatureAngleSpinBox->setValue(postSceneStyle().normalsFeatureAngle);
    m_normalsFeatureAngleSpinBox->setEnabled(postSceneStyle().computePointNormals && postSceneStyle().splitSharpEdges);
    m_tickModeComboBox->setCurrentIndex(bar.tickMode);
    m_tickStepSpinBox->setValue(bar.tickStep);
    m_tickStepSpinBox->setEnabled(bar.tickMode == 2);
    ui->tickCountSpinBox->setEnabled(bar.tickMode != 2);
    ui->showContourCheckBox->setChecked(contour.enabled);
    const int contourModeIndex = m_contourModeComboBox->findData(contour.monochrome ? 1 : 0);
    m_contourModeComboBox->setCurrentIndex(contourModeIndex >= 0 ? contourModeIndex : 0);

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
    m_thresholdGroup->setEnabled(context.valid && context.supportsThreshold);
    const auto& thresholdStyle = postFieldDisplayStyle(context.fieldName);
    m_thresholdEnabled->setChecked(thresholdStyle.thresholdEnabled);
    m_thresholdValue->setValue(thresholdStyle.threshold);
    m_belowTransparency->setValue(thresholdStyle.belowTransparency);
    m_thresholdValue->setEnabled(thresholdStyle.thresholdEnabled);
    m_belowTransparency->setEnabled(thresholdStyle.thresholdEnabled);
    ui->rangeGroupBox->setEnabled(context.valid);
    ui->scalarBarGroupBox->setEnabled(context.valid && context.supportsScalarBar);
    ui->showScalarBarCheckBox->setEnabled(context.valid && context.supportsScalarBar);
    ui->contourGroupBox->setEnabled(context.valid && context.supportsContours);
    ui->showContourCheckBox->setEnabled(context.valid && context.supportsContours);
    m_contourModeComboBox->setEnabled(context.valid && context.supportsContours);

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

    ui->currentFieldValueLabel->setTextFormat(Qt::RichText);
    ui->currentFieldValueLabel->setText(
        PostScientificText::toHtml(ResultDisplayNames::displayName(context.fieldName)));
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
        m_visualService->setScalarBarTitle(title, false);
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
        // A raw renderWindow->Render() bypasses VisualizationWidget::render(),
        // which owns the Qt <sub>/<sup> title overlay placement. Keep both in sync.
        m_visualService->render();
        return true;
    }

    if (eventId == vtkCommand::MiddleButtonReleaseEvent && m_draggingScalarBar)
    {
        m_draggingScalarBar = false;
        // Re-sync after the last mouse event as well (high-DPI/resizing).
        m_visualService->render();
        return true;
    }
    return false;
}
