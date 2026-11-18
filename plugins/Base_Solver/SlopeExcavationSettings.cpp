#include "SlopeExcavationSettings.h"
#include "CaveExcavationBodyPage.h"
#include "CaveReinforcementFieldPage.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

SlopeExcavationSettings::SlopeExcavationSettings(QWidget* parent) : QWidget(parent)
{
    setObjectName("slopeExcavationSettings");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_enabled = new QCheckBox(tr("启用分级开挖"), this);
    m_enabled->setObjectName("slopeExcavationEnabled");
    layout->addWidget(m_enabled);
    m_content = new QWidget(this);
    auto* content = new QVBoxLayout(m_content);
    content->setContentsMargins(0, 0, 0, 0);
    auto* hint = new QLabel(
        tr("先计算初始地应力，再按顺序设置各级开挖区域。各级区域不能重复，未选区域保留。"),
        m_content);
    hint->setWordWrap(true);
    content->addWidget(hint);
    auto* initial = new QPushButton(tr("设置／计算初始地应力"), m_content);
    initial->setObjectName("slopeInitialStressButton");
    content->addWidget(initial);
    connect(initial, &QPushButton::clicked, this, &SlopeExcavationSettings::initialStressRequested);
    m_support = new QComboBox(m_content);
    m_support->setObjectName("slopeExcavationSupportMode");
    m_support->addItem(tr("逐级开挖，不加固"));
    m_support->addItem(tr("逐级开挖，考虑加固"));
    content->addWidget(m_support);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("新增开挖级"), m_content);
    add->setObjectName("slopeAddExcavationStage");
    buttons->addWidget(add);
    auto* remove = new QPushButton(tr("删除当前级"), m_content);
    remove->setObjectName("slopeRemoveExcavationStage");
    buttons->addWidget(remove);
    content->addLayout(buttons);
    m_tabs = new QTabWidget(m_content);
    m_tabs->setObjectName("slopeExcavationStages");
    content->addWidget(m_tabs);
    auto* box = new QGroupBox(tr("开挖面加固"), m_content);
    auto* fields = new QVBoxLayout(box);
    m_reinforcement = new CaveReinforcementFieldPage(box);
    m_reinforcement->setObjectName("slopeReinforcementField");
    fields->addWidget(m_reinforcement);
    content->addWidget(box);
    layout->addWidget(m_content);
    connect(add, &QPushButton::clicked, this,
            [this]
            {
                addStage();
                m_tabs->setCurrentIndex(m_tabs->count() - 1);
            });
    connect(
        remove, &QPushButton::clicked, this,
        [this]
        {
            const int index = m_tabs->currentIndex();
            if (index < 0)
                return;
            const bool entered = m_entered;
            leave();
            auto* page = m_tabs->widget(index);
            m_tabs->removeTab(index);
            delete page;
            for (int i = 0; i < m_tabs->count(); ++i)
            {
                static_cast<CaveExcavationBodyPage*>(m_tabs->widget(i))->setExcavationLayer(-i - 1);
                m_tabs->setTabText(i, tr("第 %1 级").arg(i + 1));
            }
            if (entered)
                enter();
        });
    connect(m_tabs, &QTabWidget::currentChanged, this, &SlopeExcavationSettings::activateStage);
    connect(m_enabled, &QCheckBox::toggled, this,
            [this]
            {
                updateState();
            });
    connect(m_support, &QComboBox::currentIndexChanged, this,
            [this, box]
            {
                box->setVisible(reinforced());
            });
    box->setVisible(false);
    updateState();
}
bool SlopeExcavationSettings::enabled() const
{
    return m_enabled->isChecked();
}
SlopeExcavationSettings::~SlopeExcavationSettings()
{
    leave();
}
bool SlopeExcavationSettings::reinforced() const
{
    return enabled() && m_support->currentIndex() == 1;
}
int SlopeExcavationSettings::stageCount() const
{
    return m_tabs->count();
}
void SlopeExcavationSettings::updateState()
{
    m_content->setVisible(enabled());
    m_reinforcement->parentWidget()->setVisible(reinforced());
    if (!enabled())
    {
        if (m_active)
            m_active->beforeUnload();
        m_active = nullptr;
    }
    else
        activateStage(m_tabs->currentIndex());
}
void SlopeExcavationSettings::activateStage(int index)
{
    if (m_active)
        m_active->beforeUnload();
    m_active = nullptr;
    if (m_entered && enabled() && index >= 0)
    {
        m_active = static_cast<CaveExcavationBodyPage*>(m_tabs->widget(index));
        m_active->beforeLoad();
    }
}
void SlopeExcavationSettings::enter()
{
    m_entered = true;
    activateStage(m_tabs->currentIndex());
}
void SlopeExcavationSettings::leave()
{
    if (m_active)
        m_active->beforeUnload();
    m_active = nullptr;
    m_entered = false;
}
void SlopeExcavationSettings::addStage(const QJsonObject& state)
{
    auto* page = new CaveExcavationBodyPage;
    if (!state.isEmpty())
        page->loadJson(state);
    page->setExcavationLayer(-m_tabs->count() - 1);
    // Tab order is the construction order; no second, conflicting stage number.
    if (auto* spin = page->findChild<QSpinBox*>("excavationLayerSpinBox"))
        spin->setEnabled(false);
    m_tabs->addTab(page, tr("第 %1 级").arg(m_tabs->count() + 1));
}
QVector<QVector<int>> SlopeExcavationSettings::selectedStages()
{
    QVector<QVector<int>> result;
    for (int i = 0; i < m_tabs->count(); ++i)
        result.push_back(
            static_cast<CaveExcavationBodyPage*>(m_tabs->widget(i))->selectedElementIds());
    return result;
}
QJsonObject SlopeExcavationSettings::save()
{
    QJsonArray stages;
    for (int i = 0; i < m_tabs->count(); ++i)
        stages.append(static_cast<CaveExcavationBodyPage*>(m_tabs->widget(i))->saveJson());
    return {{"enabled", enabled()},
            {"support", m_support->currentIndex()},
            {"stages", stages},
            {"reinforcement", m_reinforcement->saveJson()}};
}
void SlopeExcavationSettings::load(const QJsonObject& state)
{
    const bool entered = m_entered;
    leave();
    while (m_tabs->count())
    {
        auto* page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete page;
    }
    for (const auto& stage : state["stages"].toArray())
        addStage(stage.toObject());
    m_reinforcement->loadJson(state["reinforcement"].toObject());
    m_support->setCurrentIndex(state["support"].toInt(0));
    m_enabled->setChecked(state["enabled"].toBool(false));
    updateState();
    if (entered)
        enter();
}
