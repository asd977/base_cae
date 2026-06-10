#include "MaterialTransitionPage.h"

#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "MaterialManager.h"

namespace
{
int currentMatype(QComboBox* box)
{
    if (!box)
        return -1;

    const QVariant data = box->currentData();
    return data.isValid() ? data.toInt() : -1;
}

int currentDomainDim()
{
    const int meshDim = PublicVariables::instance()
    .get(PublicConstants::kMeshDimension)
        .toInt();

    return (meshDim == 3) ? 3 : 2;
}

DomainKey matypeDomainKey(int matype)
{
    if (matype <= 0)
        return DomainKey{};

    return DomainKey{
                     QStringLiteral("MATYPE"),
                     currentDomainDim(),
                     matype};
}

void clearMaterialTransitionHighlight(const QString& pageType, const QString& uuid)
{
    auto& pm = PickManager::instance();

    pm.setAppliedDomains(pageType, uuid, QSet<DomainKey>{});
    pm.setSelectedDomain(pageType, uuid, DomainKey{});
    pm.setPreviewDomain(pageType, uuid, DomainKey{});
    pm.setPreDeleteDomain(pageType, uuid, DomainKey{});
}

} // namespace

MaterialTransitionPage::MaterialTransitionPage(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialTransitionPage"), m_manager(manager)
{
    setAttribute(Qt::WA_StyledBackground, true);
    QPalette transPal = this->palette();
    transPal.setColor(QPalette::Window, Qt::white);
    this->setPalette(transPal);
    this->setAutoFillBackground(true);

    auto* pageHeaderLabel = new QLabel("材料变换设置", this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pageHeaderLabel);

    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(9, 9, 9, 9);
    contentLayout->setSpacing(6);
    root->addLayout(contentLayout);

    contentLayout->addWidget(
        new QLabel(QStringLiteral("材料变换：明确选择“原材料/区域”到“变更后材料”。"), this));

    m_table = new QTableWidget(this);
    m_table->setMinimumWidth(10);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("选中"),
         QStringLiteral("原材料"),
         QStringLiteral("变更后材料")});

    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);

    contentLayout->addWidget(m_table);

    auto* row = new QHBoxLayout();

    auto* addBtn = new QPushButton(QStringLiteral("添加映射"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除选中"), this);

    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch();

    contentLayout->addLayout(row);

    connect(addBtn, &QPushButton::clicked,
            this, &MaterialTransitionPage::onAddRow);

    connect(delBtn, &QPushButton::clicked,
            this, &MaterialTransitionPage::onDeleteRow);

    connect(m_table, &QTableWidget::currentCellChanged,
            this, &MaterialTransitionPage::onCurrentRowChanged);
}

void MaterialTransitionPage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Domain);

    reloadFromManager();

    if (m_table && m_table->currentRow() >= 0)
        refreshRowHighlight(m_table->currentRow());
    else
        clearMaterialTransitionHighlight(getPageType(), m_uuid);
}

void MaterialTransitionPage::beforeUnload()
{
    clearMaterialTransitionHighlight(getPageType(), m_uuid);
    PickManager::instance().clearDomainSelection(getPageType(), m_uuid);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Domain);
}

void MaterialTransitionPage::reloadFromManager()
{
    if (!m_manager || !m_table)
        return;

    m_loading = true;

    m_oldOptions = m_manager->transitionSourceLabels();
    m_newOptions = m_manager->transitionTargetLabels();

    const auto mappings = m_manager->materialTransitions();

    m_table->setRowCount(0);

    if (mappings.isEmpty())
    {
        addRow(-1, -1);
    }
    else
    {
        for (const auto& p : mappings)
            addRow(p.first, p.second);
    }

    if (m_table->rowCount() > 0)
        m_table->setCurrentCell(0, 1);

    refreshCurrentRowMarker(m_table->currentRow());

    m_loading = false;

    if (m_table->currentRow() >= 0)
        refreshRowHighlight(m_table->currentRow());
    else
        clearMaterialTransitionHighlight(getPageType(), m_uuid);
}

void MaterialTransitionPage::addRow(int oldMatype, int newMatype)
{
    if (!m_table)
        return;

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* markerItem = new QTableWidgetItem(QString());
    markerItem->setFlags(markerItem->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 0, markerItem);

    auto* oldBox = new QComboBox(m_table);
    oldBox->setMinimumWidth(10);
    for (auto it = m_oldOptions.constBegin(); it != m_oldOptions.constEnd(); ++it)
        oldBox->addItem(it.value(), it.key());

    const int oldIndex = oldBox->findData(oldMatype);
    if (oldIndex >= 0)
        oldBox->setCurrentIndex(oldIndex);

    m_table->setCellWidget(row, 1, oldBox);

    auto* newBox = new QComboBox(m_table);
    newBox->setMinimumWidth(10);
    for (auto it = m_newOptions.constBegin(); it != m_newOptions.constEnd(); ++it)
        newBox->addItem(it.value(), it.key());

    const int newIndex = newBox->findData(newMatype);
    if (newIndex >= 0)
        newBox->setCurrentIndex(newIndex);

    m_table->setCellWidget(row, 2, newBox);

    connect(oldBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, oldBox](int)
            {
                if (!m_table)
                    return;

                for (int r = 0; r < m_table->rowCount(); ++r)
                {
                    if (m_table->cellWidget(r, 1) == oldBox)
                    {
                        if (m_table->currentRow() != r)
                            m_table->setCurrentCell(r, 1);

                        refreshRowHighlight(r);
                        syncMappingsToManager();
                        break;
                    }
                }
            });

    connect(newBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, newBox](int)
            {
                if (!m_table)
                    return;

                for (int r = 0; r < m_table->rowCount(); ++r)
                {
                    if (m_table->cellWidget(r, 2) == newBox)
                    {
                        if (m_table->currentRow() != r)
                            m_table->setCurrentCell(r, 2);

                        refreshRowHighlight(r);
                        syncMappingsToManager();
                        break;
                    }
                }
            });

    if (m_table->currentRow() < 0)
        m_table->setCurrentCell(row, 1);

    refreshCurrentRowMarker(m_table->currentRow());
}

void MaterialTransitionPage::refreshRowHighlight(int row)
{
    if (!m_table || row < 0 || row >= m_table->rowCount())
    {
        clearMaterialTransitionHighlight(getPageType(), m_uuid);
        return;
    }

    auto* oldBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, 1));
    auto* newBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));

    const int oldMat = currentMatype(oldBox);
    const int newMat = currentMatype(newBox);

    const DomainKey oldKey = matypeDomainKey(oldMat);
    const DomainKey newKey = matypeDomainKey(newMat);

    QSet<DomainKey> appliedKeys;

    if (oldKey.isValid())
        appliedKeys.insert(oldKey);

    if (newKey.isValid())
        appliedKeys.insert(newKey);

    auto& pm = PickManager::instance();

    pm.activate(getPageType(), m_uuid, PickMode::Domain);

    pm.setAppliedDomains(getPageType(), m_uuid, appliedKeys);
    pm.setSelectedDomain(getPageType(), m_uuid, DomainKey{});

           // 原材料用预览色标识，变更后材料用待删除/强调色标识。
    pm.setPreviewDomain(getPageType(), m_uuid, oldKey);
    pm.setPreDeleteDomain(getPageType(), m_uuid, newKey);
}

void MaterialTransitionPage::onAddRow()
{
    if (!m_table)
        return;

    const int row = m_table->rowCount();

    addRow(-1, -1);

    if (row >= 0 && row < m_table->rowCount())
        m_table->setCurrentCell(row, 1);

    refreshCurrentRowMarker(m_table->currentRow());
    refreshRowHighlight(m_table->currentRow());
    syncMappingsToManager();
}

void MaterialTransitionPage::onDeleteRow()
{
    if (!m_table)
        return;

    const int row = m_table->currentRow();

    if (row >= 0)
        m_table->removeRow(row);

    if (m_table->rowCount() <= 0)
        addRow(-1, -1);

    const int nextRow = std::min(row, m_table->rowCount() - 1);
    if (nextRow >= 0)
        m_table->setCurrentCell(nextRow, 1);

    refreshCurrentRowMarker(m_table->currentRow());
    refreshRowHighlight(m_table->currentRow());
    syncMappingsToManager();
}

void MaterialTransitionPage::onCurrentRowChanged(int currentRow, int, int, int)
{
    if (m_loading)
        return;

    refreshCurrentRowMarker(currentRow);

    if (currentRow < 0)
    {
        clearMaterialTransitionHighlight(getPageType(), m_uuid);
        return;
    }

    refreshRowHighlight(currentRow);
}

void MaterialTransitionPage::syncMappingsToManager()
{
    if (m_loading || !m_manager || !m_table)
        return;

    QVector<QPair<int, int>> mappings;

    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        auto* oldBox = qobject_cast<QComboBox*>(m_table->cellWidget(r, 1));
        auto* newBox = qobject_cast<QComboBox*>(m_table->cellWidget(r, 2));

        const int oldV = currentMatype(oldBox);
        const int newV = currentMatype(newBox);

        if (oldV > 0 && newV > 0)
            mappings.push_back({oldV, newV});
    }

    m_manager->setMaterialTransitions(mappings);
}

void MaterialTransitionPage::refreshCurrentRowMarker(int currentRow)
{
    if (!m_table)
        return;

    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        auto* item = m_table->item(r, 0);
        if (!item)
            continue;

        item->setText(r == currentRow ? QStringLiteral("▶") : QString());
    }
}
