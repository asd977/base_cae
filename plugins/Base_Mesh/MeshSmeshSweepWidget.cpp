#include "CompactDoubleSpinBox.h"
#include "MeshSmeshSweepWidget.h"
#include "MeshSmeshSubmeshWidget.h"
#include "core/MeshSettingsUi.h"


#include <ServiceRegistry.h>
#include <EventManager.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <ProjectNode.h>
#include "PublicConstants.h"
#include <QJsonArray>

#include <QCheckBox>
#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSizePolicy>
#include <QVector3D>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMetaObject>
#include <QSet>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <exception>
#include <functional>
#include <utility>

using namespace MeshSmeshWorkflowState;

namespace {
// Directions are queried only when a row is opened. A large assembly should
// not repeat expensive CAD face-matching for every spin-box edit.
// The geometry analyzer validates one solid at a time.  It does not know
// whether two touching solids can share a conforming Prism_3D side mesh.
// A sweep *node* therefore uses one dominant extrusion axis by default.
int candidateSweepAxis(const QVariantMap& candidate)
{
    const QVector3D d = candidate.value(QStringLiteral("direction")).value<QVector3D>();
    const float x = std::abs(d.x()), y = std::abs(d.y()), z = std::abs(d.z());
    if (x <= 1.0e-8f && y <= 1.0e-8f && z <= 1.0e-8f)
        return -1;
    return x >= y && x >= z ? 0 : (y >= z ? 1 : 2);
}

class SweepDirectionCellCombo final : public QComboBox
{
public:
    using QComboBox::QComboBox;
    std::function<void()> beforePopup;
protected:
    void showPopup() override
    {
        if (beforePopup) beforePopup();
        QComboBox::showPopup();
    }
};
}

MeshSmeshSweepWidget::MeshSmeshSweepWidget(QWidget* parent)
    : BasePage("MeshSmeshSweepWidget")
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(5);
    root->addWidget(m_selectionPresentation.createViewSelector(this));

    m_ownerLabel = new QLabel(tr("扫掠只作用于加入列表的区域；其他区域按各自的网格任务计算。扫掠区域的横向密度由“起始面尺寸”控制，纵向密度由“扫掠层数”控制。"), this);
    m_ownerLabel->setWordWrap(true);
    m_ownerLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_ownerLabel->hide(); // The current workflow hint below already explains this scope.

    m_hint = new QLabel(this);
    MeshSettingsUi::hideExplanation(m_ownerLabel);
    m_hint->setWordWrap(true);
    m_hint->setMaximumHeight(36);
    m_hint->setMinimumWidth(0);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // The old status label could overlap the selector in a compact panel.
    // Detailed progress/errors are already available in the log panel.
    m_hint->hide();

    auto* sharedGroup = new QGroupBox(tr("扫掠参数"), this);
    auto* sharedForm = new QFormLayout(sharedGroup);
    sharedForm->setContentsMargins(8, 8, 8, 8);
    sharedForm->setHorizontalSpacing(8);
    sharedForm->setVerticalSpacing(4);
    sharedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* sweepGroup = new QGroupBox(tr("区域与方向"), this);
    auto* form = new QFormLayout(sweepGroup);
    form->setContentsMargins(8, 8, 8, 8);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(4);
    form->setLabelAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_objectMode = new QComboBox(this);
    m_objectMode->addItem(tr("几何体"), 0);
    m_objectMode->addItem(tr("分块"), 1);
    m_objectMode->setVisible(false);
    // 这些数值控件只作为内部状态保存，不再让用户直接编辑 Region/Face ID。
    // 分块后 Face ID 会重新编号，暴露数字输入既难用也容易把旧编号带到新 Region。
    m_objectId = new QSpinBox(this); m_objectId->setRange(0, 1000000); m_objectId->hide();
    m_sourceFace = new QSpinBox(this); m_sourceFace->setRange(0, 1000000); m_sourceFace->hide();
    m_targetFace = new QSpinBox(this); m_targetFace->setRange(0, 1000000); m_targetFace->hide();
    m_objectDisplay = new QLabel(tr("未选择"), this);
    m_layers = new QSpinBox(this); m_layers->setRange(1, 10000); m_layers->setValue(10);
    m_sourceSize = new CompactDoubleSpinBox(this); m_sourceSize->setRange(1.0e-9, 1.0e12); m_sourceSize->setDecimals(8); m_sourceSize->setValue(5.0);
    m_quadSource = new QCheckBox(tr("优先四边形"), this); m_quadSource->setChecked(true);
    m_layers->setObjectName("sweepLayers");
    m_sourceSize->setObjectName("sweepSourceSize");

    auto makePickRow = [this](QLabel* display, QPushButton*& button,
                              const QString& buttonText, const std::function<void()>& fn)
    {
        auto* w = new QWidget(this);
        auto* l = new QHBoxLayout(w); l->setContentsMargins(0,0,0,0);
        display->setMinimumHeight(26);
        display->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        display->setStyleSheet(QStringLiteral("QLabel { padding: 4px 8px; border: 1px solid palette(midlight); border-radius: 2px; background: palette(base); }"));
        button = new QPushButton(buttonText, w);
        button->setMinimumHeight(26);
        l->setSpacing(5);
        l->addWidget(display, 1); l->addWidget(button);
        connect(button, &QPushButton::clicked, this, fn);
        return w;
    };

    form->addRow(tr("区域"), makePickRow(m_objectDisplay, m_objectPickButton, tr("选取"), [this]() { beginObjectPick(); }));
    m_direction=new QComboBox(this);m_direction->setObjectName("sweepDirection");
    m_direction->addItem(tr("选择区域后识别"));
    m_direction->setEnabled(false);
    form->addRow(tr("扫掠方向"),m_direction);
    connect(m_direction,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index){
        const auto candidate=m_direction->itemData(index).toMap();if(candidate.isEmpty())return;
        m_sourceFace->setValue(candidate.value("source").toInt());m_targetFace->setValue(candidate.value("target").toInt());
        refreshPickDisplay();showCurrentPartitionSelection();
        syncCurrentTask();
    });
    // Source and target faces are derived from the chosen validated direction.
    // Keep the IDs internally for the mesher, not as editable per-region controls.
    sharedForm->addRow(tr("扫掠层数"), m_layers);
    sharedForm->addRow(tr("起始面尺寸"), m_sourceSize);
    sharedForm->addRow(tr("起始面单元"), m_quadSource);
    root->addWidget(sharedGroup);
    root->addWidget(sweepGroup);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(4);
    auto* autoBtn = new QPushButton(tr("识别方向"), this);
    m_batchAddCad = new QPushButton(tr("批量添加"), this);
    m_syncRegions = new QPushButton(tr("添加全部分块"), this);
    m_partitionButton = new QPushButton(tr("分块设置"), this);
    actionRow->addWidget(autoBtn, 1);
    // The two batch-add actions are mutually exclusive.
    actionRow->addWidget(m_batchAddCad, 1);
    actionRow->addWidget(m_syncRegions, 1);
    actionRow->addWidget(m_partitionButton, 1);
    root->addLayout(actionRow);

    auto* taskGroup = new QGroupBox(tr("扫掠任务"), this);
    m_taskGroup = taskGroup;
    auto* taskLayout = new QVBoxLayout(taskGroup);
    taskLayout->setContentsMargins(5, 5, 5, 5);
    taskLayout->setSpacing(5);

    m_table = new QTableWidget(this);
    m_table->setObjectName("sweepTasks");
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("区域"), tr("扫掠方向")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(29);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setMinimumHeight(170);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    taskLayout->addWidget(m_table, 1);

    auto* taskRow = new QHBoxLayout();
    taskRow->setSpacing(5);
    auto* removeBtn = new QPushButton(tr("移除选中"), this);
    auto* clearBtn = new QPushButton(tr("清空列表"), this);
    taskRow->addStretch(1);
    taskRow->addWidget(removeBtn);
    taskRow->addWidget(clearBtn);
    taskLayout->addLayout(taskRow);
    root->addWidget(taskGroup, 1);

    auto* generateButton = new QPushButton(tr("生成网格"), this);
    generateButton->setObjectName("generateSweepMesh");
    generateButton->setMinimumHeight(30);
    root->addWidget(generateButton);
    connect(generateButton, &QPushButton::clicked, this, &MeshSmeshSweepWidget::generateMesh);

    connect(m_objectMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { refreshMode(); });
    connect(autoBtn, &QPushButton::clicked, this, &MeshSmeshSweepWidget::autoAnalyze);
    connect(m_batchAddCad, &QPushButton::clicked, this, &MeshSmeshSweepWidget::batchAddCadVolumes);
    connect(m_syncRegions, &QPushButton::clicked, this, &MeshSmeshSweepWidget::syncAllPartitionRegions);
    connect(m_partitionButton, &QPushButton::clicked, this, [this]()
    {
        emit partitionRequested();
    });
    connect(removeBtn, &QPushButton::clicked, this, &MeshSmeshSweepWidget::removeTask);
    connect(clearBtn, &QPushButton::clicked, this, &MeshSmeshSweepWidget::clearTasks);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) { applyTaskToEditors(row); });
    // A sweep node owns ONE set of density settings, shared by all its rows.
    connect(m_layers, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { syncSharedParameters(); });
    connect(m_sourceSize, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { syncSharedParameters(); });
    connect(m_quadSource, &QCheckBox::toggled,
            this, [this](bool) { syncSharedParameters(); });

    loadState();
    refreshMode();
    refreshPickDisplay();
}

void MeshSmeshSweepWidget::setMeshPlanIdentity(const QString& uuid, const QString& displayName)
{
    const QString newPlan = uuid.trimmed();
    if (m_planUuid != newPlan)
    {
        m_groupAxis = -1;
        m_directionCandidates.clear();
        m_checkedDirectionKeys.clear();
    }
    m_planUuid = newPlan;
    m_planName = displayName.trimmed();
    setProperty("meshPlanUuid", m_planUuid);
    if (m_ownerLabel)
        m_ownerLabel->setText(tr("扫掠只作用于加入列表的区域；其他区域按各自的网格任务计算。"));
    if (m_tasks.isEmpty() && !m_planUuid.isEmpty())
        loadState();
    else if (!m_planUuid.isEmpty())
    {
        normalizeSharedParameters();
        replaceSweepTasksForPlan(m_planUuid, m_tasks);
    }
    MixedSettings settings = mixedSettingsForPlan(m_planUuid);
    settings.planUuid = m_planUuid;
    settings.preferHex = true; // Unified conformal workflow is the only exposed mode.
    setMixedSettingsForPlan(settings);
    refreshMode();
}

void MeshSmeshSweepWidget::setBaseVolumeSize(double size)
{
    if (size > 0.0) m_baseVolumeSize = size;
}

void MeshSmeshSweepWidget::generateMesh()
{
    generate(false);
}

void MeshSmeshSweepWidget::quickCheckCurrentRegion() { generateMesh(); }



MeshSmeshSweepWidget::~MeshSmeshSweepWidget()
{
    stopPick();
    if (m_thread)
    {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

QObject* MeshSmeshSweepWidget::geometryBridge() const
{
    auto* svc = ServiceRegistry::instance().getService<PreProcessingService>();
    return svc ? dynamic_cast<QObject*>(svc) : nullptr;
}

void MeshSmeshSweepWidget::beforeLoad()
{
    m_selectionPresentation.enter();
    // Geometry and face IDs can change while this page is closed. Never
    // reuse CAD sweep candidates cached for an earlier geometry state.
    m_directionCandidates.clear();
    m_checkedDirectionKeys.clear();
    loadState();
    // Upgrade older saved pending rows using one batched CAD query.
    // No manual re-add is needed after an editor upgrade.
    autoAssignLegacyDirections();
    refreshMode();
    QList<int> ids;
    for (const auto& task:m_tasks) if (!task.partitionRegion && !ids.contains(task.objectId)) ids.append(task.objectId);
    m_selectionPresentation.setSelectedEntities(3, ids);
    MeshSettingsUi::simplify(this);
}

void MeshSmeshSweepWidget::beforeUnload()
{
    stopPick();
    if (QObject* bridge = geometryBridge())
        QMetaObject::invokeMethod(bridge, "clearExternalPartitionRegionPreview", Qt::DirectConnection);
    m_selectionPresentation.leave();
}

void MeshSmeshSweepWidget::loadState()
{
    {
        const QScopedValueRollback<bool> restoring(m_restoringTask, true);
        m_tasks = sweepTasksForPlan(m_planUuid);
        normalizeSharedParameters();
        refreshTable();
    }
    if (!m_tasks.isEmpty()) applyTaskToEditors(0);
}

void MeshSmeshSweepWidget::saveState()
{
    if (!m_planUuid.isEmpty()) replaceSweepTasksForPlan(m_planUuid,m_tasks);
    emit configurationChanged();
}

QSet<int> MeshSmeshSweepWidget::assignedCadVolumeIds() const
{
    QSet<int> ids;
    for (const auto& task:m_tasks)
        if (!task.partitionRegion && task.objectId>0) ids.insert(task.objectId);
    return ids;
}

QSet<int> MeshSmeshSweepWidget::occupiedByVolumeSettings() const
{
    QSet<int> occupied;
    if (m_partitionPlanUuid.isEmpty()) return occupied;
    auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
    auto* global=pm?pm->findNodeByUuid(m_partitionPlanUuid):nullptr;
    if (!global) return occupied;
    for(auto* sibling:global->getChildren()) {
        auto* sub=dynamic_cast<MeshSmeshSubmeshWidget*>(sibling->getPage());
        if (!sub) continue;
        const auto scope=sub->settings();
        if (scope.dimension!=3) continue;
        for(int id:scope.entityIds) occupied.insert(id);
    }
    return occupied;
}

QSet<int> MeshSmeshSweepWidget::occupiedByOtherSweepNodes(bool partitioned) const
{
    QSet<int> occupied;
    if (m_partitionPlanUuid.isEmpty()) return occupied;
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    auto* global = pm ? pm->findNodeByUuid(m_partitionPlanUuid) : nullptr;
    if (!global) return occupied;
    for (auto* sibling : global->getChildren())
    {
        if (sibling->getUuid() == m_planUuid ||
            sibling->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("SmeshSweep"))
            continue;
        const auto* other = dynamic_cast<MeshSmeshSweepWidget*>(sibling->getPage());
        if (!other) continue;
        for (const auto& t : other->m_tasks)
            if (t.objectId > 0 && t.partitionRegion == partitioned)
                occupied.insert(t.objectId);
    }
    return occupied;
}

void MeshSmeshSweepWidget::normalizeSharedParameters()
{
    for (SweepTask& task : m_tasks)
    {
        task.layers = m_layers->value();
        task.sourceSize = m_sourceSize->value();
        task.quadrangleSource = m_quadSource->isChecked();
    }
}

void MeshSmeshSweepWidget::syncSharedParameters()
{
    if (m_restoringTask) return;
    normalizeSharedParameters();
    saveState();
    refreshTable();
}

bool MeshSmeshSweepWidget::hasPartition() const
{
    return !planePayload(partitionRulesForPlan(m_partitionPlanUuid)).isEmpty();
}

QVariantList MeshSmeshSweepWidget::planes() const
{
    return planePayload(partitionRulesForPlan(m_partitionPlanUuid));
}

void MeshSmeshSweepWidget::refreshMode()
{
    const bool partitioned = hasPartition();
    if (m_hint) m_hint->setStyleSheet(QString());
    if (m_batchAddCad) m_batchAddCad->setVisible(!partitioned);
    if (m_syncRegions) m_syncRegions->setVisible(partitioned);
    if (m_partitionButton) m_partitionButton->setText(partitioned ? tr("编辑分块") : tr("分块设置"));
    if (m_objectPickButton)
        m_objectPickButton->setText(partitioned ? tr("选取分块") : tr("选取几何体"));
    if (partitioned)
    {
        const int idx = m_objectMode->findData(1);
        if (idx >= 0) m_objectMode->setCurrentIndex(idx);
        m_objectMode->setEnabled(false);
        m_hint->clear();
    }
    else
    {
        const int idx = m_objectMode->findData(0);
        if (idx >= 0) m_objectMode->setCurrentIndex(idx);
        m_objectMode->setEnabled(false);
        m_hint->clear();
    }
    refreshPickDisplay();
}

QString MeshSmeshSweepWidget::partitionFaceDisplayText(int faceId) const
{
    if (faceId <= 0)
        return tr("未选择");
    if (!hasPartition())
        return tr("已选择");

    QObject* bridge = geometryBridge();
    if (!bridge || m_objectId->value() <= 0)
        return tr("已选择");

    QVariantMap info;
    const bool invoked = QMetaObject::invokeMethod(
        bridge, "externalPartitionFaceInfo", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantMap, info), Q_ARG(QVariantList, planes()),
        Q_ARG(int, m_objectId->value()), Q_ARG(int, faceId));
    if (!invoked || !info.value(QStringLiteral("valid")).toBool())
        return tr("已选择");

    const int edgeCount = info.value(QStringLiteral("edgeCount")).toInt();
    if (edgeCount > 0)
        return tr("已选择（%1 条边）").arg(edgeCount);
    return tr("已选择");
}

void MeshSmeshSweepWidget::refreshPickDisplay()
{
    if (m_objectDisplay)
    {
        const int id = m_objectId ? m_objectId->value() : 0;
        m_objectDisplay->setText(id > 0
            ? (hasPartition() ? tr("分块 %1").arg(id) : tr("几何体 %1").arg(id))
            : tr("未选择"));
    }
}

void MeshSmeshSweepWidget::showCurrentPartitionSelection()
{
    if (!hasPartition())
        return;
    QObject* bridge = geometryBridge();
    if (!bridge || !m_objectId || m_objectId->value() <= 0)
        return;

    const QVariantList payload = planes();
    const int region = m_objectId->value();
    const int source = m_sourceFace ? m_sourceFace->value() : 0;
    const int target = m_targetFace ? m_targetFace->value() : 0;
    bool shown = false;
    const bool invoked = QMetaObject::invokeMethod(
        bridge, "showExternalPartitionSweepSelection", Qt::DirectConnection,
        Q_RETURN_ARG(bool, shown), Q_ARG(QVariantList, payload), Q_ARG(int, region),
        Q_ARG(int, source), Q_ARG(int, target));
    if (!invoked || !shown)
        QMetaObject::invokeMethod(bridge, "highlightExternalPartitionRegion", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, shown), Q_ARG(QVariantList, payload), Q_ARG(int, region));
}

QString MeshSmeshSweepWidget::directionKey(const SweepTask& task) const
{
    return QStringLiteral("%1:%2").arg(task.partitionRegion ? 1 : 0).arg(task.objectId);
}

QString MeshSmeshSweepWidget::directionLabel(const QVariantMap& candidate) const
{
    const QVector3D dir = candidate.value(QStringLiteral("direction")).value<QVector3D>();
    const float ax = std::abs(dir.x());
    const float ay = std::abs(dir.y());
    const float az = std::abs(dir.z());
    const QString axis = (ax >= ay && ax >= az) ? QStringLiteral("X")
                       : (ay >= az ? QStringLiteral("Y") : QStringLiteral("Z"));
    return tr("%1 · %2").arg(axis).arg(candidate.value(QStringLiteral("distance")).toDouble(), 0, 'g', 6);
}

QVariantList MeshSmeshSweepWidget::directionCandidates(const SweepTask& task, bool force)
{
    const QString key = directionKey(task);
    if (!force && m_checkedDirectionKeys.contains(key))
        return m_directionCandidates.value(key);
    if (force)
    {
        m_directionCandidates.remove(key);
        m_checkedDirectionKeys.remove(key);
    }

    QVariantList validCandidates;
    bool analysisAvailable = false;
    if (task.objectId > 0)
    {
        if (QObject* bridge = geometryBridge())
        {
            QVariantMap analysis;
            bool invoked = false;
            if (task.partitionRegion)
            {
                invoked = QMetaObject::invokeMethod(
                    bridge, "externalAnalyzePartitionSweepRegion", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(QVariantList, planes()),
                    Q_ARG(int, task.objectId));
            }
            else
            {
                invoked = QMetaObject::invokeMethod(
                    bridge, "externalAnalyzeSweepVolume", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(int, task.objectId));
            }
            analysisAvailable = invoked;
            if (invoked && analysis.value(QStringLiteral("valid")).toBool())
            {
                for (const QVariant& raw : analysis.value(QStringLiteral("candidates")).toList())
                {
                    const QVariantMap c = raw.toMap();
                    const int source = c.value(QStringLiteral("source")).toInt();
                    const int target = c.value(QStringLiteral("target")).toInt();
                    if (source > 0 && target > 0 && source != target)
                        validCandidates.append(c);
                }
            }
        }
    }
    if (analysisAvailable)
    {
        m_directionCandidates.insert(key, validCandidates);
        m_checkedDirectionKeys.insert(key);
    }
    return validCandidates;
}

void MeshSmeshSweepWidget::fillDirectionCombo(QComboBox* combo, int row)
{
    if (!combo || row < 0 || row >= m_tasks.size()) return;
    const SweepTask& task = m_tasks[row];
    const QString key = directionKey(task);
    const bool checked = m_checkedDirectionKeys.contains(key);
    const QVariantList candidates = m_directionCandidates.value(key);
    const QSignalBlocker blocker(combo);
    combo->clear();
    int selected = -1;
    for (const QVariant& raw : candidates)
    {
        const QVariantMap c = raw.toMap();
        if (m_groupAxis >= 0 && candidateSweepAxis(c) != m_groupAxis)
            continue;
        combo->addItem(directionLabel(c), c);
        const int idx = combo->count() - 1;
        if (task.sourceFaceId == c.value(QStringLiteral("source")).toInt() &&
            task.targetFaceId == c.value(QStringLiteral("target")).toInt())
            selected = idx;
        combo->setItemData(idx, c.value(QStringLiteral("label")).toString(), Qt::ToolTipRole);
    }
    if (combo->count() == 0)
    {
        combo->addItem(checked && !candidates.isEmpty()
            ? tr("与本组方向不兼容") : checked ? tr("无可用方向") : tr("待识别"));
        combo->setEnabled(false);
    }
    else
    {
        if (selected < 0)
        {
            // Never show a valid first candidate as if it had already been
            // saved for a task with stale source/target face IDs.
            combo->insertItem(0, tr("方向已失效，请重新选择"));
            selected = 0;
        }
        combo->setEnabled(true);
    }
    combo->setCurrentIndex(selected >= 0 ? selected : 0);
    combo->setToolTip(candidates.isEmpty()
        ? tr("未识别到可用的扫掠方向") : tr("选择已经识别的扫掠方向"));
}

void MeshSmeshSweepWidget::reconcileSweepDirections()
{
    if (m_reconcilingDirections || m_tasks.isEmpty()) return;
    const QScopedValueRollback<bool> guard(m_reconcilingDirections, true);
    int coverage[3] = {0, 0, 0};
    int retained[3] = {0, 0, 0};
    for (const SweepTask& task : std::as_const(m_tasks))
    {
        const QString key = directionKey(task);
        if (!m_checkedDirectionKeys.contains(key)) continue;
        bool supported[3] = {false, false, false};
        for (const QVariant& raw : m_directionCandidates.value(key))
        {
            const QVariantMap candidate = raw.toMap();
            const int axis = candidateSweepAxis(candidate);
            if (axis < 0) continue;
            supported[axis] = true;
            if (task.sourceFaceId == candidate.value(QStringLiteral("source")).toInt() &&
                task.targetFaceId == candidate.value(QStringLiteral("target")).toInt())
                ++retained[axis];
        }
        for (int axis = 0; axis < 3; ++axis)
            if (supported[axis]) ++coverage[axis];
    }
    int chosen = -1;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!coverage[axis]) continue;
        if (chosen < 0 || coverage[axis] > coverage[chosen] ||
            (coverage[axis] == coverage[chosen] && retained[axis] > retained[chosen]) ||
            (coverage[axis] == coverage[chosen] && retained[axis] == retained[chosen] &&
             axis == m_groupAxis))
            chosen = axis;
    }
    if (chosen < 0) { m_groupAxis = -1; return; }
    m_groupAxis = chosen;
    bool modified = false;
    for (SweepTask& task : m_tasks)
    {
        const QString key = directionKey(task);
        if (!m_checkedDirectionKeys.contains(key)) continue;
        const QVariantList options = m_directionCandidates.value(key);
        const bool savedPair = task.sourceFaceId > 0 && task.targetFaceId > 0 &&
                               task.sourceFaceId != task.targetFaceId;
        bool savedPairStillExists = false;
        for (const QVariant& raw : options)
        {
            const QVariantMap c = raw.toMap();
            if (c.value(QStringLiteral("source")).toInt() == task.sourceFaceId &&
                c.value(QStringLiteral("target")).toInt() == task.targetFaceId)
            { savedPairStillExists = true; break; }
        }
        if (savedPair && !savedPairStillExists)
            continue; // Preserve stale IDs for the explicit invalid-direction warning.
        QVariantMap selected;
        for (const QVariant& raw : options)
        {
            const QVariantMap c = raw.toMap();
            if (candidateSweepAxis(c) != chosen) continue;
            if (selected.isEmpty()) selected = c;
            if (c.value(QStringLiteral("source")).toInt() == task.sourceFaceId &&
                c.value(QStringLiteral("target")).toInt() == task.targetFaceId)
            { selected = c; break; }
        }
        const int src = selected.isEmpty() ? -1 : selected.value(QStringLiteral("source")).toInt();
        const int dst = selected.isEmpty() ? -1 : selected.value(QStringLiteral("target")).toInt();
        if (src != task.sourceFaceId || dst != task.targetFaceId)
        {
            task.sourceFaceId = src;
            task.targetFaceId = dst;
            modified = true;
        }
    }
    if (modified) saveState();
}

void MeshSmeshSweepWidget::autoAssignLegacyDirections()
{
    // beforeLoad() drops CAD direction caches because the geometry may have
    // changed. Rebuild candidates for ALL persisted rows, not just rows with
    // empty source/target IDs. Otherwise every restored valid row is displayed
    // as "待识别" even though its saved face IDs are still present.
    if (m_tasks.isEmpty()) return;
    QObject* bridge = geometryBridge();
    if (!bridge) return;

    // One batched CAD analysis for the node; avoid one OCC query per table row.
    QVariantList ids;
    for (const SweepTask& task : std::as_const(m_tasks))
    {
        if (!task.partitionRegion && task.objectId > 0 &&
            !m_checkedDirectionKeys.contains(directionKey(task)))
            ids.append(task.objectId);
    }
    QHash<int, QVariantMap> byId;
    if (!ids.isEmpty())
    {
        QVariantList analyses;
        const bool batchOk = QMetaObject::invokeMethod(
            bridge, "externalAnalyzeSweepVolumes", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantList, analyses), Q_ARG(QVariantList, ids));
        if (batchOk)
        {
            for (const QVariant& raw : analyses)
            {
                const QVariantMap a = raw.toMap();
                const int id = a.value(QStringLiteral("volumeUiId")).toInt();
                if (id > 0) byId.insert(id, a);
            }
        }
    }

    bool changed = false;
    for (SweepTask& task : m_tasks)
    {
        const QString key = directionKey(task);
        if (task.objectId <= 0 || m_checkedDirectionKeys.contains(key))
            continue;

        QVariantMap analysis;
        bool available = false;
        if (task.partitionRegion)
        {
            available = QMetaObject::invokeMethod(
                bridge, "externalAnalyzePartitionSweepRegion", Qt::DirectConnection,
                Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(QVariantList, planes()),
                Q_ARG(int, task.objectId));
        }
        else
        {
            analysis = byId.value(task.objectId);
            available = !analysis.isEmpty();
            if (!available)
            {
                available = QMetaObject::invokeMethod(
                    bridge, "externalAnalyzeSweepVolume", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(int, task.objectId));
            }
        }
        if (!available || analysis.isEmpty())
            continue; // Geometry service unavailable: never erase saved face IDs.

        QVariantList candidates;
        if (analysis.value(QStringLiteral("valid")).toBool())
        {
            for (const QVariant& raw : analysis.value(QStringLiteral("candidates")).toList())
            {
                const QVariantMap c = raw.toMap();
                const int src = c.value(QStringLiteral("source")).toInt();
                const int dst = c.value(QStringLiteral("target")).toInt();
                if (src > 0 && dst > 0 && src != dst)
                    candidates.append(c);
            }
        }
        m_directionCandidates.insert(key, candidates);
        m_checkedDirectionKeys.insert(key);

        // Existing valid pairs are user settings. Keep them when they are
        // still supported; if stale, show "方向已失效" instead of silently
        // choosing a different pair. Pending legacy rows get a default.
        const bool pending = task.sourceFaceId <= 0 || task.targetFaceId <= 0 ||
                             task.sourceFaceId == task.targetFaceId;
        if (pending && !candidates.isEmpty())
        {
            const QVariantMap best = candidates.front().toMap();
            const int source = best.value(QStringLiteral("source")).toInt();
            const int target = best.value(QStringLiteral("target")).toInt();
            if (task.sourceFaceId != source || task.targetFaceId != target)
            {
                task.sourceFaceId = source;
                task.targetFaceId = target;
                changed = true;
            }
        }
    }
    if (changed) saveState();
    reconcileSweepDirections();
    refreshTable();
    if (m_table->currentRow() >= 0)
        applyTaskToEditors(m_table->currentRow());
}

void MeshSmeshSweepWidget::refreshTable()
{
    const int previousRow = m_table->currentRow();
    {
        const QSignalBlocker blocker(m_table);
        m_table->setRowCount(m_tasks.size());
        for (int row = 0; row < m_tasks.size(); ++row)
        {
            const SweepTask& task = m_tasks[row];
            const QString key = directionKey(task);
            const QString name = task.partitionRegion
                ? tr("分块 %1").arg(task.objectId) : tr("几何体 %1").arg(task.objectId);
            auto* item = m_table->item(row, 0);
            if (!item)
            {
                item = new QTableWidgetItem();
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                m_table->setItem(row, 0, item);
            }
            item->setText(name);
            const bool invalidPair = task.sourceFaceId <= 0 || task.targetFaceId <= 0 ||
                                     task.sourceFaceId == task.targetFaceId;
            item->setToolTip(invalidPair ? tr("无有效扫掠方向") : QString());
            auto* combo = dynamic_cast<SweepDirectionCellCombo*>(m_table->cellWidget(row, 1));
            if (!combo || combo->property("sweepKey").toString() != key)
            {
                combo = new SweepDirectionCellCombo(m_table);
                combo->setObjectName("sweepTaskDirection");
                combo->setProperty("sweepKey", key);
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                m_table->setCellWidget(row, 1, combo);
                // Fetch on opening only; changing a numeric value won't cause
                // hundreds of repeated geometry analyses in large assemblies.
                combo->beforePopup = [this, combo, row, key]()
                {
                    if (row >= m_tasks.size() || directionKey(m_tasks[row]) != key) return;
                    // Re-analyze when the user opens the list: CAD topology
                    // may have changed, but opening a popup must be read-only.
                    // The saved direction changes only after an actual selection.
                    directionCandidates(m_tasks[row], true);
                    fillDirectionCombo(combo, row);
                };
                connect(combo, QOverload<int>::of(&QComboBox::activated), this,
                        [this, combo, row, key](int index)
                {
                    if (row >= m_tasks.size() || directionKey(m_tasks[row]) != key) return;
                    const QVariantMap c = combo->itemData(index).toMap();
                    const int source = c.value(QStringLiteral("source")).toInt();
                    const int target = c.value(QStringLiteral("target")).toInt();
                    if (source > 0 && target > 0 && source != target)
                    {
                        m_tasks[row].sourceFaceId = source;
                        m_tasks[row].targetFaceId = target;
                        saveState();
                    }
                    m_table->setCurrentCell(row, 0);
                    m_table->selectRow(row);
                    applyTaskToEditors(row);

                });
            }
            fillDirectionCombo(combo, row);
        }
        if (previousRow >= 0 && previousRow < m_table->rowCount())
            m_table->setCurrentCell(previousRow, 0);
    }
    if (m_taskGroup) m_taskGroup->setTitle(tr("扫掠任务 (%1)").arg(m_tasks.size()));
}

void MeshSmeshSweepWidget::applyTaskToEditors(int row)
{
    if (row < 0 || row >= m_tasks.size()) return;
    bool autoPaired = false;
    {
        const QScopedValueRollback<bool> applying(m_restoringTask, true);
        stopPick();
        const SweepTask task = m_tasks[row];
        m_objectId->setValue(qMax(0, task.objectId));
        m_sourceFace->setValue(qMax(0, task.sourceFaceId));
        m_targetFace->setValue(qMax(0, task.targetFaceId));
        refreshPickDisplay();
        showCurrentPartitionSelection();
        if (!task.partitionRegion)
            m_selectionPresentation.setSelectedEntities(3, {task.objectId});
        if (task.partitionRegion == hasPartition())
            populateDirections(false);
        autoPaired = m_sourceFace->value() != task.sourceFaceId ||
                     m_targetFace->value() != task.targetFaceId;
    }
    // The automatic direction detection on row selection is also a setting
    // change, and must be persisted without requiring another button.
    if (autoPaired) syncCurrentTask();
}

SweepTask MeshSmeshSweepWidget::currentTask() const
{
    SweepTask t;
    t.planUuid = m_planUuid;
    t.partitionRegion = hasPartition();
    t.objectId = m_objectId->value();
    t.sourceFaceId = m_sourceFace->value();
    t.targetFaceId = m_targetFace->value();
    t.layers = m_layers->value();
    t.sourceSize = m_sourceSize->value();
    t.quadrangleSource = m_quadSource->isChecked();
    return t;
}

bool MeshSmeshSweepWidget::validateTask(const SweepTask& t, QString* error) const
{
    auto fail = [&](const QString& s) { if (error) *error = s; return false; };
    if (t.objectId <= 0) return fail(tr("请先选择需要扫掠的区域。"));
    if (t.sourceFaceId <= 0 || t.targetFaceId <= 0) return fail(tr("请同时选择起始面和目标面。"));
    if (t.sourceFaceId == t.targetFaceId) return fail(tr("起始面和目标面不能是同一个面。"));
    if (t.partitionRegion)
    {
        QObject* bridge = geometryBridge();
        if (bridge)
        {
            QVariantMap sourceInfo, targetInfo;
            const QVariantList payload = planes();
            const bool sourceOk = QMetaObject::invokeMethod(
                bridge, "externalPartitionFaceInfo", Qt::DirectConnection,
                Q_RETURN_ARG(QVariantMap, sourceInfo), Q_ARG(QVariantList, payload),
                Q_ARG(int, t.objectId), Q_ARG(int, t.sourceFaceId));
            const bool targetOk = QMetaObject::invokeMethod(
                bridge, "externalPartitionFaceInfo", Qt::DirectConnection,
                Q_RETURN_ARG(QVariantMap, targetInfo), Q_ARG(QVariantList, payload),
                Q_ARG(int, t.objectId), Q_ARG(int, t.targetFaceId));
            if (sourceOk && targetOk &&
                sourceInfo.value(QStringLiteral("valid")).toBool() &&
                targetInfo.value(QStringLiteral("valid")).toBool())
            {
                const int sourceEdges = sourceInfo.value(QStringLiteral("edgeCount")).toInt();
                const int targetEdges = targetInfo.value(QStringLiteral("edgeCount")).toInt();
                // Boolean/Partition often leaves one geometrically continuous circle or line
                // split into several topological edges.  Raw edge-count mismatch is therefore
                // only a warning, not a reason to reject the user's visually corresponding
                // Source/Target faces.  The actual Prism_3D compatibility check remains the
                // authoritative test during Compute; Hex-preferred mode can fall back to Tet.
                Q_UNUSED(sourceEdges);
                Q_UNUSED(targetEdges);
            }
        }
    }
    if (t.layers < 1) return fail(tr("扫掠层数必须大于 0。"));
    if (!(t.sourceSize > 0.0)) return fail(tr("起始面网格尺寸必须大于 0。"));
    if (error) error->clear();
    return true;
}

void MeshSmeshSweepWidget::beginObjectPick()
{
    QObject* bridge = geometryBridge();
    if (!bridge) return;
    stopPick();
    bool ok = false;
    if (hasPartition())
    {
        const QVariantList payload = planes();
        QMetaObject::invokeMethod(bridge, "beginExternalPartitionRegionPick", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(QVariantList, payload));
    }
    else
    {
        QMetaObject::invokeMethod(bridge, "beginExternalEntityPick", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(int, 3));
    }
    if (ok)
    {
        m_pickMode = PickObject;
        m_hint->setStyleSheet(QString());
        m_hint->setText(hasPartition()
            ? tr("选择分块")
            : tr("选择几何体"));
    }
}

void MeshSmeshSweepWidget::beginFacePick(bool source)
{
    QObject* bridge = geometryBridge();
    if (!bridge) return;
    stopPick();
    const int object = m_objectId->value();
    if (object <= 0)
    {
        QMessageBox::information(this, tr("扫掠"),
                                 hasPartition() ? tr("请先选择一个分块。") : tr("请先选择一个几何体。"));
        return;
    }
    bool ok = false;
    if (hasPartition())
    {
        const QVariantList payload = planes();
        const int fixedFace = source ? m_targetFace->value() : m_sourceFace->value();
        QMetaObject::invokeMethod(bridge, "beginExternalPartitionSweepFacePick", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(QVariantList, payload),
                                  Q_ARG(int, object), Q_ARG(int, fixedFace), Q_ARG(bool, source));
    }
    else
    {
        QMetaObject::invokeMethod(bridge, "beginExternalSolidFacePick", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(int, object));
    }
    if (ok)
    {
        m_pickMode = source ? PickSource : PickTarget;
        m_hint->setText(source
            ? tr("选择起始面")
            : tr("选择目标面"));
    }
}

bool MeshSmeshSweepWidget::event(QEvent* ev)
{
    if (ev && ev->type() == static_cast<QEvent::Type>(QEvent::User + 421) && m_pickMode != PickNone)
    {
        const bool pickedObject=m_pickMode==PickObject;
        const int dim = property("_geometryExternalPickDim").toInt();
        const int id = property("_geometryExternalPickId").toInt();
        const bool isRegion = property("_geometryExternalPickIsPartitionRegion").toBool();
        const bool isPartitionFace = property("_geometryExternalPickIsPartitionFace").toBool();
        const int pickedRegion = property("_geometryExternalPickRegionOrdinal").toInt();
        setProperty("_geometryExternalPickIsPartitionRegion", false);
        setProperty("_geometryExternalPickIsPartitionFace", false);
        setProperty("_geometryExternalPickRegionOrdinal", 0);

        if (m_pickMode == PickObject)
        {
            if ((hasPartition() && isRegion && id > 0) || (!hasPartition() && dim == 3 && id > 0))
            {
                const bool changed = m_objectId->value() != id;
                m_objectId->setValue(id);
                // Face ID 只在当前 Region 内有意义。切换分块时绝不能沿用上一块的面号。
                // 如果该分块已经保存过任务，则直接恢复它自己的设置；否则从空白开始。
                if (changed)
                {
                    int savedRow = -1;
                    for (int i = 0; i < m_tasks.size(); ++i)
                    {
                        if (m_tasks[i].partitionRegion == hasPartition() && m_tasks[i].objectId == id)
                        {
                            savedRow = i;
                            break;
                        }
                    }
                    if (savedRow >= 0)
                    {
                        const SweepTask& saved = m_tasks[savedRow];
                        m_sourceFace->setValue(qMax(0, saved.sourceFaceId));
                        m_targetFace->setValue(qMax(0, saved.targetFaceId));
                        m_table->setCurrentCell(savedRow, 0);
                        m_table->selectRow(savedRow);
                    }
                    else
                    {
                        m_sourceFace->setValue(0);
                        m_targetFace->setValue(0);
                        m_table->clearSelection();
                    }
                }
            }
        }
        else if (m_pickMode == PickSource)
        {
            const bool validPartitionPick = hasPartition() && isPartitionFace && id > 0 &&
                                            pickedRegion == m_objectId->value();
            if (validPartitionPick || (!hasPartition() && dim == 2 && id > 0))
                m_sourceFace->setValue(id);
        }
        else if (m_pickMode == PickTarget)
        {
            const bool validPartitionPick = hasPartition() && isPartitionFace && id > 0 &&
                                            pickedRegion == m_objectId->value();
            if (validPartitionPick || (!hasPartition() && dim == 2 && id > 0))
                m_targetFace->setValue(id);
        }
        stopPick();
        refreshPickDisplay();
        showCurrentPartitionSelection();
        if(pickedObject&&m_objectId->value()>0)populateDirections(false);
        syncCurrentTask();
        if (hasPartition())
        {
            m_hint->setStyleSheet(QString());
            if (m_sourceFace->value() <= 0)
                m_hint->setText(tr("分块 %1") .arg(m_objectId->value()));
            else if (m_targetFace->value() <= 0)
                m_hint->setText(tr("已选择起始面"));
            else
            {
                QString pairError;
                if (!validateTask(currentTask(), &pairError))
                {
                    m_hint->setStyleSheet(QStringLiteral("color: #b54708; font-weight: 600;"));
                    m_hint->setText(pairError);
                }
                else
                {
                    int sourceEdges = 0;
                    int targetEdges = 0;
                    QObject* bridge = geometryBridge();
                    if (bridge)
                    {
                        QVariantMap sourceInfo, targetInfo;
                        const QVariantList payload = planes();
                        const int region = m_objectId->value();
                        const int source = m_sourceFace->value();
                        const int target = m_targetFace->value();
                        const bool sourceOk = QMetaObject::invokeMethod(
                            bridge, "externalPartitionFaceInfo", Qt::DirectConnection,
                            Q_RETURN_ARG(QVariantMap, sourceInfo), Q_ARG(QVariantList, payload),
                            Q_ARG(int, region), Q_ARG(int, source));
                        const bool targetOk = QMetaObject::invokeMethod(
                            bridge, "externalPartitionFaceInfo", Qt::DirectConnection,
                            Q_RETURN_ARG(QVariantMap, targetInfo), Q_ARG(QVariantList, payload),
                            Q_ARG(int, region), Q_ARG(int, target));
                        if (sourceOk && sourceInfo.value(QStringLiteral("valid")).toBool())
                            sourceEdges = sourceInfo.value(QStringLiteral("edgeCount")).toInt();
                        if (targetOk && targetInfo.value(QStringLiteral("valid")).toBool())
                            targetEdges = targetInfo.value(QStringLiteral("edgeCount")).toInt();
                    }

                    if (sourceEdges > 0 && targetEdges > 0 && sourceEdges != targetEdges)
                    {
                        // Boolean/Partition can split one geometric circle or straight side into
                        // several topological edges.  Keep this visible as a warning, but do not
                        // force the user to rebuild geometry or use modelling history.
                        m_hint->setStyleSheet(QStringLiteral("color: #b54708; font-weight: 600;"));
                        m_hint->setText(tr(
                            "起始面与目标面的拓扑边数不一致：%1 / %2。")
                            .arg(sourceEdges).arg(targetEdges));
                    }
                    else
                    {
                        m_hint->setStyleSheet(QString());
                        m_hint->setText(tr("已选择起始面与目标面"));
                    }
                }
            }
        }
        return true;
    }
    return BasePage::event(ev);
}

void MeshSmeshSweepWidget::stopPick()
{
    if (QObject* bridge = geometryBridge())
        QMetaObject::invokeMethod(bridge, "stopExternalEntityPick", Qt::DirectConnection, Q_ARG(QObject*, this));
    m_pickMode = PickNone;
}

void MeshSmeshSweepWidget::autoAnalyze()
{
    if (m_objectId->value() <= 0) return;
    populateDirections(true);
    reconcileSweepDirections();
    refreshTable();
}

void MeshSmeshSweepWidget::populateDirections(bool notify)
{
    const SweepTask task = currentTask();
    const QVariantList candidates = directionCandidates(task, notify);
    const int oldSource = m_sourceFace->value();
    const int oldTarget = m_targetFace->value();
    int selected = -1;
    {
        const QSignalBlocker blocker(m_direction);
        m_direction->clear();
        for (const QVariant& raw : candidates)
        {
            const QVariantMap c = raw.toMap();
            if (m_groupAxis >= 0 && candidateSweepAxis(c) != m_groupAxis) continue;
            m_direction->addItem(directionLabel(c), c);
            if (oldSource == c.value(QStringLiteral("source")).toInt() &&
                oldTarget == c.value(QStringLiteral("target")).toInt())
                selected = m_direction->count() - 1;
        }
        // An explicit "识别方向" request, or the first choice for a newly
        // picked region, can establish a default. Simply switching table
        // rows must never replace an existing saved face pair.
        const bool newRegion = oldSource <= 0 && oldTarget <= 0;
        const bool chooseDefault = notify || newRegion;
        if (selected < 0 && m_direction->count() > 0 && chooseDefault)
            selected = 0;
        if (m_direction->count() == 0)
            m_direction->addItem(candidates.isEmpty()
                ? tr("无可用方向") : tr("与本组方向不兼容"));
        else if (selected < 0)
        {
            m_direction->insertItem(0, tr("方向已失效，请重新识别"));
            selected = 0;
        }
        m_direction->setCurrentIndex(selected >= 0 ? selected : 0);
        m_direction->setEnabled(m_direction->count() > 0 &&
                                !m_direction->itemData(m_direction->count() - 1).toMap().isEmpty());
    }

    const QVariantMap selectedCandidate = m_direction->itemData(m_direction->currentIndex()).toMap();
    if (!selectedCandidate.isEmpty() && (notify || (oldSource <= 0 && oldTarget <= 0)))
    {
        const int source = selectedCandidate.value(QStringLiteral("source")).toInt();
        const int target = selectedCandidate.value(QStringLiteral("target")).toInt();
        if (source != oldSource || target != oldTarget)
        {
            m_sourceFace->setValue(source);
            m_targetFace->setValue(target);
            refreshPickDisplay();
            showCurrentPartitionSelection();
            syncCurrentTask();
        }
    }
    if (notify)
        m_hint->setText(candidates.isEmpty()
            ? tr("未识别到可用方向")
            : tr("已识别 %1 个方向").arg(candidates.size()));
}

void MeshSmeshSweepWidget::syncCurrentTask()
{
    if (m_restoringTask || !m_objectId || m_objectId->value() <= 0)
        return;

    // Pending regions are real table entries too: the user can select both
    // faces later. Do not require a valid face pair just to save the row.
    const SweepTask task = currentTask();
    int row = -1;
    for (int i = 0; i < m_tasks.size(); ++i)
    {
        if (m_tasks[i].partitionRegion == task.partitionRegion &&
            m_tasks[i].objectId == task.objectId)
        {
            row = i;
            break;
        }
    }
    if (row >= 0)
        m_tasks[row] = task;
    else
    {
        if (occupiedByOtherSweepNodes(task.partitionRegion).contains(task.objectId)) {
            emit logMessage(tr("[网格] 区域 %1 已在其他扫掠网格节点中，请先从原节点移除。")
                            .arg(task.objectId));
            return;
        }
        if (!task.partitionRegion && occupiedByVolumeSettings().contains(task.objectId)) {
            emit logMessage(tr("[网格] 几何体 %1 已用于体网格设置，请先取消对应选择。")
                            .arg(task.objectId));
            return;
        }
        m_tasks.push_back(task);
        row = m_tasks.size() - 1;
    }
    normalizeSharedParameters();
    saveState();
    refreshTable();
    // Do not applyTaskToEditors() here: it performs geometry analysis and
    // would overwrite what the user is currently editing.
    m_table->setCurrentCell(row, 0);
    m_table->selectRow(row);
}


void MeshSmeshSweepWidget::batchAddCadVolumes()
{
    if (hasPartition())
    {
        QMessageBox::information(this, tr("批量添加几何体"),
                                 tr("当前已启用扫掠分块，请使用“添加全部分块”。如需直接选择原始几何体，请先清除扫掠分块。"));
        return;
    }

    QObject* bridge = geometryBridge();
    if (!bridge)
    {
        QMessageBox::warning(this, tr("批量添加几何体"), tr("当前无法读取几何模型，请确认几何已正常加载。"));
        return;
    }

    bool hasStaleRegionTasks = false;
    for (const SweepTask& task : std::as_const(m_tasks))
    {
        if (task.partitionRegion) { hasStaleRegionTasks = true; break; }
    }
    if (hasStaleRegionTasks)
    {
        const auto answer = QMessageBox::question(
            this, tr("批量添加几何体"),
            tr("当前已取消分块，但扫掠列表中还保留着旧的分块对象。\n\n"
               "这些对象已经失效。继续后将先清除旧分块对象，再添加当前几何体。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return;
        for (int i = m_tasks.size() - 1; i >= 0; --i)
            if (m_tasks[i].partitionRegion) m_tasks.removeAt(i);
        saveState();
        refreshTable();
    }

    QVariantList entries;
    const bool entriesInvoked = QMetaObject::invokeMethod(
        bridge, "externalSolidEntries", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantList, entries));
    if (!entriesInvoked || entries.isEmpty())
    {
        QMessageBox::information(this, tr("批量添加几何体"), tr("当前没有可用的几何体。"));
        return;
    }

    QSet<int> existingIds;
    for (const SweepTask& task : std::as_const(m_tasks))
        if (!task.partitionRegion && task.objectId > 0)
            existingIds.insert(task.objectId);
    const QSet<int> assignedElsewhere = occupiedByOtherSweepNodes(false);
    const QSet<int> assignedToVolume = occupiedByVolumeSettings();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("批量添加区域"));
    dlg.resize(580, 640);
    auto* layout = new QVBoxLayout(&dlg);

    auto* filter = new QLineEdit(&dlg);
    filter->setPlaceholderText(tr("按名称筛选"));
    layout->addWidget(filter);

    auto* list = new QListWidget(&dlg);
    list->setAlternatingRowColors(true);
    layout->addWidget(list, 1);

    for (const QVariant& value : entries)
    {
        const QVariantMap e = value.toMap();
        const int id = e.value(QStringLiteral("id")).toInt();
        const QString name = e.value(QStringLiteral("name")).toString();
        if (id <= 0) continue;

        QString text = tr("%1    [几何体 %2]").arg(name).arg(id);
        if (existingIds.contains(id)) text += tr("    （当前节点）");
        else if (assignedElsewhere.contains(id)) text += tr("    （已被其他扫掠节点使用）");
        else if (assignedToVolume.contains(id)) text += tr("    （已用于体网格设置）");
        auto* item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setData(Qt::UserRole + 2, e.value(QStringLiteral("visible")));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        if (existingIds.contains(id) || assignedElsewhere.contains(id) || assignedToVolume.contains(id))
        {
            item->setCheckState(Qt::Unchecked);
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        else
        {
            item->setCheckState(e.value(QStringLiteral("selected")).toBool() ? Qt::Checked : Qt::Unchecked);
        }
    }

    auto updateFilter = [list, filter]()
    {
        const QString key = filter->text().trimmed();
        for (int i = 0; i < list->count(); ++i)
        {
            QListWidgetItem* item = list->item(i);
            const QString name = item->data(Qt::UserRole + 1).toString();
            item->setHidden(!key.isEmpty() && !name.contains(key, Qt::CaseInsensitive));
        }
    };
    connect(filter, &QLineEdit::textChanged, &dlg, [updateFilter](const QString&) { updateFilter(); });

    auto* selectRow = new QHBoxLayout();
    auto* checkFiltered = new QPushButton(tr("勾选筛选结果"), &dlg);
    auto* uncheckFiltered = new QPushButton(tr("取消筛选结果"), &dlg);
    auto* checkVisible = new QPushButton(tr("勾选当前可见几何体"), &dlg);
    auto* clearChecks = new QPushButton(tr("全不选"), &dlg);
    selectRow->addWidget(checkFiltered);
    selectRow->addWidget(uncheckFiltered);
    selectRow->addWidget(checkVisible);
    selectRow->addWidget(clearChecks);
    selectRow->addStretch(1);
    layout->addLayout(selectRow);

    auto setFiltered = [list](Qt::CheckState state)
    {
        for (int i = 0; i < list->count(); ++i)
        {
            QListWidgetItem* item = list->item(i);
            if (!item->isHidden() && item->flags().testFlag(Qt::ItemIsEnabled))
                item->setCheckState(state);
        }
    };
    connect(checkFiltered, &QPushButton::clicked, &dlg, [setFiltered]() { setFiltered(Qt::Checked); });
    connect(uncheckFiltered, &QPushButton::clicked, &dlg, [setFiltered]() { setFiltered(Qt::Unchecked); });
    connect(clearChecks, &QPushButton::clicked, &dlg, [list]()
    {
        for (int i = 0; i < list->count(); ++i)
        {
            QListWidgetItem* item = list->item(i);
            if (item->flags().testFlag(Qt::ItemIsEnabled)) item->setCheckState(Qt::Unchecked);
        }
    });
    connect(checkVisible, &QPushButton::clicked, &dlg, [list]()
    {
        for (int i = 0; i < list->count(); ++i)
        {
            QListWidgetItem* item = list->item(i);
            if (item->flags().testFlag(Qt::ItemIsEnabled) && item->data(Qt::UserRole + 2).toBool())
                item->setCheckState(Qt::Checked);
        }
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    QList<int> ids;
    for (int i = 0; i < list->count(); ++i)
    {
        QListWidgetItem* item = list->item(i);
        if (item->flags().testFlag(Qt::ItemIsEnabled) && item->checkState() == Qt::Checked)
        {
            const int id = item->data(Qt::UserRole).toInt();
            if (id > 0 && !existingIds.contains(id) && !assignedElsewhere.contains(id)) ids.push_back(id);
        }
    }
    if (ids.isEmpty())
    {
        QMessageBox::information(this, tr("批量添加几何体"), tr("没有勾选新的几何体。"));
        return;
    }

    QVariantList idPayload;
    idPayload.reserve(ids.size());
    for (int id : ids) idPayload.push_back(id);

    QVariantList analyses;
    const bool batchInvoked = QMetaObject::invokeMethod(
        bridge, "externalAnalyzeSweepVolumes", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantList, analyses), Q_ARG(QVariantList, idPayload));

    QHash<int, QVariantMap> analysisById;
    if (batchInvoked)
    {
        for (const QVariant& value : analyses)
        {
            const QVariantMap a = value.toMap();
            const int id = a.value(QStringLiteral("volumeUiId")).toInt();
            if (id > 0) analysisById.insert(id, a);
        }
    }

    const int firstRow = m_tasks.size();
    int ready = 0;
    int pending = 0;
    for (int id : ids)
    {
        SweepTask task;
        task.planUuid = m_planUuid;
        task.partitionRegion = false;
        task.objectId = id;
        task.sourceFaceId = -1;
        task.targetFaceId = -1;
        task.layers = m_layers->value();
        task.sourceSize = m_sourceSize->value();
        task.quadrangleSource = m_quadSource->isChecked();

        QVariantMap analysis = analysisById.value(id);
        if (analysis.isEmpty())
        {
            // 兼容旧 Geometry：批量接口不可用/遗漏时只对该对象回退一次单体 OCC 分析。
            QMetaObject::invokeMethod(bridge, "externalAnalyzeSweepVolume", Qt::DirectConnection,
                                      Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(int, id));
        }
        QVariantList candidates;
        if (analysis.value(QStringLiteral("valid")).toBool())
        {
            for (const QVariant& raw : analysis.value(QStringLiteral("candidates")).toList())
            {
                const QVariantMap c = raw.toMap();
                const int src = c.value(QStringLiteral("source")).toInt();
                const int dst = c.value(QStringLiteral("target")).toInt();
                if (src > 0 && dst > 0 && src != dst) candidates.append(c);
            }
        }
        m_directionCandidates.insert(directionKey(task), candidates);
        m_checkedDirectionKeys.insert(directionKey(task));
        if (!candidates.isEmpty())
        {
            const QVariantMap best = candidates.front().toMap();
            task.sourceFaceId = best.value(QStringLiteral("source")).toInt();
            task.targetFaceId = best.value(QStringLiteral("target")).toInt();
            ++ready;
        }
        else
        {
            ++pending;
            emit logMessage(tr("[扫掠] 几何体 %1 没有可用的自动扫掠方向。").arg(id));
        }
        m_tasks.push_back(task);
    }

    reconcileSweepDirections();
    ready = pending = 0;
    for (const SweepTask& task : std::as_const(m_tasks))
        task.sourceFaceId > 0 && task.targetFaceId > 0 ? ++ready : ++pending;
    saveState();
    refreshTable();
    if (firstRow >= 0 && firstRow < m_tasks.size())
    {
        m_table->setCurrentCell(firstRow, 0);
        m_table->selectRow(firstRow);
        applyTaskToEditors(firstRow);
    }

    const QString summary = tr("已添加 %1 个，%2 个可扫掠，%3 个无可用方向")
        .arg(ids.size()).arg(ready).arg(pending);
    m_hint->setText(summary);
    emit logMessage(QStringLiteral("[扫掠] ") + summary);
}


void MeshSmeshSweepWidget::syncAllPartitionRegions()
{
    if (!hasPartition())
    {
        QMessageBox::information(this, tr("扫掠"), tr("当前没有启用的扫掠分块，请先点击“分块设置…”设置切面。"));
        return;
    }
    QObject* bridge = geometryBridge();
    if (!bridge)
    {
        QMessageBox::warning(this, tr("扫掠"), tr("当前无法读取几何模型，请确认几何已正常加载。"));
        return;
    }
    const QVariantList payload = planes();
    int regionCount = 0;
    const bool countInvoked = QMetaObject::invokeMethod(
        bridge, "externalPartitionRegionCount", Qt::DirectConnection,
        Q_RETURN_ARG(int, regionCount), Q_ARG(QVariantList, payload));
    if (!countInvoked || regionCount <= 0)
    {
        QMessageBox::information(this, tr("扫掠"), tr("当前切面没有形成有效分块。请先点击“查看 / 修改分块…”检查切面和分块预览。"));
        return;
    }

    QHash<int, SweepTask> existing;
    for (const SweepTask& task : std::as_const(m_tasks))
        if (task.partitionRegion && task.objectId > 0 && task.objectId <= regionCount)
            existing.insert(task.objectId, task);
    const QSet<int> assignedElsewhere = occupiedByOtherSweepNodes(true);

    QVector<SweepTask> next;
    next.reserve(regionCount);
    int ready = 0;
    int pending = 0;
    int firstNeedsManual = -1;

    for (int region = 1; region <= regionCount; ++region)
    {
        if (assignedElsewhere.contains(region)) continue;
        if (existing.contains(region))
        {
            SweepTask task = existing.value(region);
            const QVariantList candidates = directionCandidates(task, true);
            int selected = -1;
            for (int i = 0; i < candidates.size(); ++i)
            {
                const QVariantMap c = candidates[i].toMap();
                if (c.value(QStringLiteral("source")).toInt() == task.sourceFaceId &&
                    c.value(QStringLiteral("target")).toInt() == task.targetFaceId)
                { selected = i; break; }
            }
            if (selected < 0 && !candidates.isEmpty()) selected = 0;
            const QVariantMap chosen = selected >= 0 ? candidates[selected].toMap() : QVariantMap();
            task.sourceFaceId = chosen.value(QStringLiteral("source"), -1).toInt();
            task.targetFaceId = chosen.value(QStringLiteral("target"), -1).toInt();
            next.push_back(task);
            QString taskError;
            if (task.sourceFaceId > 0 && task.targetFaceId > 0 &&
                task.sourceFaceId != task.targetFaceId && validateTask(task, &taskError))
                ++ready;
            else
            {
                ++pending;
                if (firstNeedsManual < 0) firstNeedsManual = region;
            }
            continue;
        }

        SweepTask task;
        task.planUuid = m_planUuid;
        task.partitionRegion = true;
        task.objectId = region;
        task.sourceFaceId = -1;
        task.targetFaceId = -1;
        task.layers = m_layers->value();
        task.sourceSize = m_sourceSize->value();
        task.quadrangleSource = m_quadSource->isChecked();

        QVariantMap analysis;
        const bool invoked = QMetaObject::invokeMethod(
            bridge, "externalAnalyzePartitionSweepRegion", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(QVariantList, payload), Q_ARG(int, region));
        QVariantList candidates;
        if (invoked && analysis.value(QStringLiteral("valid")).toBool())
        {
            for (const QVariant& raw : analysis.value(QStringLiteral("candidates")).toList())
            {
                const QVariantMap c = raw.toMap();
                const int src = c.value(QStringLiteral("source")).toInt();
                const int dst = c.value(QStringLiteral("target")).toInt();
                if (src > 0 && dst > 0 && src != dst) candidates.append(c);
            }
        }
        m_directionCandidates.insert(directionKey(task), candidates);
        m_checkedDirectionKeys.insert(directionKey(task));
        if (!candidates.isEmpty())
        {
            const QVariantMap best = candidates.front().toMap();
            task.sourceFaceId = best.value(QStringLiteral("source")).toInt();
            task.targetFaceId = best.value(QStringLiteral("target")).toInt();
            QString taskError;
            if (validateTask(task, &taskError))
            {
                ++ready;
            }
            else
            {
                task.sourceFaceId = -1;
                task.targetFaceId = -1;
                ++pending;
                if (firstNeedsManual < 0) firstNeedsManual = region;
            }
        }
        else
        {
            ++pending;
            if (firstNeedsManual < 0) firstNeedsManual = region;
            emit logMessage(tr("[扫掠] 分块 %1 没有可用的自动扫掠方向。").arg(region));
        }

        // 关键：批量同步必须把所有 Region 都留在扫掠列表里。
        // 自动分析失败/多候选只意味着“待配置”，不能把 Region 悄悄丢掉。
        next.push_back(task);
    }

    m_tasks = next;
    reconcileSweepDirections();
    ready = pending = 0;
    for (const SweepTask& task : std::as_const(m_tasks))
        task.sourceFaceId > 0 && task.targetFaceId > 0 ? ++ready : ++pending;
    saveState();
    refreshTable();
    if (!m_tasks.isEmpty())
    {
        const int row = firstNeedsManual > 0 ? firstNeedsManual - 1 : 0;
        m_table->setCurrentCell(qBound(0, row, m_tasks.size() - 1), 0);
        m_table->selectRow(qBound(0, row, m_tasks.size() - 1));
        applyTaskToEditors(qBound(0, row, m_tasks.size() - 1));
    }

    const QString summary = tr("当前节点 %1 个分块，%2 个可扫掠，%3 个无可用方向")
        .arg(m_tasks.size()).arg(ready).arg(pending);
    m_hint->setText(summary);
    emit logMessage(QStringLiteral("[扫掠] ") + summary);
}

void MeshSmeshSweepWidget::removeTask()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_tasks.size()) return;
    stopPick();
    m_tasks.removeAt(row);
    reconcileSweepDirections();
    saveState();
    refreshTable();
    if (!m_tasks.isEmpty())
    {
        const int next = qMin(row, m_tasks.size() - 1);
        m_table->setCurrentCell(next, 0);
        m_table->selectRow(next);
        applyTaskToEditors(next);
    }
    else
    {
        m_objectId->setValue(0);
        m_sourceFace->setValue(0);
        m_targetFace->setValue(0);
        refreshPickDisplay();
    }
}

void MeshSmeshSweepWidget::clearTasks()
{
    stopPick();
    m_tasks.clear();
    m_directionCandidates.clear();
    m_checkedDirectionKeys.clear();
    m_groupAxis = -1;
    saveState();
    refreshTable();
    m_objectId->setValue(0);
    m_sourceFace->setValue(0);
    m_targetFace->setValue(0);
    refreshPickDisplay();
}


void MeshSmeshSweepWidget::generate(bool, const QVector<SweepTask>&)
{
    syncCurrentTask();
    reconcileSweepDirections();
    refreshTable();
    if (m_tasks.isEmpty())
    {
        emit logMessage(tr("[网格] 请先选择扫掠区域。"));
        return;
    }
    int ready = 0;
    int pending = 0;
    int incompatible = 0;
    for (const auto& task : m_tasks)
    {
        if (task.partitionRegion != hasPartition())
        {
            emit logMessage(tr("[网格] 分块已改变，请重新选择扫掠区域。"));
            return;
        }
        const bool paired = task.sourceFaceId > 0 && task.targetFaceId > 0 &&
                            task.sourceFaceId != task.targetFaceId;
        if (!paired)
        {
            ++pending;
            if (!m_directionCandidates.value(directionKey(task)).isEmpty())
                ++incompatible;
            continue;
        }
        QString error;
        if (!validateTask(task, &error))
        {
            emit logMessage(tr("[网格] 区域 %1：%2").arg(task.objectId).arg(error));
            return;
        }
        ++ready;
    }
    if (pending > 0)
    {
        const QString message = incompatible > 0
            ? tr("有 %1 个区域与本组方向不兼容，另有 %2 个区域未设置方向。")
                  .arg(incompatible).arg(pending - incompatible)
            : tr("有 %1 个区域未识别到可用的扫掠方向，请移除或调整分块。")
                  .arg(pending);
        emit logMessage(QStringLiteral("[扫掠] ") + message);
        QMessageBox::warning(this, tr("扫掠网格"), message);
        return;
    }
    if (ready == 0)
    {
        emit logMessage(tr("[网格] 没有可用的扫掠区域。"));
        return;
    }
    emit computeRequested();
}
void MeshSmeshSweepWidget::setPartitionPlanIdentity(const QString& uuid)
{
    if (m_partitionPlanUuid != uuid)
    {
        m_directionCandidates.clear();
        m_checkedDirectionKeys.clear();
    }
    m_partitionPlanUuid = uuid;
    refreshMode();
}
void MeshSmeshSweepWidget::invalidatePartitionTasks()
{
    // Switching between CAD and temporary regions changes the selection space.
    // Keep density editors, but require explicit selections on the new topology.
    m_tasks.clear();
    m_directionCandidates.clear();
    m_checkedDirectionKeys.clear();
    m_groupAxis = -1;
    saveState();m_objectId->setValue(0);m_sourceFace->setValue(0);m_targetFace->setValue(0);refreshTable();refreshMode();
    const QSignalBlocker blocked(m_direction);m_direction->clear();m_direction->addItem(tr("选择区域后识别"));m_direction->setEnabled(false);
}
SmeshSweepSettings MeshSmeshSweepWidget::settings() const
{ SmeshSweepSettings s;s.uuid=getUuid();s.name=m_displayName;return s; }
QVector<SmeshSweepSettings> MeshSmeshSweepWidget::tasks() const
{
    QVector<SmeshSweepSettings> out;
    for(const auto& task:m_tasks) {
        // Pending entries remain in the editor, but are NOT passed to the
        // mesher as invalid sweeps. Unassigned solids follow the Tet path.
        if (task.sourceFaceId <= 0 || task.targetFaceId <= 0 ||
            task.sourceFaceId == task.targetFaceId) continue;
        SmeshSweepSettings s;s.uuid=getUuid()+QStringLiteral(":%1:%2").arg(task.partitionRegion).arg(task.objectId);
        s.name=m_displayName;s.partitionRegion=task.partitionRegion;s.volumeId=task.objectId;
        s.sourceFaceId=task.sourceFaceId;s.targetFaceId=task.targetFaceId;s.layers=m_layers->value();
        s.sourceSize=m_sourceSize->value();s.quadrangleSource=m_quadSource->isChecked();out.push_back(s);
    } return out;
}
QJsonObject MeshSmeshSweepWidget::saveJson()
{
    auto out=QJsonObject{{"name",m_displayName},{"tasks",sweepTasksToJson(m_tasks)},
        {"prefer_hex",true},{"layers",m_layers->value()},
        {"source_size",m_sourceSize->value()},{"quadrangle_source",m_quadSource->isChecked()}};
    // Preserve the legacy one-task fields for older project readers.
    if(m_tasks.size()==1&&!m_tasks[0].partitionRegion) {
        const auto& t=m_tasks[0];out.insert("volume_id",t.objectId);out.insert("source_face_id",t.sourceFaceId);
        out.insert("target_face_id",t.targetFaceId);out.insert("layers",m_layers->value());out.insert("source_size",m_sourceSize->value());
        out.insert("quadrangle_source",m_quadSource->isChecked());
    } return out;
}
void MeshSmeshSweepWidget::loadJson(const QJsonObject& obj)
{
    {
        const QScopedValueRollback<bool> restoring(m_restoringTask, true);
        m_displayName = obj.value("name").toString(tr("扫掠网格"));
        m_groupAxis = -1;
        m_directionCandidates.clear();
        m_checkedDirectionKeys.clear();
        m_tasks = sweepTasksFromJson(obj.value("tasks").toArray());
        const SweepTask fallback = m_tasks.isEmpty() ? SweepTask() : m_tasks.front();
        m_layers->setValue(obj.value("layers").toInt(qMax(1, fallback.layers)));
        m_sourceSize->setValue(obj.value("source_size").toDouble(fallback.sourceSize > 0.0 ? fallback.sourceSize : 5.0));
        m_quadSource->setChecked(obj.value("quadrangle_source").toBool(fallback.quadrangleSource));
        if (!obj.contains("tasks") && obj.value("volume_id").toInt() > 0)
        {
            SweepTask task;
            task.objectId = obj.value("volume_id").toInt();
            task.sourceFaceId = obj.value("source_face_id").toInt();
            task.targetFaceId = obj.value("target_face_id").toInt();
            task.layers = obj.value("layers").toInt(10);
            task.sourceSize = obj.value("source_size").toDouble(5);
            task.quadrangleSource = obj.value("quadrangle_source").toBool(true);
            m_tasks.push_back(task);
        }
        normalizeSharedParameters(); // Upgrade old per-region values to node-level values.
        if (!m_planUuid.isEmpty()) replaceSweepTasksForPlan(m_planUuid, m_tasks);
        refreshTable();
    }
    if (!m_tasks.isEmpty()) applyTaskToEditors(0);
}
void MeshSmeshSweepWidget::setDisplayName(const QString& name) { m_displayName=name; }
