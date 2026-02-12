#include "MaterialEntryWidget.h"
#include "MaterialManager.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QColor>
#include <QBrush>
#include <algorithm>
#include <QFontDatabase>
#include <QSignalBlocker>
#include <QAbstractItemView>
#include <QTimer>

#include <CustomComboBox.h>

MaterialEntryWidget::MaterialEntryWidget(MaterialManager *manager, QWidget *parent)
    : BasePage("MaterialEntryWidget")
    , m_materialManager(manager)
    , m_isUpdating(false)
    , m_formLayout(new QFormLayout)
    , m_nameEdit(nullptr)
{
    auto *container = new QWidget(this);
    container->setLayout(m_formLayout);

    this->setStyleSheet(R"(
QWidget{
    font-family: "Microsoft YaHei", "微软雅黑";
}
QPushButton {
    background-color: rgb(255, 255, 255);
    border-width: 1px;
    border-color: rgb(121, 121, 121);
    border-style: solid;
    font-size:17px;
    border-radius: 2px;
    text-align: center;
}
QPushButton:hover {
    background-color: rgb(235, 235, 235);
    border-color: rgb(100, 100, 100);
}
QPushButton:pressed {
    background-color: rgb(200, 200, 200);
    border-color: rgb(80, 80, 80);
}
QPushButton#pushButton {
    background-color: rgb(224, 233, 244);
    border-width: 1px;
    border-color: rgb(121, 121, 121);
    border-style: solid;
    height: 33px;
    border-top: 1px solid rgb(169, 169, 169);
    border-bottom: 1px solid rgb(169, 169, 169);
    border-left: none;
    border-right: none;
    text-align: left;
    padding-left: 15px;
    font-size: 17px;
}
QLabel {
    font-family: "Microsoft YaHei", "微软雅黑";
    font-size: 17px;
    text-align: left;
    color: rgb(51, 51, 51);
    white-space: nowrap;
    line-height: 50px;
    position: absolute;
    top: 0px;
    left: 0px;
}
QLabel#label {
    font-size: 20px;
    font-family: "Microsoft YaHei", "微软雅黑";
    text-align: left;
    padding-left:9px;
    color: rgb(53, 103, 147);
}
QComboBox {
    font-family: "Microsoft YaHei", "微软雅黑";
    font-size: 17px;
    text-align: left;
    color: rgb(51, 51, 51);
    padding: 0px;
    padding-left:6px;
    background: rgb(240, 240, 240);
    border: 1px solid rgb(169, 169, 169);
    border-radius: 3px;
}
QComboBox::drop-down {
    background: rgb(240, 240, 240);
}
QComboBox::down-arrow {
    image: url(:/resources/down_arrow.png);
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    border-top-right-radius: 3px;
    border-bottom-right-radius: 3px;
}
QLineEdit {
    border: 1px solid rgb(200, 200, 200);
    border-radius: 2px;
    font-size: 17px;
}
QLineEdit:focus {
    border-color: rgb(100, 100, 255);
    outline: none;
}
QRadioButton {
    font-size: 17px;
}
)");


    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(container);
    scrollArea->setWidgetResizable(true);

    auto *layout = new QVBoxLayout(this);
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

    // 先兜底清空全局高亮，避免上一页面残留状态带入
    if (m_materialManager) {
        m_materialManager->clearAllMatypeHighlight();
    }

    // 回到页面：只恢复该条目的 Applied（预选中/预删除不恢复）
    m_previewMatype = -1;
    m_preDeleteMatype = -1;
    if (!m_entryId.isEmpty()) {
        m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        m_materialManager->refreshMatypeHighlight(m_entryId);
    }
    updateAppliedListColors();
}

void MaterialEntryWidget::beforeUnload()
{
    if (m_beforeUnloadHandled) return;
    m_beforeUnloadHandled = true;

    // 离开页面：清空所有高亮
    if (!m_entryId.isEmpty()) {
        m_previewMatype = -1;
        m_preDeleteMatype = -1;
        m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        m_materialManager->clearAllMatypeHighlight();
    }

    // UI 层清理 hover 背景
    onDropdownClosed();

    // list 背景也清掉
    if (m_appliedMatypesList) {
        for (int i = 0; i < m_appliedMatypesList->count(); ++i) {
            m_appliedMatypesList->item(i)->setBackground(QBrush());
        }
    }
}

void MaterialEntryWidget::setEntry(const QString &modelKey, const QString &entryId)
{
    m_modelKey = modelKey;
    m_entryId = entryId;

    MaterialModel *model = nullptr;
    MaterialEntry *entry = lookupEntry(&model);
    rebuildForm(model, entry);
}

void MaterialEntryWidget::onNameEdited(const QString &text)
{
    if (m_isUpdating) return;

    MaterialEntry *entry = lookupEntry();
    if (!entry) return;

    entry->name = text;
    m_materialManager->notifyEntryUpdated(m_entryId, true);
}

void MaterialEntryWidget::onValueEdited(const QString &text)
{
    if (m_isUpdating) return;

    auto *lineEdit = qobject_cast<QLineEdit *>(sender());
    if (!lineEdit) return;

    MaterialModel *model = nullptr;
    MaterialEntry *entry = lookupEntry(&model);
    if (!entry || !model) return;

    const QString column = lineEdit->property("columnName").toString();
    entry->properties.insert(column, text);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::rebuildForm(MaterialModel *model, MaterialEntry *entry)
{
    m_isUpdating = true;
    clearForm();

    if (!model || !entry) {
        m_isUpdating = false;
        return;
    }

    // ===== 名称 =====
    m_nameEdit = new QLineEdit(entry->name, this);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onNameEdited);
    m_formLayout->addRow(QStringLiteral("材料名称"), m_nameEdit);

    // 先取 appliedList，后面初始化要用
    QList<int> appliedList = entry->appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());

    // ===== MATYPE 选择 + 施加（放到动态字段上面）=====
    QWidget *matypeRowWidget = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(matypeRowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    m_matypeCombo = new CustomComboBox(matypeRowWidget);
    m_matypeCombo->addItem(QStringLiteral("未选择"), QVariant());

    QList<int> matypes = m_materialManager->availableMatypes().values();
    std::sort(matypes.begin(), matypes.end());
    for (int value : matypes) {
        m_matypeCombo->addItem(QString::number(value), value);
    }

    connect(m_matypeCombo, &CustomComboBox::itemHovered, this, &MaterialEntryWidget::onMatypeHovered);
    connect(m_matypeCombo, &CustomComboBox::dropdownClosed, this, &MaterialEntryWidget::onDropdownClosed);
    connect(m_matypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaterialEntryWidget::onMatypeChanged);
    connect(m_matypeCombo, QOverload<int>::of(&QComboBox::activated),
            this, &MaterialEntryWidget::onMatypeActivated);

    m_applyButton = new QPushButton(QStringLiteral("施加到网格"), matypeRowWidget);
    connect(m_applyButton, &QPushButton::clicked, this, &MaterialEntryWidget::onApplyMatypeClicked);

    rowLayout->addWidget(m_matypeCombo);
    rowLayout->addWidget(m_applyButton);
    rowLayout->addStretch();

    m_formLayout->addRow(QStringLiteral("关联MATYPE"), matypeRowWidget);

    // ===== 已施加 MATYPE 列表 + 移除 =====
    QWidget *appliedRowWidget = new QWidget(this);
    auto *appliedLayout = new QHBoxLayout(appliedRowWidget);
    appliedLayout->setContentsMargins(0, 0, 0, 0);

    m_appliedMatypesList = new QListWidget(appliedRowWidget);
    for (int value : appliedList) {
        m_appliedMatypesList->addItem(QString::number(value));
    }

    connect(m_appliedMatypesList, &QListWidget::currentTextChanged,
            this, &MaterialEntryWidget::onAppliedMatypeSelected);

    m_removeButton = new QPushButton(QStringLiteral("移除选中"), appliedRowWidget);
    connect(m_removeButton, &QPushButton::clicked, this, &MaterialEntryWidget::onRemoveMatypeClicked);

    appliedLayout->addWidget(m_appliedMatypesList);
    appliedLayout->addWidget(m_removeButton);

    m_formLayout->addRow(QStringLiteral("已施加材料单元"), appliedRowWidget);

    // ===== 动态字段（放在 MATYPE 区域下面）=====
    for (const QString &column : model->columns) {
        auto *valueEdit = new QLineEdit(entry->properties.value(column), this);
        valueEdit->setProperty("columnName", column);
        connect(valueEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onValueEdited);
        m_formLayout->addRow(column, valueEdit);
        m_valueEdits.insert(column, valueEdit);
    }

    // ===== 初始化选中 MATYPE =====
    int initialMatype = m_materialManager->selectedMatypeForEntry(m_entryId);
    if (initialMatype < 0 && !appliedList.isEmpty()) {
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
    m_materialManager->refreshMatypeHighlight(m_entryId);
}


void MaterialEntryWidget::clearForm()
{
    while (m_formLayout->rowCount() > 0) {
        QLayoutItem *labelItem = m_formLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem *fieldItem = m_formLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem) delete labelItem->widget();
        if (fieldItem) delete fieldItem->widget();
        m_formLayout->removeRow(0);
    }

    m_nameEdit = nullptr;
    m_valueEdits.clear();

    m_matypeCombo = nullptr;
    m_applyButton = nullptr;

    m_appliedMatypesList = nullptr;
    m_removeButton = nullptr;

    m_lastHoveredIndex = -1;
}

MaterialEntry *MaterialEntryWidget::lookupEntry(MaterialModel **model) const
{
    return m_materialManager->findEntry(m_entryId, model);
}

void MaterialEntryWidget::refreshAppliedMatypes(const QSet<int> &appliedMatypes, int selectedMatype)
{
    if (!m_appliedMatypesList || !m_matypeCombo) return;

    m_isUpdating = true;

    m_appliedMatypesList->clear();
    QList<int> appliedList = appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());
    for (int value : appliedList) {
        m_appliedMatypesList->addItem(QString::number(value));
    }

    int comboIndex = (selectedMatype >= 0) ? m_matypeCombo->findData(selectedMatype) : 0;
    if (comboIndex < 0) comboIndex = 0;
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
    if (m_isUpdating || !m_matypeCombo) return;

    // 下拉关闭时某些实现会额外发一次 currentIndexChanged（并非真实提交），直接忽略。
    if (m_ignoreNextMatypeChanged) {
        m_ignoreNextMatypeChanged = false;
        updateAppliedListColors();
        return;
    }

    // 下拉弹层可见时，currentIndexChanged 可能由“悬浮预览”触发，
    // 不能把该值写成 Selected（否则关闭下拉后会残留预选色/选中色）。
    if (m_matypeCombo->view() && m_matypeCombo->view()->isVisible()) {
        updateAppliedListColors();
        return;
    }

    const QVariant data = m_matypeCombo->currentData();
    const int matype = data.isValid() ? data.toInt() : -1;

    // 仅在非下拉预览阶段同步 Selected，覆盖键盘/滚轮等非 activated 路径。
    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);
    m_materialManager->refreshMatypeHighlight(m_entryId);

    updateAppliedListColors();
}

void MaterialEntryWidget::onMatypeActivated(int)
{
    if (m_isUpdating || !m_matypeCombo) return;

    QVariant data = m_matypeCombo->currentData();
    int matype = data.isValid() ? data.toInt() : -1;

    // 选中态：写入 Selected
    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);

    // 改选中时通常清掉预删除（更符合直觉）
    m_preDeleteMatype = -1;
    if (m_appliedMatypesList) m_appliedMatypesList->clearSelection();
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    updateAppliedListColors();
    m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::onApplyMatypeClicked()
{
    if (!m_matypeCombo) return;

    QVariant data = m_matypeCombo->currentData();
    int matype = data.isValid() ? data.toInt() : -1;

    m_materialManager->applyMatypeToEntry(m_entryId, matype);
}

void MaterialEntryWidget::onAppliedMatypeSelected(const QString &text)
{
    if (m_isUpdating) return;

    bool ok = false;
    int matype = text.toInt(&ok);
    if (!ok) {
        // 清选择 => 取消预删除
        m_preDeleteMatype = -1;
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        updateAppliedListColors();
        m_materialManager->refreshMatypeHighlight(m_entryId);
        return;
    }

    // ★ 只设置“预删除”，不改变 Selected
    m_preDeleteMatype = matype;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, matype);

    updateAppliedListColors();
    m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::onMatypeHovered(int index)
{
    if (!m_matypeCombo) return;

    // UI：下拉项背景淡蓝（你原逻辑保留）
    if (m_lastHoveredIndex >= 0) {
        m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    }
    if (index >= 0) {
        m_matypeCombo->setItemData(index, QColor(230, 247, 255), Qt::BackgroundRole);
        m_lastHoveredIndex = index;
    } else {
        m_lastHoveredIndex = -1;
    }

    // 业务：Preview matype
    QVariant data = m_matypeCombo->itemData(index);
    int matype = data.isValid() ? data.toInt() : -1;

    m_previewMatype = matype;
    m_materialManager->setPreviewMatypeForEntry(m_entryId, matype);
    m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::onDropdownClosed()
{
    if (m_matypeCombo && m_lastHoveredIndex >= 0) {
        m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_lastHoveredIndex = -1;
    }

    // 下拉关闭：取消 Preview，恢复（PreDelete/Selected/Applied）
    if (m_previewMatype != -1) {
        m_previewMatype = -1;
        m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
    }

    if (m_matypeCombo) {
        const int selected = m_materialManager->selectedMatypeForEntry(m_entryId);
        int comboIndex = (selected >= 0) ? m_matypeCombo->findData(selected) : 0;
        if (comboIndex < 0) comboIndex = 0;
        m_ignoreNextMatypeChanged = true;
        QSignalBlocker blocker(m_matypeCombo);
        m_matypeCombo->setCurrentIndex(comboIndex);
        QTimer::singleShot(0, this, [this]() { m_ignoreNextMatypeChanged = false; });
    }

    updateAppliedListColors();

    // 下拉关闭后仅取消 Preview，并恢复到该条目的应有高亮状态（Selected / PreDelete / Applied）。
    m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::onRemoveMatypeClicked()
{
    if (!m_appliedMatypesList) return;

    auto *item = m_appliedMatypesList->currentItem();
    if (!item) return;

    bool ok = false;
    int matype = item->text().toInt(&ok);
    if (!ok) return;

    m_materialManager->removeMatypeFromEntry(m_entryId, matype);

    // 移除后清掉预删除
    m_preDeleteMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
    updateAppliedListColors();
    m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::updateAppliedListColors()
{
    if (!m_appliedMatypesList) return;

    // 两种色：预删除(淡红)、其他(无)
    const QColor preDeleteColor(255, 230, 230);

    for (int i = 0; i < m_appliedMatypesList->count(); ++i) {
        auto *it = m_appliedMatypesList->item(i);
        bool ok = false;
        int v = it->text().toInt(&ok);
        if (!ok) {
            it->setBackground(QBrush());
            continue;
        }

        if (m_preDeleteMatype >= 0 && v == m_preDeleteMatype) {
            it->setBackground(preDeleteColor);
        } else {
            it->setBackground(QBrush());
        }
    }
}
