#include "MeshCompactDoubleSpinBox.h"
#include "MeshSmeshPartitionWidget.h"
#include "core/MeshSettingsUi.h"
#include <GmdsPartitionRunner.h>
#include <QJsonArray>
#include <QJsonObject>

#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <services/MeshModuleControllerService.h>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QVariantList>
#include <QVariantMap>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace MeshSmeshWorkflowState;

namespace {

constexpr int kSliderSteps = 1000;

double computeAdaptiveStep(double span)
{
    if (!(span > 1.0e-12) || !std::isfinite(span))
        return 0.01;
    const double raw = span / 100.0;
    const double exponent = std::floor(std::log10(raw));
    const double base = std::pow(10.0, exponent);
    const double norm = raw / base;
    double mult = 1.0;
    if (norm >= 5.0) mult = 5.0;
    else if (norm >= 2.0) mult = 2.0;
    return std::max(1.0e-6, mult * base);
}

int computeAdaptiveDecimals(double span)
{
    if (!(span > 1.0e-12) || !std::isfinite(span))
        return 4;
    const int exp = static_cast<int>(std::floor(std::log10(span)));
    return std::clamp(4 - exp, 2, 6);
}

} // namespace

MeshSmeshPartitionWidget::MeshSmeshPartitionWidget(QWidget* parent)
    : BasePage("MeshSmeshPartitionWidget")
{
    Q_UNUSED(parent);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);
    root->addWidget(m_selectionPresentation.createViewSelector(this));

    auto* title = new QLabel(tr("扫掠分块"), this);
    QFont f = title->font();
    f.setBold(true); f.setPointSize(f.pointSize() + 2); title->setFont(f);
    root->addWidget(title);

    m_ownerLabel = new QLabel(tr("当前网格：分块用于扫掠区域"), this);
    m_ownerLabel->setWordWrap(true);
    m_ownerLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(m_ownerLabel);

    auto* hint = new QLabel(
        tr("先选择实体，再添加切面或分析分块。"), this);
    hint->setWordWrap(true);
    MeshSettingsUi::hideExplanation(hint);
    MeshSettingsUi::hideExplanation(m_ownerLabel);
    root->addWidget(hint);

    auto* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(6);

    m_targetVolume = new QComboBox(this);
    m_targetVolume->setMinimumWidth(0);
    m_targetVolume->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* targetEditor = new QWidget(this);
    auto* targetRow = new QHBoxLayout(targetEditor);
    targetRow->setContentsMargins(0, 0, 0, 0);
    targetRow->setSpacing(6);
    auto* pickTarget = new QPushButton(tr("图形区选择"), targetEditor);
    targetRow->addWidget(m_targetVolume, 1);
    targetRow->addWidget(pickTarget);
    form->addRow(tr("目标实体"), targetEditor);

    m_axis = new QComboBox(this);
    m_axis->addItem(tr("X = 常数"), 0);
    m_axis->addItem(tr("Y = 常数"), 1);
    m_axis->addItem(tr("Z = 常数"), 2);

    auto* coordEditor = new QWidget(this);
    auto* coordCol = new QVBoxLayout(coordEditor);
    coordCol->setContentsMargins(0, 0, 0, 0);
    coordCol->setSpacing(4);

    m_coordinate = new MeshCompactDoubleSpinBox(coordEditor);
    m_coordinate->setRange(-1.0e12, 1.0e12);
    m_coordinate->setDecimals(4);
    m_coordinate->setSingleStep(0.1);
    coordCol->addWidget(m_coordinate);

    m_slider = new QSlider(Qt::Horizontal, coordEditor);
    m_slider->setRange(0, kSliderSteps);
    m_slider->setSingleStep(10);
    m_slider->setPageStep(50);
    m_slider->setTracking(true);
    m_slider->setEnabled(false);
    coordCol->addWidget(m_slider);

    m_rangeLabel = new QLabel(tr("实体范围：--"), coordEditor);
    m_rangeLabel->setWordWrap(true);
    m_rangeLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    coordCol->addWidget(m_rangeLabel);

    form->addRow(tr("切面方向"), m_axis);
    form->addRow(tr("切面位置"), coordEditor);
    root->addLayout(form);

    auto* addRow = new QGridLayout();
    addRow->setHorizontalSpacing(6);
    addRow->setVerticalSpacing(5);
    auto* center = new QPushButton(tr("放到实体中心"), this);
    auto* addAxis = new QPushButton(tr("添加切面"), this);
    m_updateAxis = new QPushButton(tr("更新选中"), this);
    m_updateAxis->setEnabled(false);
    addRow->addWidget(center, 0, 0);
    addRow->addWidget(addAxis, 0, 1);
    addRow->addWidget(m_updateAxis, 1, 0, 1, 2);
    addRow->setColumnStretch(0, 1);
    addRow->setColumnStretch(1, 1);
    root->addLayout(addRow);

    auto* semiAutoRow = new QHBoxLayout();
    semiAutoRow->setSpacing(6);
    auto* autoSuggest = new QPushButton(tr("GMDS 半自动分块"), this);
    autoSuggest->setObjectName(QStringLiteral("gmdsPartitionSuggest"));
    m_gmdsButton = autoSuggest;
    m_gmdsRunner = new GmdsPartitionRunner(this);
    connect(m_gmdsRunner,&GmdsPartitionRunner::finished,this,&MeshSmeshPartitionWidget::finishGmdsSuggestions);
    auto* pickFaceCut = new QPushButton(tr("点面切"), this);
    auto* pickEdgeCut = new QPushButton(tr("点边切"), this);
    semiAutoRow->addWidget(autoSuggest, 2);
    semiAutoRow->addWidget(pickFaceCut, 1);
    semiAutoRow->addWidget(pickEdgeCut, 1);
    root->addLayout(semiAutoRow);

    m_rules = new QListWidget(this);
    m_rules->setMinimumHeight(84);
    m_rules->setMaximumHeight(130);
    root->addWidget(m_rules);

    auto* editRow = new QGridLayout();
    editRow->setHorizontalSpacing(6);
    editRow->setVerticalSpacing(5);
    auto* remove = new QPushButton(tr("删除选中"), this);
    auto* clear = new QPushButton(tr("清空"), this);
    auto* showCutters = new QPushButton(tr("显示切面"), this);
    m_preview = new QPushButton(tr("预览分块"), this);
    editRow->addWidget(remove, 0, 0);
    editRow->addWidget(clear, 0, 1);
    editRow->addWidget(showCutters, 1, 0);
    editRow->addWidget(m_preview, 1, 1);
    editRow->setColumnStretch(0, 1);
    editRow->setColumnStretch(1, 1);
    root->addLayout(editRow);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    m_regions = new QListWidget(this);
    m_regions->setMinimumHeight(76);
    m_regions->setMaximumHeight(115);
    root->addWidget(m_regions);

    auto* regionRow = new QGridLayout();
    regionRow->setHorizontalSpacing(6);
    regionRow->setVerticalSpacing(5);
    auto* pickRegion = new QPushButton(tr("图形区选择分块"), this);
    auto* showRegion = new QPushButton(tr("突出选中分块"), this);
    auto* restore = new QPushButton(tr("恢复普通显示"), this);
    regionRow->addWidget(pickRegion, 0, 0);
    regionRow->addWidget(showRegion, 0, 1);
    regionRow->addWidget(restore, 1, 0, 1, 2);
    regionRow->setColumnStretch(0, 1);
    regionRow->setColumnStretch(1, 1);
    root->addLayout(regionRow);

    connect(pickTarget, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::beginTargetVolumePick);
    connect(center, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::snapCoordinateToTargetCenter);
    connect(addAxis, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::addAxisRule);
    connect(m_updateAxis, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::updateSelectedAxisRule);
    connect(autoSuggest, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::startGmdsAnalysis);
    connect(pickFaceCut, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::beginCadPlanePick);
    connect(pickEdgeCut, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::beginEdgePairPick);
    connect(m_targetVolume, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
    {
        updateAxisBounds(true);
        showCuttingPlanePreview();
        const int tid = m_targetVolume->currentData().toInt();
        m_selectionPresentation.setSelectedEntities(3, tid > 0 ? QList<int>{tid} : QList<int>{});
    });
    connect(m_axis, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
    {
        updateAxisBounds(true);
        showCuttingPlanePreview();
    });
    connect(m_coordinate, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double)
    {
        syncSliderFromCoordinate();
        showCuttingPlanePreview();
    });
    connect(m_slider, &QSlider::valueChanged, this, &MeshSmeshPartitionWidget::onSliderValueChanged);
    connect(remove, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::removeSelectedRule);
    connect(clear, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::clearRules);
    connect(m_preview, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::previewPartition);
    connect(showCutters, &QPushButton::clicked, this, [this]() {
        if (auto* view = findChild<QComboBox*>(QStringLiteral("meshSettingsViewSelector"));
            view && view->currentIndex() != 0)
            view->setCurrentIndex(0);
        else if (m_ruleData.isEmpty())
            showCuttingPlanePreview();
        else
            showSavedCutters();
    });
    if (auto* view = findChild<QComboBox*>(QStringLiteral("meshSettingsViewSelector")))
        connect(view, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index == 0) {
                if (m_ruleData.isEmpty()) showCuttingPlanePreview();
                else showSavedCutters();
            } else {
                restoreCad();
                m_selectionPresentation.refresh();
            }
        });
    connect(pickRegion, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::beginRegionPick);
    connect(showRegion, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::showSelectedRegion);
    connect(m_regions, &QListWidget::itemSelectionChanged, this, [this]() {
        if (m_pickMode != PickRegion) showSelectedRegion();
    });
    connect(restore, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::restoreCad);
    connect(m_rules, &QListWidget::itemSelectionChanged, this, &MeshSmeshPartitionWidget::loadSelectedRuleToEditors);
    connect(m_rules, &QListWidget::itemChanged, this, [this](QListWidgetItem* item)
    {
        if (m_loadingRules || !item) return;
        const int row = m_rules->row(item);
        if (row < 0 || row >= m_ruleData.size()) return;
        const bool enabled = item->checkState() == Qt::Checked;
        if (m_ruleData[row].enabled == enabled) return;
        m_ruleData[row].enabled = enabled;
        item->setToolTip(enabled ? tr("已启用：本次分块会使用该切面")
                                 : tr("已停用：保留切面，但本次不参与分块"));
        saveState();
        invalidateRegionPreview();
        emit logMessage(tr("[扫掠分块] 切面 %1 已%2。")
                        .arg(row + 1).arg(enabled ? tr("启用") : tr("停用")));
    });

    rebuildTargetVolumes();
    loadState();
    updateAxisBounds(true);
}

MeshSmeshPartitionWidget::~MeshSmeshPartitionWidget()
{
    m_discardGmds = true;
    m_gmdsRunner->cancel();
    stopPick();
    restoreCad();
}

void MeshSmeshPartitionWidget::setMeshPlanIdentity(const QString& uuid, const QString& displayName)
{
    if(m_planUuid!=uuid.trimmed()) { m_discardGmds=true; m_gmdsRunner->cancel(); }
    m_planUuid = uuid.trimmed();
    m_planName = displayName.trimmed();
    setProperty("meshPlanUuid", m_planUuid);
    if (m_ownerLabel)
        m_ownerLabel->setText(tr("当前网格：%1。分块仅用于准备扫掠区域。")
            .arg(m_planName.isEmpty() ? tr("网格") : m_planName));
    rebuildTargetVolumes();
    loadState();
    updateAxisBounds(m_ruleData.isEmpty());
    MeshSettingsUi::simplify(this);
}

QObject* MeshSmeshPartitionWidget::geometryBridge() const
{
    auto* svc = ServiceRegistry::instance().getService<PreProcessingService>();
    return svc ? dynamic_cast<QObject*>(svc) : nullptr;
}

void MeshSmeshPartitionWidget::rebuildTargetVolumes()
{
    if (!m_targetVolume) return;
    const bool hadItems = m_targetVolume->count() > 0;
    const int oldId = hadItems ? m_targetVolume->currentData().toInt() : -1;
    m_targetVolume->blockSignals(true);
    m_targetVolume->clear();
    m_targetVolume->addItem(tr("全部实体（自动）"), -1);
    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
    {
        const QStringList ids = svc->listEntityIds(3);
        for (const QString& text : ids)
        {
            bool ok = false;
            const int id = text.toInt(&ok);
            if (ok && id > 0)
                m_targetVolume->addItem(tr("实体 %1").arg(id), id);
        }
    }
    int index = hadItems ? m_targetVolume->findData(oldId) : 0;
    if (index < 0) index = 0;
    m_targetVolume->setCurrentIndex(index);
    m_targetVolume->blockSignals(false);
}

void MeshSmeshPartitionWidget::updateAxisBounds(bool snapToCenter)
{
    m_hasAxisBounds = false;
    if (!m_targetVolume || !m_axis || !m_coordinate || !m_slider || !m_rangeLabel)
        return;

    const int id = m_targetVolume->currentData().toInt();
    const int axis = m_axis->currentData().toInt();
    if (m_targetVolume->count() <= 1 || axis < 0 || axis > 2)
    {
        m_slider->setEnabled(false);
        m_rangeLabel->setText(tr("实体范围：无可用实体"));
        return;
    }

    QVariantList bounds;
    if (QObject* bridge = geometryBridge())
    {
        QVariantList ids;
        if (id > 0) ids.push_back(id);
        QMetaObject::invokeMethod(bridge, "meshingGeometryBounds", Qt::DirectConnection,
                                  Q_RETURN_ARG(QVariantList, bounds),
                                  Q_ARG(int, 3), Q_ARG(QVariantList, ids));
    }

    if (bounds.size() == 6)
    {
        const double bmin = bounds[axis * 2].toDouble();
        const double bmax = bounds[axis * 2 + 1].toDouble();
        if (std::isfinite(bmin) && std::isfinite(bmax) && bmax - bmin > 1.0e-12)
        {
            m_axisMin = bmin;
            m_axisMax = bmax;
            m_hasAxisBounds = true;

            const double span = m_axisMax - m_axisMin;
            const double step = computeAdaptiveStep(span);
            const int decimals = computeAdaptiveDecimals(span);

            {
                const QSignalBlocker blockCoord(m_coordinate);
                m_coordinate->setDecimals(decimals);
                m_coordinate->setSingleStep(step);
                if (snapToCenter)
                    m_coordinate->setValue((m_axisMin + m_axisMax) * 0.5);
            }

            m_slider->setEnabled(true);
            syncSliderFromCoordinate();
            m_rangeLabel->setText(tr("范围：%1 ~ %2（步距 %3）")
                .arg(m_axisMin, 0, 'f', decimals)
                .arg(m_axisMax, 0, 'f', decimals)
                .arg(step, 0, 'g', 4));
            return;
        }
    }

    m_slider->setEnabled(false);
    m_rangeLabel->setText(tr("范围：未获取到包围盒，可直接输入坐标"));
    if (snapToCenter)
    {
        auto* svc = ServiceRegistry::instance().getService<PreProcessingService>();
        QVector3D center;
        if (svc && svc->entityCenter(3, id, center))
        {
            const QSignalBlocker blockCoord(m_coordinate);
            const double value = axis == 0 ? center.x() : (axis == 1 ? center.y() : center.z());
            m_coordinate->setValue(value);
        }
    }
}

void MeshSmeshPartitionWidget::syncSliderFromCoordinate()
{
    if (!m_slider || !m_coordinate || !m_hasAxisBounds)
        return;
    const double span = m_axisMax - m_axisMin;
    if (!(span > 1.0e-12))
        return;
    const double ratio = std::clamp((m_coordinate->value() - m_axisMin) / span, 0.0, 1.0);
    const int pos = static_cast<int>(std::llround(ratio * kSliderSteps));
    const QSignalBlocker blockSlider(m_slider);
    m_slider->setValue(pos);
}

void MeshSmeshPartitionWidget::onSliderValueChanged(int sliderPos)
{
    if (!m_coordinate || !m_hasAxisBounds)
        return;
    const double span = m_axisMax - m_axisMin;
    if (!(span > 1.0e-12))
        return;
    const double ratio = std::clamp(double(sliderPos) / double(kSliderSteps), 0.0, 1.0);
    const double raw = m_axisMin + span * ratio;
    const double scale = std::pow(10.0, m_coordinate->decimals());
    const double rounded = std::round(raw * scale) / scale;
    {
        const QSignalBlocker blockCoord(m_coordinate);
        m_coordinate->setValue(rounded);
    }
    showCuttingPlanePreview();
}

void MeshSmeshPartitionWidget::snapCoordinateToTargetCenter()
{
    updateAxisBounds(true);
    showCuttingPlanePreview();
}

void MeshSmeshPartitionWidget::beginTargetVolumePick()
{
    QObject* bridge = geometryBridge();
    if (!bridge)
    {
        QMessageBox::warning(this, tr("扫掠分块"), tr("当前无法读取几何模型，请确认几何已正常加载。"));
        return;
    }
    stopPick();
    bool ok = false;
    const bool invoked = QMetaObject::invokeMethod(bridge, "beginExternalEntityPick", Qt::DirectConnection,
                                                    Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(int, 3));
    if (!invoked || !ok)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("暂时无法选择实体，请重新进入该功能后再试。"));
        return;
    }
    m_pickMode = PickTargetVolume;
    m_summary->setText(tr("请在图形区点选一个要分块的实体。"));
}

void MeshSmeshPartitionWidget::beforeLoad()
{
    m_selectionPresentation.setSelectedEntities(3, {});
    m_selectionPresentation.enter();
    rebuildTargetVolumes();
    loadState();
    updateAxisBounds(m_ruleData.isEmpty());
    invalidateRegionPreview();
    if (m_ruleData.isEmpty())
        showCuttingPlanePreview();
    MeshSettingsUi::simplify(this);
}

void MeshSmeshPartitionWidget::beforeUnload()
{
    m_discardGmds = true;
    m_gmdsRunner->cancel();
    stopPick();
    restoreCad();
    m_selectionPresentation.leave();
}

void MeshSmeshPartitionWidget::loadState()
{
    m_ruleData = partitionRulesForPlan(m_planUuid);
    refreshRuleList();
}

void MeshSmeshPartitionWidget::saveState()
{
    if (m_planUuid.isEmpty()) return;
    for (PartitionRule& r : m_ruleData) r.planUuid = m_planUuid;
    replacePartitionRulesForPlan(m_planUuid, m_ruleData);
    if (auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        auto faceRules = svc->faceControlRules();
        bool clearedFaceRefs = false;
        for (auto& rule : faceRules)
        {
            const QString owner = !rule.planUuid.trimmed().isEmpty() ? rule.planUuid.trimmed() : rule.ownerTaskUuid.trimmed();
            if (owner != m_planUuid || rule.partitionFaceRefs.trimmed().isEmpty()) continue;
            rule.partitionFaceRefs.clear();
            clearedFaceRefs = true;
        }
        if (clearedFaceRefs)
        {
            svc->setFaceControlRules(faceRules);
            emit logMessage(tr("[面尺寸][提示] 分块已修改，旧的分块面尺寸选择已清空，请重新选择。"));
        }

        auto edgeRules = svc->edgeControlRules();
        bool clearedEdgeRefs = false;
        for (auto& rule : edgeRules)
        {
            const QString owner = !rule.planUuid.trimmed().isEmpty() ? rule.planUuid.trimmed() : rule.ownerTaskUuid.trimmed();
            if (owner != m_planUuid || rule.partitionEdgeRefs.trimmed().isEmpty()) continue;
            rule.partitionEdgeRefs.clear();
            clearedEdgeRefs = true;
        }
        if (clearedEdgeRefs)
        {
            svc->setEdgeControlRules(edgeRules);
            emit logMessage(tr("[边尺寸][提示] 分块已修改，旧的分块边尺寸选择已清空，请重新选择。"));
        }

        if (auto* obj = dynamic_cast<QObject*>(svc))
            QMetaObject::invokeMethod(obj, "markMeshControlsChangedForPlan", Qt::DirectConnection,
                Q_ARG(QString, m_planUuid), Q_ARG(QString, tr("扫掠分块设置已修改")));
    }
    QVector<SweepTask> kept = sweepTasksForPlan(m_planUuid);
    for (int i = kept.size() - 1; i >= 0; --i)
        if (kept[i].partitionRegion) kept.removeAt(i);
    replaceSweepTasksForPlan(m_planUuid, kept);
    emit partitionChanged();
}

QVariantList MeshSmeshPartitionWidget::planes() const
{
    return planePayload(m_ruleData);
}

void MeshSmeshPartitionWidget::refreshRuleList()
{
    m_loadingRules = true;
    m_rules->clear();
    int enabledCount = 0;
    for (int i = 0; i < m_ruleData.size(); ++i)
    {
        const PartitionRule& r = m_ruleData[i];
        if (r.enabled) ++enabledCount;
        QString text;
        const QString targetText = r.targetVolumeUiId > 0
            ? tr("实体 %1 · ").arg(r.targetVolumeUiId) : tr("全部实体 · ");
        if (r.axis >= 0 && r.axis <= 2)
        {
            const QChar axis = r.axis == 0 ? QLatin1Char('X') : (r.axis == 1 ? QLatin1Char('Y') : QLatin1Char('Z'));
            text = tr("%1. %2%3 = %4").arg(i + 1).arg(targetText).arg(axis).arg(r.coordinate, 0, 'g', 7);
        }
        else
        {
            if (r.sourceEdgeUiId1 > 0 && r.sourceEdgeUiId2 > 0)
            {
                text = tr("%1. %2两边确定切面：E%3 + E%4")
                    .arg(i + 1).arg(targetText).arg(r.sourceEdgeUiId1).arg(r.sourceEdgeUiId2);
            }
            else if (r.sourceFaceUiId > 0)
            {
                text = tr("%1. %2选取平面：F%3")
                    .arg(i + 1).arg(targetText).arg(r.sourceFaceUiId);
            }
            else if (r.axis == 4)
            {
                const QVector3D n = r.planeNormal();
                QString axisHint;
                if (std::abs(n.x()) > 0.99f) axisHint = QStringLiteral("X");
                else if (std::abs(n.y()) > 0.99f) axisHint = QStringLiteral("Y");
                else if (std::abs(n.z()) > 0.99f) axisHint = QStringLiteral("Z");
                else axisHint = tr("(%1,%2,%3)").arg(n.x(), 0, 'f', 2).arg(n.y(), 0, 'f', 2).arg(n.z(), 0, 'f', 2);
                text = tr("%1. %2柱面穿透切 R=%3（沿 %4）")
                    .arg(i + 1).arg(targetText).arg(r.coordinate, 0, 'g', 6).arg(axisHint);
            }
            else
            {
                const QVector3D n = r.planeNormal();
                text = tr("%1. %2智能斜切面 n=(%3,%4,%5)")
                    .arg(i + 1).arg(targetText)
                    .arg(n.x(), 0, 'f', 2).arg(n.y(), 0, 'f', 2).arg(n.z(), 0, 'f', 2);
            }
        }
        auto* item = new QListWidgetItem(text, m_rules);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(r.enabled ? tr("已启用：本次分块会使用该切面")
                                   : tr("已停用：保留切面，但本次不参与分块"));
    }
    m_loadingRules = false;
    if (m_updateAxis)
        m_updateAxis->setEnabled(false);
    if (m_ruleData.isEmpty())
        m_summary->setText(tr("未设置切面（拖动滑块定位后点击“添加切面”）"));
    else
        m_summary->setText(tr("切面 %1，已启用 %2")
                           .arg(m_ruleData.size()).arg(enabledCount));
}

void MeshSmeshPartitionWidget::addAxisRule()
{
    const int targetVolume = m_targetVolume ? m_targetVolume->currentData().toInt() : -1;
    if (!m_targetVolume || m_targetVolume->count() <= 1)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("当前模型没有可分块的实体。"));
        return;
    }
    const int axis = m_axis->currentData().toInt();
    const double coord = m_coordinate->value();
    const double tol = m_hasAxisBounds ? std::max(1.0e-6, (m_axisMax - m_axisMin) * 1.0e-4) : 1.0e-6;

    if (m_hasAxisBounds && (coord <= m_axisMin + tol || coord >= m_axisMax - tol))
    {
        m_summary->setText(tr("切面位置需位于目标实体内部（%1 ~ %2），请拖动滑块调整。")
                           .arg(m_axisMin, 0, 'g', 6).arg(m_axisMax, 0, 'g', 6));
        return;
    }

    for (const PartitionRule& existing : m_ruleData)
    {
        if (existing.enabled && existing.targetVolumeUiId == targetVolume
            && existing.axis == axis && std::abs(existing.coordinate - coord) <= tol)
        {
            m_summary->setText(tr("相同位置已存在切面，请拖动滑块到新位置后再添加。"));
            return;
        }
    }

    PartitionRule r;
    r.planUuid = m_planUuid;
    r.targetVolumeUiId = targetVolume;
    r.axis = axis;
    r.coordinate = coord;
    m_ruleData.push_back(r);
    saveState();
    refreshRuleList();
    invalidateRegionPreview();
    emit logMessage(tr("[扫掠分块] 已添加切面。"));
}

void MeshSmeshPartitionWidget::startGmdsAnalysis()
{
    if(m_gmdsRunner->running()) { m_gmdsRunner->cancel(); return; }
    QObject* bridge=geometryBridge();
    if(!bridge || !m_targetVolume || m_targetVolume->count()<=1) return;
    m_gmdsTarget=m_targetVolume->currentData().toInt();
    QVariantMap snapshot;
    if(!QMetaObject::invokeMethod(bridge,"externalGmdsPartitionSnapshot",Qt::DirectConnection,
        Q_RETURN_ARG(QVariantMap,snapshot),Q_ARG(int,m_gmdsTarget)) || !snapshot.value("valid").toBool()) {
        m_summary->setText(snapshot.value("message",tr("无法读取 GMDS 几何快照。")).toString()); return;
    }
    stopPick(); m_discardGmds=false;
    m_gmdsSnapshot=snapshot; m_gmdsPlanes=planes(); m_gmdsPlanUuid=m_planUuid;
    QJsonArray cutters;
    for(const auto& value:m_gmdsPlanes) {
        const auto p=value.toMap(); QJsonObject cut=QJsonObject::fromVariantMap(p);
        for(const auto& key:{QStringLiteral("origin"),QStringLiteral("normal")}) {
            auto v=p.value(key).value<QVector3D>(); cut[key]=QJsonArray{double(v.x()),double(v.y()),double(v.z())};
        }
        cutters.append(cut);
    }
    const QJsonObject request{{"bodyIds",QJsonArray::fromVariantList(snapshot.value("bodyIds").toList())},
        {"preSkipped",QJsonArray::fromVariantList(snapshot.value("preSkipped").toList())},
        {"planes",cutters},{"maxSuggestions",4}};
    m_summary->setText(tr("GMDS 正在分析，可点击取消。")); m_gmdsButton->setText(tr("取消 GMDS 分析"));
    m_gmdsRunner->start(snapshot.value("brep").toByteArray(),request);
}

void MeshSmeshPartitionWidget::finishGmdsSuggestions(const QVariantMap& result)
{
    m_gmdsButton->setText(tr("GMDS 半自动分块"));
    if(m_discardGmds) return;
    if(result.value("cancelled").toBool()) { m_summary->setText(result.value("message").toString()); return; }
    const int targetVolume=m_gmdsTarget;
    QObject* bridge=geometryBridge(); QVariantMap snapshot;
    if(!bridge || m_planUuid!=m_gmdsPlanUuid || planes()!=m_gmdsPlanes || m_targetVolume->currentData().toInt()!=targetVolume ||
       !QMetaObject::invokeMethod(bridge,"externalGmdsPartitionSnapshot",Qt::DirectConnection,
           Q_RETURN_ARG(QVariantMap,snapshot),Q_ARG(int,targetVolume)) ||
       snapshot!=m_gmdsSnapshot) {
        m_summary->setText(tr("几何或分块设置已变化，请重新分析。")); return;
    }
    const auto currentPlanes=m_gmdsPlanes;
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        const QString msg = result.value(QStringLiteral("message")).toString();
        QMessageBox::information(this, tr("GMDS 分块"),
                                 msg.isEmpty() ? tr("当前无法分析目标实体，请检查几何拓扑。") : msg);
        return;
    }

    const QString summaryMsg = result.value(QStringLiteral("message")).toString();
    m_summary->setText(summaryMsg);
    emit logMessage(tr("[GMDS 分块] %1").arg(summaryMsg));

    if (result.value(QStringLiteral("alreadySweepable")).toBool())
    {
        QMessageBox::information(this, tr("GMDS 分块"), summaryMsg);
        return;
    }

    const QVariantList suggestions = result.value(QStringLiteral("suggestions")).toList();
    if (suggestions.isEmpty())
    {
        QMessageBox::information(this, tr("GMDS 分块"), summaryMsg);
        return;
    }

    // Preview existing saved cuts (gold) together with recommended cuts (sky-blue) in 3D.
    QVariantList previewList = currentPlanes;
    QStringList detailLines;
    for (int i = 0; i < suggestions.size(); ++i)
    {
        const QVariantMap s = suggestions[i].toMap();
        const int sTarget = s.value(QStringLiteral("targetVolumeUiId"), targetVolume).toInt();
        const int axis = s.value(QStringLiteral("axis"), 3).toInt();
        const double coord = s.value(QStringLiteral("coordinate"), 0.0).toDouble();
        QVariantMap p;
        p.insert(QStringLiteral("origin"), s.value(QStringLiteral("origin")));
        p.insert(QStringLiteral("normal"), s.value(QStringLiteral("normal")));
        p.insert(QStringLiteral("targetVolumeUiId"), sTarget);
        p.insert(QStringLiteral("axis"), axis);
        if (axis == 4) p.insert(QStringLiteral("radius"), coord);
        p.insert(QStringLiteral("isDraft"), true);
        previewList.push_back(p);

        const QString reason = s.value(QStringLiteral("reason")).toString();
        const double minVolPct = s.value(QStringLiteral("minVolumeRatio"), 0.0).toDouble() * 100.0;
        QString posText;
        if (axis >= 0 && axis <= 2)
        {
            const QChar ax = axis == 0 ? QLatin1Char('X') : (axis == 1 ? QLatin1Char('Y') : QLatin1Char('Z'));
            posText = tr("实体 %1 · %2 = %3").arg(sTarget).arg(ax).arg(coord, 0, 'g', 6);
        }
        else if (axis == 4)
        {
            const QVector3D n = s.value(QStringLiteral("normal")).value<QVector3D>();
            posText = tr("实体 %1 · 柱面穿透切 R=%2, 轴(%3,%4,%5)")
                .arg(sTarget).arg(coord, 0, 'g', 6)
                .arg(n.x(), 0, 'f', 1).arg(n.y(), 0, 'f', 1).arg(n.z(), 0, 'f', 1);
        }
        else
        {
            const QVector3D n = s.value(QStringLiteral("normal")).value<QVector3D>();
            posText = tr("实体 %1 · 斜平面 n=(%2,%3,%4)").arg(sTarget).arg(n.x(), 0, 'f', 2).arg(n.y(), 0, 'f', 2).arg(n.z(), 0, 'f', 2);
        }
        detailLines << tr("  %1. %2（%3，最小子块占比 %4%）")
                           .arg(i + 1).arg(posText).arg(reason).arg(minVolPct, 0, 'f', 1);
    }

    if (m_selectionPresentation.showingGeometry())
    {
        QMetaObject::invokeMethod(bridge, "showExternalPartitionCutters", Qt::DirectConnection,
                                  Q_ARG(QVariantList, previewList));
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("GMDS 分块候选"));
    box.setIcon(QMessageBox::Question);
    box.setText(summaryMsg);
    box.setInformativeText(tr("蓝色半透明面为候选切面：\n%1\n\n是否采纳这些切面并预览分块？")
                               .arg(detailLines.join(QLatin1Char('\n'))));
    QPushButton* acceptBtn = box.addButton(tr("采纳切面"), QMessageBox::AcceptRole);
    box.addButton(tr("仅预览不添加"), QMessageBox::RejectRole);
    box.setDefaultButton(acceptBtn);
    box.exec();

    if (box.clickedButton() != acceptBtn)
        return;

    QVariantMap confirmation;
    if(m_planUuid!=m_gmdsPlanUuid || planes()!=m_gmdsPlanes ||
       !QMetaObject::invokeMethod(bridge,"externalGmdsPartitionSnapshot",Qt::DirectConnection,
           Q_RETURN_ARG(QVariantMap,confirmation),Q_ARG(int,targetVolume)) || confirmation!=m_gmdsSnapshot) {
        m_summary->setText(tr("几何或分块设置已变化，请重新分析。")); return;
    }

    for (const QVariant& sv : suggestions)
    {
        const QVariantMap s = sv.toMap();
        PartitionRule r;
        r.planUuid = m_planUuid;
        r.targetVolumeUiId = s.value(QStringLiteral("targetVolumeUiId"), targetVolume).toInt();
        r.axis = s.value(QStringLiteral("axis"), 3).toInt();
        r.coordinate = s.value(QStringLiteral("coordinate"), 0.0).toDouble();
        if (r.axis >= 3)
        {
            r.origin = s.value(QStringLiteral("origin")).value<QVector3D>();
            r.normal = s.value(QStringLiteral("normal")).value<QVector3D>();
        }
        r.enabled = true;
        m_ruleData.push_back(r);
    }
    saveState();
    refreshRuleList();
    refreshRegionList();
    emit logMessage(tr("[GMDS 分块] 已采纳 %1 个候选切面。").arg(suggestions.size()));
}

void MeshSmeshPartitionWidget::loadSelectedRuleToEditors()
{
    if (m_loadingRules || !m_rules || !m_targetVolume || !m_axis || !m_coordinate)
        return;
    const int row = m_rules->currentRow();
    const bool validRow = (row >= 0 && row < m_ruleData.size());
    if (m_updateAxis)
        m_updateAxis->setEnabled(validRow && m_ruleData[row].axis >= 0 && m_ruleData[row].axis <= 2);
    if (!validRow) return;

    const PartitionRule& r = m_ruleData[row];
    if (r.axis < 0 || r.axis > 2)
    {
        m_summary->setText(tr("这是旧版平面/两边切面规则。当前简化版不再编辑这种规则；建议删除后用“目标实体 + X/Y/Z + 切面位置”重新添加。"));
        return;
    }

    {
        const QSignalBlocker blockTarget(m_targetVolume);
        const QSignalBlocker blockAxis(m_axis);
        const int targetIndex = m_targetVolume->findData(r.targetVolumeUiId);
        if (targetIndex >= 0) m_targetVolume->setCurrentIndex(targetIndex);
        const int axisIndex = m_axis->findData(r.axis);
        if (axisIndex >= 0) m_axis->setCurrentIndex(axisIndex);
    }
    updateAxisBounds(false);
    {
        const QSignalBlocker blockCoordinate(m_coordinate);
        m_coordinate->setValue(r.coordinate);
    }
    syncSliderFromCoordinate();
    m_summary->setText(tr("已选中切面 %1（拖动滑块后点击“更新选中”修改，或点击“添加切面”新增）").arg(row + 1));
    showCuttingPlanePreview();
}

void MeshSmeshPartitionWidget::updateSelectedAxisRule()
{
    const int row = m_rules ? m_rules->currentRow() : -1;
    if (row < 0 || row >= m_ruleData.size())
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("请先在切面列表中选中要修改的切面。"));
        return;
    }
    const int targetVolume = m_targetVolume ? m_targetVolume->currentData().toInt() : -1;
    if (!m_targetVolume || m_targetVolume->count() <= 1)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("当前模型没有可分块的实体。"));
        return;
    }
    const double coord = m_coordinate->value();
    const double tol = m_hasAxisBounds ? std::max(1.0e-6, (m_axisMax - m_axisMin) * 1.0e-4) : 1.0e-6;
    if (m_hasAxisBounds && (coord <= m_axisMin + tol || coord >= m_axisMax - tol))
    {
        m_summary->setText(tr("切面位置需位于目标实体内部（%1 ~ %2），请拖动滑块调整。")
                           .arg(m_axisMin, 0, 'g', 6).arg(m_axisMax, 0, 'g', 6));
        return;
    }

    PartitionRule& r = m_ruleData[row];
    r.targetVolumeUiId = targetVolume;
    r.axis = m_axis->currentData().toInt();
    r.coordinate = coord;
    r.origin = QVector3D();
    r.normal = QVector3D();
    r.sourceFaceUiId = -1;
    r.sourceEdgeUiId1 = -1;
    r.sourceEdgeUiId2 = -1;
    r.enabled = true;
    saveState();
    refreshRuleList();
    {
        const QSignalBlocker blockRules(m_rules);
        m_rules->setCurrentRow(row);
    }
    if (m_updateAxis) m_updateAxis->setEnabled(true);
    invalidateRegionPreview();
    emit logMessage(tr("[扫掠分块] 已更新切面 %1。").arg(row + 1));
}

void MeshSmeshPartitionWidget::beginCadPlanePick()
{
    QObject* bridge = geometryBridge();
    if (!bridge) { QMessageBox::warning(this, tr("扫掠分块"), tr("当前无法读取几何模型，请确认几何已正常加载。")); return; }
    stopPick();
    bool ok = false;
    const bool invoked = QMetaObject::invokeMethod(bridge, "beginExternalEntityPick", Qt::DirectConnection,
                                                    Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(int, 2));
    if (!invoked || !ok)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("暂时无法选择几何面，请重新进入该功能后再试。"));
        return;
    }
    m_pickMode = PickCadPlane;
}

void MeshSmeshPartitionWidget::beginEdgePairPick()
{
    QObject* bridge = geometryBridge();
    if (!bridge) { QMessageBox::warning(this, tr("扫掠分块"), tr("当前无法读取几何模型，请确认几何已正常加载。")); return; }
    stopPick();
    bool ok = false;
    const bool invoked = QMetaObject::invokeMethod(bridge, "beginExternalEntityPick", Qt::DirectConnection,
                                                    Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(int, 1));
    if (!invoked || !ok)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("暂时无法选择几何边，请重新进入该功能后再试。"));
        return;
    }
    m_firstEdgeId = -1;
    m_pickMode = PickEdgePair;
    m_summary->setText(tr("请选择第 1 条直边，然后选择与它平行的对应直边。"));
}

bool MeshSmeshPartitionWidget::buildPlaneFromEdgePair(
    int edge1, int edge2, PartitionRule& rule, QString& error) const
{
    rule = PartitionRule();
    error.clear();
    QObject* bridge = geometryBridge();
    if (!bridge) { error = tr("当前无法读取几何模型，请确认几何已正常加载。"); return false; }

    QVariantMap a, b;
    const bool ia = QMetaObject::invokeMethod(bridge, "externalEdgeLine", Qt::DirectConnection,
                                               Q_RETURN_ARG(QVariantMap, a), Q_ARG(int, edge1));
    const bool ib = QMetaObject::invokeMethod(bridge, "externalEdgeLine", Qt::DirectConnection,
                                               Q_RETURN_ARG(QVariantMap, b), Q_ARG(int, edge2));
    if (!ia || !ib || !a.value(QStringLiteral("valid")).toBool() || !b.value(QStringLiteral("valid")).toBool())
    {
        error = tr("该方式只支持直线边。请选择两条平行的对应直边。");
        return false;
    }

    const QVector3D p1 = a.value(QStringLiteral("origin")).value<QVector3D>();
    const QVector3D p2 = b.value(QStringLiteral("origin")).value<QVector3D>();
    QVector3D d1 = a.value(QStringLiteral("direction")).value<QVector3D>();
    QVector3D d2 = b.value(QStringLiteral("direction")).value<QVector3D>();
    if (d1.lengthSquared() <= 1.0e-16f || d2.lengthSquared() <= 1.0e-16f)
    {
        error = tr("无法读取所选边的方向，请重新选择。");
        return false;
    }
    d1.normalize(); d2.normalize();
    if (std::abs(double(QVector3D::dotProduct(d1, d2))) < 0.999)
    {
        error = tr("两条边不平行，请重新选择。");
        return false;
    }

    const QVector3D delta = p2 - p1;
    const QVector3D across = delta - d1 * QVector3D::dotProduct(delta, d1);
    if (across.lengthSquared() <= 1.0e-12f)
    {
        error = tr("两条边过于接近，无法确定切面，请重新选择。");
        return false;
    }
    QVector3D normal = QVector3D::crossProduct(d1, across);
    if (normal.lengthSquared() <= 1.0e-16f)
    {
        error = tr("无法根据所选两条边确定切面，请重新选择。");
        return false;
    }
    normal.normalize();
    const float ax = std::abs(normal.x()), ay = std::abs(normal.y()), az = std::abs(normal.z());
    float dominant = normal.x();
    if (ay > ax && ay >= az) dominant = normal.y();
    else if (az > ax && az > ay) dominant = normal.z();
    if (dominant < 0.0f) normal = -normal;

    rule.targetVolumeUiId = m_targetVolume ? m_targetVolume->currentData().toInt() : -1;
    rule.axis = 3;
    rule.origin = (p1 + p2) * 0.5f;
    rule.normal = normal;
    rule.sourceFaceUiId = -1;
    rule.sourceEdgeUiId1 = edge1;
    rule.sourceEdgeUiId2 = edge2;
    return true;
}

void MeshSmeshPartitionWidget::beginRegionPick()
{
    if (planes().isEmpty()) { QMessageBox::information(this, tr("扫掠分块"), tr("请先设置有效切面。")); return; }
    if (m_regions->count() == 0)
        refreshRegionList();
    if (m_regions->count() == 0)
        return;
    QObject* bridge = geometryBridge();
    if (!bridge) return;
    stopPick();
    bool ok = false;
    const QVariantList payload = planes();
    const bool invoked = QMetaObject::invokeMethod(bridge, "beginExternalPartitionRegionPick", Qt::DirectConnection,
                                                    Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(QVariantList, payload));
    if (invoked && ok) m_pickMode = PickRegion;
}

bool MeshSmeshPartitionWidget::event(QEvent* ev)
{
    if (ev && ev->type() == static_cast<QEvent::Type>(QEvent::User + 421) && m_pickMode != PickNone)
    {
        const int dim = property("_geometryExternalPickDim").toInt();
        const int id = property("_geometryExternalPickId").toInt();
        const bool isRegion = property("_geometryExternalPickIsPartitionRegion").toBool();
        setProperty("_geometryExternalPickIsPartitionRegion", false);

        if (m_pickMode == PickTargetVolume && dim == 3 && id > 0)
        {
            const int index = m_targetVolume ? m_targetVolume->findData(id) : -1;
            if (index >= 0)
            {
                m_targetVolume->setCurrentIndex(index);
                m_summary->setText(tr("已选定目标实体 %1").arg(id));
            }
            else
            {
                QMessageBox::information(this, tr("扫掠分块"), tr("所选实体不在当前可分块列表中。"));
            }
        }
        else if (m_pickMode == PickCadPlane && dim == 2 && id > 0)
        {
            QObject* bridge = geometryBridge();
            QVariantMap info;
            const bool invoked = bridge && QMetaObject::invokeMethod(bridge, "externalFacePlane", Qt::DirectConnection,
                                                                      Q_RETURN_ARG(QVariantMap, info), Q_ARG(int, id));
            if (!invoked || !info.value(QStringLiteral("valid")).toBool())
            {
                QMessageBox::information(this, tr("扫掠分块"), tr("所选几何面不是平面，请选择平面。"));
            }
            else
            {
                PartitionRule r;
                r.planUuid = m_planUuid;
                r.targetVolumeUiId = m_targetVolume ? m_targetVolume->currentData().toInt() : -1;
                r.axis = 3;
                r.origin = info.value(QStringLiteral("origin")).value<QVector3D>();
                r.normal = info.value(QStringLiteral("normal")).value<QVector3D>();
                r.sourceFaceUiId = id;
                m_ruleData.push_back(r);
                saveState();
                refreshRuleList();
                invalidateRegionPreview();
                emit logMessage(tr("[扫掠分块] 已使用面 %1 添加切面。").arg(id));
            }
        }
        else if (m_pickMode == PickEdgePair && dim == 1 && id > 0)
        {
            if (m_firstEdgeId <= 0)
            {
                m_firstEdgeId = id;
                m_summary->setText(tr("已选择第 1 条直边 %1，请选择第 2 条平行对应边。").arg(id));
                return true;
            }
            if (id == m_firstEdgeId)
            {
                m_summary->setText(tr("第 2 条边不能与第 1 条相同，请选择另一侧对应直边。"));
                return true;
            }
            PartitionRule r;
            r.planUuid = m_planUuid;
            QString error;
            const int edge1 = m_firstEdgeId;
            if (!buildPlaneFromEdgePair(edge1, id, r, error))
            {
                QMessageBox::information(this, tr("扫掠分块"), error);
                return true;
            }
            m_ruleData.push_back(r);
            saveState();
            refreshRuleList();
            invalidateRegionPreview();
            emit logMessage(tr("[扫掠分块] 已根据边 %1 和边 %2 添加切面。").arg(edge1).arg(id));
        }
        else if (m_pickMode == PickRegion && isRegion && id > 0)
        {
            if (id <= m_regions->count()) m_regions->setCurrentRow(id - 1);
            stopPick();
            showSelectedRegion();
            return true;
        }
        stopPick();
        return true;
    }
    return BasePage::event(ev);
}

void MeshSmeshPartitionWidget::removeSelectedRule()
{
    const int row = m_rules->currentRow();
    if (row < 0 || row >= m_ruleData.size()) return;
    m_ruleData.removeAt(row);
    saveState();
    refreshRuleList();
    invalidateRegionPreview();
    if (m_ruleData.isEmpty())
        showCuttingPlanePreview();
}

void MeshSmeshPartitionWidget::clearRules()
{
    m_ruleData.clear();
    saveState();
    refreshRuleList();
    invalidateRegionPreview();
    restoreCad();
    showCuttingPlanePreview();
}

void MeshSmeshPartitionWidget::showCuttingPlanePreview()
{
    if (!m_selectionPresentation.showingGeometry()) return;
    QObject* bridge = geometryBridge();
    if (!bridge || !m_targetVolume || !m_axis || !m_coordinate) return;
    const int targetVolume = m_targetVolume->currentData().toInt();
    if (m_targetVolume->count() <= 1) return;

    const int selectedRow = m_rules ? m_rules->currentRow() : -1;
    QVariantList visible;
    for (int i = 0; i < m_ruleData.size(); ++i)
    {
        if (i == selectedRow) continue;
        const PartitionRule& r = m_ruleData[i];
        if (!r.enabled) continue;
        const QVector3D n = r.planeNormal();
        if (n.lengthSquared() <= 1.0e-16f) continue;
        QVariantMap m;
        m.insert(QStringLiteral("origin"), QVariant::fromValue(r.planeOrigin()));
        m.insert(QStringLiteral("normal"), QVariant::fromValue(n));
        m.insert(QStringLiteral("targetVolumeUiId"), r.targetVolumeUiId);
        m.insert(QStringLiteral("axis"), r.axis);
        if (r.axis == 4) m.insert(QStringLiteral("radius"), r.coordinate);
        m.insert(QStringLiteral("isDraft"), false);
        visible.push_back(m);
    }

    PartitionRule draft;
    draft.planUuid = m_planUuid;
    draft.targetVolumeUiId = targetVolume;
    draft.axis = m_axis->currentData().toInt();
    draft.coordinate = m_coordinate->value();
    const QVector3D n = draft.planeNormal();
    if (n.lengthSquared() > 1.0e-16f)
    {
        QVariantMap m;
        m.insert(QStringLiteral("origin"), QVariant::fromValue(draft.planeOrigin()));
        m.insert(QStringLiteral("normal"), QVariant::fromValue(n));
        m.insert(QStringLiteral("targetVolumeUiId"), targetVolume);
        m.insert(QStringLiteral("isDraft"), true);
        visible.push_back(m);
    }

    QMetaObject::invokeMethod(bridge, "showExternalPartitionCutters", Qt::DirectConnection,
                              Q_ARG(QVariantList, visible));
}

void MeshSmeshPartitionWidget::previewPartition()
{
    if (auto* view = findChild<QComboBox*>(QStringLiteral("meshSettingsViewSelector")))
        view->setCurrentIndex(0);
    refreshRegionList();
}

void MeshSmeshPartitionWidget::showSavedCutters()
{
    if (!m_selectionPresentation.showingGeometry()) return;
    if (auto* bridge = geometryBridge())
        QMetaObject::invokeMethod(bridge, "showExternalPartitionCutters", Qt::DirectConnection,
                                  Q_ARG(QVariantList, planes()));
}

void MeshSmeshPartitionWidget::invalidateRegionPreview()
{
    const bool hadPreview = m_regions->count() > 0;
    const QSignalBlocker blocker(m_regions);
    m_regions->clear();
    if (hadPreview)
    {
        const auto enabled = std::count_if(m_ruleData.cbegin(), m_ruleData.cend(),
                                           [](const PartitionRule& rule) { return rule.enabled; });
        m_summary->setText(tr("切面 %1，已启用 %2").arg(m_ruleData.size()).arg(enabled));
        restoreCad();
    }
    showSavedCutters();
}

void MeshSmeshPartitionWidget::refreshRegionList()
{
    m_regions->clear();
    const QVariantList payload = planes();
    if (payload.isEmpty())
    {
        m_summary->setText(tr("未设置有效切面"));
        showCuttingPlanePreview();
        return;
    }
    QObject* bridge = geometryBridge();
    int count = 0;
    const bool invoked = bridge && QMetaObject::invokeMethod(bridge, "externalPartitionRegionCount", Qt::DirectConnection,
                                                              Q_RETURN_ARG(int, count), Q_ARG(QVariantList, payload));
    if (!invoked || count <= 0)
    {
        m_summary->setText(tr("切面未穿过实体内部"));
        showCuttingPlanePreview();
        return;
    }
    QVariantList visible;
    int sweepableCount = 0;
    for (int i = 1; i <= count; ++i)
    {
        QString text = tr("分块 %1").arg(i);
        QVariantMap info;
        const bool gotInfo = QMetaObject::invokeMethod(
            bridge, "externalPartitionRegionInfo", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, info), Q_ARG(QVariantList, payload), Q_ARG(int, i));
        const int ownerId = gotInfo ? info.value(QStringLiteral("ownerVolumeId")).toInt() : 0;
        if (ownerId > 0) text += tr("（实体 %1）").arg(ownerId);

        QVariantMap sweepInfo;
        const bool gotSweep = QMetaObject::invokeMethod(
            bridge, "externalAnalyzePartitionSweepRegion", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, sweepInfo), Q_ARG(QVariantList, payload), Q_ARG(int, i));
        const bool sweepable = gotSweep && sweepInfo.value(QStringLiteral("sweepable")).toBool();
        if (sweepable) ++sweepableCount;

        auto* item = new QListWidgetItem(
            (sweepable ? tr("[可扫掠] ") : tr("[需继续切] ")) + text, m_regions);
        item->setForeground(QBrush(sweepable ? QColor(22, 130, 56) : QColor(196, 68, 28)));
        visible.push_back(i);
    }
    if (bridge)
        QMetaObject::invokeMethod(bridge, "showExternalPartitionRegions", Qt::DirectConnection,
                                  Q_ARG(QVariantList, payload), Q_ARG(QVariantList, visible), Q_ARG(int, 0));
    const int remaining = count - sweepableCount;
    if (remaining == 0)
        m_summary->setText(tr("共 %1 块：全部可扫掠").arg(count));
    else
        m_summary->setText(tr("共 %1 块：%2 块可扫掠，%3 块需继续切分")
                               .arg(count).arg(sweepableCount).arg(remaining));
    emit logMessage(tr("[扫掠分块] 共 %1 块（%2 块可扫掠，%3 块需继续切分）。")
                        .arg(count).arg(sweepableCount).arg(remaining));
}

void MeshSmeshPartitionWidget::showSelectedRegion()
{
    const int row = m_regions->currentRow();
    if (row < 0) return;
    const int region = row + 1;
    QObject* bridge = geometryBridge();
    const QVariantList payload = planes();
    if (!bridge || payload.isEmpty()) return;
    QVariantList visible; visible.push_back(region);
    QMetaObject::invokeMethod(bridge, "showExternalPartitionRegions", Qt::DirectConnection,
                              Q_ARG(QVariantList, payload), Q_ARG(QVariantList, visible), Q_ARG(int, region));
}

void MeshSmeshPartitionWidget::restoreCad()
{
    QObject* bridge = geometryBridge();
    if (bridge) QMetaObject::invokeMethod(bridge, "clearExternalPartitionRegionPreview", Qt::DirectConnection);
}

void MeshSmeshPartitionWidget::stopPick()
{
    QObject* bridge = geometryBridge();
    if (bridge) QMetaObject::invokeMethod(bridge, "stopExternalEntityPick", Qt::DirectConnection, Q_ARG(QObject*, this));
    m_pickMode = PickNone;
    m_firstEdgeId = -1;
}

QJsonObject MeshSmeshPartitionWidget::saveJson()
{ return {{"rules", partitionRulesToJson(m_ruleData)}}; }
void MeshSmeshPartitionWidget::loadJson(const QJsonObject& obj)
{
    m_ruleData = partitionRulesFromJson(obj.value("rules").toArray());
    if (!m_planUuid.isEmpty()) replacePartitionRulesForPlan(m_planUuid,m_ruleData);
    refreshRuleList();
}
