#include "MaterialManagerWidget.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
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
    ui->treeWidget->clear();
    for (const auto& model : models)
    {
        auto* modelItem = new QTreeWidgetItem(ui->treeWidget);
        modelItem->setText(0, model.displayName);
        modelItem->setData(0, Qt::UserRole, model.key);
        for (const auto& entry : model.materials)
        {
            auto* entryItem = new QTreeWidgetItem(modelItem);
            entryItem->setText(0, entry.name);
            entryItem->setData(0, Qt::UserRole, model.key);
            entryItem->setData(0, Qt::UserRole + 1, entry.id);
        }
    }
    ui->treeWidget->expandAll();
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
        QMessageBox::information(this, QString("提示"), QString("已生成 Imat.dat（测试版）"));
    }
    else
    {
        QMessageBox::warning(this, QString("提示"),
                             QString("生成失败，请确认材料已添加到工程并选择 MATYPE。"));
    }
}
