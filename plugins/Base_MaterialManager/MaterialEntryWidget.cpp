#include "MaterialEntryWidget.h"

#include <CustomComboBox.h>

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
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
QString matypeDisplayText(int matype, const MshPickMap* pickMap = nullptr)
{
    if (matype <= 0)
        return QString();
    if (pickMap)
    {
        QString name = pickMap->physicalName(matype, 3).trimmed();
        if (name.isEmpty())
            name = pickMap->physicalName(matype).trimmed();
        if (!name.isEmpty())
            return name;
    }
    else if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        QString name = meshService->matypePhysicalName(matype, 3).trimmed();
        if (name.isEmpty())
            name = meshService->matypePhysicalName(matype).trimmed();
        if (!name.isEmpty())
            return name;
    }
    return QString::number(matype);
}
}

MaterialEntryWidget::MaterialEntryWidget(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialEntryWidget"), m_materialManager(manager), m_isUpdating(false),
      m_formLayout(new QFormLayout), m_nameEdit(nullptr)
{
    setAttribute(Qt::WA_StyledBackground, true);
    auto* pageHeaderLabel = new QLabel("材料参数设置", this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");

    m_formLayout->setContentsMargins(9, 9, 9, 9);
    m_formLayout->setSpacing(6);

    auto* container = new QWidget(this);
    container->setLayout(m_formLayout);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(container);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(pageHeaderLabel);
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
        seepageValueEdit->setMinimumWidth(10);
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
    m_nameEdit->setMinimumWidth(10);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onNameEdited);
    m_formLayout->addRow(QString("材料名称"), m_nameEdit);

    // 先取 appliedList，后面初始化要用
    QList<int> appliedList = entry->appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());

    // ===== MATYPE 选择 + 施加（放到动态字段上面）=====
    QWidget* matypeRowWidget = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(matypeRowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    m_matypeCombo = new CustomComboBox(matypeRowWidget);
    m_matypeCombo->setMinimumWidth(10);
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

    // ===== 已施加 MATYPE 列表 + 移除 =====
    QWidget* appliedRowWidget = new QWidget(this);
    auto* appliedLayout = new QHBoxLayout(appliedRowWidget);
    appliedLayout->setContentsMargins(0, 0, 0, 0);

    m_appliedMatypesList = new QListWidget(appliedRowWidget);
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

    appliedLayout->addWidget(m_appliedMatypesList);
    appliedLayout->addWidget(m_removeButton);

    m_formLayout->addRow(QString("已施加材料单元"), appliedRowWidget);

    QWidget* transitionRowWidget = new QWidget(this);
    auto* transitionLayout = new QHBoxLayout(transitionRowWidget);
    transitionLayout->setContentsMargins(0, 0, 0, 0);
    m_transitionOnlyCheck = new QCheckBox(QStringLiteral("仅用于后续变换"), transitionRowWidget);
    m_transitionOnlyCheck->setChecked(entry->transitionOnly);
    m_transitionMatypeSpin = new QSpinBox(transitionRowWidget);
    m_transitionMatypeSpin->setMinimumWidth(10);
    m_transitionMatypeSpin->setRange(1, 1000000);
    m_transitionMatypeSpin->setValue(entry->transitionMatype > 0 ? entry->transitionMatype : 1);
    m_transitionMatypeSpin->setEnabled(true);
    transitionLayout->addWidget(m_transitionOnlyCheck);
    transitionLayout->addWidget(new QLabel(QStringLiteral("编号"), transitionRowWidget));
    transitionLayout->addWidget(m_transitionMatypeSpin);
    transitionLayout->addStretch();
    connect(m_transitionOnlyCheck, &QCheckBox::stateChanged, this,
            &MaterialEntryWidget::onTransitionModeChanged);
    connect(m_transitionMatypeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &MaterialEntryWidget::onTransitionMatypeChanged);
    m_formLayout->addRow(QStringLiteral("后续变换"), transitionRowWidget);
    if (entry->transitionOnly)
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
    for (const QString& column : model->columns)
    {
        if (MaterialManager::isAutoNumberColumn(column))
        {
            continue;
        }
        auto* valueEdit = new QLineEdit(entry->properties.value(column), this);
        valueEdit->setMinimumWidth(10);
        valueEdit->setProperty("columnName", column);
        connect(valueEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onValueEdited);
        m_formLayout->addRow(column, valueEdit);
        m_valueEdits.insert(column, valueEdit);
    }

    // ===== 渗流/流变模型（放到最下方）=====
    m_seepageCombo = new CustomComboBox(this);
    m_seepageCombo->setMinimumWidth(10);
    m_seepageCombo->addItem(QStringLiteral("无"), QString());
    const auto& seepageModels = m_materialManager->seepageModels();
    for (const auto& seepageModel : seepageModels)
    {
        m_seepageCombo->addItem(seepageModel.displayName, seepageModel.key);
    }
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
    m_valueEdits.clear();
    m_seepageCombo = nullptr;
    m_seepageFieldsLayout = nullptr;
    m_seepageValueEdits.clear();

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
    if (!m_matypeCombo || matype <= 0)
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
    if (matype <= 0)
        return;

    // ★ 只设置“预删除”，不改变 Selected
    m_preDeleteMatype = matype;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, matype);

    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onTransitionModeChanged(int state)
{
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionMatypeSpin)
        return;
    const bool enabled = (state == Qt::Checked);
    if (!enabled)
    {
        int sourceMatype = m_materialManager->selectedMatypeForEntry(m_entryId);
        if (sourceMatype <= 0 && m_matypeCombo)
        {
            const QVariant sourceData = m_matypeCombo->currentData();
            sourceMatype = sourceData.isValid() ? sourceData.toInt() : -1;
        }
        entry->transitionOnly = false;
        entry->transitionMatype = m_transitionMatypeSpin->value();
        if (sourceMatype > 0)
        {
            auto mappings = m_materialManager->materialTransitions();
            mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
                                          [sourceMatype](const QPair<int, int>& p)
                                          { return p.first == sourceMatype; }),
                           mappings.end());
            m_materialManager->setMaterialTransitions(mappings);
        }
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
        m_transitionOnlyCheck->setChecked(false);
        entry->transitionOnly = false;
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = matype;
    int sourceMatype = m_materialManager->selectedMatypeForEntry(m_entryId);
    if (sourceMatype <= 0 && m_matypeCombo)
    {
        const QVariant sourceData = m_matypeCombo->currentData();
        sourceMatype = sourceData.isValid() ? sourceData.toInt() : -1;
    }
    if (sourceMatype > 0)
    {
        auto mappings = m_materialManager->materialTransitions();
        bool replaced = false;
        for (auto& p : mappings)
        {
            if (p.first == sourceMatype)
            {
                p.second = matype;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            mappings.push_back({sourceMatype, matype});
        m_materialManager->setMaterialTransitions(mappings);
    }
    entry->appliedMatypes.clear();
    m_materialManager->refreshEntryWidgetMatypes(entry->id, entry->appliedMatypes, -1);
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
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionOnlyCheck)
        return;
    const int previousMatype = entry->transitionMatype;
    entry->transitionMatype = value;
    if (!m_transitionOnlyCheck->isChecked())
    {
        m_materialManager->notifyEntryUpdated(entry->id, false);
        return;
    }
    QString error;
    if (!m_materialManager->validateTransitionMatype(m_entryId, value, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        entry->transitionMatype = previousMatype;
        QSignalBlocker blocker(m_transitionMatypeSpin);
        m_transitionMatypeSpin->setValue(previousMatype > 0 ? previousMatype : 1);
        return;
    }
    entry->transitionOnly = true;
    int sourceMatype = m_materialManager->selectedMatypeForEntry(m_entryId);
    if (sourceMatype <= 0 && m_matypeCombo)
    {
        const QVariant sourceData = m_matypeCombo->currentData();
        sourceMatype = sourceData.isValid() ? sourceData.toInt() : -1;
    }
    if (sourceMatype > 0)
    {
        auto mappings = m_materialManager->materialTransitions();
        bool replaced = false;
        for (auto& p : mappings)
        {
            if (p.first == sourceMatype)
            {
                p.second = value;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            mappings.push_back({sourceMatype, value});
        m_materialManager->setMaterialTransitions(mappings);
    }
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
                               // 下拉关闭后仅取消 Preview，并恢复到该条目的应有高亮状态
                               //（Applied / PreDelete）。Selected 只作为下拉框当前值，不参与三维高亮。
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
    if (key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) != 0 || key.id <= 0)
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
