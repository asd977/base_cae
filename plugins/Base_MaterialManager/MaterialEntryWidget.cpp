#include "MaterialEntryWidget.h"

#include <CustomComboBox.h>

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDoubleValidator>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QScopedValueRollback>
#include <QSet>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include "MaterialManager.h"

namespace
{
constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr int FIRST_SUGGESTED_MATYPE = 3;
constexpr int MAX_SOLVER_MATYPE = 300;

bool isComputableMatype(int matype)
{
    return matype >= FIRST_COMPUTABLE_MATYPE;
}

bool isDamStaticMaterialModel(const QString& modelKey)
{
    static const QSet<QString> keys = {
        QStringLiteral("soil_eb"), QStringLiteral("soil_kg"),
        QStringLiteral("soil_emu"), QStringLiteral("soil_nanshui"),
        QStringLiteral("soil_emu_crush")};
    return keys.contains(modelKey);
}

QString matypeDisplayText(int matype, const MshPickMap* pickMap = nullptr)
{
    if (!isComputableMatype(matype))
        return QString();
    QString display;
    if (pickMap)
    {
        QString name = pickMap->physicalName(matype, 3).trimmed();
        if (name.isEmpty())
            name = pickMap->physicalName(matype).trimmed();
        if (!name.isEmpty())
            display = name;
    }
    else if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        QString name = meshService->matypePhysicalName(matype, 3).trimmed();
        if (name.isEmpty())
            name = meshService->matypePhysicalName(matype).trimmed();
        if (!name.isEmpty())
            display = name;
    }
    if (display.isEmpty())
        display = QString::number(matype);
    if (matype == 2)
        display += QStringLiteral("（保留编号，固定线弹性）");
    return display;
}
}

MaterialEntryWidget::MaterialEntryWidget(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialEntryWidget"), m_materialManager(manager), m_isUpdating(false),
      m_formLayout(new QFormLayout), m_nameEdit(nullptr)
{
    setAttribute(Qt::WA_StyledBackground, true);

    m_formLayout->setContentsMargins(9, 9, 9, 9);
    m_formLayout->setHorizontalSpacing(8);
    m_formLayout->setVerticalSpacing(6);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_formLayout->setFormAlignment(Qt::AlignTop);

    auto* container = new QWidget(this);
    container->setObjectName(QStringLiteral("materialEntryContent"));
    container->setAttribute(Qt::WA_StyledBackground, true);
    container->setLayout(m_formLayout);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    // Dock 外层已经统一提供滚动区域。材料页本身只负责内容布局，
    // 避免嵌套滚动条和 QFormLayout 将可扩展行拉出大块空白。
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container, 0, Qt::AlignTop);
    layout->addStretch(1);
}

MaterialEntryWidget::~MaterialEntryWidget()
{
    beforeUnload();
}

void MaterialEntryWidget::beforeLoad()
{
    m_beforeUnloadHandled = false;
    if (m_materialManager)
    {
        m_materialManager->setPickContext(getPageType(), m_uuid);
        if (!m_entryId.isEmpty())
        {
            MaterialEntry* entry = lookupEntry();
            const int preview = m_materialManager->previewMatypeForEntry(m_entryId);
            const int preDelete = m_materialManager->preDeleteMatypeForEntry(m_entryId);
            const bool hasHighlightState =
                entry && (!entry->appliedMatypes.isEmpty() || preview >= 0 || preDelete >= 0);
            if (hasHighlightState)
            {
                refreshMatypeHighlightSafe();
            }
        }
    }
}

void MaterialEntryWidget::beforeUnload()
{
    if (m_beforeUnloadHandled)
        return;
    m_beforeUnloadHandled = true;

    // 离开材料页面时必须清掉本页面在 PickManager 中留下的 Domain 高亮状态。
    // 只 deactivate 不会清 setAppliedDomains / preview / preDelete，容易导致颜色残留。
    m_previewMatype = -1;
    m_preDeleteMatype = -1;

    auto& pm = PickManager::instance();

    disconnect(&pm, &PickManager::domainSelected, this,
               &MaterialEntryWidget::onDomainPicked);

    pm.setAppliedDomains(getPageType(), m_uuid, QSet<DomainKey>{});
    pm.setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    pm.setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    pm.setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
    pm.clearDomainSelection(getPageType(), m_uuid);
    pm.deactivate(getPageType(), m_uuid, PickMode::Domain);

    m_pickInteractionReady = false;

    if (m_materialManager)
    {
        m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        m_materialManager->clearPickContext();
    }

    if (m_matypeCombo && m_lastHoveredIndex >= 0)
    {
        m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_lastHoveredIndex = -1;
    }

    if (m_appliedMatypesList)
    {
        for (int i = 0; i < m_appliedMatypesList->count(); ++i)
            m_appliedMatypesList->item(i)->setBackground(QBrush());
    }
}

void MaterialEntryWidget::setEntry(const QString& modelKey, const QString& entryId)
{
    // 工程树刷新时，大多数材料并没有发生变化。避免重复销毁并重建所有材料页，
    // 否则材料较多时切换模型会出现明显卡顿。
    if (m_modelKey == modelKey && m_entryId == entryId && m_nameEdit)
        return;

    m_modelKey = modelKey;
    m_entryId = entryId;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    rebuildForm(model, entry);
}

void MaterialEntryWidget::onNameEdited(const QString& text)
{
    if (m_isUpdating)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry)
        return;

    entry->name = text;
    m_materialManager->notifyEntryUpdated(m_entryId, true);
}

void MaterialEntryWidget::onNameEditingFinished()
{
    if (m_isUpdating || !m_nameEdit || !m_materialManager)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry)
        return;

    const QString uniqueName =
        m_materialManager->makeUniqueMaterialName(m_nameEdit->text(), m_entryId);
    if (entry->name == uniqueName && m_nameEdit->text() == uniqueName)
        return;

    entry->name = uniqueName;
    {
        const QSignalBlocker blocker(m_nameEdit);
        m_nameEdit->setText(uniqueName);
    }
    m_materialManager->notifyEntryUpdated(m_entryId, true);
}

void MaterialEntryWidget::onMaterialModelActivated(int)
{
    if (m_isUpdating || !m_modelCombo || !m_materialManager)
        return;

    const QString targetModelKey = m_modelCombo->currentData().toString();
    if (targetModelKey.isEmpty() || targetModelKey == m_modelKey)
        return;

    // 切换会重建当前材料页。排到本次下拉框信号处理结束后执行，
    // 避免在 QComboBox 正在发射 activated 时删除它自身。
    m_modelCombo->setEnabled(false);
    QMetaObject::invokeMethod(
        this,
        [this, targetModelKey]()
        {
            QString error;
            if (!m_materialManager->changeEntryModel(m_entryId, targetModelKey, &error))
            {
                if (m_modelCombo)
                {
                    const QSignalBlocker blocker(m_modelCombo);
                    const int oldIndex = m_modelCombo->findData(m_modelKey);
                    m_modelCombo->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
                    m_modelCombo->setEnabled(true);
                }
                QMessageBox::warning(this, QStringLiteral("切换材料模型"),
                                     error.isEmpty() ? QStringLiteral("切换材料模型失败。")
                                                     : error);
            }
        },
        Qt::QueuedConnection);
}

void MaterialEntryWidget::onValueEdited(const QString& text)
{
    if (m_isUpdating)
        return;

    auto* lineEdit = qobject_cast<QLineEdit*>(sender());
    if (!lineEdit)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!entry || !model)
        return;

    const QString column = lineEdit->property("columnName").toString();
    entry->properties.insert(column, text);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::onOptionValueChanged(int)
{
    if (m_isUpdating)
        return;

    auto* combo = qobject_cast<QComboBox*>(sender());
    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!combo || !entry || !model)
        return;

    const QString column = combo->property("columnName").toString();
    entry->properties.insert(column, combo->currentData().toString());
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::onSeepageModelChanged(int)
{
    if (m_isUpdating || !m_seepageCombo)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!entry || !model)
        return;

    entry->seepageModelKey = m_seepageCombo->currentData().toString();
    m_materialManager->notifyEntryUpdated(m_entryId, false);
    rebuildSeepageFields(entry);
}

void MaterialEntryWidget::onCreepModelChanged(int)
{
    if (m_isUpdating || !m_creepCombo)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!entry || !model)
        return;

    entry->creepModelKey = m_creepCombo->currentData().toString();
    m_materialManager->notifyEntryUpdated(m_entryId, false);
    rebuildCreepFields(entry);
}

void MaterialEntryWidget::onSeepageValueEdited(const QString& text)
{
    if (m_isUpdating)
        return;

    auto* lineEdit = qobject_cast<QLineEdit*>(sender());
    if (!lineEdit)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry)
        return;

    const QString column = lineEdit->property("columnName").toString();
    entry->seepageProperties.insert(column, text);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::rebuildSeepageFields(MaterialEntry* entry)
{
    if (!m_seepageFieldsLayout || !entry)
        return;

    while (m_seepageFieldsLayout->rowCount() > 0)
    {
        QLayoutItem* labelItem = m_seepageFieldsLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = m_seepageFieldsLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem)
            delete labelItem->widget();
        if (fieldItem)
            delete fieldItem->widget();
        m_seepageFieldsLayout->removeRow(0);
    }
    m_seepageValueEdits.clear();

    const QString seepageModelKey = m_seepageCombo ? m_seepageCombo->currentData().toString()
                                                    : entry->seepageModelKey;
    const auto& seepageModels = m_materialManager->seepageModels();
    const MaterialModel* selectedSeepageModel = nullptr;
    for (const auto& seepageModel : seepageModels)
    {
        if (seepageModel.key == seepageModelKey)
        {
            selectedSeepageModel = &seepageModel;
            break;
        }
    }
    if (m_seepageFieldsWidget && m_formLayout)
        m_formLayout->setRowVisible(m_seepageFieldsWidget, selectedSeepageModel != nullptr);
    if (!selectedSeepageModel)
        return;

    for (int columnIndex = 0; columnIndex < selectedSeepageModel->columns.size(); ++columnIndex)
    {
        const QString& column = selectedSeepageModel->columns.at(columnIndex);
        if (MaterialManager::isAutoNumberColumn(column))
        {
            continue;
        }
        if (!entry->seepageProperties.contains(column))
            entry->seepageProperties.insert(column, QStringLiteral("0"));
        auto* seepageValueEdit =
            new QLineEdit(entry->seepageProperties.value(column, QStringLiteral("0")), this);
        seepageValueEdit->setMinimumWidth(10);
        auto* validator = new QDoubleValidator(seepageValueEdit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        seepageValueEdit->setValidator(validator);
        seepageValueEdit->setProperty("columnName", column);
        connect(seepageValueEdit, &QLineEdit::textEdited, this,
                &MaterialEntryWidget::onSeepageValueEdited);
        const QString label = columnIndex < selectedSeepageModel->displayColumns.size()
                                  ? selectedSeepageModel->displayColumns.at(columnIndex)
                                  : column;
        m_seepageFieldsLayout->addRow(label, seepageValueEdit);
        m_seepageValueEdits.insert(column, seepageValueEdit);
    }
}

void MaterialEntryWidget::rebuildCreepFields(MaterialEntry* entry)
{
    if (!m_creepFieldsLayout || !entry)
        return;

    while (m_creepFieldsLayout->rowCount() > 0)
    {
        QLayoutItem* labelItem = m_creepFieldsLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = m_creepFieldsLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem)
            delete labelItem->widget();
        if (fieldItem)
            delete fieldItem->widget();
        m_creepFieldsLayout->removeRow(0);
    }
    m_creepValueEdits.clear();

    const QString creepModelKey =
        m_creepCombo ? m_creepCombo->currentData().toString() : entry->creepModelKey;
    const MaterialModel* selectedModel = nullptr;
    const auto& supplementalModels = m_materialManager->seepageModels();
    for (const auto& model : supplementalModels)
    {
        if (model.key == creepModelKey &&
            model.key != QStringLiteral("jk_corewall_steady_seepage"))
        {
            selectedModel = &model;
            break;
        }
    }
    if (m_creepFieldsWidget && m_formLayout)
        m_formLayout->setRowVisible(m_creepFieldsWidget, selectedModel != nullptr);
    if (!selectedModel)
        return;

    for (int columnIndex = 0; columnIndex < selectedModel->columns.size(); ++columnIndex)
    {
        const QString& column = selectedModel->columns.at(columnIndex);
        if (MaterialManager::isAutoNumberColumn(column))
            continue;
        if (!entry->seepageProperties.contains(column))
            entry->seepageProperties.insert(column, QStringLiteral("0"));

        auto* valueEdit =
            new QLineEdit(entry->seepageProperties.value(column, QStringLiteral("0")), this);
        valueEdit->setMinimumWidth(10);
        auto* validator = new QDoubleValidator(valueEdit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        valueEdit->setValidator(validator);
        valueEdit->setProperty("columnName", column);
        connect(valueEdit, &QLineEdit::textEdited, this,
                &MaterialEntryWidget::onSeepageValueEdited);
        const QString label = columnIndex < selectedModel->displayColumns.size()
                                  ? selectedModel->displayColumns.at(columnIndex)
                                  : column;
        m_creepFieldsLayout->addRow(label, valueEdit);
        m_creepValueEdits.insert(column, valueEdit);
    }
}

void MaterialEntryWidget::rebuildForm(MaterialModel* model, MaterialEntry* entry)
{
    m_isUpdating = true;
    clearForm();

    if (!model || !entry)
    {
        m_isUpdating = false;
        return;
    }

    // ===== 名称 =====
    m_nameEdit = new QLineEdit(entry->name, this);
    m_nameEdit->setMinimumWidth(10);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onNameEdited);
    connect(m_nameEdit, &QLineEdit::editingFinished, this,
            &MaterialEntryWidget::onNameEditingFinished);
    m_formLayout->addRow(QString("材料名称"), m_nameEdit);

    // ===== 材料模型：材料条目可直接在已有模型之间切换 =====
    m_modelCombo = new QComboBox(this);
    m_modelCombo->setMinimumWidth(10);
    m_modelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modelCombo->setMinimumContentsLength(0);
    for (const MaterialModel& availableModel : m_materialManager->models())
        m_modelCombo->addItem(availableModel.displayName, availableModel.key);
    const int modelIndex = m_modelCombo->findData(model->key);
    m_modelCombo->setCurrentIndex(modelIndex >= 0 ? modelIndex : 0);
    m_modelCombo->setToolTip(
        QStringLiteral("切换后保留材料名称、网格关联、附加模型和已有参数；新模型缺少的参数初始化为 0。"));
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::activated), this,
            &MaterialEntryWidget::onMaterialModelActivated);
    m_formLayout->addRow(QStringLiteral("材料模型"), m_modelCombo);

    // 先取 appliedList，后面初始化要用
    QList<int> appliedList = entry->appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());

    // ===== MATYPE 选择 + 施加（放到动态字段上面）=====
    QWidget* matypeRowWidget = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(matypeRowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);

    m_matypeCombo = new CustomComboBox(matypeRowWidget);
    m_matypeCombo->setMinimumWidth(84);
    m_matypeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_matypeCombo->setMinimumContentsLength(4);
    m_matypeCombo->setToolTip(
        QStringLiteral("编号 1 不参加计算；编号 2 仅兼容旧数据；新材料使用 3 至 300。"));
    m_matypeCombo->addItem(QString("未选择"), QVariant());
    m_matypeOptionsLoaded = false;
    for (int value : appliedList)
        appendMatypeOption(value);

    connect(m_matypeCombo, &CustomComboBox::itemHovered, this,
            &MaterialEntryWidget::onMatypeHovered);
    connect(m_matypeCombo, &CustomComboBox::dropdownOpened, this,
            [this]()
            {
                m_matypeDropdownOpen = true;
                ensureMatypeOptionsLoaded();
            });
    connect(m_matypeCombo, &CustomComboBox::dropdownClosed, this,
            &MaterialEntryWidget::onDropdownClosed);
    connect(m_matypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onMatypeChanged);
    connect(m_matypeCombo, QOverload<int>::of(&QComboBox::activated), this,
            &MaterialEntryWidget::onMatypeActivated);

    m_applyButton = new QPushButton(QString("施加到网格"), matypeRowWidget);
    connect(m_applyButton, &QPushButton::clicked, this, &MaterialEntryWidget::onApplyMatypeClicked);

    rowLayout->addWidget(m_matypeCombo);
    rowLayout->addWidget(m_applyButton);
    rowLayout->addStretch();

    m_formLayout->addRow(QString("关联MATYPE"), matypeRowWidget);

    // ===== 已施加 MATYPE 列表：列表占满整行，操作按钮放在下方 =====
    QWidget* appliedRowWidget = new QWidget(this);
    appliedRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* appliedLayout = new QVBoxLayout(appliedRowWidget);
    appliedLayout->setContentsMargins(0, 0, 0, 0);
    appliedLayout->setSpacing(5);

    m_appliedMatypesList = new QListWidget(appliedRowWidget);
    m_appliedMatypesList->setObjectName(QStringLiteral("appliedMatypesList"));
    m_appliedMatypesList->setFixedHeight(96);
    m_appliedMatypesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_appliedMatypesList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_appliedMatypesList->setUniformItemSizes(true);
    for (int value : appliedList)
    {
        auto* item = new QListWidgetItem(matypeDisplayText(value), m_appliedMatypesList);
        item->setData(Qt::UserRole, value);
    }

    connect(m_appliedMatypesList, &QListWidget::currentTextChanged, this,
            &MaterialEntryWidget::onAppliedMatypeSelected);

    m_removeButton = new QPushButton(QString("移除选中"), appliedRowWidget);
    connect(m_removeButton, &QPushButton::clicked, this,
            &MaterialEntryWidget::onRemoveMatypeClicked);

    auto* removeRow = new QHBoxLayout;
    removeRow->setContentsMargins(0, 0, 0, 0);
    removeRow->setSpacing(0);
    removeRow->addStretch(1);
    removeRow->addWidget(m_removeButton);

    appliedLayout->addWidget(m_appliedMatypesList);
    appliedLayout->addLayout(removeRow);

    m_formLayout->addRow(QString("已施加材料单元"), appliedRowWidget);

    const bool isDynamicMaterial = model->key == QStringLiteral("dam_dynamic");
    const bool supportsCalculationStage =
        isDynamicMaterial || isDamStaticMaterialModel(model->key);
    if (supportsCalculationStage)
    {
        QWidget* transitionRowWidget = new QWidget(this);
        auto* transitionLayout = new QHBoxLayout(transitionRowWidget);
        transitionLayout->setContentsMargins(0, 0, 0, 0);
        const QString stageUseText =
            isDynamicMaterial ? QStringLiteral("作为计算阶段材料（动力及地震波动）")
                              : QStringLiteral("作为计算阶段材料（浸润）");
        const QString stageUseTip =
            isDynamicMaterial
                ? QStringLiteral("该材料只供土石坝动力及地震波动计算的材料对应关系选择，不直接分配给网格。")
                : QStringLiteral("该材料只供土石坝浸润计算的材料对应关系选择，不直接分配给网格。");
        m_transitionOnlyCheck = new QCheckBox(stageUseText, transitionRowWidget);
        m_transitionOnlyCheck->setChecked(entry->transitionOnly);
        m_transitionOnlyCheck->setToolTip(stageUseTip);
        m_transitionMatypeSpin = new QSpinBox(transitionRowWidget);
        m_transitionMatypeSpin->setMinimumWidth(10);
        m_transitionMatypeSpin->setRange(FIRST_SUGGESTED_MATYPE, MAX_SOLVER_MATYPE);
        m_transitionMatypeSpin->setValue(isComputableMatype(entry->transitionMatype)
                                             ? entry->transitionMatype
                                             : FIRST_SUGGESTED_MATYPE);
        m_transitionMatypeSpin->setToolTip(stageUseTip);
        m_transitionMatypeSpin->setEnabled(true);
        transitionLayout->addWidget(m_transitionOnlyCheck);
        transitionLayout->addWidget(new QLabel(QStringLiteral("编号"), transitionRowWidget));
        transitionLayout->addWidget(m_transitionMatypeSpin);
        transitionLayout->addStretch();
        connect(m_transitionOnlyCheck, &QCheckBox::stateChanged, this,
                &MaterialEntryWidget::onTransitionModeChanged);
        connect(m_transitionMatypeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                &MaterialEntryWidget::onTransitionMatypeChanged);
        m_formLayout->addRow(QStringLiteral("材料用途"), transitionRowWidget);
    }
    if (m_transitionOnlyCheck && entry->transitionOnly)
    {
        if (m_matypeCombo)
            m_matypeCombo->setEnabled(false);
        if (m_applyButton)
            m_applyButton->setEnabled(false);
        if (m_appliedMatypesList)
            m_appliedMatypesList->setEnabled(false);
        if (m_removeButton)
            m_removeButton->setEnabled(false);
    }

    // ===== 动态字段（放在 MATYPE 区域下面）=====
    for (int columnIndex = 0; columnIndex < model->columns.size(); ++columnIndex)
    {
        const QString& column = model->columns.at(columnIndex);
        if (MaterialManager::isAutoNumberColumn(column))
        {
            continue;
        }
        if (model->key == QStringLiteral("dam_dynamic") &&
            column == QStringLiteral("静力计算编号"))
        {
            continue;
        }
        const QString label = columnIndex < model->displayColumns.size()
                                  ? model->displayColumns.at(columnIndex)
                                  : column;
        if (model->key == QStringLiteral("dam_dynamic") &&
            column == QStringLiteral("是否液化"))
        {
            auto* optionBox = new QComboBox(this);
            optionBox->addItem(QStringLiteral("否"), QStringLiteral("0"));
            optionBox->addItem(QStringLiteral("是"), QStringLiteral("1"));
            const int optionIndex = optionBox->findData(entry->properties.value(column));
            if (optionIndex < 0)
                entry->properties.insert(column, QStringLiteral("0"));
            optionBox->setCurrentIndex(optionIndex >= 0 ? optionIndex : 0);
            optionBox->setProperty("columnName", column);
            connect(optionBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    &MaterialEntryWidget::onOptionValueChanged);
            m_formLayout->addRow(QStringLiteral("是否液化"), optionBox);
            continue;
        }
        if (model->key == QStringLiteral("huangjinping_underground_plant") &&
            column == QStringLiteral("岩类分区"))
        {
            auto* optionBox = new QComboBox(this);
            optionBox->addItem(QStringLiteral("普通围岩"), QStringLiteral("0"));
            optionBox->addItem(QStringLiteral("半开挖区"), QStringLiteral("-1"));
            optionBox->addItem(QStringLiteral("开挖区"), QStringLiteral("-2"));
            optionBox->addItem(QStringLiteral("再生材料"), QStringLiteral("1"));
            optionBox->addItem(QStringLiteral("加固材料"), QStringLiteral("2"));
            const QString storedValue = entry->properties.value(column);
            int optionIndex = optionBox->findData(storedValue);
            if (optionIndex < 0 && !storedValue.trimmed().isEmpty())
            {
                optionBox->addItem(QStringLiteral("原工程设置（%1）").arg(storedValue),
                                   storedValue);
                optionIndex = optionBox->count() - 1;
            }
            optionBox->setCurrentIndex(optionIndex >= 0 ? optionIndex : 0);
            optionBox->setProperty("columnName", column);
            connect(optionBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    &MaterialEntryWidget::onOptionValueChanged);
            m_formLayout->addRow(QStringLiteral("开挖状态"), optionBox);
            continue;
        }
        auto* valueEdit = new QLineEdit(entry->properties.value(column), this);
        valueEdit->setMinimumWidth(10);
        auto* validator = new QDoubleValidator(valueEdit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        valueEdit->setValidator(validator);
        valueEdit->setProperty("columnName", column);
        connect(valueEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onValueEdited);
        m_formLayout->addRow(label, valueEdit);
        m_valueEdits.insert(column, valueEdit);
    }

    // ===== 渗流与流变是两个可同时启用的附加模型 =====
    m_seepageCombo = new CustomComboBox(this);
    m_seepageCombo->setMinimumWidth(10);
    m_seepageCombo->addItem(QStringLiteral("无"), QString());
    const auto& seepageModels = m_materialManager->seepageModels();
    for (const auto& seepageModel : seepageModels)
    {
        if (seepageModel.key == QStringLiteral("jk_corewall_steady_seepage"))
            m_seepageCombo->addItem(seepageModel.displayName, seepageModel.key);
    }
    int seepageIndex = m_seepageCombo->findData(entry->seepageModelKey);
    m_seepageCombo->setCurrentIndex(seepageIndex >= 0 ? seepageIndex : 0);
    connect(m_seepageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onSeepageModelChanged);
    m_formLayout->addRow(QStringLiteral("渗流模型"), m_seepageCombo);
    m_seepageFieldsWidget = new QWidget(this);
    m_seepageFieldsLayout = new QFormLayout(m_seepageFieldsWidget);
    m_seepageFieldsLayout->setContentsMargins(0, 0, 0, 0);
    m_seepageFieldsLayout->setHorizontalSpacing(8);
    m_seepageFieldsLayout->setVerticalSpacing(6);
    m_seepageFieldsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(QStringLiteral("渗流参数"), m_seepageFieldsWidget);
    rebuildSeepageFields(entry);

    m_creepCombo = new CustomComboBox(this);
    m_creepCombo->setMinimumWidth(10);
    m_creepCombo->addItem(QStringLiteral("无"), QString());
    for (const auto& creepModel : seepageModels)
    {
        if (creepModel.key != QStringLiteral("jk_corewall_steady_seepage"))
            m_creepCombo->addItem(creepModel.displayName, creepModel.key);
    }
    const int creepIndex = m_creepCombo->findData(entry->creepModelKey);
    m_creepCombo->setCurrentIndex(creepIndex >= 0 ? creepIndex : 0);
    connect(m_creepCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onCreepModelChanged);
    m_formLayout->addRow(QStringLiteral("流变模型"), m_creepCombo);
    m_creepFieldsWidget = new QWidget(this);
    m_creepFieldsLayout = new QFormLayout(m_creepFieldsWidget);
    m_creepFieldsLayout->setContentsMargins(0, 0, 0, 0);
    m_creepFieldsLayout->setHorizontalSpacing(8);
    m_creepFieldsLayout->setVerticalSpacing(6);
    m_creepFieldsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(QStringLiteral("流变参数"), m_creepFieldsWidget);
    rebuildCreepFields(entry);

    // ===== 初始化选中 MATYPE =====
    int initialMatype = m_materialManager->selectedMatypeForEntry(m_entryId);
    if (initialMatype < 0 && !appliedList.isEmpty())
    {
        initialMatype = appliedList.first();
    }

    int comboIndex = (initialMatype >= 0) ? m_matypeCombo->findData(initialMatype) : -1;
    m_matypeCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);

    m_materialManager->setSelectedMatypeForEntry(m_entryId, initialMatype);

    // 初始化：清空 preview / predelete
    m_previewMatype = -1;
    m_preDeleteMatype = -1;
    m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    m_isUpdating = false;

    updateAppliedListColors();
    // 这里仅重建页面控件，不主动触发网格高亮刷新。
    // 高亮由 beforeLoad()/用户交互路径触发，避免在“创建材料/重建树”时对未激活页面
    // 执行 PickManager 刷新导致的崩溃风险。
}

void MaterialEntryWidget::clearForm()
{
    while (m_formLayout->rowCount() > 0)
    {
        QLayoutItem* labelItem = m_formLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = m_formLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem)
            delete labelItem->widget();
        if (fieldItem)
            delete fieldItem->widget();
        m_formLayout->removeRow(0);
    }

    m_nameEdit = nullptr;
    m_modelCombo = nullptr;
    m_valueEdits.clear();
    m_seepageCombo = nullptr;
    m_seepageFieldsWidget = nullptr;
    m_seepageFieldsLayout = nullptr;
    m_seepageValueEdits.clear();
    m_creepCombo = nullptr;
    m_creepFieldsWidget = nullptr;
    m_creepFieldsLayout = nullptr;
    m_creepValueEdits.clear();

    m_matypeCombo = nullptr;
    m_applyButton = nullptr;

    m_appliedMatypesList = nullptr;
    m_removeButton = nullptr;
    m_transitionOnlyCheck = nullptr;
    m_transitionMatypeSpin = nullptr;

    m_lastHoveredIndex = -1;
    m_matypeDropdownOpen = false;
    m_matypeOptionsLoaded = false;
}

MaterialEntry* MaterialEntryWidget::lookupEntry(MaterialModel** model) const
{
    return m_materialManager->findEntry(m_entryId, model);
}

void MaterialEntryWidget::ensurePickInteractionReady()
{
    if (!m_materialManager || m_pickInteractionReady)
        return;

    // 材料页不做 Domain 鼠标点选，只使用 PickManager 的 Domain 高亮能力。
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Domain);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, false);

    disconnect(&PickManager::instance(), &PickManager::domainSelected, this,
               &MaterialEntryWidget::onDomainPicked);

    m_pickInteractionReady = true;
}

void MaterialEntryWidget::refreshMatypeHighlightSafe()
{
    ensurePickInteractionReady();
    if (m_materialManager)
        m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::appendMatypeOption(int matype)
{
    if (!m_matypeCombo || !isComputableMatype(matype))
        return;
    if (m_matypeCombo->findData(matype) >= 0)
        return;
    m_matypeCombo->addItem(matypeDisplayText(matype), matype);
}

void MaterialEntryWidget::ensureMatypeOptionsLoaded()
{
    if (!m_matypeCombo || m_matypeOptionsLoaded || !m_materialManager)
        return;

    const QVariant currentValue = m_matypeCombo->currentData();
    const QSignalBlocker blocker(m_matypeCombo);
    QList<int> matypes = m_materialManager->availableMatypes().values();
    std::sort(matypes.begin(), matypes.end());
    for (int value : matypes)
        appendMatypeOption(value);

    int currentIndex = currentValue.isValid() ? m_matypeCombo->findData(currentValue) : 0;
    if (currentIndex < 0)
        currentIndex = 0;
    m_matypeCombo->setCurrentIndex(currentIndex);
    m_matypeOptionsLoaded = true;
}

void MaterialEntryWidget::refreshAppliedMatypes(const QSet<int>& appliedMatypes, int selectedMatype)
{
    if (!m_appliedMatypesList || !m_matypeCombo)
        return;

    m_isUpdating = true;

    m_appliedMatypesList->clear();
    QList<int> appliedList = appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());
    for (int value : appliedList)
    {
        auto* item = new QListWidgetItem(matypeDisplayText(value), m_appliedMatypesList);
        item->setData(Qt::UserRole, value);
    }

    int comboIndex = (selectedMatype >= 0) ? m_matypeCombo->findData(selectedMatype) : 0;
    if (comboIndex < 0)
        comboIndex = 0;
    m_matypeCombo->setCurrentIndex(comboIndex);

    // 数据刷新后，预删除/预览都清空（避免旧状态影响）
    m_preDeleteMatype = -1;
    m_previewMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
    m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);

    m_isUpdating = false;

    updateAppliedListColors();
}

void MaterialEntryWidget::onMatypeChanged(int)
{
    if (m_isUpdating || !m_matypeCombo)
        return;

    // 下拉关闭时某些实现会额外发一次
    // currentIndexChanged（并非真实提交），直接忽略。
    if (m_ignoreNextMatypeChanged)
    {
        updateAppliedListColors();
        return;
    }

    // 下拉弹层可见时，currentIndexChanged 可能由“悬浮预览”触发，
    // 不能把该值写成 Selected（否则关闭下拉后会残留预选色/选中色）。
    if (m_matypeDropdownOpen || (m_matypeCombo->view() && m_matypeCombo->view()->isVisible()))
    {
        updateAppliedListColors();
        return;
    }

    const QVariant data = m_matypeCombo->currentData();
    const int matype = data.isValid() ? data.toInt() : -1;

    // 仅同步材料页下拉框当前值，不触发三维高亮刷新。
    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);

    updateAppliedListColors();
}

void MaterialEntryWidget::onMatypeActivated(int)
{
    if (m_isUpdating || !m_matypeCombo)
        return;

    QVariant data = m_matypeCombo->currentData();
    int matype = data.isValid() ? data.toInt() : -1;

    // 选中态：写入 Selected
    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);

    // 改选中时通常清掉预删除（更符合直觉）
    m_preDeleteMatype = -1;
    if (m_appliedMatypesList)
        m_appliedMatypesList->clearSelection();
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onApplyMatypeClicked()
{
    if (!m_matypeCombo)
        return;

    QVariant data = m_matypeCombo->currentData();
    int matype = data.isValid() ? data.toInt() : -1;

    m_materialManager->applyMatypeToEntry(m_entryId, matype);
}

void MaterialEntryWidget::onAppliedMatypeSelected(const QString& text)
{
    Q_UNUSED(text);
    if (m_isUpdating)
        return;

    auto* item = m_appliedMatypesList ? m_appliedMatypesList->currentItem() : nullptr;
    if (!item)
    {
        // 清选择 => 取消预删除
        m_preDeleteMatype = -1;
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        updateAppliedListColors();
        refreshMatypeHighlightSafe();
        return;
    }
    const int matype = item->data(Qt::UserRole).toInt();
    if (!isComputableMatype(matype))
        return;

    // ★ 只设置“预删除”，不改变 Selected
    m_preDeleteMatype = matype;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, matype);

    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onTransitionModeChanged(int state)
{
    if (m_isUpdating || m_transitionChangeInProgress)
        return;
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionOnlyCheck || !m_transitionMatypeSpin)
        return;
    QScopedValueRollback<bool> guard(m_transitionChangeInProgress, true);
    const bool enabled = (state == Qt::Checked);
    if (!enabled)
    {
        entry->transitionOnly = false;
        entry->transitionMatype = m_transitionMatypeSpin->value();
        if (m_matypeCombo)
            m_matypeCombo->setEnabled(true);
        if (m_applyButton)
            m_applyButton->setEnabled(true);
        if (m_appliedMatypesList)
            m_appliedMatypesList->setEnabled(true);
        if (m_removeButton)
            m_removeButton->setEnabled(true);
        m_materialManager->notifyEntryUpdated(entry->id, false);
        return;
    }
    QString error;
    const int matype = m_transitionMatypeSpin->value();
    if (!m_materialManager->validateTransitionMatype(m_entryId, matype, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        QSignalBlocker blocker(m_transitionOnlyCheck);
        m_transitionOnlyCheck->setCheckState(Qt::Unchecked);
        m_transitionOnlyCheck->setEnabled(false);
        entry->transitionOnly = false;
        if (m_matypeCombo)
            m_matypeCombo->setEnabled(true);
        if (m_applyButton)
            m_applyButton->setEnabled(true);
        if (m_appliedMatypesList)
            m_appliedMatypesList->setEnabled(true);
        if (m_removeButton)
            m_removeButton->setEnabled(true);
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = matype;
    m_transitionOnlyCheck->setEnabled(true);
    m_transitionMatypeSpin->setEnabled(true);
    if (m_matypeCombo)
        m_matypeCombo->setEnabled(false);
    if (m_applyButton)
        m_applyButton->setEnabled(false);
    if (m_appliedMatypesList)
        m_appliedMatypesList->setEnabled(false);
    if (m_removeButton)
        m_removeButton->setEnabled(false);
    m_materialManager->notifyEntryUpdated(entry->id, false);
}

void MaterialEntryWidget::onTransitionMatypeChanged(int value)
{
    if (m_isUpdating || m_transitionChangeInProgress)
        return;
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionOnlyCheck || !m_transitionMatypeSpin)
        return;
    if (!m_transitionOnlyCheck->isChecked())
    {
        m_transitionOnlyCheck->setEnabled(true);
        return;
    }
    QScopedValueRollback<bool> guard(m_transitionChangeInProgress, true);
    const int previousMatype = entry->transitionMatype;
    QString error;
    if (!m_materialManager->validateTransitionMatype(m_entryId, value, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        entry->transitionMatype = previousMatype;
        QSignalBlocker blocker(m_transitionMatypeSpin);
        m_transitionMatypeSpin->setValue(isComputableMatype(previousMatype)
                                             ? previousMatype
                                             : FIRST_SUGGESTED_MATYPE);
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = value;
    m_materialManager->notifyEntryUpdated(entry->id, false);
}

void MaterialEntryWidget::onMatypeHovered(int index)
{
    if (!m_matypeCombo)
        return;

    // UI：下拉项背景淡蓝（你原逻辑保留）
    if (m_lastHoveredIndex >= 0)
    {
        m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    }
    if (index >= 0)
    {
        m_matypeCombo->setItemData(index, QColor(230, 247, 255), Qt::BackgroundRole);
        m_lastHoveredIndex = index;
    }
    else
    {
        m_lastHoveredIndex = -1;
    }

    // 业务：Preview matype
    QVariant data = m_matypeCombo->itemData(index);
    int matype = data.isValid() ? data.toInt() : -1;

    m_previewMatype = matype;
    m_materialManager->setPreviewMatypeForEntry(m_entryId, matype);
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onDropdownClosed()
{
    m_matypeDropdownOpen = false;
    // 注意：CustomComboBox 在 hidePopup() 里先 emit dropdownClosed，再调用 QComboBox::hidePopup()。
    // 因此这里必须把清理/恢复放到下一轮事件循环，避免被基类后续的 index 提交覆盖。
    m_ignoreNextMatypeChanged = true;
    QTimer::singleShot(0, this,
                       [this]()
                       {
                           if (m_matypeCombo && m_lastHoveredIndex >= 0)
                           {
                               m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(),
                                                          Qt::BackgroundRole);
                               m_lastHoveredIndex = -1;
                           }

                           // 下拉关闭：无条件取消 Preview，避免悬浮高亮残留。
                           m_previewMatype = -1;
                           if (m_materialManager)
                               m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);

                           updateAppliedListColors();

                           // 卸载过程中只做状态清理，不回刷高亮，避免离开页面后颜色被重新点亮
                           if (!m_beforeUnloadHandled)
                           {
                               // 下拉关闭后取消悬浮预览，并恢复当前选中域及已施加域的显示状态。
                               refreshMatypeHighlightSafe();
                           }

                           m_ignoreNextMatypeChanged = false;
                       });
}

void MaterialEntryWidget::onRemoveMatypeClicked()
{
    if (!m_appliedMatypesList)
        return;

    auto* item = m_appliedMatypesList->currentItem();
    if (!item)
        return;

    bool ok = false;
    int matype = item->data(Qt::UserRole).toInt(&ok);
    if (!ok)
        return;

    m_materialManager->removeMatypeFromEntry(m_entryId, matype);

    // 移除后清掉预删除
    m_preDeleteMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onDomainPicked(const QString& domain, const QString& uuid, const DomainKey& key)
{
    if (!m_materialManager || !m_matypeCombo)
        return;
    if (domain != getPageType() || uuid != m_uuid)
        return;
    if (key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) != 0 ||
        !isComputableMatype(key.id))
        return;

    const int matype = key.id;
    const int comboIndex = m_matypeCombo->findData(matype);
    if (comboIndex < 0)
        return;

    if (m_appliedMatypesList)
        m_appliedMatypesList->clearSelection();
    m_preDeleteMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    {
        QSignalBlocker blocker(m_matypeCombo);
        m_matypeCombo->setCurrentIndex(comboIndex);
    }

    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);
    refreshMatypeHighlightSafe();
    updateAppliedListColors();
}

void MaterialEntryWidget::updateAppliedListColors()
{
    if (!m_appliedMatypesList)
        return;

    // 两种色：预删除(淡红)、其他(无)
    const QColor preDeleteColor(255, 230, 230);

    for (int i = 0; i < m_appliedMatypesList->count(); ++i)
    {
        auto* it = m_appliedMatypesList->item(i);
        bool ok = false;
        int v = it->data(Qt::UserRole).toInt(&ok);
        if (!ok)
        {
            it->setBackground(QBrush());
            continue;
        }

        if (m_preDeleteMatype >= 0 && v == m_preDeleteMatype)
        {
            it->setBackground(preDeleteColor);
        }
        else
        {
            it->setBackground(QBrush());
        }
    }
}
