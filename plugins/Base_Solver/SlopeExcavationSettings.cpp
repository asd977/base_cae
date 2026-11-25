#include "SlopeExcavationSettings.h"

#include "CaveExcavationBodyPage.h"
#include "CaveReinforcementFieldPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SlopeExcavationSettings::SlopeExcavationSettings(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("slopeExcavationSettings"));

    // 隐藏宿主容器：持有未挂载或暂时隐藏的开挖级/加固页面，避免重复创建或丢失已选区域。
    m_storageHost = new QWidget(this);
    m_storageHost->setVisible(false);
    m_reinforcement = new CaveReinforcementFieldPage(m_storageHost);
    m_reinforcement->setObjectName(QStringLiteral("slopeReinforcementField"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_enabled = new QCheckBox(QStringLiteral("启用分级开挖"), this);
    m_enabled->setObjectName(QStringLiteral("slopeExcavationEnabled"));
    layout->addWidget(m_enabled);

    m_content = new QWidget(this);
    auto* content = new QVBoxLayout(m_content);
    content->setContentsMargins(0, 2, 0, 0);
    content->setSpacing(6);

    auto* hint = new QLabel(
        QStringLiteral("施工流程：1. 先在“地应力回归分析”中计算初始地应力 → "
                       "2. 在左侧工程树中依次设置各“开挖级”选区 → "
                       "3. 如需加固则配置“开挖面加固” → 4. 点击下方“数值法计算”。"),
        m_content);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #5b6775;"));
    content->addWidget(hint);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    m_support = new QComboBox(m_content);
    m_support->setObjectName(QStringLiteral("slopeExcavationSupportMode"));
    m_support->addItem(QStringLiteral("逐级开挖，不加固"));
    m_support->addItem(QStringLiteral("逐级开挖，考虑加固"));
    form->addRow(QStringLiteral("支护方式"), m_support);
    content->addLayout(form);

    m_summaryLabel = new QLabel(m_content);
    m_summaryLabel->setObjectName(QStringLiteral("slopeExcavationSummary"));
    m_summaryLabel->setWordWrap(true);
    content->addWidget(m_summaryLabel);

    auto* row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(6);
    auto* initial = new QPushButton(QStringLiteral("初始地应力"), m_content);
    initial->setObjectName(QStringLiteral("slopeInitialStressButton"));
    initial->setToolTip(QStringLiteral("定位或创建当前边坡分析的初始地应力回归节点"));
    row1->addWidget(initial);

    m_reinforcementButton = new QPushButton(QStringLiteral("开挖面加固"), m_content);
    m_reinforcementButton->setObjectName(QStringLiteral("slopeReinforcementButton"));
    m_reinforcementButton->setToolTip(QStringLiteral("定位到左侧工程树中的开挖面加固参数页"));
    row1->addWidget(m_reinforcementButton);
    content->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    row2->setContentsMargins(0, 0, 0, 0);
    row2->setSpacing(6);
    auto* add = new QPushButton(QStringLiteral("新增开挖级"), m_content);
    add->setObjectName(QStringLiteral("slopeAddExcavationStage"));
    row2->addWidget(add);

    m_removeButton = new QPushButton(QStringLiteral("删除末级"), m_content);
    m_removeButton->setObjectName(QStringLiteral("slopeRemoveExcavationStage"));
    row2->addWidget(m_removeButton);
    content->addLayout(row2);

    layout->addWidget(m_content);

    connect(initial, &QPushButton::clicked, this, &SlopeExcavationSettings::initialStressRequested);
    connect(m_reinforcementButton, &QPushButton::clicked, this,
            &SlopeExcavationSettings::navigateToReinforcementRequested);
    connect(add, &QPushButton::clicked, this,
            [this]
            {
                const int idx = addStage({}, true);
                if (idx >= 0)
                    emit navigateToStageRequested(idx);
            });
    connect(m_removeButton, &QPushButton::clicked, this,
            [this]
            {
                if (!m_stages.isEmpty())
                    removeStage(m_stages.size() - 1, true);
            });
    connect(m_enabled, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                if (checked && !m_loading && m_stages.isEmpty())
                    addStage({}, false);
                updateState();
                if (!m_loading)
                    emit structureChanged();
            });
    connect(m_support, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                updateState();
                if (!m_loading)
                    emit structureChanged();
            });

    updateState();
}

SlopeExcavationSettings::~SlopeExcavationSettings()
{
    leave();
}

bool SlopeExcavationSettings::enabled() const
{
    return m_enabled && m_enabled->isChecked();
}

bool SlopeExcavationSettings::reinforced() const
{
    return enabled() && m_support && m_support->currentIndex() == 1;
}

int SlopeExcavationSettings::stageCount() const
{
    return m_stages.size();
}

CaveExcavationBodyPage* SlopeExcavationSettings::stagePage(int index) const
{
    if (index < 0 || index >= m_stages.size())
        return nullptr;
    return m_stages.at(index).data();
}

CaveReinforcementFieldPage* SlopeExcavationSettings::reinforcement() const
{
    return m_reinforcement.data();
}

int SlopeExcavationSettings::addStage(const QJsonObject& state, bool emitSignals)
{
    auto* page = new CaveExcavationBodyPage(m_storageHost);
    if (!state.isEmpty())
        page->loadJson(state);
    page->setExcavationLayer(-m_stages.size() - 1);
    // 级次顺序由工程树子节点顺序统一决定，禁用页内重复的层号编辑框。
    if (auto* spin = page->findChild<QSpinBox*>(QStringLiteral("excavationLayerSpinBox")))
        spin->setEnabled(false);
    m_stages.push_back(page);
    renumberStages();
    refreshSummary();
    if (emitSignals && !m_loading)
        emit structureChanged();
    return m_stages.size() - 1;
}

bool SlopeExcavationSettings::removeStage(int index, bool emitSignals)
{
    if (index < 0 || index >= m_stages.size())
        return false;
    QPointer<CaveExcavationBodyPage> page = m_stages.takeAt(index);
    if (page)
    {
        page->beforeUnload();
        page->setParent(nullptr);
        delete page;
    }
    renumberStages();
    refreshSummary();
    if (emitSignals && !m_loading)
        emit structureChanged();
    return true;
}

bool SlopeExcavationSettings::moveStage(int index, int delta, bool emitSignals)
{
    const int target = index + delta;
    if (index < 0 || index >= m_stages.size() || target < 0 || target >= m_stages.size() ||
        index == target)
        return false;
    m_stages.swapItemsAt(index, target);
    renumberStages();
    refreshSummary();
    if (emitSignals && !m_loading)
        emit structureChanged();
    return true;
}

void SlopeExcavationSettings::renumberStages()
{
    for (int i = 0; i < m_stages.size(); ++i)
    {
        if (m_stages.at(i))
            m_stages.at(i)->setExcavationLayer(-i - 1);
    }
}

void SlopeExcavationSettings::refreshSummary()
{
    if (!m_summaryLabel)
        return;
    const int count = m_stages.size();
    if (m_removeButton)
        m_removeButton->setEnabled(count > 0);
    if (m_reinforcementButton)
        m_reinforcementButton->setVisible(reinforced());

    if (count == 0)
    {
        m_summaryLabel->setText(QStringLiteral("尚未添加开挖级，请点击“新增开挖级”。"));
        return;
    }
    m_summaryLabel->setText(
        QStringLiteral("已配置 %1 个开挖级（请在左侧工程树中点击对应“开挖级”节点选择开挖区域）。")
            .arg(count));
}

void SlopeExcavationSettings::updateState()
{
    if (m_content)
        m_content->setVisible(enabled());
    refreshSummary();
}

void SlopeExcavationSettings::enter()
{
    refreshSummary();
}

void SlopeExcavationSettings::leave()
{
}

QVector<QVector<int>> SlopeExcavationSettings::selectedStages()
{
    QVector<QVector<int>> result;
    result.reserve(m_stages.size());
    for (const auto& page : m_stages)
    {
        result.push_back(page ? page->selectedElementIds() : QVector<int>{});
    }
    return result;
}

QJsonObject SlopeExcavationSettings::save()
{
    QJsonArray stages;
    for (const auto& page : m_stages)
        stages.append(page ? page->saveJson() : QJsonObject{});
    return {{QStringLiteral("enabled"), enabled()},
            {QStringLiteral("support"), m_support ? m_support->currentIndex() : 0},
            {QStringLiteral("stages"), stages},
            {QStringLiteral("reinforcement"),
             m_reinforcement ? m_reinforcement->saveJson() : QJsonObject{}}};
}

void SlopeExcavationSettings::load(const QJsonObject& state)
{
    m_loading = true;
    for (auto& page : m_stages)
    {
        if (page)
        {
            page->beforeUnload();
            page->setParent(nullptr);
            delete page;
        }
    }
    m_stages.clear();

    for (const auto& stage : state.value(QStringLiteral("stages")).toArray())
        addStage(stage.toObject(), false);

    if (m_reinforcement)
        m_reinforcement->loadJson(state.value(QStringLiteral("reinforcement")).toObject());
    if (m_support)
        m_support->setCurrentIndex(state.value(QStringLiteral("support")).toInt(0));
    if (m_enabled)
        m_enabled->setChecked(state.value(QStringLiteral("enabled")).toBool(false));

    m_loading = false;
    updateState();
    emit structureChanged();
}
