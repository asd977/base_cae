#include "MaterialManager.h"
#include "MaterialManagerWidget.h"
#include "ui_MaterialManagerWidget.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDebug>
#include <QTreeWidget>
#include <QHeaderView>
#include <QUuid>
#include <QMessageBox>

MaterialManagerWidget::MaterialManagerWidget(MaterialManager *manager,QWidget *parent) :
    m_materialManager(manager),
    QWidget(parent),
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

void MaterialManagerWidget::refreshSummary(const QVector<MaterialModel> &models)
{
    ui->treeWidget->clear();
    for (const auto &model : models) {
        auto *modelItem = new QTreeWidgetItem(ui->treeWidget);
        modelItem->setText(0, model.displayName);
        modelItem->setData(0, Qt::UserRole, model.key);
        for (const auto &entry : model.materials) {
            auto *entryItem = new QTreeWidgetItem(modelItem);
            entryItem->setText(0, entry.name);
            entryItem->setData(0, Qt::UserRole, model.key);
            entryItem->setData(0, Qt::UserRole + 1, entry.id);
        }
    }
    ui->treeWidget->expandAll();
}

void MaterialManagerWidget::on_pushButton_2_clicked()
{
    m_materialManager->openLibrary();
}

//新建材料，直接添加
void MaterialManagerWidget::on_pushButton_3_clicked()
{
    QString targetKey;
    QTreeWidgetItem *item = ui->treeWidget->currentItem();
    if (item) {
        targetKey = item->data(0, Qt::UserRole).toString();
        if (targetKey.isEmpty() && item->parent()) {
            targetKey = item->parent()->data(0, Qt::UserRole).toString();
        }
    }
    if (targetKey.isEmpty() && !m_materialManager->models().isEmpty()) {
        targetKey = m_materialManager->models().first().key;
    }

    m_materialManager->openLibrary(targetKey, true);
}

void MaterialManagerWidget::on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item) {
        return;
    }
    const QString entryId = item->data(0, Qt::UserRole + 1).toString();
    const QString modelKey = item->data(0, Qt::UserRole).toString();
    if (!entryId.isEmpty()) {
        m_materialManager->focusOnMaterial(entryId);
    } else if (!modelKey.isEmpty()) {
        m_materialManager->focusOnModel(modelKey);
    }
}

void MaterialManagerWidget::on_addToTreeButton_clicked()
{
    QTreeWidgetItem *item = ui->treeWidget->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个材料。"));
        return;
    }

    const QString entryId = item->data(0, Qt::UserRole + 1).toString();
    if (entryId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选中具体的材料节点。"));
        return;
    }

    m_materialManager->addEntryToProjectTree(entryId);
}

void MaterialManagerWidget::on_removeFromTreeButton_clicked()
{
    QTreeWidgetItem *item = ui->treeWidget->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个材料。"));
        return;
    }

    const QString entryId = item->data(0, Qt::UserRole + 1).toString();
    if (entryId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选中具体的材料节点。"));
        return;
    }

    m_materialManager->removeEntryFromProjectTree(entryId);
}

void MaterialManagerWidget::on_generateImatButton_clicked()
{
    if (m_materialManager->generateCaculateFile()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("已生成 Imat.dat（测试版）"));
    } else {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("生成失败，请确认材料已添加到工程并选择 MATYPE。"));
    }
}
