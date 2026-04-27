#include "MaterialEntryWidget.h"

#include <CustomComboBox.h>

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QScrollArea>
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
QString domainTagDisplayText(const MeshTagSet& set)
{
    QString name = set.name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("%1=%2").arg(set.key.category).arg(set.key.id);
    return QStringLiteral("[%1] %2").arg(set.key.category, name);
}

QString domainTagDisplayText(const MeshTagKey& key)
{
    if (!key.isValid())
        return QString();
    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        const MshPickMap pick = meshService->mshPickMapSolid();
        const MeshTagSet set = pick.tagSets.value(key);
        if (set.key.isValid())
            return domainTagDisplayText(set);
    }
    return QStringLiteral("[%1] %2").arg(key.category).arg(key.id);
}

MeshTagKey comboDomainKey(QComboBox* combo)
{
    if (!combo)
        return MeshTagKey{};
    const QVariant data = combo->currentData();
    if (!data.isValid())
        return MeshTagKey{};
    return data.value<MeshTagKey>();
}

MeshTagKey itemDomainKey(QListWidgetItem* item)
{
    if (!item)
        return MeshTagKey{};
    const QVariant data = item->data(Qt::UserRole);
    if (!data.isValid())
        return MeshTagKey{};
    return data.value<MeshTagKey>();
}

int findDomainKeyIndex(QComboBox* combo, const MeshTagKey& key)
{
    if (!combo || !key.isValid())
        return -1;
    for (int i = 0; i < combo->count(); ++i)
    {
        const MeshTagKey k = combo->itemData(i).value<MeshTagKey>();
        if (k == key)
            return i;
    }
    return -1;
}

QList<MeshTagKey> sortedKeys(const QSet<MeshTagKey>& keys)
{
    QList<MeshTagKey> list = keys.values();
    std::sort(list.begin(), list.end(), [](const MeshTagKey& a, const MeshTagKey& b)
              {
                  if (a.category != b.category)
                      return a.category < b.category;
                  if (a.dim != b.dim)
                      return a.dim < b.dim;
                  return a.id < b.id;
              });
    return list;
}
} // namespace

MaterialEntryWidget::MaterialEntryWidget(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialEntryWidget"), m_materialManager(manager), m_isUpdating(false),
      m_formLayout(new QFormLayout), m_nameEdit(nullptr)
{
    auto* container = new QWidget(this);
    container->setLayout(m_formLayout);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(container);
    scrollArea->setWidgetResizable(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);
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
        PickManager::instance().activate(getPageType(), m_uuid, PickMode::Domain);
        PickManager::instance().setClickPickEnabled(getPageType(), m_uuid,
                                                    false);
        PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid,
                                                    false);
        if (!m_entryId.isEmpty())
            m_materialManager->refreshDomainTagHighlight(m_entryId);
    }
}

void MaterialEntryWidget::beforeUnload()
{
    if (m_beforeUnloadHandled)
        return;
    m_beforeUnloadHandled = true;

           // 离开页面：仅做“清理”，不要再触发恢复高亮逻辑
    m_previewDomainTag = MeshTagKey{};
    m_preDeleteDomainTag = MeshTagKey{};

    if (m_materialManager)
    {
        PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Domain);
        m_materialManager->clearPickContext();
    }

           // 仅清理 UI hover，不调用 onDropdownClosed（其末尾会 refresh 高亮）
    if (m_domainCombo && m_lastHoveredIndex >= 0)
    {
        m_domainCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_lastHoveredIndex = -1;
    }

           // list 背景也清掉
    if (m_appliedDomainList)
    {
        for (int i = 0; i < m_appliedDomainList->count(); ++i)
        {
            m_appliedDomainList->item(i)->setBackground(QBrush());
        }
    }
}

void MaterialEntryWidget::setEntry(const QString& modelKey, const QString& entryId)
{
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
    if (!selectedSeepageModel)
    {
        return;
    }

    for (const QString& column : selectedSeepageModel->columns)
    {
        if (MaterialManager::isAutoNumberColumn(column))
        {
            continue;
        }
        auto* seepageValueEdit =
            new QLineEdit(entry->seepageProperties.value(column, QStringLiteral("0")), this);
        seepageValueEdit->setProperty("columnName", column);
        connect(seepageValueEdit, &QLineEdit::textEdited, this,
                &MaterialEntryWidget::onSeepageValueEdited);
        m_seepageFieldsLayout->addRow(column, seepageValueEdit);
        m_seepageValueEdits.insert(column, seepageValueEdit);
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
    connect(m_nameEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onNameEdited);
    m_formLayout->addRow(QStringLiteral("材料名称"), m_nameEdit);

    const QList<MeshTagKey> appliedList = sortedKeys(entry->appliedDomainTags);

           // ===== 域分组选择 + 施加 =====
    QWidget* domainRowWidget = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(domainRowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    m_domainCombo = new CustomComboBox(domainRowWidget);
    m_domainCombo->addItem(QStringLiteral("未选择"), QVariant());

    const QVector<MeshTagSet> domainSets = m_materialManager->availableDomainTagSets();
    for (const MeshTagSet& set : domainSets)
    {
        m_domainCombo->addItem(domainTagDisplayText(set), QVariant::fromValue(set.key));
    }

    connect(m_domainCombo, &CustomComboBox::itemHovered, this,
            &MaterialEntryWidget::onDomainHovered);
    connect(m_domainCombo, &CustomComboBox::dropdownClosed, this,
            &MaterialEntryWidget::onDropdownClosed);
    connect(m_domainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onDomainChanged);
    connect(m_domainCombo, QOverload<int>::of(&QComboBox::activated), this,
            &MaterialEntryWidget::onDomainActivated);

    m_applyDomainButton = new QPushButton(QStringLiteral("施加到网格"), domainRowWidget);
    connect(m_applyDomainButton, &QPushButton::clicked, this, &MaterialEntryWidget::onApplyDomainClicked);

    rowLayout->addWidget(m_domainCombo);
    rowLayout->addWidget(m_applyDomainButton);
    rowLayout->addStretch();

    m_formLayout->addRow(QStringLiteral("关联域分组"), domainRowWidget);

           // ===== 已施加域分组列表 + 移除 =====
    QWidget* appliedDomainRowWidget = new QWidget(this);
    auto* appliedLayout = new QHBoxLayout(appliedDomainRowWidget);
    appliedLayout->setContentsMargins(0, 0, 0, 0);

    m_appliedDomainList = new QListWidget(appliedDomainRowWidget);
    for (const MeshTagKey& key : appliedList)
    {
        auto* item = new QListWidgetItem(domainTagDisplayText(key), m_appliedDomainList);
        item->setData(Qt::UserRole, QVariant::fromValue(key));
    }

    connect(m_appliedDomainList, &QListWidget::currentTextChanged, this,
            &MaterialEntryWidget::onAppliedDomainSelected);

    m_removeDomainButton = new QPushButton(QStringLiteral("移除选中"), appliedDomainRowWidget);
    connect(m_removeDomainButton, &QPushButton::clicked, this,
            &MaterialEntryWidget::onRemoveDomainClicked);

    appliedLayout->addWidget(m_appliedDomainList);
    appliedLayout->addWidget(m_removeDomainButton);

    m_formLayout->addRow(QStringLiteral("已施加域分组"), appliedDomainRowWidget);

    QWidget* transitionRowWidget = new QWidget(this);
    auto* transitionLayout = new QHBoxLayout(transitionRowWidget);
    transitionLayout->setContentsMargins(0, 0, 0, 0);
    m_transitionOnlyCheck = new QCheckBox(QStringLiteral("仅用于后续变换"), transitionRowWidget);
    m_transitionOnlyCheck->setChecked(entry->transitionOnly);
    m_transitionMatypeSpin = new QSpinBox(transitionRowWidget);
    m_transitionMatypeSpin->setRange(1, 1000000);
    m_transitionMatypeSpin->setValue(entry->transitionMatype > 0 ? entry->transitionMatype : 1);
    m_transitionMatypeSpin->setEnabled(entry->transitionOnly);
    transitionLayout->addWidget(m_transitionOnlyCheck);
    transitionLayout->addWidget(new QLabel(QStringLiteral("编号"), transitionRowWidget));
    transitionLayout->addWidget(m_transitionMatypeSpin);
    transitionLayout->addStretch();
    connect(m_transitionOnlyCheck, &QCheckBox::stateChanged, this,
            &MaterialEntryWidget::onTransitionModeChanged);
    connect(m_transitionMatypeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &MaterialEntryWidget::onTransitionDomainChanged);
    m_formLayout->addRow(QStringLiteral("后续变换"), transitionRowWidget);
    if (entry->transitionOnly)
    {
        if (m_domainCombo)
            m_domainCombo->setEnabled(false);
        if (m_applyDomainButton)
            m_applyDomainButton->setEnabled(false);
        if (m_appliedDomainList)
            m_appliedDomainList->setEnabled(false);
        if (m_removeDomainButton)
            m_removeDomainButton->setEnabled(false);
    }

           // ===== 动态字段（放在域分组区域下面）=====
    for (const QString& column : model->columns)
    {
        if (MaterialManager::isAutoNumberColumn(column))
            continue;
        auto* valueEdit = new QLineEdit(entry->properties.value(column), this);
        valueEdit->setProperty("columnName", column);
        connect(valueEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onValueEdited);
        m_formLayout->addRow(column, valueEdit);
        m_valueEdits.insert(column, valueEdit);
    }

           // ===== 渗流/流变模型（放到最下方）=====
    m_seepageCombo = new CustomComboBox(this);
    m_seepageCombo->addItem(QStringLiteral("无"), QString());
    const auto& seepageModels = m_materialManager->seepageModels();
    for (const auto& seepageModel : seepageModels)
        m_seepageCombo->addItem(seepageModel.displayName, seepageModel.key);
    int seepageIndex = m_seepageCombo->findData(entry->seepageModelKey);
    m_seepageCombo->setCurrentIndex(seepageIndex >= 0 ? seepageIndex : 0);
    connect(m_seepageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onSeepageModelChanged);
    m_formLayout->addRow(QStringLiteral("渗流/流变模型"), m_seepageCombo);
    auto* seepageFieldsWidget = new QWidget(this);
    m_seepageFieldsLayout = new QFormLayout(seepageFieldsWidget);
    m_seepageFieldsLayout->setContentsMargins(0, 0, 0, 0);
    m_formLayout->addRow(QStringLiteral("渗流参数"), seepageFieldsWidget);
    rebuildSeepageFields(entry);

           // ===== 初始化选中域分组 =====
    MeshTagKey initialKey = m_materialManager->selectedDomainTagForEntry(m_entryId);
    if (!initialKey.isValid() && !appliedList.isEmpty())
        initialKey = appliedList.first();

    const int comboIndex = initialKey.isValid() ? findDomainKeyIndex(m_domainCombo, initialKey) : -1;
    m_domainCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    m_materialManager->setSelectedDomainTagForEntry(m_entryId, initialKey);

           // 初始化：清空 preview / predelete
    m_previewDomainTag = MeshTagKey{};
    m_preDeleteDomainTag = MeshTagKey{};
    m_materialManager->setPreviewDomainTagForEntry(m_entryId, MeshTagKey{});
    m_materialManager->setPreDeleteDomainTagForEntry(m_entryId, MeshTagKey{});

    m_isUpdating = false;
    updateAppliedListColors();
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
    m_valueEdits.clear();
    m_seepageCombo = nullptr;
    m_seepageFieldsLayout = nullptr;
    m_seepageValueEdits.clear();

    m_domainCombo = nullptr;
    m_applyDomainButton = nullptr;

    m_appliedDomainList = nullptr;
    m_removeDomainButton = nullptr;
    m_transitionOnlyCheck = nullptr;
    m_transitionMatypeSpin = nullptr;

    m_lastHoveredIndex = -1;
}

MaterialEntry* MaterialEntryWidget::lookupEntry(MaterialModel** model) const
{
    return m_materialManager->findEntry(m_entryId, model);
}

void MaterialEntryWidget::refreshAppliedDomainTags(const QSet<MeshTagKey>& appliedTags,
                                                   const MeshTagKey& selectedKey)
{
    if (!m_appliedDomainList || !m_domainCombo)
        return;

    m_isUpdating = true;
    m_appliedDomainList->clear();

    const QList<MeshTagKey> appliedList = sortedKeys(appliedTags);
    for (const MeshTagKey& key : appliedList)
    {
        auto* item = new QListWidgetItem(domainTagDisplayText(key), m_appliedDomainList);
        item->setData(Qt::UserRole, QVariant::fromValue(key));
    }

    int comboIndex = selectedKey.isValid() ? findDomainKeyIndex(m_domainCombo, selectedKey) : 0;
    if (comboIndex < 0)
        comboIndex = 0;
    m_domainCombo->setCurrentIndex(comboIndex);

    m_preDeleteDomainTag = MeshTagKey{};
    m_previewDomainTag = MeshTagKey{};
    m_materialManager->setPreDeleteDomainTagForEntry(m_entryId, MeshTagKey{});
    m_materialManager->setPreviewDomainTagForEntry(m_entryId, MeshTagKey{});

    m_isUpdating = false;
    updateAppliedListColors();
}

void MaterialEntryWidget::onDomainChanged(int)
{
    if (m_isUpdating || !m_domainCombo)
        return;

    if (m_ignoreNextDomainChanged)
    {
        m_ignoreNextDomainChanged = false;
        updateAppliedListColors();
        return;
    }

    if (m_domainCombo->view() && m_domainCombo->view()->isVisible())
    {
        updateAppliedListColors();
        return;
    }

    const MeshTagKey key = comboDomainKey(m_domainCombo);
    m_materialManager->setSelectedDomainTagForEntry(m_entryId, key);
    m_materialManager->refreshDomainTagHighlight(m_entryId);
    updateAppliedListColors();
}

void MaterialEntryWidget::onDomainActivated(int)
{
    if (m_isUpdating || !m_domainCombo)
        return;

    const MeshTagKey key = comboDomainKey(m_domainCombo);
    m_materialManager->setSelectedDomainTagForEntry(m_entryId, key);

    m_preDeleteDomainTag = MeshTagKey{};
    if (m_appliedDomainList)
        m_appliedDomainList->clearSelection();
    m_materialManager->setPreDeleteDomainTagForEntry(m_entryId, MeshTagKey{});

    updateAppliedListColors();
    m_materialManager->refreshDomainTagHighlight(m_entryId);
}

void MaterialEntryWidget::onApplyDomainClicked()
{
    if (!m_domainCombo)
        return;

    const MeshTagKey key = comboDomainKey(m_domainCombo);
    m_materialManager->applyDomainTagToEntry(m_entryId, key);
}

void MaterialEntryWidget::onAppliedDomainSelected(const QString& text)
{
    Q_UNUSED(text);
    if (m_isUpdating)
        return;

    auto* item = m_appliedDomainList ? m_appliedDomainList->currentItem() : nullptr;
    if (!item)
    {
        m_preDeleteDomainTag = MeshTagKey{};
        m_materialManager->setPreDeleteDomainTagForEntry(m_entryId, MeshTagKey{});
        updateAppliedListColors();
        m_materialManager->refreshDomainTagHighlight(m_entryId);
        return;
    }

    const MeshTagKey key = itemDomainKey(item);
    if (!key.isValid())
        return;

    m_preDeleteDomainTag = key;
    m_materialManager->setPreDeleteDomainTagForEntry(m_entryId, key);
    updateAppliedListColors();
    m_materialManager->refreshDomainTagHighlight(m_entryId);
}

void MaterialEntryWidget::onTransitionModeChanged(int state)
{
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionMatypeSpin)
        return;
    const bool enabled = (state == Qt::Checked);
    m_transitionMatypeSpin->setEnabled(enabled);
    if (!enabled)
    {
        entry->transitionOnly = false;
        entry->transitionMatype = -1;
        if (m_domainCombo)
            m_domainCombo->setEnabled(true);
        if (m_applyDomainButton)
            m_applyDomainButton->setEnabled(true);
        if (m_appliedDomainList)
            m_appliedDomainList->setEnabled(true);
        if (m_removeDomainButton)
            m_removeDomainButton->setEnabled(true);
        return;
    }
    QString error;
    const int matype = m_transitionMatypeSpin->value();
    if (!m_materialManager->validateTransitionMatype(m_entryId, matype, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        QSignalBlocker blocker(m_transitionOnlyCheck);
        m_transitionOnlyCheck->setChecked(false);
        m_transitionMatypeSpin->setEnabled(false);
        entry->transitionOnly = false;
        entry->transitionMatype = -1;
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = matype;
    entry->appliedDomainTags.clear();
    m_materialManager->refreshEntryWidgetDomainTags(entry->id, entry->appliedDomainTags, MeshTagKey{});
    m_materialManager->refreshDomainTagHighlight(entry->id);
    if (m_domainCombo)
        m_domainCombo->setEnabled(false);
    if (m_applyDomainButton)
        m_applyDomainButton->setEnabled(false);
    if (m_appliedDomainList)
        m_appliedDomainList->setEnabled(false);
    if (m_removeDomainButton)
        m_removeDomainButton->setEnabled(false);
    m_materialManager->notifyEntryUpdated(entry->id, false);
}

void MaterialEntryWidget::onTransitionDomainChanged(int value)
{
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionOnlyCheck || !m_transitionOnlyCheck->isChecked())
        return;
    QString error;
    if (!m_materialManager->validateTransitionMatype(m_entryId, value, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        QSignalBlocker blocker(m_transitionMatypeSpin);
        m_transitionMatypeSpin->setValue(entry->transitionMatype > 0 ? entry->transitionMatype : 1);
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = value;
}

void MaterialEntryWidget::onDomainHovered(int index)
{
    if (!m_domainCombo)
        return;

    if (m_lastHoveredIndex >= 0)
        m_domainCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    if (index >= 0)
    {
        m_domainCombo->setItemData(index, QColor(230, 247, 255), Qt::BackgroundRole);
        m_lastHoveredIndex = index;
    }
    else
    {
        m_lastHoveredIndex = -1;
    }

    MeshTagKey key;
    if (index >= 0)
        key = m_domainCombo->itemData(index).value<MeshTagKey>();

    m_previewDomainTag = key;
    m_materialManager->setPreviewDomainTagForEntry(m_entryId, key);
    m_materialManager->refreshDomainTagHighlight(m_entryId);
}

void MaterialEntryWidget::onDropdownClosed()
{
    if (m_domainCombo && m_lastHoveredIndex >= 0)
    {
        m_domainCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_lastHoveredIndex = -1;
    }

    if (m_previewDomainTag.isValid())
    {
        m_previewDomainTag = MeshTagKey{};
        m_materialManager->setPreviewDomainTagForEntry(m_entryId, MeshTagKey{});
    }

    if (m_domainCombo)
    {
        const MeshTagKey selected = m_materialManager->selectedDomainTagForEntry(m_entryId);
        int comboIndex = selected.isValid() ? findDomainKeyIndex(m_domainCombo, selected) : 0;
        if (comboIndex < 0)
            comboIndex = 0;
        m_ignoreNextDomainChanged = true;
        QSignalBlocker blocker(m_domainCombo);
        m_domainCombo->setCurrentIndex(comboIndex);
        QTimer::singleShot(0, this,
                           [this]()
                           {
                               m_ignoreNextDomainChanged = false;
                           });
    }

    updateAppliedListColors();
    if (m_beforeUnloadHandled)
        return;
    m_materialManager->refreshDomainTagHighlight(m_entryId);
}

void MaterialEntryWidget::onRemoveDomainClicked()
{
    if (!m_appliedDomainList)
        return;

    auto* item = m_appliedDomainList->currentItem();
    if (!item)
        return;

    const MeshTagKey key = itemDomainKey(item);
    if (!key.isValid())
        return;

    m_materialManager->removeDomainTagFromEntry(m_entryId, key);
    m_preDeleteDomainTag = MeshTagKey{};
    m_materialManager->setPreDeleteDomainTagForEntry(m_entryId, MeshTagKey{});
    updateAppliedListColors();
    m_materialManager->refreshDomainTagHighlight(m_entryId);
}

void MaterialEntryWidget::updateAppliedListColors()
{
    if (!m_appliedDomainList)
        return;

    const QColor preDeleteColor(255, 230, 230);
    for (int i = 0; i < m_appliedDomainList->count(); ++i)
    {
        auto* it = m_appliedDomainList->item(i);
        const MeshTagKey key = itemDomainKey(it);
        if (!key.isValid())
        {
            it->setBackground(QBrush());
            continue;
        }
        if (m_preDeleteDomainTag.isValid() && key == m_preDeleteDomainTag)
            it->setBackground(preDeleteColor);
        else
            it->setBackground(QBrush());
    }
}
