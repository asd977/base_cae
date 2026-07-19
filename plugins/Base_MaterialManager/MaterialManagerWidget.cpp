#include "MaterialManagerWidget.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDebug>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QSet>
#include <QTreeWidget>
#include <QUuid>

#include "MaterialManager.h"
#include "ui_MaterialManagerWidget.h"

MaterialManagerWidget::MaterialManagerWidget(MaterialManager* manager, QWidget* parent)
    : m_materialManager(manager), BasePage("MaterialManagerWidget"),
      ui(new Ui::MaterialManagerWidget)
{
    ui->setupUi(this);

    ui->treeWidget->setHeaderHidden(false);
    ui->treeWidget->setIndentation(18);
    ui->treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->treeWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->treeWidget->setColumnCount(1);
}

MaterialManagerWidget::~MaterialManagerWidget()
{
    delete ui;
}

void MaterialManagerWidget::refreshSummary(const QVector<MaterialModel>& models)
{
    QSet<QString> expandedModelKeys;
    for (int index = 0; index < ui->treeWidget->topLevelItemCount(); ++index)
    {
        QTreeWidgetItem* item = ui->treeWidget->topLevelItem(index);
        if (item && item->isExpanded())
            expandedModelKeys.insert(item->data(0, Qt::UserRole).toString());
    }

    QString selectedModelKey;
    QString selectedEntryId;
    if (QTreeWidgetItem* selectedItem = ui->treeWidget->currentItem())
    {
        selectedModelKey = selectedItem->data(0, Qt::UserRole).toString();
        selectedEntryId = selectedItem->data(0, Qt::UserRole + 1).toString();
    }

    ui->treeWidget->clear();
    QTreeWidgetItem* restoredSelection = nullptr;
    for (const auto& model : models)
    {
        auto* modelItem = new QTreeWidgetItem(ui->treeWidget);
        modelItem->setText(0, model.displayName);
        modelItem->setData(0, Qt::UserRole, model.key);
        modelItem->setExpanded(expandedModelKeys.contains(model.key));
        if (selectedEntryId.isEmpty() && selectedModelKey == model.key)
            restoredSelection = modelItem;
        for (const auto& entry : model.materials)
        {
            auto* entryItem = new QTreeWidgetItem(modelItem);
            entryItem->setText(0, entry.name);
            entryItem->setData(0, Qt::UserRole, model.key);
            entryItem->setData(0, Qt::UserRole + 1, entry.id);
            if (!selectedEntryId.isEmpty() && selectedEntryId == entry.id)
                restoredSelection = entryItem;
        }
    }
    if (restoredSelection)
        ui->treeWidget->setCurrentItem(restoredSelection);
}

void MaterialManagerWidget::beforeLoad()
{
    if (!m_materialManager)
    {
        return;
    }
    // 进入管理页时不应保留来自材料条目的高亮状态
    m_materialManager->clearAllMatypeHighlight();
}

void MaterialManagerWidget::beforeUnload()
{
    if (!m_materialManager)
    {
        return;
    }
    // 离开管理页时兜底清空高亮
    m_materialManager->clearAllMatypeHighlight();
}
void MaterialManagerWidget::on_pushButton_2_clicked()
{
    on_pushButton_3_clicked();
}

// 新建材料，直接添加
void MaterialManagerWidget::on_pushButton_3_clicked()
{
    QString targetKey;
    QTreeWidgetItem* item = ui->treeWidget->currentItem();
    if (item)
    {
        targetKey = item->data(0, Qt::UserRole).toString();
        if (targetKey.isEmpty() && item->parent())
        {
            targetKey = item->parent()->data(0, Qt::UserRole).toString();
        }
    }
    if (targetKey.isEmpty() && !m_materialManager->models().isEmpty())
    {
        targetKey = m_materialManager->models().first().key;
    }

    const QString entryId = m_materialManager->addMaterialToModel(targetKey, true);
    if (!entryId.isEmpty())
    {
        m_materialManager->focusOnMaterial(entryId);
    }
}

void MaterialManagerWidget::on_treeWidget_itemDoubleClicked(QTreeWidgetItem* item, int)
{
    if (!item)
    {
        return;
    }
    const QString entryId = item->data(0, Qt::UserRole + 1).toString();
    const QString modelKey = item->data(0, Qt::UserRole).toString();
    if (!entryId.isEmpty())
    {
        m_materialManager->focusOnMaterial(entryId);
    }
    else if (!modelKey.isEmpty())
    {
        m_materialManager->focusOnModel(modelKey);
    }
}

void MaterialManagerWidget::on_addToTreeButton_clicked()
{
    QTreeWidgetItem* item = ui->treeWidget->currentItem();
    if (!item)
    {
        QMessageBox::information(this, QString("提示"), QString("请先选择一个材料。"));
        return;
    }

    const QString entryId = item->data(0, Qt::UserRole + 1).toString();
    if (entryId.isEmpty())
    {
        QMessageBox::information(this, QString("提示"), QString("请选中具体的材料节点。"));
        return;
    }

    m_materialManager->addEntryToProjectTree(entryId);
}

void MaterialManagerWidget::on_removeFromTreeButton_clicked()
{
    QTreeWidgetItem* item = ui->treeWidget->currentItem();
    if (!item)
    {
        QMessageBox::information(this, QString("提示"), QString("请先选择一个材料。"));
        return;
    }

    const QString entryId = item->data(0, Qt::UserRole + 1).toString();
    if (entryId.isEmpty())
    {
        QMessageBox::information(this, QString("提示"), QString("请选中具体的材料节点。"));
        return;
    }

    m_materialManager->removeEntryFromProjectTree(entryId);
}

void MaterialManagerWidget::on_generateImatButton_clicked()
{
    if (m_materialManager->generateCaculateFile())
    {
    QMessageBox::information(this, QString("提示"), QString("已生成材料计算输入。"));
    }
    else
    {
        QMessageBox::warning(this, QString("提示"),
                             QString("生成失败，请确认材料已添加到工程并选择 MATYPE。"));
    }
}
void MaterialManagerWidget::on_importImatButton_clicked()
{
    if (!m_materialManager)
    {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入 IMAT.DAT"), QString(),
        QStringLiteral("IMAT 文件 (IMAT.DAT *.dat *.DAT);;所有文件 (*.*)"));
    if (filePath.isEmpty())
    {
        return;
    }

    int importedCount = 0;
    QString error;
    if (m_materialManager->importIMATFile(filePath, &importedCount, &error))
    {
        QMessageBox::information(
            this, QStringLiteral("提示"),
            QStringLiteral("已从 IMAT.DAT 创建 %1 个材料，并自动添加到工程。").arg(importedCount));
    }
    else
    {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             error.isEmpty() ? QStringLiteral("导入失败，请确认 IMAT.DAT 格式。")
                                             : error);
    }
}
