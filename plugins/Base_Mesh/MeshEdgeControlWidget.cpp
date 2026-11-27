#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include "core/MeshEntityIdParser.h"
#include "core/MeshSettingsUi.h"
#include "core/MeshSmeshWorkflowState.h"

#include <QColor>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <algorithm>


MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget* parent)
    : BasePage("MeshEdgeControlWidget"), ui(new Ui::MeshEdgeControlWidget),
      m_highlightPropId("mesh.edge.control.highlight")
{
    ui->setupUi(this);
    setupUiState();
    ui->verticalLayout->insertWidget(1, m_selectionPresentation.createViewSelector(this));
    MeshSettingsUi::hideExplanation(ui->checkReferenceMesh);

    connect(ui->checkReferenceMesh, &QCheckBox::toggled, this,
            [this](bool on) { m_selectionPresentation.setReferenceMeshVisible(on); });

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAddControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshEdgeControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this,
            &MeshEdgeControlWidget::onSelectionChanged);
    connect(ui->editEdgeIds, &QLineEdit::textChanged, this,
            &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinDivisions, qOverload<int>(&QSpinBox::valueChanged), this,
            &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this,
            &MeshEdgeControlWidget::onAppendSelectedEntity);
    auto* entityCombo = qobject_cast<CustomComboBox*>(ui->cmbEntity);
    if (entityCombo)
    {
        connect(entityCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    const int id = ui->cmbEntity->itemData(row).toInt();
                    if (id <= 0)
                    {
                        clearHighlight();
                        return;
                    }
                    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
                        svc->highlightEntities(1, QList<int>{id}, true);
                });
        connect(entityCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
                        svc->clearEntityHoverHighlight();
                    onSelectionChanged();
                });
    }
}

bool MeshEdgeControlWidget::event(QEvent* ev)
{
    if (ev && ev->type() == static_cast<QEvent::Type>(QEvent::User + 421))
    {
        const int dim = property("_geometryExternalPickDim").toInt();
        const int id = property("_geometryExternalPickId").toInt();
        const bool isEdge = property("_geometryExternalPickIsPartitionEdge").toBool();
        const int region = property("_geometryExternalPickRegionOrdinal").toInt();
        const bool append = property("_geometryExternalPickAppend").toBool();
        if (m_partitionPickStage == PickCadEdge && !isEdge && dim == 1 && id > 0)
        {
            finishPartitionEdgePick();
            const bool accepted = appendCadEdge(id);
            if (accepted && append)
                QTimer::singleShot(0, this, [this]() { beginPartitionEdgePick(); });
            return true;
        }
        if (m_partitionPickStage == PickPartitionEdge && isEdge && dim == 1 && id > 0 && region > 0)
        {
            QVariantMap info;
            QObject* bridge = geometryBridgeObject();
            const bool got = bridge && QMetaObject::invokeMethod(
                bridge, "externalPartitionEdgeInfo", Qt::DirectConnection,
                Q_RETURN_ARG(QVariantMap, info), Q_ARG(QVariantList, partitionPlanes()),
                Q_ARG(int, region), Q_ARG(int, id));
            if (!got || !info.value(QStringLiteral("valid")).toBool())
            {
                finishPartitionEdgePick();
                QMessageBox::information(this, tr("边尺寸"), tr("无法读取所选边，请重新选择。"));
                return true;
            }
            togglePartitionEdgeRef(region, id);
            finishPartitionEdgePick();
            if (append)
                beginPartitionEdgePick();
            else
                showPartitionEdgeSelection();
            return true;
        }
    }
    return BasePage::event(ev);
}

void MeshEdgeControlWidget::setOwnerTask(const QString& kind, const QString& uuid, const QString& displayName)
{
    m_ownerTaskKind = kind.trimmed();
    m_ownerTaskUuid = uuid.trimmed();
    m_ownerTaskName = displayName.trimmed();

    QLabel* label = findChild<QLabel*>(QStringLiteral("meshControlOwnerLabel"));
    if (!label)
    {
        label = new QLabel(this);
        label->setObjectName(QStringLiteral("meshControlOwnerLabel"));
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color: palette(mid);"));
        ui->verticalLayout->insertWidget(1, label);
    }
    label->setText(tr("在图形区点选 CAD 边；Ctrl+单击可连续添加。网格仅供参考，不参与边拾取。"));
    updateEntityUiState();
    refreshButtons();
}

MeshEdgeControlWidget::~MeshEdgeControlWidget()
{
    beforeUnload();
    delete ui;
}
void MeshEdgeControlWidget::beforeLoad()
{
    // Mesh controls always target CAD geometry. Existing mesh results stay in
    // memory but are hidden by default; users can opt in to a non-pickable
    // translucent reference mesh when they really need it.
    m_selectionPresentation.enter();
    MeshSettingsUi::simplify(this);
    rebuildEdgeCandidates();
    loadRulesFromService();
    if (m_items.isEmpty())
        onAddControl();
}
void MeshEdgeControlWidget::beforeUnload()
{
    finishPartitionEdgePick();
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
    m_selectionPresentation.leave();
}

void MeshEdgeControlWidget::onAddControl()
{
    EdgeControlItem item;
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    const int row = m_items.size() - 1;
    ui->tableControls->selectRow(row);
    fillFormData(item);
    onSelectionChanged();
    refreshButtons();
}
void MeshEdgeControlWidget::onDeleteControl()
{
    int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    m_items.removeAt(row);
    ui->tableControls->removeRow(row);
    syncRulesToService();
    onResetForm();
}
void MeshEdgeControlWidget::onResetForm()
{
    ui->tableControls->clearSelection();
    const QSignalBlocker blockIds(ui->editEdgeIds);
    const QSignalBlocker blockDivisions(ui->spinDivisions);
    ui->editEdgeIds->clear();
    ui->spinDivisions->setValue(10);
    clearHighlight();
    updatePartitionRefLabel();
    refreshButtons();
    if (m_partitionPickStage == PickPartitionNone) showPartitionEdgeSelection();
}
void MeshEdgeControlWidget::onSelectionChanged()
{
    if (m_partitionPickStage != PickPartitionNone)
        finishPartitionEdgePick();
    int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
        fillFormData(m_items[row]);
    QSet<int> selectedIds;
    QString err;
    if (row >= 0 && row < m_items.size())
        MeshEntityIdParser::parseEntityIds(m_items[row].edgeIds, selectedIds, err);
    if (!selectedIds.isEmpty())
    {
        QList<int> v = selectedIds.values();
        std::sort(v.begin(), v.end());
        if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
            m_selectionPresentation.setSelectedEntities(1, v);
    }
    else
        clearHighlight();
    refreshButtons();
    if (m_partitionPickStage == PickPartitionNone)
        showPartitionEdgeSelection();
}
void MeshEdgeControlWidget::onParameterEdited()
{
    int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
    {
        QString err;
        if (validateInput(err, row))
        {
            auto item = collectFormData();
            m_items[row] = item;
            appendOrUpdateTableRow(row, item);
            syncRulesToService();
        }
    }
    refreshButtons();
}

void MeshEdgeControlWidget::onAppendSelectedEntity()
{
    appendCadEdge(ui->cmbEntity->currentData().toInt());
}

bool MeshEdgeControlWidget::appendCadEdge(int id)
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size() || id <= 0)
        return false;
    if (!m_entityIds.contains(QString::number(id)))
    {
        QMessageBox::warning(this, tr("边尺寸"), tr("所选边不属于当前 CAD 几何。"));
        return false;
    }
    QSet<int> ids;
    QString error;
    if (!m_items[row].edgeIds.trimmed().isEmpty() &&
        !MeshEntityIdParser::parseEntityIds(m_items[row].edgeIds, ids, error))
    {
        QMessageBox::warning(this, tr("边尺寸"), error);
        return false;
    }
    if (ids.contains(id))
        return true;
    ids.insert(id);
    QList<int> sortedIds = ids.values();
    std::sort(sortedIds.begin(), sortedIds.end());
    QStringList parts;
    for (int edgeId : sortedIds) parts << QString::number(edgeId);

    const QString oldIds = ui->editEdgeIds->text();
    {
        const QSignalBlocker blocker(ui->editEdgeIds);
        ui->editEdgeIds->setText(parts.join(QLatin1Char(',')));
    }
    if (!validateInput(error, row))
    {
        const QSignalBlocker blocker(ui->editEdgeIds);
        ui->editEdgeIds->setText(oldIds);
        QMessageBox::warning(this, tr("边尺寸"), error);
        return false;
    }
    m_items[row].edgeIds = ui->editEdgeIds->text();
    appendOrUpdateTableRow(row, m_items[row]);
    syncRulesToService();
    onSelectionChanged();
    return true;
}

void MeshEdgeControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(2);
    ui->tableControls->setHorizontalHeaderLabels({tr("边"), tr("分段设置")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->spinDivisions->setToolTip(tr("设置所选边的目标分段数。数值越大，该边附近的网格越细。"));
    ui->labelDivisions->setText(tr("目标分段数"));
    ui->labelDivisions->setToolTip(ui->spinDivisions->toolTip());
    m_partitionPickCaption = new QLabel(tr("图形区选择"), ui->groupBoxInput);
    auto* row = new QHBoxLayout();
    m_pickPartitionEdge = new QPushButton(tr("选择边"), ui->groupBoxInput);
    m_clearPartitionEdges = new QPushButton(tr("清空"), ui->groupBoxInput);
    row->addWidget(m_pickPartitionEdge);
    row->addWidget(m_clearPartitionEdges);
    ui->gridLayout->addWidget(m_partitionPickCaption, 5, 0);
    ui->gridLayout->addLayout(row, 5, 1);
    m_partitionRefLabel = new QLabel(tr("未选择"), ui->groupBoxInput);
    m_partitionRefLabel->setWordWrap(true);
    m_partitionRefLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    ui->gridLayout->addWidget(m_partitionRefLabel, 6, 0, 1, 2);
    connect(m_pickPartitionEdge, &QPushButton::clicked, this, &MeshEdgeControlWidget::beginPartitionEdgePick);
    connect(m_clearPartitionEdges, &QPushButton::clicked, this, &MeshEdgeControlWidget::clearPartitionEdgeRefs);
    onResetForm();
    rebuildEdgeCandidates();
}
bool MeshEdgeControlWidget::validateInput(QString& err, int ignoreRow) const
{
    const int row = ui->tableControls->currentRow();
    const QString partitionRefs = (row >= 0 && row < m_items.size())
        ? normalizedPartitionRefs(m_items[row].partitionEdgeRefs) : QString();
    const QString edgeIds = ui->editEdgeIds->text().trimmed();
    if (edgeIds.isEmpty() && partitionRefs.isEmpty())
    {
        err = tr("请先选择原始边或分块边。");
        return false;
    }
    QSet<int> currentIds;
    if (!edgeIds.isEmpty() && !MeshEntityIdParser::parseEntityIds(edgeIds, currentIds, err))
        return false;
    for (int i = 0; i < m_items.size(); ++i)
    {
        if (i == ignoreRow)
            continue;
        QSet<int> existingIds;
        QString parseErr;
        if (!MeshEntityIdParser::parseEntityIds(m_items[i].edgeIds, existingIds, parseErr))
            continue;
        QSet<int> overlap = currentIds & existingIds;
        if (!overlap.isEmpty())
        {
            QList<int> ids = overlap.values();
            std::sort(ids.begin(), ids.end());
            QStringList overlapStr;
            for (int id : ids)
                overlapStr << QString::number(id);
            err = tr("边 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。")
                      .arg(i + 1)
                      .arg(overlapStr.join(','));
            return false;
        }
    }
    if (!partitionRefs.isEmpty())
    {
        QSet<QString> currentRefs;
        for (const QString& ref : partitionRefs.split(QLatin1Char(','), Qt::SkipEmptyParts)) currentRefs.insert(ref);
        for (int i = 0; i < m_items.size(); ++i)
        {
            if (i == ignoreRow) continue;
            const QString other = normalizedPartitionRefs(m_items[i].partitionEdgeRefs);
            for (const QString& ref : other.split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                if (currentRefs.contains(ref))
                {
                    err = tr("分块边 %1 已在第 %2 条规则中设置。").arg(ref).arg(i + 1);
                    return false;
                }
            }
        }
    }
    return true;
}
MeshEdgeControlWidget::EdgeControlItem MeshEdgeControlWidget::collectFormData() const
{
    EdgeControlItem i;
    i.edgeIds = ui->editEdgeIds->text().trimmed();
    const int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size()) i.partitionEdgeRefs = normalizedPartitionRefs(m_items[row].partitionEdgeRefs);
    i.divisions = ui->spinDivisions->value();
    i.bias = 1.0;
    return i;
}
void MeshEdgeControlWidget::fillFormData(const EdgeControlItem& i)
{
    // Switching rule rows must not write intermediate form values back to the model.
    const QSignalBlocker blockIds(ui->editEdgeIds);
    const QSignalBlocker blockDivisions(ui->spinDivisions);
    ui->editEdgeIds->setText(i.edgeIds);
    ui->spinDivisions->setValue(i.divisions);
    updatePartitionRefLabel();
}
void MeshEdgeControlWidget::appendOrUpdateTableRow(int row, const EdgeControlItem& i)
{
    const QString summary = tr("目标分段:%1").arg(i.divisions);
    QStringList targets;
    if (!i.edgeIds.trimmed().isEmpty())
        targets << (!partitionPlanes().isEmpty()
            ? tr("原始边 %1（分块后不生效）").arg(i.edgeIds.trimmed())
            : i.edgeIds.trimmed());
    const QStringList partitionRefs = normalizedPartitionRefs(i.partitionEdgeRefs)
        .split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (!partitionRefs.isEmpty())
        targets << tr("已选 %1 条边").arg(partitionRefs.size());
    const QString targetText = targets.join(QStringLiteral("; "));
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(targetText);
        ui->tableControls->item(row, 1)->setText(summary);
        return;
    }
    int r = ui->tableControls->rowCount();
    ui->tableControls->insertRow(r);
    ui->tableControls->setItem(r, 0, new QTableWidgetItem(targetText));
    ui->tableControls->setItem(r, 1, new QTableWidgetItem(summary));
}
void MeshEdgeControlWidget::refreshButtons()
{
    const bool sel = ui->tableControls->currentRow() >= 0;
    const bool partitioned = !partitionPlanes().isEmpty();
    ui->btnAdd->setEnabled(true);
    ui->btnDelete->setEnabled(sel);
    if (m_pickPartitionEdge) m_pickPartitionEdge->setEnabled(sel && (partitioned || !m_entityIds.isEmpty()));
    if (!sel && m_clearPartitionEdges) m_clearPartitionEdges->setEnabled(false);
}
void MeshEdgeControlWidget::rebuildEdgeCandidates()
{
    ui->cmbEntity->clear();
    m_entityIds.clear();
    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
        m_entityIds = svc->listEntityIds(1);
    for (const auto& id : m_entityIds)
        ui->cmbEntity->addItem(tr("边 #%1").arg(id), id.toInt());
    updateEntityUiState();
    onSelectionChanged();
}
void MeshEdgeControlWidget::clearHighlight()
{
    m_selectionPresentation.setSelectedEntities(1, {});
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
}
void MeshEdgeControlWidget::updateEntityUiState()
{
    const bool has = !m_entityIds.isEmpty();
    const bool partitioned = !partitionPlanes().isEmpty();

    // Once a manual partition exists, raw CAD edge IDs are no longer the user's
    // working topology. Hiding those disabled fields is clearer than asking users
    // to understand why an old Edge #14 cannot be used on the partition result.
    ui->labelEdgeIds->setVisible(!partitioned);
    ui->cmbEntity->setVisible(!partitioned);
    ui->btnAppendEntity->setVisible(!partitioned);
    ui->label->setVisible(!partitioned);
    ui->editEdgeIds->setVisible(!partitioned);
    ui->cmbEntity->setEnabled(has && !partitioned);
    ui->btnAppendEntity->setEnabled(has && !partitioned);
    ui->editEdgeIds->setEnabled(!partitioned);

    if (m_partitionPickCaption) m_partitionPickCaption->setVisible(true);
    if (m_pickPartitionEdge) {
        m_pickPartitionEdge->setVisible(true);
        m_pickPartitionEdge->setText(partitioned ? tr("选择分块边") : tr("图形区点选"));
    }
    if (m_clearPartitionEdges) m_clearPartitionEdges->setVisible(partitioned);
    if (m_partitionRefLabel) m_partitionRefLabel->setVisible(partitioned);
}

void MeshEdgeControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc || m_ownerTaskKind.isEmpty())
        return;

    auto rules = svc->edgeControlRules();
    rules.erase(std::remove_if(rules.begin(), rules.end(), [this](const EdgeControlRule& r)
    {
        if (!m_ownerTaskUuid.isEmpty() && !r.planUuid.isEmpty())
            return r.planUuid == m_ownerTaskUuid;
        if (!m_ownerTaskUuid.isEmpty() && !r.ownerTaskUuid.isEmpty())
            return r.ownerTaskUuid == m_ownerTaskUuid;
        if (r.ownerTaskKind.isEmpty())
            return m_ownerTaskKind == QStringLiteral("MeshRoot");
        return r.ownerTaskKind == m_ownerTaskKind;
    }), rules.end());

    for (const auto& it : m_items)
    {
        if (it.edgeIds.trimmed().isEmpty() && normalizedPartitionRefs(it.partitionEdgeRefs).isEmpty())
            continue;
        EdgeControlRule r;
        r.planUuid = m_ownerTaskUuid;
        r.ownerTaskKind = m_ownerTaskKind;
        r.ownerTaskUuid = m_ownerTaskUuid;
        r.edgeIds = it.edgeIds;
        r.partitionEdgeRefs = normalizedPartitionRefs(it.partitionEdgeRefs);
        r.divisions = it.divisions;
        r.bias = it.bias;
        rules.push_back(r);
    }
    svc->setEdgeControlRules(rules);
    m_selectionPresentation.refresh();
}

void MeshEdgeControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc)
        return;
    const auto rules = svc->edgeControlRules();
    m_items.clear();
    ui->tableControls->setRowCount(0);
    for (const auto& r : rules)
    {
        bool belongs = false;
        if (!m_ownerTaskUuid.isEmpty() && !r.planUuid.isEmpty())
            belongs = (r.planUuid == m_ownerTaskUuid);
        else if (!m_ownerTaskUuid.isEmpty() && !r.ownerTaskUuid.isEmpty())
            belongs = (r.ownerTaskUuid == m_ownerTaskUuid);
        else if (r.ownerTaskKind.isEmpty())
            belongs = (m_ownerTaskKind == QStringLiteral("MeshRoot"));
        else
            belongs = (r.ownerTaskKind == m_ownerTaskKind);
        if (!belongs)
            continue;

        EdgeControlItem i;
        i.edgeIds = r.edgeIds;
        i.partitionEdgeRefs = normalizedPartitionRefs(r.partitionEdgeRefs);
        i.divisions = r.divisions;
        i.bias = r.bias;
        m_items.push_back(i);
        appendOrUpdateTableRow(ui->tableControls->rowCount(), i);
    }
    if (!m_items.isEmpty())
        ui->tableControls->selectRow(0);
    else
        refreshButtons();
}

QObject* MeshEdgeControlWidget::geometryBridgeObject() const
{
    auto* svc = ServiceRegistry::instance().getService<PreProcessingService>();
    return svc ? dynamic_cast<QObject*>(svc) : nullptr;
}

QVariantList MeshEdgeControlWidget::partitionPlanes() const
{
    return MeshSmeshWorkflowState::planePayload(
        MeshSmeshWorkflowState::partitionRulesForPlan(m_ownerTaskUuid));
}

QString MeshEdgeControlWidget::normalizedPartitionRefs(const QString& text)
{
    QSet<QString> refs;
    for (const QString& raw : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QStringList p = raw.trimmed().split(QLatin1Char(':'));
        if (p.size() != 2) continue;
        bool okR = false, okE = false;
        const int r = p[0].toInt(&okR), e = p[1].toInt(&okE);
        if (okR && okE && r > 0 && e > 0) refs.insert(QStringLiteral("%1:%2").arg(r).arg(e));
    }
    QStringList out = refs.values();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b)
    {
        const QStringList ap = a.split(QLatin1Char(':')), bp = b.split(QLatin1Char(':'));
        const int ar = ap.value(0).toInt(), br = bp.value(0).toInt();
        if (ar != br) return ar < br;
        return ap.value(1).toInt() < bp.value(1).toInt();
    });
    return out.join(QLatin1Char(','));
}

void MeshEdgeControlWidget::beginPartitionEdgePick()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        QMessageBox::information(this, tr("边尺寸"), tr("请先选择要编辑的尺寸规则。"));
        return;
    }
    const QVariantList pp = partitionPlanes();
    if (pp.isEmpty())
    {
        finishPartitionEdgePick();
        m_selectionPresentation.enter(ui->checkReferenceMesh->isChecked());
        QObject* bridge = geometryBridgeObject();
        bool started = false;
        const bool invoked = bridge && QMetaObject::invokeMethod(
            bridge, "beginExternalEntityPick", Qt::DirectConnection,
            Q_RETURN_ARG(bool, started), Q_ARG(QObject*, static_cast<QObject*>(this)),
            Q_ARG(int, 1));
        if (!invoked || !started)
        {
            QMessageBox::information(this, tr("边尺寸"),
                tr("无法进入 CAD 边点选，请确认原始几何已加载并显示。"));
            return;
        }
        m_partitionPickStage = PickCadEdge;
        if (m_pickPartitionEdge) m_pickPartitionEdge->setText(tr("正在点选…"));
        return;
    }
    finishPartitionEdgePick();
    QObject* bridge = geometryBridgeObject();
    bool started = false;
    const QString selectedRefs = normalizedPartitionRefs(m_items[row].partitionEdgeRefs);
    const bool invoked = bridge && QMetaObject::invokeMethod(
        bridge, "beginExternalPartitionEdgeScenePick", Qt::DirectConnection,
        Q_RETURN_ARG(bool, started), Q_ARG(QObject*, static_cast<QObject*>(this)),
        Q_ARG(QVariantList, pp), Q_ARG(QString, selectedRefs));
    if (!invoked || !started)
    {
        QMessageBox::information(this, tr("边尺寸"), tr("无法进入边选择，请检查当前分块是否有效。"));
        return;
    }
    m_partitionPickStage = PickPartitionEdge;
    m_partitionPickRegion = -1;
    m_pickPartitionEdge->setText(tr("正在选择边…"));
}

void MeshEdgeControlWidget::finishPartitionEdgePick()
{
    if (QObject* bridge = geometryBridgeObject())
    {
        QMetaObject::invokeMethod(bridge, "stopExternalEntityPick", Qt::DirectConnection,
                                  Q_ARG(QObject*, static_cast<QObject*>(this)));
        QMetaObject::invokeMethod(bridge, "clearExternalPartitionRegionPreview", Qt::DirectConnection);
    }
    m_partitionPickStage = PickPartitionNone;
    m_partitionPickRegion = -1;
    if (m_pickPartitionEdge) m_pickPartitionEdge->setText(
        partitionPlanes().isEmpty() ? tr("图形区点选") : tr("选择分块边"));
}

void MeshEdgeControlWidget::togglePartitionEdgeRef(int region, int edge)
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    const QString oldRefs = m_items[row].partitionEdgeRefs;
    QSet<QString> refs;
    for (const QString& ref : normalizedPartitionRefs(oldRefs).split(QLatin1Char(','), Qt::SkipEmptyParts)) refs.insert(ref);
    const QString ref = QStringLiteral("%1:%2").arg(region).arg(edge);
    if (refs.contains(ref)) refs.remove(ref); else refs.insert(ref);
    m_items[row].partitionEdgeRefs = normalizedPartitionRefs(QStringList(refs.values()).join(QLatin1Char(',')));
    QString err;
    if (!validateInput(err, row))
    {
        m_items[row].partitionEdgeRefs = oldRefs;
        QMessageBox::warning(this, tr("边尺寸"), err);
        return;
    }
    appendOrUpdateTableRow(row, m_items[row]);
    syncRulesToService();
    updatePartitionRefLabel();
}

void MeshEdgeControlWidget::clearPartitionEdgeRefs()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    m_items[row].partitionEdgeRefs.clear();
    appendOrUpdateTableRow(row, m_items[row]);
    syncRulesToService();
    updatePartitionRefLabel();
    if (m_partitionPickStage == PickPartitionNone) showPartitionEdgeSelection();
}

void MeshEdgeControlWidget::showPartitionEdgeSelection()
{
    QObject* bridge = geometryBridgeObject();
    if (!bridge) return;
    const int row = ui->tableControls->currentRow();
    const QString refs = (row >= 0 && row < m_items.size())
        ? normalizedPartitionRefs(m_items[row].partitionEdgeRefs) : QString();
    QMetaObject::invokeMethod(bridge, "showExternalPartitionEdgeRefs", Qt::DirectConnection,
                              Q_ARG(QVariantList, partitionPlanes()), Q_ARG(QString, refs));
}

void MeshEdgeControlWidget::updatePartitionRefLabel()
{
    if (!m_partitionRefLabel) return;
    const int row = ui->tableControls->currentRow();
    const QString refs = (row >= 0 && row < m_items.size()) ? normalizedPartitionRefs(m_items[row].partitionEdgeRefs) : QString();
    if (refs.isEmpty())
    {
        m_partitionRefLabel->setText(tr("未选择边"));
        if (m_clearPartitionEdges) m_clearPartitionEdges->setEnabled(false);
        return;
    }
    const int count = refs.split(QLatin1Char(','), Qt::SkipEmptyParts).size();
    m_partitionRefLabel->setText(tr("已选择 %1 条边").arg(count));
    if (m_clearPartitionEdges) m_clearPartitionEdges->setEnabled(true);
}
