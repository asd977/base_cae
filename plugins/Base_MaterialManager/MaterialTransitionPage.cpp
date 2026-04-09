#include "MaterialTransitionPage.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
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
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("原材料/区域"), QStringLiteral("原编号"),
                                        QStringLiteral("变更后材料"), QStringLiteral("新编号")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    root->addWidget(m_table);

    auto* row = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("添加映射"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除选中"), this);
    auto* saveBtn = new QPushButton(QStringLiteral("保存映射"), this);
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch();
    row->addWidget(saveBtn);
    root->addLayout(row);

    connect(addBtn, &QPushButton::clicked, this, &MaterialTransitionPage::onAddRow);
    connect(delBtn, &QPushButton::clicked, this, &MaterialTransitionPage::onDeleteRow);
    connect(saveBtn, &QPushButton::clicked, this, &MaterialTransitionPage::onSave);
}

void MaterialTransitionPage::beforeLoad()
{
    reloadFromManager();
}

void MaterialTransitionPage::beforeUnload() {}

void MaterialTransitionPage::reloadFromManager()
{
    if (!m_manager || !m_table)
        return;
    m_oldOptions = m_manager->transitionSourceLabels();
    m_newOptions = m_manager->transitionTargetLabels();
    const auto mappings = m_manager->materialTransitions();

    m_table->setRowCount(0);
    if (mappings.isEmpty())
    {
        addRow(-1, -1);
        return;
    }
    for (const auto& p : mappings)
        addRow(p.first, p.second);
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
    m_table->setCellWidget(row, 0, oldBox);
    m_table->setItem(row, 1, new QTableWidgetItem(QString()));

    auto* newBox = new QComboBox(m_table);
    for (auto it = m_newOptions.constBegin(); it != m_newOptions.constEnd(); ++it)
        newBox->addItem(it.value(), it.key());
    int newIndex = newBox->findData(newMatype);
    if (newIndex >= 0)
        newBox->setCurrentIndex(newIndex);
    m_table->setCellWidget(row, 2, newBox);
    m_table->setItem(row, 3, new QTableWidgetItem(QString()));

    connect(oldBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, row](int) { refreshRowNumbers(row); });
    connect(newBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, row](int) { refreshRowNumbers(row); });
    refreshRowNumbers(row);
}

void MaterialTransitionPage::refreshRowNumbers(int row)
{
    if (!m_table || row < 0 || row >= m_table->rowCount())
        return;
    auto* oldBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, 0));
    auto* newBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));
    if (m_table->item(row, 1))
        m_table->item(row, 1)->setText(QString::number(currentMatype(oldBox)));
    if (m_table->item(row, 3))
        m_table->item(row, 3)->setText(QString::number(currentMatype(newBox)));
}

void MaterialTransitionPage::onAddRow()
{
    addRow(-1, -1);
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
}

void MaterialTransitionPage::onSave()
{
    if (!m_manager || !m_table)
        return;
    QVector<QPair<int, int>> mappings;
    QStringList badRows;
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        auto* oldBox = qobject_cast<QComboBox*>(m_table->cellWidget(r, 0));
        auto* newBox = qobject_cast<QComboBox*>(m_table->cellWidget(r, 2));
        const int oldV = currentMatype(oldBox);
        const int newV = currentMatype(newBox);
        if (oldV <= 0 || newV <= 0)
        {
            badRows.append(QString::number(r + 1));
            continue;
        }
        mappings.push_back({oldV, newV});
    }
    if (!badRows.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("第 %1 行映射不完整，请重新选择。")
                                 .arg(badRows.join(QStringLiteral(", "))));
        return;
    }
    m_manager->setMaterialTransitions(mappings);
    QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("材料变化映射已保存。"));
}

