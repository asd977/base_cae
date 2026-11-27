#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include "core/MeshEntityIdParser.h"
#include "core/MeshSettingsUi.h"

#include <QColor>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QTableWidgetItem>
#include <QComboBox>
#include <QSignalBlocker>
#include <QJsonObject>
#include <QCompleter>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <PublicVariables.h>
#include <algorithm>
#include <cmath>


MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget* parent)
    : BasePage("MeshVolumeControlWidget"), ui(new Ui::MeshVolumeControlWidget),
      m_highlightPropId("mesh.volume.control.highlight")
{
    ui->setupUi(this);
    setupUiState();
    ui->verticalLayout->insertWidget(1, m_selectionPresentation.createViewSelector(this));
    MeshSettingsUi::hideExplanation(ui->checkReferenceMesh);

    connect(ui->checkReferenceMesh, &QCheckBox::toggled, this,
            [this](bool on) { m_selectionPresentation.setReferenceMeshVisible(on); });

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshVolumeControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this,
            &MeshVolumeControlWidget::onSelectionChanged);
    connect(ui->editVolumeIds, &QLineEdit::textChanged, this,
            &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinLocalSizeFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this,
            &MeshVolumeControlWidget::onAppendSelectedEntity);
    connect(ui->cmbEntity, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() { updateEntityUiState(); });
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
                        svc->highlightEntities(3, QList<int>{id}, true);
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

void MeshVolumeControlWidget::setOwnerTask(const QString& kind, const QString& uuid, const QString& displayName)
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
    label->setText(tr("全局尺寸为基准，选中重要域加密即可。装配整体划分，公共接口保持连接；单体剖分仅用于独立检查。"));
}

MeshVolumeControlWidget::~MeshVolumeControlWidget()
{
    beforeUnload();
    delete ui;
}
void MeshVolumeControlWidget::beforeLoad()
{
    m_selectionPresentation.enter();
    MeshSettingsUi::simplify(this);
    rebuildVolumeCandidates();
    loadRulesFromService();
    if (m_items.isEmpty())
        onResetForm();
    refreshSizePreview();
    updateRuleStatus(m_items.isEmpty() ? tr("请选择一个域并点击“加入规则”；不需要先新增空规则。") : QString());
}
void MeshVolumeControlWidget::beforeUnload()
{
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
    m_selectionPresentation.leave();
}

void MeshVolumeControlWidget::onAddControl()
{
    VolumeControlItem item;
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    const int row = m_items.size() - 1;
    ui->tableControls->selectRow(row);
    fillFormData(item);
    onSelectionChanged();
    updateRuleStatus(tr("请选择域并点击“加入规则”，完成后自动保存。"));
    refreshButtons();
}
void MeshVolumeControlWidget::onDeleteControl()
{
    int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    m_items.removeAt(row);
    ui->tableControls->removeRow(row);
    syncRulesToService();
    onResetForm();
    updateRuleStatus(tr("规则已删除。"));
}
void MeshVolumeControlWidget::onResetForm()
{
    ui->editVolumeIds->clear();
    ui->spinLocalSizeFactor->setValue(1.0);
    ui->tableControls->clearSelection();
    ui->tableControls->selectionModel()->clearCurrentIndex();
    clearHighlight();
    refreshButtons();
}
void MeshVolumeControlWidget::onSelectionChanged()
{
    int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
        fillFormData(m_items[row]);
    QSet<int> selectedIds;
    QString err;
    if (row >= 0 && row < m_items.size())
        MeshEntityIdParser::parseEntityIds(m_items[row].volumeIds, selectedIds, err);
    if (!selectedIds.isEmpty())
    {
        QList<int> v = selectedIds.values();
        std::sort(v.begin(), v.end());
        if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
            m_selectionPresentation.setSelectedEntities(3, v);
    }
    else
        clearHighlight();
    refreshButtons();
    if (row < 0 || row >= m_items.size())
        updateRuleStatus(tr("请选择域并点击“加入规则”，或者新增域规则。"));
    else if (m_items[row].volumeIds.trimmed().isEmpty())
        updateRuleStatus(tr("当前规则尚未选择域，不会应用到网格。"));
    else
        updateRuleStatus(tr("当前规则已保存；修改尺寸后需重新生成网格。"));
}
void MeshVolumeControlWidget::onParameterEdited()
{
    refreshSizePreview();
    const int row = ui->tableControls->selectionModel()->hasSelection()
        ? ui->tableControls->currentRow() : -1;
    if (row < 0 || row >= m_items.size())
    {
        updateRuleStatus(tr("请选择域并点击“加入规则”，或新增域规则。"));
        return;
    }
    QString err;
    if (!validateInput(err, row))
    {
        // Keep the last valid rule active. Never silently replace it with an
        // invalid/incomplete line edit while the user is typing a range.
        updateRuleStatus(tr("尚未应用：%1（原有有效规则仍保留）").arg(err), true);
        return;
    }
    const auto item = collectFormData();
    if (m_items[row].volumeIds != item.volumeIds ||
        !qFuzzyCompare(m_items[row].localSizeFactor + 1.0, item.localSizeFactor + 1.0))
    {
        m_items[row] = item;
        appendOrUpdateTableRow(row, item);
        syncRulesToService();
    }
    updateRuleStatus(tr("已应用到当前网格方案，重新生成后生效。"));
}

void MeshVolumeControlWidget::onAppendSelectedEntity()
{
    int id = ui->cmbEntity->currentData().toInt();
    if (id <= 0 || !m_entityIds.contains(QString::number(id)))
    {
        updateRuleStatus(tr("请从搜索结果中选中一个有效域。"), true);
        return;
    }
    int row = ui->tableControls->selectionModel()->hasSelection()
        ? ui->tableControls->currentRow() : -1;
    if (row < 0 || row >= m_items.size())
    {
        // Preserve a preset chosen before the first domain is added.
        const double requestedRatio = ui->spinLocalSizeFactor->value();
        onAddControl();
        row = ui->tableControls->currentRow();
        ui->spinLocalSizeFactor->setValue(requestedRatio);
    }
    QStringList p = m_items[row].volumeIds.trimmed().split(',', Qt::SkipEmptyParts);
    QString s = QString::number(id);
    if (!p.contains(s))
    {
        const QString oldIds = m_items[row].volumeIds;
        p << s;
        m_items[row].volumeIds = p.join(',');
        ui->editVolumeIds->setText(m_items[row].volumeIds);
        QString err;
        if (!validateInput(err, row))
        {
            m_items[row].volumeIds = oldIds;
            ui->editVolumeIds->setText(oldIds);
            updateRuleStatus(err, true);
            QMessageBox::warning(this, tr("按域设置尺寸"), err);
            return;
        }
        appendOrUpdateTableRow(row, m_items[row]);
        syncRulesToService();
        onSelectionChanged();
        updateRuleStatus(tr("已添加 %1；当前规则已保存。")
            .arg(m_domainNames.value(id, tr("域 #%1").arg(id))));
    }
    else
        updateRuleStatus(tr("这个域已经包含在当前规则中。"));
}

void MeshVolumeControlWidget::setupUiState()
{
    ui->pageNameLabel->setText(tr("按域设置尺寸"));
    ui->groupBoxInput->setTitle(tr("选域与加密"));
    ui->labelVolumeIds->setText(tr("选择域"));
    ui->btnAppendEntity->setText(tr("加入规则"));
    ui->label->setText(tr("已选域编号"));
    ui->btnAdd->setText(tr("新增域规则"));
    ui->tableControls->setColumnCount(3);
    ui->tableControls->setHorizontalHeaderLabels({tr("域名称"), tr("编号"), tr("尺寸比例")});
    ui->tableControls->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableControls->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->tableControls->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->cmbEntity->setEditable(true);
    ui->cmbEntity->setInsertPolicy(QComboBox::NoInsert);
    ui->cmbEntity->completer()->setFilterMode(Qt::MatchContains);
    ui->cmbEntity->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    ui->cmbEntity->setToolTip(tr("输入名称搜索域；展开后悬停可在图形区预览。选择后点击“加入规则”，多个域可以共用一条设置。"));
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->spinLocalSizeFactor->setToolTip(tr("相对全局尺寸上限的比例。0.5 表示目标尺寸减半，体单元数量可能大幅增加。局部规则只能加密；要让其它域更粗，应增大全局基准，仅对重要域加密。曲率及接口约束仍可能要求更细网格。"));
    m_densityPreset = new QComboBox(this);
    m_densityPreset->setObjectName("domainDensityPreset");
    m_densityPreset->addItem(tr("沿用全局尺寸"), 1.0);
    m_densityPreset->addItem(tr("适度加密（尺寸 75%）"), .75);
    m_densityPreset->addItem(tr("加密（尺寸 50%）"), .5);
    m_densityPreset->addItem(tr("强加密（尺寸 25%，数量较多）"), .25);
    m_densityPreset->addItem(tr("自定义比例"), -1.0);
    ui->gridLayout->addWidget(new QLabel(tr("域内粗细"), this), 4, 0);
    ui->gridLayout->addWidget(m_densityPreset, 4, 1);
    m_sizePreview = new QLabel(this);
    m_sizePreview->setObjectName("domainSizePreview");
    m_sizePreview->setWordWrap(true);
    ui->gridLayout->addWidget(m_sizePreview, 5, 0, 1, 2);
    m_ruleStatus = new QLabel(this);
    m_ruleStatus->setObjectName("domainRuleStatus");
    m_ruleStatus->setWordWrap(true);
    ui->gridLayout->addWidget(m_ruleStatus, 6, 0, 1, 2);
    connect(m_densityPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const double factor = m_densityPreset->currentData().toDouble();
        if (factor > 0) ui->spinLocalSizeFactor->setValue(factor);
        ui->spinLocalSizeFactor->setEnabled(factor < 0);
    });
    onResetForm();
    rebuildVolumeCandidates();
}
bool MeshVolumeControlWidget::validateInput(QString& err, int ignoreRow) const
{
    if (ui->editVolumeIds->text().trimmed().isEmpty())
    {
        err = tr("请先输入或选择体 ID。");
        return false;
    }
    QSet<int> currentIds;
    if (!MeshEntityIdParser::parseEntityIds(ui->editVolumeIds->text().trimmed(), currentIds, err))
        return false;
    for (int id : currentIds)
    {
        if (!m_entityIds.contains(QString::number(id)))
        {
            err = tr("域 #%1 不在当前几何模型中，请重新选择。").arg(id);
            return false;
        }
    }
    const double factor = ui->spinLocalSizeFactor->value();
    if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0)
    {
        err = tr("尺寸比例必须大于 0 且不超过 1。");
        return false;
    }
    for (int i = 0; i < m_items.size(); ++i)
    {
        if (i == ignoreRow)
            continue;
        QSet<int> existingIds;
        QString parseErr;
        if (!MeshEntityIdParser::parseEntityIds(m_items[i].volumeIds, existingIds, parseErr))
            continue;
        QSet<int> overlap = currentIds & existingIds;
        if (!overlap.isEmpty())
        {
            QList<int> ids = overlap.values();
            std::sort(ids.begin(), ids.end());
            QStringList overlapStr;
            for (int id : ids)
                overlapStr << QString::number(id);
            err = tr("体 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。")
                      .arg(i + 1)
                      .arg(overlapStr.join(','));
            return false;
        }
    }
    return true;
}
MeshVolumeControlWidget::VolumeControlItem MeshVolumeControlWidget::collectFormData() const
{
    VolumeControlItem i;
    i.volumeIds = ui->editVolumeIds->text().trimmed();
    i.localSizeFactor = ui->spinLocalSizeFactor->value();
    return i;
}
void MeshVolumeControlWidget::fillFormData(const VolumeControlItem& i)
{
    const QSignalBlocker b0(ui->editVolumeIds), b1(ui->spinLocalSizeFactor);
    ui->editVolumeIds->setText(i.volumeIds);
    ui->spinLocalSizeFactor->setValue(i.localSizeFactor);
    refreshSizePreview();
}
void MeshVolumeControlWidget::appendOrUpdateTableRow(int row, const VolumeControlItem& i)
{
    const QString factor = QString::number(i.localSizeFactor, 'f', 2);
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(domainNames(i.volumeIds));
        ui->tableControls->item(row, 1)->setText(i.volumeIds);
        ui->tableControls->item(row, 2)->setText(factor);
        return;
    }
    int r = ui->tableControls->rowCount();
    ui->tableControls->insertRow(r);
    ui->tableControls->setItem(r, 0, new QTableWidgetItem(domainNames(i.volumeIds)));
    ui->tableControls->setItem(r, 1, new QTableWidgetItem(i.volumeIds));
    ui->tableControls->setItem(r, 2, new QTableWidgetItem(factor));
}
void MeshVolumeControlWidget::refreshButtons()
{
    const bool sel = ui->tableControls->selectionModel()->hasSelection() &&
                     ui->tableControls->currentRow() >= 0;
    ui->btnAdd->setEnabled(true);
    ui->btnDelete->setEnabled(sel);
}
void MeshVolumeControlWidget::rebuildVolumeCandidates()
{
    ui->cmbEntity->clear();
    m_entityIds.clear();
    m_domainNames.clear();
    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>()) {
        m_entityIds = svc->listEntityIds(3);
        for (const auto& value : svc->entityBindings(3)) {
            const auto binding = value.toObject();
            m_domainNames.insert(binding.value("id").toInt(), binding.value("name").toString());
        }
    }
    for (const auto& id : m_entityIds) {
        QString name = m_domainNames.value(id.toInt()).trimmed();
        if (name.isEmpty()) name = tr("未命名域");
        ui->cmbEntity->addItem(tr("%1（#%2）").arg(name).arg(id), id.toInt());
    }
    updateEntityUiState();
    onSelectionChanged();
}

QString MeshVolumeControlWidget::domainNames(const QString& text) const
{
    QSet<int> ids; QString error; QStringList names;
    if (!MeshEntityIdParser::parseEntityIds(text, ids, error)) return tr("尚未选择");
    auto ordered = ids.values(); std::sort(ordered.begin(), ordered.end());
    for (int id : ordered) names << m_domainNames.value(id, tr("域 #%1").arg(id));
    return names.join(QStringLiteral("、"));
}

void MeshVolumeControlWidget::refreshSizePreview()
{
    if (!m_sizePreview || !m_densityPreset) return;
    const double ratio = ui->spinLocalSizeFactor->value();
    const QSignalBlocker blocker(m_densityPreset);
    const int preset = m_densityPreset->findData(ratio);
    m_densityPreset->setCurrentIndex(preset < 0 ? m_densityPreset->findData(-1.0) : preset);
    ui->spinLocalSizeFactor->setEnabled(preset < 0);
    auto& pv = PublicVariables::instance();
    const double maximum = pv.get("mesh.global.lenMax").isValid() ? pv.get("mesh.global.lenMax").toDouble() : 5.0;
    const double scale = pv.get("mesh.global.sizeFactor").isValid() ? pv.get("mesh.global.sizeFactor").toDouble() : 1.0;
    m_sizePreview->setText(tr("目标尺寸：%1 × %2 × %3 = %4（模型长度单位）。\n实际尺寸仍受最小尺寸、曲率及共享接口影响。")
        .arg(maximum,0,'g',8).arg(ratio,0,'g',4).arg(scale,0,'g',4).arg(maximum*ratio*scale,0,'g',8));
}
void MeshVolumeControlWidget::clearHighlight()
{
    m_selectionPresentation.setSelectedEntities(3, {});
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
}
void MeshVolumeControlWidget::updateEntityUiState()
{
    bool has = !m_entityIds.isEmpty();
    ui->cmbEntity->setEnabled(has);
    ui->btnAppendEntity->setEnabled(has && ui->cmbEntity->currentData().toInt() > 0);
}

void MeshVolumeControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc || m_ownerTaskKind.isEmpty())
        return;

    auto rules = svc->volumeControlRules();
    rules.erase(std::remove_if(rules.begin(), rules.end(), [this](const VolumeControlRule& r)
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
        if (it.volumeIds.trimmed().isEmpty())
            continue;
        VolumeControlRule r;
        r.planUuid = m_ownerTaskUuid;
        r.ownerTaskKind = m_ownerTaskKind;
        r.ownerTaskUuid = m_ownerTaskUuid;
        r.volumeIds = it.volumeIds;
        r.localSizeFactor = it.localSizeFactor;
        rules.push_back(r);
    }
    svc->setVolumeControlRules(rules);
    m_selectionPresentation.refresh();
}

void MeshVolumeControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc)
        return;
    const auto rules = svc->volumeControlRules();
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

        VolumeControlItem i;
        i.volumeIds = r.volumeIds;
        i.localSizeFactor = r.localSizeFactor;
        m_items.push_back(i);
        appendOrUpdateTableRow(ui->tableControls->rowCount(), i);
    }
    if (!m_items.isEmpty())
        ui->tableControls->selectRow(0);
}

void MeshVolumeControlWidget::updateRuleStatus(const QString& message, bool invalid)
{
    if (!m_ruleStatus) return;
    m_ruleStatus->setText(invalid ? message : QString());
    m_ruleStatus->setStyleSheet(invalid ? QStringLiteral("color: #b45309;")
                                        : QStringLiteral("color: palette(mid);") );
}
