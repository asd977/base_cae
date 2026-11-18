#include "MeshCompactDoubleSpinBox.h"
#include "MeshSmeshPartitionWidget.h"
#include "core/MeshSettingsUi.h"

#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <services/MeshModuleControllerService.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVariantMap>
#include <QVBoxLayout>
#include <cmath>

using namespace MeshSmeshWorkflowState;

MeshSmeshPartitionWidget::MeshSmeshPartitionWidget(QWidget* parent)
    : BasePage("MeshSmeshPartitionWidget")
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
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
        tr("只做手动分块：先选要切的实体，再选 X/Y/Z 方向和坐标。"
           "切面只作用于这个实体，不会切到其它实体，也不会修改原始几何。"), this);
    hint->setWordWrap(true);
    MeshSettingsUi::hideExplanation(hint);
    MeshSettingsUi::hideExplanation(m_ownerLabel);
    root->addWidget(hint);

    auto* form = new QFormLayout();
    m_targetVolume = new QComboBox(this);
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
    m_coordinate = new MeshCompactDoubleSpinBox(this);
    m_coordinate->setRange(-1.0e12, 1.0e12);
    m_coordinate->setDecimals(8);
    m_coordinate->setSingleStep(0.001); // CAD units: fine adjustment for wheel and arrow buttons.
    m_coordinate->setToolTip(tr("滚轮/上下箭头每次调整 0.001 个 CAD 坐标单位；可直接输入精确坐标。"));
    form->addRow(tr("切面方向"), m_axis);
    form->addRow(tr("切面位置"), m_coordinate);
    root->addLayout(form);

    auto* addRow = new QHBoxLayout();
    auto* center = new QPushButton(tr("放到实体中心"), this);
    auto* addAxis = new QPushButton(tr("添加切面"), this);
    auto* updateAxis = new QPushButton(tr("更新选中"), this);
    addRow->addWidget(center);
    addRow->addWidget(addAxis);
    addRow->addWidget(updateAxis);
    addRow->addStretch(1);
    root->addLayout(addRow);
    auto* otherCuts=new QHBoxLayout();
    auto* cadPlane=new QPushButton(tr("采用几何平面"),this);
    auto* edgePair=new QPushButton(tr("两边确定切面"),this);
    otherCuts->addWidget(cadPlane);otherCuts->addWidget(edgePair);root->addLayout(otherCuts);
    connect(cadPlane,&QPushButton::clicked,this,&MeshSmeshPartitionWidget::beginCadPlanePick);
    connect(edgePair,&QPushButton::clicked,this,&MeshSmeshPartitionWidget::beginEdgePairPick);

    m_rules = new QListWidget(this);
    m_rules->setMinimumHeight(120);
    root->addWidget(m_rules);

    auto* editRow = new QHBoxLayout();
    auto* remove = new QPushButton(tr("删除选中"), this);
    auto* clear = new QPushButton(tr("清空"), this);
    m_preview = new QPushButton(tr("预览分块"), this);
    editRow->addWidget(remove);
    editRow->addWidget(clear);
    editRow->addStretch(1);
    editRow->addWidget(m_preview);
    root->addLayout(editRow);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    m_regions = new QListWidget(this);
    m_regions->setMinimumHeight(100);
    root->addWidget(m_regions);

    auto* regionRow = new QHBoxLayout();
    auto* pickRegion = new QPushButton(tr("图形区选择分块"), this);
    auto* showRegion = new QPushButton(tr("突出选中分块"), this);
    auto* restore = new QPushButton(tr("恢复普通显示"), this);
    regionRow->addWidget(pickRegion);
    regionRow->addWidget(showRegion);
    regionRow->addWidget(restore);
    regionRow->addStretch(1);
    root->addLayout(regionRow);
    root->addStretch(1);

    connect(pickTarget, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::beginTargetVolumePick);
    connect(center, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::snapCoordinateToTargetCenter);
    connect(addAxis, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::addAxisRule);
    connect(updateAxis, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::updateSelectedAxisRule);
    connect(m_targetVolume, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
    {
        snapCoordinateToTargetCenter();
        showCuttingPlanePreview();
        m_selectionPresentation.setSelectedEntities(3, {m_targetVolume->currentData().toInt()});
    });
    connect(m_axis, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
    {
        snapCoordinateToTargetCenter();
        showCuttingPlanePreview();
    });
    connect(m_coordinate, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double)
    {
        showCuttingPlanePreview();
    });
    connect(remove, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::removeSelectedRule);
    connect(clear, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::clearRules);
    connect(m_preview, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::previewPartition);
    connect(pickRegion, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::beginRegionPick);
    connect(showRegion, &QPushButton::clicked, this, &MeshSmeshPartitionWidget::showSelectedRegion);
    // Selecting a Region in the list should immediately show which solid it is.
    connect(m_regions, &QListWidget::itemSelectionChanged, this, [this]() {
        // During viewport picking the pick controller still owns the temporary
        // props; stopPick() must run before replacing those props.
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
        refreshRegionList();
        emit logMessage(tr("[扫掠分块] 切面 %1 已%2。")
                        .arg(row + 1).arg(enabled ? tr("启用") : tr("停用")));
    });

    rebuildTargetVolumes();
    loadState();
    snapCoordinateToTargetCenter();
    showCuttingPlanePreview();
}

MeshSmeshPartitionWidget::~MeshSmeshPartitionWidget()
{
    stopPick();
    restoreCad();
}

void MeshSmeshPartitionWidget::setMeshPlanIdentity(const QString& uuid, const QString& displayName)
{
    m_planUuid = uuid.trimmed();
    m_planName = displayName.trimmed();
    setProperty("meshPlanUuid", m_planUuid);
    if (m_ownerLabel)
        m_ownerLabel->setText(tr("当前网格：%1。分块仅用于准备扫掠区域。")
            .arg(m_planName.isEmpty() ? tr("网格") : m_planName));
    rebuildTargetVolumes();
    replacePartitionRulesForPlan(m_planUuid,m_ruleData);
    snapCoordinateToTargetCenter();
    refreshRegionList();
    m_selectionPresentation.setSelectedEntities(3, {m_targetVolume->currentData().toInt()});
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
    const int oldId = m_targetVolume->currentData().toInt();
    m_targetVolume->blockSignals(true);
    m_targetVolume->clear();
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
    int index = oldId > 0 ? m_targetVolume->findData(oldId) : -1;
    if (index < 0 && m_targetVolume->count() > 0) index = 0;
    if (index >= 0) m_targetVolume->setCurrentIndex(index);
    m_targetVolume->blockSignals(false);
}

void MeshSmeshPartitionWidget::snapCoordinateToTargetCenter()
{
    if (!m_targetVolume || !m_axis || !m_coordinate) return;
    const int id = m_targetVolume->currentData().toInt();
    if (id <= 0) return;
    auto* svc = ServiceRegistry::instance().getService<PreProcessingService>();
    QVector3D center;
    if (!svc || !svc->entityCenter(3, id, center)) return;
    const int axis = m_axis->currentData().toInt();
    const double value = axis == 0 ? center.x() : (axis == 1 ? center.y() : center.z());
    m_coordinate->setValue(value);
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
    m_selectionPresentation.enter();
    rebuildTargetVolumes();
    loadState();
    snapCoordinateToTargetCenter();
    refreshRegionList();
    m_selectionPresentation.setSelectedEntities(3, {m_targetVolume->currentData().toInt()});
    MeshSettingsUi::simplify(this);
}

void MeshSmeshPartitionWidget::beforeUnload()
{
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
    replacePartitionRulesForPlan(m_planUuid, m_ruleData);
    // Every sweep page sharing this parent mesh must discard transient region IDs.
    emit partitionChanged();
    emit logMessage(tr("扫掠分块已修改，原扫掠区域列表已清空，请在新分块上重新选择。"));
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
            text = tr("%1. %2%3 = %4").arg(i + 1).arg(targetText).arg(axis).arg(r.coordinate, 0, 'g', 10);
        }
        else
        {
            if (r.sourceEdgeUiId1 > 0 && r.sourceEdgeUiId2 > 0)
            {
                text = tr("%1. %2两边确定切面：E%3 + E%4")
                    .arg(i + 1).arg(targetText).arg(r.sourceEdgeUiId1).arg(r.sourceEdgeUiId2);
            }
            else
            {
                text = tr("%1. %2选取平面：F%3")
                    .arg(i + 1).arg(targetText).arg(r.sourceFaceUiId);
            }
        }
        auto* item = new QListWidgetItem(text, m_rules);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(r.enabled ? tr("已启用：本次分块会使用该切面")
                                   : tr("已停用：保留切面，但本次不参与分块"));
    }
    m_loadingRules = false;
    if (m_ruleData.isEmpty())
        m_summary->setText(tr("未设置切面"));
    else
        m_summary->setText(tr("切面 %1，已启用 %2")
                           .arg(m_ruleData.size()).arg(enabledCount));
}

void MeshSmeshPartitionWidget::addAxisRule()
{
    const int targetVolume = m_targetVolume ? m_targetVolume->currentData().toInt() : -1;
    if (targetVolume <= 0)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("请先选择要分块的实体。"));
        return;
    }
    PartitionRule r;
    r.planUuid = m_planUuid;
    r.targetVolumeUiId = targetVolume;
    r.axis = m_axis->currentData().toInt();
    r.coordinate = m_coordinate->value();
    m_ruleData.push_back(r);
    saveState();
    refreshRuleList();
    refreshRegionList();
    emit logMessage(tr("[扫掠分块] 已添加切面。"));
}

void MeshSmeshPartitionWidget::loadSelectedRuleToEditors()
{
    if (m_loadingRules || !m_rules || !m_targetVolume || !m_axis || !m_coordinate)
        return;
    const int row = m_rules->currentRow();
    if (row < 0 || row >= m_ruleData.size()) return;
    const PartitionRule& r = m_ruleData[row];
    if (r.axis < 0 || r.axis > 2)
    {
        m_summary->setText(tr("这是旧版平面/两边切面规则。当前简化版不再编辑这种规则；建议删除后用“目标实体 + X/Y/Z + 切面位置”重新添加。"));
        return;
    }

    const QSignalBlocker blockTarget(m_targetVolume);
    const QSignalBlocker blockAxis(m_axis);
    const int targetIndex = m_targetVolume->findData(r.targetVolumeUiId);
    if (targetIndex >= 0) m_targetVolume->setCurrentIndex(targetIndex);
    const int axisIndex = m_axis->findData(r.axis);
    if (axisIndex >= 0) m_axis->setCurrentIndex(axisIndex);
    m_coordinate->setValue(r.coordinate);
    m_summary->setText(tr("切面 %1").arg(row + 1));
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
    if (targetVolume <= 0)
    {
        QMessageBox::information(this, tr("扫掠分块"), tr("请先选择要分块的实体。"));
        return;
    }

    PartitionRule& r = m_ruleData[row];
    r.targetVolumeUiId = targetVolume;
    r.axis = m_axis->currentData().toInt();
    r.coordinate = m_coordinate->value();
    r.origin = QVector3D();
    r.normal = QVector3D();
    r.sourceFaceUiId = -1;
    r.sourceEdgeUiId1 = -1;
    r.sourceEdgeUiId2 = -1;
    r.enabled = true;
    saveState();
    refreshRuleList();
    m_rules->setCurrentRow(row);
    refreshRegionList();
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
                snapCoordinateToTargetCenter();
                m_summary->setText(tr("实体 %1").arg(id));
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
                refreshRegionList();
                emit logMessage(tr("[扫掠分块] 已使用面 %1 添加切面。") .arg(id));
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
            refreshRegionList();
            emit logMessage(tr("[扫掠分块] 已根据边 %1 和边 %2 添加切面。").arg(edge1).arg(id));
        }
        else if (m_pickMode == PickRegion && isRegion && id > 0)
        {
            if (id <= m_regions->count()) m_regions->setCurrentRow(id - 1);
            // stopExternalEntityPick() restores the original CAD display, so stop
            // the picking session first and then re-apply the selected Region preview.
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
    refreshRegionList();
}

void MeshSmeshPartitionWidget::clearRules()
{
    m_ruleData.clear();
    saveState();
    refreshRuleList();
    refreshRegionList();
    restoreCad();
}

void MeshSmeshPartitionWidget::showCuttingPlanePreview()
{
    QObject* bridge = geometryBridge();
    if (!bridge || !m_targetVolume || !m_axis || !m_coordinate) return;
    const int targetVolume = m_targetVolume->currentData().toInt();
    if (targetVolume <= 0) return;

    PartitionRule draft;
    draft.planUuid = m_planUuid;
    draft.targetVolumeUiId = targetVolume;
    draft.axis = m_axis->currentData().toInt();
    draft.coordinate = m_coordinate->value();
    QVariantList visible;
    const QVector3D n = draft.planeNormal();
    if (n.lengthSquared() > 1.0e-16f)
    {
        QVariantMap m;
        m.insert(QStringLiteral("origin"), QVariant::fromValue(draft.planeOrigin()));
        m.insert(QStringLiteral("normal"), QVariant::fromValue(n));
        m.insert(QStringLiteral("targetVolumeUiId"), targetVolume);
        visible.push_back(m);
    }
    QMetaObject::invokeMethod(bridge, "showExternalPartitionCutters", Qt::DirectConnection,
                              Q_ARG(QVariantList, visible));
}

void MeshSmeshPartitionWidget::previewPartition()
{
    refreshRegionList();
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
    for (int i = 1; i <= count; ++i)
    {
        QString text = tr("分块 %1").arg(i);
        QVariantMap info;
        const bool gotInfo = QMetaObject::invokeMethod(
            bridge, "externalPartitionRegionInfo", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, info), Q_ARG(QVariantList, payload), Q_ARG(int, i));
        const int ownerId = gotInfo ? info.value(QStringLiteral("ownerVolumeId")).toInt() : 0;
        if (ownerId > 0) text += tr("（来自实体 %1）").arg(ownerId);
        m_regions->addItem(text);
        visible.push_back(i);
    }
    if (bridge)
        QMetaObject::invokeMethod(bridge, "showExternalPartitionRegions", Qt::DirectConnection,
                                  Q_ARG(QVariantList, payload), Q_ARG(QVariantList, visible), Q_ARG(int, 0));
    m_summary->setText(tr("区域 %1") .arg(count));
    emit logMessage(tr("[扫掠分块] 已生成 %1 个分块。") .arg(count));
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
