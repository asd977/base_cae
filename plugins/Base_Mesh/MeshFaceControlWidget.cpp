#include "MeshCompactDoubleSpinBox.h"
#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include "core/MeshEntityIdParser.h"
#include "core/MeshSettingsUi.h"
#include "core/MeshSmeshWorkflowState.h"
#include "core/MeshLengthUnit.h"
#include "core/MeshFaceSizePolicy.h"
#include "PublicVariables.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <algorithm>
#include <cmath>


MeshFaceControlWidget::MeshFaceControlWidget(QWidget* parent)
    : BasePage("MeshFaceControlWidget"), ui(new Ui::MeshFaceControlWidget),
      m_highlightPropId("mesh.face.control.highlight")
{
    ui->setupUi(this);
    setupUiState();
    ui->verticalLayout->insertWidget(1, m_selectionPresentation.createViewSelector(this));
    MeshSettingsUi::hideExplanation(ui->checkReferenceMesh);

    connect(ui->checkReferenceMesh, &QCheckBox::toggled, this,
            [this](bool on) { m_selectionPresentation.setReferenceMeshVisible(on); });

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshFaceControlWidget::onAddControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshFaceControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this,
            &MeshFaceControlWidget::onSelectionChanged);
    connect(ui->editFaceIds, &QLineEdit::textChanged, this,
            &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinLocalSizeFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MeshFaceControlWidget::onParameterEdited);
    connect(ui->checkCurvature, &QCheckBox::toggled, this,
            &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinCurvatureLevel, qOverload<int>(&QSpinBox::valueChanged), this,
            &MeshFaceControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this,
            &MeshFaceControlWidget::onAppendSelectedEntity);
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
                        svc->highlightEntities(2, QList<int>{id}, true);
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

bool MeshFaceControlWidget::event(QEvent* ev)
{
    if (ev && ev->type() == static_cast<QEvent::Type>(QEvent::User + 421))
    {
        const int dim = property("_geometryExternalPickDim").toInt();
        const int id = property("_geometryExternalPickId").toInt();
        const bool isFace = property("_geometryExternalPickIsPartitionFace").toBool();
        const int region = property("_geometryExternalPickRegionOrdinal").toInt();
        const bool append = property("_geometryExternalPickAppend").toBool();
        if (m_partitionPickStage == PickCadFace && !isFace && dim == 2 && id > 0)
        {
            finishPartitionFacePick();
            const bool accepted = appendCadFace(id);
            if (accepted && append)
                QTimer::singleShot(0, this, [this]() { beginPartitionFacePick(); });
            return true;
        }
        if (m_partitionPickStage == PickPartitionFace && isFace && dim == 2 && id > 0 && region > 0)
        {
            QVariantMap info;
            QObject* bridge = geometryBridgeObject();
            const bool got = bridge && QMetaObject::invokeMethod(
                bridge, "externalPartitionFaceInfo", Qt::DirectConnection,
                Q_RETURN_ARG(QVariantMap, info), Q_ARG(QVariantList, partitionPlanes()),
                Q_ARG(int, region), Q_ARG(int, id));
            if (!got || !info.value(QStringLiteral("valid")).toBool())
            {
                finishPartitionFacePick();
                QMessageBox::information(this, tr("面尺寸"), tr("无法读取所选面，请重新选择。"));
                return true;
            }
            togglePartitionFaceRef(region, id);
            finishPartitionFacePick();
            if (append)
                beginPartitionFacePick();
            else
                showPartitionFaceSelection();
            return true;
        }
    }
    return BasePage::event(ev);
}

void MeshFaceControlWidget::setOwnerTask(const QString& kind, const QString& uuid, const QString& displayName)
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
    label->setText(tr("在图形区点选 CAD 面；Ctrl+单击可连续添加。网格仅供参考，不参与面拾取。"));
    updateBackendSpecificUi();
    updateEntityUiState();
    refreshButtons();
}

MeshFaceControlWidget::~MeshFaceControlWidget()
{
    beforeUnload();
    delete ui;
}
void MeshFaceControlWidget::beforeLoad()
{
    if (m_manualMinimum) {
        m_manualMinimum->setSuffix(QStringLiteral(" ") + MeshLengthUnit::symbol());
        m_manualMinimum->setToolTip(tr("本面尺寸下限，%1。")
            .arg(MeshLengthUnit::interpretation()));
    }
    m_selectionPresentation.enter();
    MeshSettingsUi::simplify(this);
    updateBackendSpecificUi();
    rebuildFaceCandidates();
    loadRulesFromService();
    // No placeholder rule: a rule is created only when the user actually selects a face.
    if (m_items.isEmpty()) onResetForm();
}
void MeshFaceControlWidget::beforeUnload()
{
    finishPartitionFacePick();
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
    m_selectionPresentation.leave();
}

void MeshFaceControlWidget::onAddControl()
{
    FaceControlItem item;
    // Preserve a preset chosen before the first face is picked.
    if (m_smeshBackend && m_absoluteFaceSize)
        item.localSizeFactor = MeshFaceSizePolicy::encodeAbsolute(m_absoluteFaceSize->value());
    else if (m_items.isEmpty())
        item.localSizeFactor = ui->spinLocalSizeFactor->value();
    // Pure NETGEN: give the selected face its own automatic lower bound so
    // a coarse 3D minimum cannot block explicit face refinement. Mixed Sweep
    // ignores face minimum rules, and must retain the inherit sentinel.
    if (!m_smeshBackend || !MeshSmeshWorkflowState::sweepTasksForPlan(m_ownerTaskUuid).isEmpty()
        || !MeshSmeshWorkflowState::partitionRulesForPlan(m_ownerTaskUuid).isEmpty())
        item.minimumSizeMode = -1;
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    const int row = m_items.size() - 1;
    ui->tableControls->selectRow(row);
    fillFormData(item);
    onSelectionChanged();
    refreshButtons();
}
void MeshFaceControlWidget::onDeleteControl()
{
    int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    m_items.removeAt(row);
    ui->tableControls->removeRow(row);
    syncRulesToService();
    onResetForm();
}
void MeshFaceControlWidget::onResetForm()
{
    ui->tableControls->clearSelection();
    ui->tableControls->setCurrentCell(-1,-1);
    fillFormData(FaceControlItem{});
    clearHighlight();
    updatePartitionRefLabel();
    refreshButtons();
    if (m_partitionPickStage == PickPartitionNone) showPartitionFaceSelection();
}
void MeshFaceControlWidget::onSelectionChanged()
{
    if (m_partitionPickStage != PickPartitionNone)
        finishPartitionFacePick();
    int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
        fillFormData(m_items[row]);
    QSet<int> selectedIds;
    QString err;
    if (row >= 0 && row < m_items.size())
        MeshEntityIdParser::parseEntityIds(m_items[row].faceIds, selectedIds, err);
    if (!selectedIds.isEmpty())
    {
        QList<int> v = selectedIds.values();
        std::sort(v.begin(), v.end());
        if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
            m_selectionPresentation.setSelectedEntities(2, v);
    }
    else
        clearHighlight();
    refreshButtons();
    if (m_partitionPickStage == PickPartitionNone)
        showPartitionFaceSelection();
    updateEffectiveSizeHint();
}
void MeshFaceControlWidget::onParameterEdited()
{
    ui->spinCurvatureLevel->setEnabled(ui->checkCurvature->isChecked());
    if (m_sizePreset) {
        const QSignalBlocker blocker(m_sizePreset);
        const int found = m_sizePreset->findData(ui->spinLocalSizeFactor->value());
        m_sizePreset->setCurrentIndex(found >= 0 ? found : m_sizePreset->findData(-1.0));
        ui->spinLocalSizeFactor->setEnabled(found < 0);
    }
    if(m_manualMinimum)m_manualMinimum->setEnabled(m_minimumMode->currentData().toInt()==1);
    updateEffectiveSizeHint();
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
    updateEffectiveSizeHint();
}

void MeshFaceControlWidget::onAppendSelectedEntity()
{
    appendCadFace(ui->cmbEntity->currentData().toInt());
}

bool MeshFaceControlWidget::appendCadFace(int id)
{
    if (id <= 0) return false;
    if (!m_entityIds.contains(QString::number(id)))
    {
        QMessageBox::warning(this, tr("面尺寸"), tr("所选面不属于当前 CAD 几何。"));
        return false;
    }
    // Pick-first UX. If this face already has a rule, select it instead of
    // creating an empty duplicate; otherwise add the first rule on demand.
    if (ui->tableControls->currentRow() < 0 || ui->tableControls->currentRow() >= m_items.size()) {
        for (int i = 0; i < m_items.size(); ++i) {
            QSet<int> existing; QString parseError;
            if (MeshEntityIdParser::parseEntityIds(m_items[i].faceIds, existing, parseError)
                && existing.contains(id)) {
                ui->tableControls->selectRow(i);
                return true;
            }
        }
        onAddControl();
    }
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size()) return false;
    QSet<int> ids;
    QString error;
    if (!m_items[row].faceIds.trimmed().isEmpty() &&
        !MeshEntityIdParser::parseEntityIds(m_items[row].faceIds, ids, error))
    {
        QMessageBox::warning(this, tr("面尺寸"), error);
        return false;
    }
    if (ids.contains(id))
        return true;
    ids.insert(id);
    QList<int> sortedIds = ids.values();
    std::sort(sortedIds.begin(), sortedIds.end());
    QStringList parts;
    for (int faceId : sortedIds) parts << QString::number(faceId);

    const QString oldIds = ui->editFaceIds->text();
    {
        const QSignalBlocker blocker(ui->editFaceIds);
        ui->editFaceIds->setText(parts.join(QLatin1Char(',')));
    }
    if (!validateInput(error, row))
    {
        const QSignalBlocker blocker(ui->editFaceIds);
        ui->editFaceIds->setText(oldIds);
        QMessageBox::warning(this, tr("面尺寸"), error);
        return false;
    }
    m_items[row].faceIds = ui->editFaceIds->text();
    appendOrUpdateTableRow(row, m_items[row]);
    syncRulesToService();
    onSelectionChanged();
    return true;
}

void MeshFaceControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(2);
    ui->tableControls->setHorizontalHeaderLabels({tr("面"), tr("尺寸设置")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Legacy storage is a factor: its stock two-decimal precision would
    // silently round an entered CAD target such as 3 / 80 to 0.04 (3.2).
    ui->spinLocalSizeFactor->setDecimals(12);
    ui->spinLocalSizeFactor->setMinimum(1.0e-12);
    ui->spinLocalSizeFactor->setToolTip(tr("旧工程兼容参数：父 Mesh 最大尺寸的比例。SMESH 模式请直接输入面目标尺寸。"));
    ui->labelLocalSizeFactor->setToolTip(ui->spinLocalSizeFactor->toolTip());
    m_sizePreset = new QComboBox(ui->groupBoxInput);
    m_sizePreset->addItem(tr("按父 Mesh 尺寸"), 1.0);
    m_sizePreset->addItem(tr("适度加密（75%）"), .75);
    m_sizePreset->addItem(tr("加密（50%）"), .5);
    m_sizePreset->addItem(tr("强加密（25%）"), .25);
    m_sizePreset->addItem(tr("自定义比例"), -1.0);
    m_sizePresetLabel = new QLabel(tr("局部细化"), ui->groupBoxInput);
    ui->gridLayout->addWidget(m_sizePresetLabel, 9, 0);
    // SMESH uses a direct target-size editor, legacy backends still expose
    // existing percentage shortcuts. Show only one editor in this grid cell.
    m_sizeModeStack = new QStackedWidget(ui->groupBoxInput);
    m_sizeModeStack->addWidget(m_sizePreset);
    ui->gridLayout->addWidget(m_sizeModeStack, 9, 1);
    m_absoluteFaceSize = new MeshCompactDoubleSpinBox(ui->groupBoxInput);
    m_absoluteFaceSize->setObjectName(QStringLiteral("meshFaceAbsoluteTargetSize"));
    m_absoluteFaceSize->setDecimals(12);
    m_absoluteFaceSize->setRange(1.0e-12, 1.0e12);
    m_absoluteFaceSize->setToolTip(tr("指定此 CAD 面使用的 NETGEN 2D 目标最大尺寸（工程长度单位）。它不随父 Mesh 最大尺寸变化；体网格仍需与面网格协调过渡。"));
    m_sizeModeStack->addWidget(m_absoluteFaceSize);
    connect(m_absoluteFaceSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double target) {
        if (!m_smeshBackend) return;
        if (std::isfinite(target) && target > 0.0)
            onParameterEdited();
    });
    connect(m_sizePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const double ratio = m_sizePreset->currentData().toDouble();
        if (ratio > 0.0) ui->spinLocalSizeFactor->setValue(ratio);
        ui->spinLocalSizeFactor->setEnabled(ratio < 0.0);
    });
    m_effectiveSizeHint = new QLabel(ui->groupBoxInput);
    m_effectiveSizeHint->setObjectName(QStringLiteral("meshFaceEffectiveSizePreview"));
    m_effectiveSizeHint->setWordWrap(true);
    ui->gridLayout->addWidget(m_effectiveSizeHint, 10, 0, 1, 2);
    m_advancedFaceOptions = new QCheckBox(tr("显示曲率与最小尺寸高级控制"), ui->groupBoxInput);
    ui->gridLayout->addWidget(m_advancedFaceOptions, 11, 0, 1, 2);
    connect(m_advancedFaceOptions, &QCheckBox::toggled, this, [this](bool) { updateBackendSpecificUi(); });
    ui->spinCurvatureLevel->setToolTip(tr("完整圆周 2π 的目标分段数。SMESH 整体四面体支持；数值越大，曲面附近通常越细，实际分段由网格器确定。"));
    ui->labelCurvatureLevel->setToolTip(ui->spinCurvatureLevel->toolTip());
    ui->checkCurvature->setToolTip(tr("在曲率变化较大的位置自动加密。是否显示该选项取决于当前网格方法。"));
    ui->labelSmeshCurvatureHint->setToolTip(tr("局部曲率规则独立于整体曲率开关，多个规则重叠时保留较高细度。"));
    m_partitionPickCaption = new QLabel(tr("图形区选择"), ui->groupBoxInput);
    auto* row = new QHBoxLayout();
    m_pickPartitionFace = new QPushButton(tr("选择面"), ui->groupBoxInput);
    m_clearPartitionFaces = new QPushButton(tr("清空"), ui->groupBoxInput);
    row->addWidget(m_pickPartitionFace);
    row->addWidget(m_clearPartitionFaces);
    ui->gridLayout->addWidget(m_partitionPickCaption, 5, 0);
    ui->gridLayout->addLayout(row, 5, 1);
    m_partitionRefLabel = new QLabel(tr("未选择"), ui->groupBoxInput);
    m_partitionRefLabel->setWordWrap(true);
    m_partitionRefLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    ui->gridLayout->addWidget(m_partitionRefLabel, 6, 0, 1, 2);
    m_minimumLabel=new QLabel(tr("本面最小尺寸"),ui->groupBoxInput);
    m_minimumMode=new QComboBox(ui->groupBoxInput);m_minimumMode->setObjectName("faceMinimumMode");
    m_minimumMode->addItem(tr("继承基础设置"),-1);
    m_minimumMode->addItem(tr("自动（NETGEN 原生）"),0);
    m_minimumMode->addItem(tr("手动"),1);
    m_minimumMode->setToolTip(tr("只设置所选面的 NETGEN 参数，不改变其他面或体内部的最小尺寸。继承较大的下限可能限制曲率加密。"));
    m_manualMinimumLabel=new QLabel(tr("本面手动最小尺寸"),ui->groupBoxInput);
    m_manualMinimum=new MeshCompactDoubleSpinBox(ui->groupBoxInput);m_manualMinimum->setObjectName("faceManualMinimum");
    m_manualMinimum->setDecimals(6);m_manualMinimum->setRange(0,1e12);
    ui->gridLayout->addWidget(m_minimumLabel,7,0);ui->gridLayout->addWidget(m_minimumMode,7,1);
    ui->gridLayout->addWidget(m_manualMinimumLabel,8,0);ui->gridLayout->addWidget(m_manualMinimum,8,1);
    connect(m_minimumMode,qOverload<int>(&QComboBox::currentIndexChanged),this,&MeshFaceControlWidget::onParameterEdited);
    connect(m_manualMinimum,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&MeshFaceControlWidget::onParameterEdited);
    connect(m_pickPartitionFace, &QPushButton::clicked, this, &MeshFaceControlWidget::beginPartitionFacePick);
    connect(m_clearPartitionFaces, &QPushButton::clicked, this, &MeshFaceControlWidget::clearPartitionFaceRefs);
    updateBackendSpecificUi();
    onResetForm();
    rebuildFaceCandidates();
}
bool MeshFaceControlWidget::validateInput(QString& err, int ignoreRow) const
{
    const int row = ui->tableControls->currentRow();
    const QString partitionRefs = (row >= 0 && row < m_items.size())
        ? normalizedPartitionRefs(m_items[row].partitionFaceRefs) : QString();
    const QString faceIds = ui->editFaceIds->text().trimmed();
    if (faceIds.isEmpty() && partitionRefs.isEmpty())
    {
        err = tr("请先选择原始面或分块面。");
        return false;
    }
    QSet<int> currentIds;
    if (!faceIds.isEmpty() && !MeshEntityIdParser::parseEntityIds(faceIds, currentIds, err))
        return false;
    for (int i = 0; i < m_items.size(); ++i)
    {
        if (i == ignoreRow)
            continue;
        QSet<int> existingIds;
        QString parseErr;
        if (!MeshEntityIdParser::parseEntityIds(m_items[i].faceIds, existingIds, parseErr))
            continue;
        QSet<int> overlap = currentIds & existingIds;
        if (!overlap.isEmpty())
        {
            QList<int> ids = overlap.values();
            std::sort(ids.begin(), ids.end());
            QStringList overlapStr;
            for (int id : ids)
                overlapStr << QString::number(id);
            err = tr("面 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。")
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
            const QString other = normalizedPartitionRefs(m_items[i].partitionFaceRefs);
            for (const QString& ref : other.split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                if (currentRefs.contains(ref))
                {
                    err = tr("分块面 %1 已在第 %2 条规则中设置。").arg(ref).arg(i + 1);
                    return false;
                }
            }
        }
    }
    return true;
}
MeshFaceControlWidget::FaceControlItem MeshFaceControlWidget::collectFormData() const
{
    FaceControlItem item;
    item.faceIds = ui->editFaceIds->text().trimmed();
    const int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size()) item.partitionFaceRefs = normalizedPartitionRefs(m_items[row].partitionFaceRefs);
    item.localSizeFactor = m_smeshBackend && m_absoluteFaceSize
        ? MeshFaceSizePolicy::encodeAbsolute(m_absoluteFaceSize->value())
        : ui->spinLocalSizeFactor->value();
    item.curvatureRefine = ui->checkCurvature->isChecked();
    item.curvatureElementsPer2Pi = ui->spinCurvatureLevel->value();
    item.minimumSizeMode=m_minimumMode->currentData().toInt();
    item.manualMinimumSize=m_manualMinimum->value();
    return item;
}
void MeshFaceControlWidget::fillFormData(const FaceControlItem& item)
{
    // Loading a different row must not write the previous row's controls into it.
    const QSignalBlocker ids(ui->editFaceIds),factor(ui->spinLocalSizeFactor);
    const QSignalBlocker absoluteSize(m_absoluteFaceSize);
    const QSignalBlocker curvature(ui->checkCurvature),segments(ui->spinCurvatureLevel);
    const QSignalBlocker minimumMode(m_minimumMode),minimumValue(m_manualMinimum);
    ui->editFaceIds->setText(item.faceIds);
    const double parentMax = parentEffectiveMaxSize();
    const double ratio = MeshFaceSizePolicy::ratio(item.localSizeFactor, parentMax);
    ui->spinLocalSizeFactor->setValue(std::isfinite(ratio) && ratio > 0.0 ? ratio : 0.5);
    syncAbsoluteFaceSize();
    if (m_sizePreset) {
        const QSignalBlocker blocker(m_sizePreset);
        const int found = m_sizePreset->findData(ui->spinLocalSizeFactor->value());
        m_sizePreset->setCurrentIndex(found >= 0 ? found : m_sizePreset->findData(-1.0));
        ui->spinLocalSizeFactor->setEnabled(found < 0);
    }
    if (m_advancedFaceOptions) {
        const QSignalBlocker blocker(m_advancedFaceOptions);
        m_advancedFaceOptions->setChecked(item.curvatureRefine || item.minimumSizeMode == 1);
    }
    updateBackendSpecificUi();
    updateEffectiveSizeHint();
    updatePartitionRefLabel();
    ui->checkCurvature->setChecked(item.curvatureRefine);
    ui->spinCurvatureLevel->setValue(item.curvatureElementsPer2Pi);
    const int modeIdx = m_minimumMode->findData(item.minimumSizeMode);
    m_minimumMode->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);
    m_manualMinimum->setValue(item.manualMinimumSize);
    m_manualMinimum->setEnabled(item.minimumSizeMode==1);
    ui->spinCurvatureLevel->setEnabled(item.curvatureRefine);
}
void MeshFaceControlWidget::appendOrUpdateTableRow(int row, const FaceControlItem& item)
{
    const QString factor = QString::number(
        MeshFaceSizePolicy::ratio(item.localSizeFactor, parentEffectiveMaxSize()), 'g', 6);
    QStringList targets;
    if (!item.faceIds.trimmed().isEmpty())
        targets << (!partitionPlanes().isEmpty()
            ? tr("原始面 %1（分块后不生效）").arg(item.faceIds.trimmed())
            : item.faceIds.trimmed());
    const QStringList partitionRefs = normalizedPartitionRefs(item.partitionFaceRefs)
        .split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (!partitionRefs.isEmpty())
        targets << tr("已选 %1 个面").arg(partitionRefs.size());
    const QString targetText = targets.join(QStringLiteral("; "));
    const QString curvature = item.curvatureRefine ? tr("圆周%1").arg(item.curvatureElementsPer2Pi) : tr("局部曲率关");
    const QString minimum=item.minimumSizeMode<0?tr("下限继承"):
        (item.minimumSizeMode==0?tr("本面下限自动"):tr("本面下限%1").arg(item.manualMinimumSize));
    const QString constraint = m_smeshBackend && parentEffectiveMaxSize() > 0.0
        ? tr("面目标 %1（%2）；%3；%4")
            .arg(MeshFaceSizePolicy::target(item.localSizeFactor, parentEffectiveMaxSize()), 0, 'g', 8)
            .arg(MeshLengthUnit::symbol(), curvature, minimum)
        : tr("尺寸 ×%1；%2；%3").arg(factor, curvature, minimum);
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(targetText);
        ui->tableControls->item(row, 1)->setText(constraint);
        return;
    }
    int r = ui->tableControls->rowCount();
    ui->tableControls->insertRow(r);
    ui->tableControls->setItem(r, 0, new QTableWidgetItem(targetText));
    ui->tableControls->setItem(r, 1, new QTableWidgetItem(constraint));
}
void MeshFaceControlWidget::refreshButtons()
{
    const bool hasSelection = ui->tableControls->currentRow() >= 0;
    const bool partitioned = !partitionPlanes().isEmpty();
    ui->btnAdd->setEnabled(true);
    ui->btnDelete->setEnabled(hasSelection);
    if (m_pickPartitionFace) m_pickPartitionFace->setEnabled(partitioned || !m_entityIds.isEmpty());
    if (!hasSelection && m_clearPartitionFaces) m_clearPartitionFaces->setEnabled(false);
}

void MeshFaceControlWidget::updateBackendSpecificUi()
{
    const QVariant dimValue = PublicVariables::instance().get(QStringLiteral("mesh.global.dimension"));
    const int dim = dimValue.isValid() ? dimValue.toInt() : 3;
    const QVariant backendValue = PublicVariables::instance().get(QStringLiteral("mesh.global.backend"));
    const int backend = backendValue.isValid() ? backendValue.toInt() : 0;
    m_smeshBackend = (dim == 3 && backend == 0);

    // Both tetrahedral backends use this persisted circle precision. NETGEN
    // consumes segments per radius, so the bridge converts units explicitly.
    const bool mixed=m_smeshBackend && (!MeshSmeshWorkflowState::sweepTasksForPlan(m_ownerTaskUuid).isEmpty()
        || !MeshSmeshWorkflowState::partitionRulesForPlan(m_ownerTaskUuid).isEmpty());
    const bool advanced = !m_smeshBackend || (m_advancedFaceOptions && m_advancedFaceOptions->isChecked());
    if (m_advancedFaceOptions) m_advancedFaceOptions->setVisible(m_smeshBackend && !mixed);
    ui->labelCurvatureLevel->setVisible(!mixed && advanced);
    ui->spinCurvatureLevel->setVisible(!mixed && advanced);
    ui->checkCurvature->setVisible(!mixed && advanced);
    ui->spinCurvatureLevel->setEnabled(ui->checkCurvature->isChecked());
    ui->labelSmeshCurvatureHint->setVisible(m_smeshBackend && advanced);
    ui->labelSmeshCurvatureHint->setText(mixed
        ? tr("扫掠混合模式仅应用尺寸比例；本面独立曲率与下限暂不参与计算。")
        : tr("面尺寸仅作用于选中 CAD 面。曲率细化默认关闭；如需更高曲面精度可单独开启。共享面仍只生成一次。"));
    if(m_minimumMode){const bool show=m_smeshBackend && !mixed && advanced;
        m_minimumMode->setVisible(show);m_minimumLabel->setVisible(show);
        m_manualMinimum->setVisible(show);m_manualMinimumLabel->setVisible(show);}

    if (m_absoluteFaceSize) {
        m_sizePresetLabel->setText(m_smeshBackend ? tr("面目标最大尺寸") : tr("局部细化"));
        m_sizeModeStack->setCurrentWidget(m_smeshBackend ?
            static_cast<QWidget*>(m_absoluteFaceSize) : static_cast<QWidget*>(m_sizePreset));
        ui->spinLocalSizeFactor->setVisible(!m_smeshBackend);
        ui->labelLocalSizeFactor->setVisible(!m_smeshBackend);
        syncAbsoluteFaceSize();
    }
    if (m_smeshBackend)
    {
        ui->spinLocalSizeFactor->setToolTip(tr(
            "旧工程的比例规则可以继续读取；新规则按 CAD 绝对目标尺寸保存，不受父 Mesh 尺寸改变影响。"));
    }
    else
    {
        ui->spinLocalSizeFactor->setToolTip(tr(
            "相对基础网格尺寸的尺寸比例。1.0 表示保持不变，0.5 表示目标尺寸约为基础网格的一半。"));
    }
    ui->labelLocalSizeFactor->setToolTip(ui->spinLocalSizeFactor->toolTip());
    updateEffectiveSizeHint();
}

double MeshFaceControlWidget::parentEffectiveMaxSize() const
{
    auto& pv = PublicVariables::instance();
    const QVariant maxValue = pv.get(QStringLiteral("mesh.global.lenMax"));
    const QVariant scaleValue = pv.get(QStringLiteral("mesh.global.sizeFactor"));
    const double maximum = maxValue.isValid() ? maxValue.toDouble() : 5.0;
    const double scale = scaleValue.isValid() ? scaleValue.toDouble() : 1.0;
    return (std::isfinite(maximum) && maximum > 0.0 &&
            std::isfinite(scale) && scale > 0.0) ? maximum * scale : 0.0;
}

void MeshFaceControlWidget::syncAbsoluteFaceSize()
{
    if (!m_absoluteFaceSize) return;
    const double maximum = parentEffectiveMaxSize();
    if (maximum <= 0.0) { m_absoluteFaceSize->setEnabled(false); return; }
    const QSignalBlocker guard(m_absoluteFaceSize);
    m_absoluteFaceSize->setEnabled(m_smeshBackend);
    m_absoluteFaceSize->setMaximum(1.0e12);
    const int row = ui->tableControls->currentRow();
    const double encoded = row >= 0 && row < m_items.size()
        ? m_items[row].localSizeFactor : ui->spinLocalSizeFactor->value();
    const double target = MeshFaceSizePolicy::target(encoded, maximum);
    m_absoluteFaceSize->setValue(std::isfinite(target) && target > 0.0
        ? target : maximum * 0.5);
}

void MeshFaceControlWidget::updateEffectiveSizeHint()
{
    if (!m_effectiveSizeHint) return;
    const double maximum = parentEffectiveMaxSize();
    if (maximum <= 0.0) {
        m_effectiveSizeHint->setText(tr("父 Mesh 尺寸无效，请先设置全局最大尺寸。"));
        return;
    }
    const double ratio = ui->spinLocalSizeFactor->value();
    const double requested = m_smeshBackend && m_absoluteFaceSize
        ? m_absoluteFaceSize->value() : maximum * ratio;
    if (m_smeshBackend) {
        m_effectiveSizeHint->setText(tr("面目标最大尺寸 %1（%2）；父级最大尺寸 %3。由该面独立 2D 假设控制，邻近体单元需满足共形过渡。")
            .arg(requested, 0, 'g', 8).arg(MeshLengthUnit::symbol())
            .arg(maximum, 0, 'g', 8));
    } else {
        const double target = std::min(maximum, requested);
        m_effectiveSizeHint->setText(tr("该面目标上限：%1 × %2 = %3（%4）。")
            .arg(maximum, 0, 'g', 6).arg(ratio, 0, 'g', 5)
            .arg(target, 0, 'g', 6).arg(MeshLengthUnit::symbol()));
    }
}

void MeshFaceControlWidget::rebuildFaceCandidates()
{
    ui->cmbEntity->clear();
    m_entityIds.clear();
    QHash<int, QString> owningGeometry;
    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>()) {
        m_entityIds = svc->listEntityIds(2);
        for (const auto& item : svc->entityBindings(2)) {
            const QJsonObject binding = item.toObject();
            const int id = binding.value(QStringLiteral("id")).toInt();
            if (id > 0)
                owningGeometry.insert(id, binding.value(QStringLiteral("name")).toString());
        }
    }
    for (const auto& id : m_entityIds) {
        const QString owner = owningGeometry.value(id.toInt()).trimmed();
        const QString label = owner.isEmpty() ? tr("面 #%1").arg(id)
            : tr("面 #%1 · %2").arg(id, owner);
        ui->cmbEntity->addItem(label, id.toInt());
        ui->cmbEntity->setItemData(ui->cmbEntity->count() - 1,
            tr("原始 CAD 面编号；所属对象：%1。共享接触面可能有两个不同实体的原始编号。")
                .arg(owner.isEmpty() ? tr("未知") : owner), Qt::ToolTipRole);
    }
    updateEntityUiState();
    onSelectionChanged();
}
void MeshFaceControlWidget::clearHighlight()
{
    m_selectionPresentation.setSelectedEntities(2, {});
    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
        svc->clearEntityHighlight();
}
void MeshFaceControlWidget::updateEntityUiState()
{
    const bool hasEntities = !m_entityIds.isEmpty();
    const bool partitioned = !partitionPlanes().isEmpty();

    // Same interaction as edge sizing: after partitioning, expose the topology the
    // user can actually control and keep Region:Face bookkeeping internal.
    ui->labelFaceIds->setVisible(!partitioned);
    ui->cmbEntity->setVisible(!partitioned);
    ui->btnAppendEntity->setVisible(!partitioned);
    ui->label->setVisible(!partitioned);
    ui->editFaceIds->setVisible(!partitioned);
    ui->cmbEntity->setEnabled(hasEntities && !partitioned);
    ui->btnAppendEntity->setEnabled(hasEntities && !partitioned);
    ui->editFaceIds->setEnabled(!partitioned);

    if (m_partitionPickCaption) m_partitionPickCaption->setVisible(true);
    if (m_pickPartitionFace) {
        m_pickPartitionFace->setVisible(true);
        m_pickPartitionFace->setText(partitioned ? tr("选择分块面") : tr("图形区点选"));
    }
    if (m_clearPartitionFaces) m_clearPartitionFaces->setVisible(partitioned);
    if (m_partitionRefLabel) m_partitionRefLabel->setVisible(partitioned);
}
void MeshFaceControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc || m_ownerTaskKind.isEmpty())
        return;

    auto rules = svc->faceControlRules();
    rules.erase(std::remove_if(rules.begin(), rules.end(), [this](const FaceControlRule& r)
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
        if (it.faceIds.trimmed().isEmpty() && normalizedPartitionRefs(it.partitionFaceRefs).isEmpty())
            continue;
        FaceControlRule r;
        r.planUuid = m_ownerTaskUuid;
        r.ownerTaskKind = m_ownerTaskKind;
        r.ownerTaskUuid = m_ownerTaskUuid;
        r.faceIds = it.faceIds;
        r.partitionFaceRefs = normalizedPartitionRefs(it.partitionFaceRefs);
        r.localSizeFactor = it.localSizeFactor;
        r.curvatureRefine = it.curvatureRefine;
        r.curvatureElementsPer2Pi = it.curvatureElementsPer2Pi;
        r.minimumSizeMode=it.minimumSizeMode;
        r.manualMinimumSize=it.manualMinimumSize;
        rules.push_back(r);
    }
    svc->setFaceControlRules(rules);
    m_selectionPresentation.refresh();
}

void MeshFaceControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc)
        return;
    const auto rules = svc->faceControlRules();
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

        FaceControlItem i;
        i.faceIds = r.faceIds;
        i.partitionFaceRefs = normalizedPartitionRefs(r.partitionFaceRefs);
        i.localSizeFactor = r.localSizeFactor;
        i.curvatureRefine = r.curvatureRefine;
        i.curvatureElementsPer2Pi = r.curvatureElementsPer2Pi;
        i.minimumSizeMode=r.minimumSizeMode;
        i.manualMinimumSize=r.manualMinimumSize;
        m_items.push_back(i);
        appendOrUpdateTableRow(ui->tableControls->rowCount(), i);
    }
    if (!m_items.isEmpty())
        ui->tableControls->selectRow(0);
    else
        refreshButtons();
}

QObject* MeshFaceControlWidget::geometryBridgeObject() const
{
    auto* svc = ServiceRegistry::instance().getService<PreProcessingService>();
    return svc ? dynamic_cast<QObject*>(svc) : nullptr;
}

QVariantList MeshFaceControlWidget::partitionPlanes() const
{
    return MeshSmeshWorkflowState::planePayload(
        MeshSmeshWorkflowState::partitionRulesForPlan(m_ownerTaskUuid));
}

QString MeshFaceControlWidget::normalizedPartitionRefs(const QString& text)
{
    QSet<QString> refs;
    for (const QString& raw : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QStringList p = raw.trimmed().split(QLatin1Char(':'));
        if (p.size() != 2) continue;
        bool okR = false, okF = false;
        const int r = p[0].toInt(&okR), f = p[1].toInt(&okF);
        if (okR && okF && r > 0 && f > 0) refs.insert(QStringLiteral("%1:%2").arg(r).arg(f));
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

void MeshFaceControlWidget::beginPartitionFacePick()
{
    // First press on Pick creates an editable rule if none exists.
    if (ui->tableControls->currentRow() < 0 || ui->tableControls->currentRow() >= m_items.size())
        onAddControl();
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    const QVariantList pp = partitionPlanes();
    if (pp.isEmpty())
    {
        finishPartitionFacePick();
        m_selectionPresentation.enter(ui->checkReferenceMesh->isChecked());
        QObject* bridge = geometryBridgeObject();
        bool started = false;
        const bool invoked = bridge && QMetaObject::invokeMethod(
            bridge, "beginExternalEntityPick", Qt::DirectConnection,
            Q_RETURN_ARG(bool, started), Q_ARG(QObject*, static_cast<QObject*>(this)),
            Q_ARG(int, 2));
        if (!invoked || !started)
        {
            QMessageBox::information(this, tr("面尺寸"),
                tr("无法进入 CAD 面点选，请确认原始几何已加载并显示。"));
            return;
        }
        m_partitionPickStage = PickCadFace;
        if (m_pickPartitionFace) m_pickPartitionFace->setText(tr("正在点选…"));
        return;
    }
    finishPartitionFacePick();
    QObject* bridge = geometryBridgeObject();
    bool started = false;
    const QString selectedRefs = normalizedPartitionRefs(m_items[row].partitionFaceRefs);
    const bool invoked = bridge && QMetaObject::invokeMethod(
        bridge, "beginExternalPartitionFaceScenePick", Qt::DirectConnection,
        Q_RETURN_ARG(bool, started), Q_ARG(QObject*, static_cast<QObject*>(this)),
        Q_ARG(QVariantList, pp), Q_ARG(bool, false), Q_ARG(QString, selectedRefs));
    if (!invoked || !started)
    {
        QMessageBox::information(this, tr("面尺寸"), tr("无法进入面选择，请检查当前分块是否有效。"));
        return;
    }
    m_partitionPickStage = PickPartitionFace;
    m_partitionPickRegion = -1;
    m_pickPartitionFace->setText(tr("正在选择面…"));
}

void MeshFaceControlWidget::finishPartitionFacePick()
{
    if (QObject* bridge = geometryBridgeObject())
    {
        QMetaObject::invokeMethod(bridge, "stopExternalEntityPick", Qt::DirectConnection,
                                  Q_ARG(QObject*, static_cast<QObject*>(this)));
        QMetaObject::invokeMethod(bridge, "clearExternalPartitionRegionPreview", Qt::DirectConnection);
    }
    m_partitionPickStage = PickPartitionNone;
    m_partitionPickRegion = -1;
    if (m_pickPartitionFace) m_pickPartitionFace->setText(
        partitionPlanes().isEmpty() ? tr("图形区点选") : tr("选择分块面"));
}

void MeshFaceControlWidget::togglePartitionFaceRef(int region, int face)
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    const QString oldRefs = m_items[row].partitionFaceRefs;
    QSet<QString> refs;
    for (const QString& ref : normalizedPartitionRefs(oldRefs).split(QLatin1Char(','), Qt::SkipEmptyParts)) refs.insert(ref);
    const QString ref = QStringLiteral("%1:%2").arg(region).arg(face);
    if (refs.contains(ref)) refs.remove(ref); else refs.insert(ref);
    m_items[row].partitionFaceRefs = normalizedPartitionRefs(QStringList(refs.values()).join(QLatin1Char(',')));
    QString err;
    if (!validateInput(err, row))
    {
        m_items[row].partitionFaceRefs = oldRefs;
        QMessageBox::warning(this, tr("面尺寸"), err);
        return;
    }
    appendOrUpdateTableRow(row, m_items[row]);
    syncRulesToService();
    updatePartitionRefLabel();
}

void MeshFaceControlWidget::clearPartitionFaceRefs()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    m_items[row].partitionFaceRefs.clear();
    appendOrUpdateTableRow(row, m_items[row]);
    syncRulesToService();
    updatePartitionRefLabel();
    if (m_partitionPickStage == PickPartitionNone) showPartitionFaceSelection();
}

void MeshFaceControlWidget::showPartitionFaceSelection()
{
    QObject* bridge = geometryBridgeObject();
    if (!bridge) return;
    const int row = ui->tableControls->currentRow();
    const QString refs = (row >= 0 && row < m_items.size())
        ? normalizedPartitionRefs(m_items[row].partitionFaceRefs) : QString();
    QMetaObject::invokeMethod(bridge, "showExternalPartitionFaceRefs", Qt::DirectConnection,
                              Q_ARG(QVariantList, partitionPlanes()), Q_ARG(QString, refs));
}

void MeshFaceControlWidget::updatePartitionRefLabel()
{
    if (!m_partitionRefLabel) return;
    const int row = ui->tableControls->currentRow();
    const QString refs = (row >= 0 && row < m_items.size()) ? normalizedPartitionRefs(m_items[row].partitionFaceRefs) : QString();
    if (refs.isEmpty())
    {
        m_partitionRefLabel->setText(tr("未选择面"));
        if (m_clearPartitionFaces) m_clearPartitionFaces->setEnabled(false);
        return;
    }
    const int count = refs.split(QLatin1Char(','), Qt::SkipEmptyParts).size();
    m_partitionRefLabel->setText(tr("已选择 %1 个面").arg(count));
    if (m_clearPartitionFaces) m_clearPartitionFaces->setEnabled(true);
}
