#include "MaterialLibraryWidget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFontDatabase>
#include <QHeaderView>
#include <QMessageBox>
#include <QUuid>

#include "MaterialManager.h"
#include "ui_MaterialLibraryWidget.h"

MaterialLibraryWidget::MaterialLibraryWidget(MaterialManager* manager, QWidget* parent)
    : QDialog(parent), ui(new Ui::MaterialLibraryWidget), m_materialManager(manager),
      m_isUpdating(false)
{
    ui->setupUi(this);

    const QString family = QFontDatabase().hasFamily("Microsoft YaHei") ? QString("Microsoft YaHei")
                                                                        : QString("微软雅黑");

    QFont f(family);
    this->setFont(f);

    setModal(true);
    setWindowFlag(Qt::Dialog, true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    ui->materialTable->horizontalHeader()->setStretchLastSection(true);
    ui->materialTable->verticalHeader()->setVisible(false);
    ui->materialTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->materialTable->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->materialTable, &QTableWidget::itemChanged, this,
            &MaterialLibraryWidget::onItemChanged);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &MaterialLibraryWidget::close);

    refreshModels();
}

MaterialLibraryWidget::~MaterialLibraryWidget()
{
    delete ui;
}

bool MaterialLibraryWidget::event(QEvent* event)
{
    if (event->type() == QEvent::Show && ui->modelComboBox->count() == 0)
    {
        refreshModels();
    }
    return QWidget::event(event);
}

void MaterialLibraryWidget::refreshModels()
{
    const auto& models = m_materialManager->models();
    const QString targetKey = m_currentKey;

    m_isUpdating = true;
    ui->modelComboBox->clear();
    for (const auto& model : models)
    {
        ui->modelComboBox->addItem(model.displayName, model.key);
    }
    m_isUpdating = false;

    if (!targetKey.isEmpty())
    {
        int index = ui->modelComboBox->findData(targetKey);
        if (index >= 0)
        {
            ui->modelComboBox->setCurrentIndex(index);
            loadModel(targetKey);
            return;
        }
    }

    if (!models.isEmpty())
    {
        ui->modelComboBox->setCurrentIndex(0);
        loadModel(models.first().key);
    }
    else
    {
        ui->materialTable->clearContents();
        ui->materialTable->setRowCount(0);
    }
}

void MaterialLibraryWidget::setCurrentModelKey(const QString& key)
{
    m_currentKey = key;
}

void MaterialLibraryWidget::loadModel(const QString& key)
{
    m_currentKey = key;
    MaterialModel* model = m_materialManager->findModel(key);
    populateTable(model);
}

void MaterialLibraryWidget::populateTable(MaterialModel* model)
{
    m_isUpdating = true;

    if (!model)
    {
        ui->materialTable->clearContents();
        ui->materialTable->setRowCount(0);
        m_isUpdating = false;
        return;
    }

    QStringList headers;
    headers << QString("材料名称");
    headers.append(model->columns);

    ui->materialTable->setColumnCount(headers.size());
    ui->materialTable->setHorizontalHeaderLabels(headers);
    ui->materialTable->setRowCount(model->materials.size());

    for (int row = 0; row < model->materials.size(); ++row)
    {
        const MaterialEntry& entry = model->materials.at(row);
        auto* nameItem = new QTableWidgetItem(entry.name);
        ui->materialTable->setItem(row, 0, nameItem);

        for (int col = 0; col < model->columns.size(); ++col)
        {
            const QString& columnName = model->columns.at(col);
            auto* item = new QTableWidgetItem(entry.properties.value(columnName));
            ui->materialTable->setItem(row, col + 1, item);
        }
    }

    ui->materialTable->resizeColumnsToContents();
    m_isUpdating = false;
}

void MaterialLibraryWidget::addMaterialAndFocus()
{
    MaterialModel* model = m_materialManager->findModel(m_currentKey);
    if (!model)
    {
        return;
    }

    MaterialEntry entry;
    entry.id = QUuid::createUuid().toString();
    entry.name = QString("新材料%1").arg(model->materials.size() + 1);
    for (const QString& column : model->columns)
    {
        entry.properties.insert(column, QString());
    }
    model->materials.append(entry);

    populateTable(model);
    if (ui->materialTable->rowCount() > 0)
    {
        int row = ui->materialTable->rowCount() - 1;
        ui->materialTable->setCurrentCell(row, 0);
        ui->materialTable->editItem(ui->materialTable->item(row, 0));
    }
    m_materialManager->notifyLibraryChanged();
}

void MaterialLibraryWidget::on_modelComboBox_currentIndexChanged(int index)
{
    if (m_isUpdating)
    {
        return;
    }

    QString key = ui->modelComboBox->itemData(index).toString();
    loadModel(key);
}

void MaterialLibraryWidget::on_addButton_clicked()
{
    if (ui->modelComboBox->count() == 0)
    {
        QMessageBox::information(this, QString("提示"), QString("请先选择一个模型。"));
        return;
    }

    if (m_currentKey.isEmpty())
    {
        m_currentKey = ui->modelComboBox->currentData().toString();
    }
    addMaterialAndFocus();
}

void MaterialLibraryWidget::on_removeButton_clicked()
{
    if (m_currentKey.isEmpty())
    {
        return;
    }

    int row = ui->materialTable->currentRow();
    if (row < 0)
    {
        QMessageBox::information(this, QString("提示"), QString("请选择要删除的材料行。"));
        return;
    }

    MaterialModel* model = m_materialManager->findModel(m_currentKey);
    if (!model || row >= model->materials.size())
    {
        return;
    }

    model->materials.removeAt(row);
    populateTable(model);
    m_materialManager->notifyLibraryChanged();
}

void MaterialLibraryWidget::on_closeButton_clicked()
{
    close();
}

void MaterialLibraryWidget::onItemChanged(QTableWidgetItem* item)
{
    if (m_isUpdating || !item)
    {
        return;
    }

    MaterialModel* model = m_materialManager->findModel(m_currentKey);
    if (!model)
    {
        return;
    }

    const int row = item->row();
    const int column = item->column();
    if (row < 0 || row >= model->materials.size())
    {
        return;
    }

    const QString entryId = model->materials.at(row).id;
    syncRowToModel(model, row, column, item->text());
    m_materialManager->notifyEntryUpdated(entryId, column == 0);
}

void MaterialLibraryWidget::syncRowToModel(MaterialModel* model, int row, int column,
                                           const QString& value)
{
    if (!model || row < 0 || row >= model->materials.size())
    {
        return;
    }

    MaterialEntry& entry = model->materials[row];
    if (column == 0)
    {
        entry.name = value;
        return;
    }

    int propertyIndex = column - 1;
    if (propertyIndex >= 0 && propertyIndex < model->columns.size())
    {
        const QString& key = model->columns.at(propertyIndex);
        entry.properties.insert(key, value);
    }
}
