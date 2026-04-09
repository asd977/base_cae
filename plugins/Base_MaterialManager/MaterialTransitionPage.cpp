#include "MaterialTransitionPage.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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
} // namespace

MaterialTransitionPage::MaterialTransitionPage(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialTransitionPage"), m_manager(manager)
{
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("材料变化：明确选择“原材料/区域”到“变更后材料”。"), this));

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("选中"), QStringLiteral("原材料"), QStringLiteral("变更后材料")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    root->addWidget(m_table);

    auto* row = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("添加映射"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除选中"), this);
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch();
    root->addLayout(row);

    connect(addBtn, &QPushButton::clicked, this, &MaterialTransitionPage::onAddRow);
    connect(delBtn, &QPushButton::clicked, this, &MaterialTransitionPage::onDeleteRow);
    connect(m_table, &QTableWidget::currentCellChanged, this,
            &MaterialTransitionPage::onCurrentRowChanged);
}

void MaterialTransitionPage::beforeLoad()
{
    reloadFromManager();
}

void MaterialTransitionPage::beforeUnload()
{
    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        meshService->highlightMatypes({}, -1, -1, -1);
    }
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
        if (m_table->rowCount() > 0)
            m_table->setCurrentCell(0, 1);
        m_loading = false;
        return;
    }
    for (const auto& p : mappings)
        addRow(p.first, p.second);
    if (m_table->rowCount() > 0)
        m_table->setCurrentCell(0, 1);
    m_loading = false;
}

void MaterialTransitionPage::addRow(int oldMatype, int newMatype)
{
    if (!m_table)
        return;
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* oldBox = new QComboBox(m_table);
    for (auto it = m_oldOptions.constBegin(); it != m_oldOptions.constEnd(); ++it)
        oldBox->addItem(it.value(), it.key());
    int oldIndex = oldBox->findData(oldMatype);
    if (oldIndex >= 0)
        oldBox->setCurrentIndex(oldIndex);
    auto* markerItem = new QTableWidgetItem(QString());
    markerItem->setFlags(markerItem->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 0, markerItem);
    m_table->setCellWidget(row, 1, oldBox);

    auto* newBox = new QComboBox(m_table);
    for (auto it = m_newOptions.constBegin(); it != m_newOptions.constEnd(); ++it)
        newBox->addItem(it.value(), it.key());
    int newIndex = newBox->findData(newMatype);
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
                        refreshRowHighlight(r);
                        syncMappingsToManager();
                        break;
                    }
                }
            });
    refreshRowHighlight(row);
    if (m_table->currentRow() < 0)
        m_table->setCurrentCell(row, 1);
    refreshCurrentRowMarker(m_table->currentRow());
}

void MaterialTransitionPage::refreshRowHighlight(int row)
{
    if (!m_table || row < 0 || row >= m_table->rowCount())
        return;
    auto* oldBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, 1));
    auto* newBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return;
    QSet<int> matypes;
    const int oldMat = currentMatype(oldBox);
    const int newMat = currentMatype(newBox);
    if (oldMat > 0)
        matypes.insert(oldMat);
    if (newMat > 0)
        matypes.insert(newMat);
    meshService->highlightMatypes(matypes, oldMat, newMat, -1);
}

void MaterialTransitionPage::onAddRow()
{
    addRow(-1, -1);
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
    syncMappingsToManager();
}

void MaterialTransitionPage::onCurrentRowChanged(int currentRow, int, int, int)
{
    if (currentRow < 0)
    {
        if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            meshService->highlightMatypes({}, -1, -1, -1);
        refreshCurrentRowMarker(-1);
        return;
    }
    refreshCurrentRowMarker(currentRow);
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
