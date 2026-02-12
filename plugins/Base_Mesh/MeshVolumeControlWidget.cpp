#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

#include <QMessageBox>
#include <QTableWidgetItem>

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget *parent)
    : BasePage("MeshVolumeControlWidget")
    , ui(new Ui::MeshVolumeControlWidget)
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddControl);
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshVolumeControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshVolumeControlWidget::onDeleteControl);
    connect(ui->btnReset, &QPushButton::clicked, this, &MeshVolumeControlWidget::onResetForm);

    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshVolumeControlWidget::onSelectionChanged);
    connect(ui->editVolumeIds, &QLineEdit::textChanged, this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinMinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinMaxSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinGrowthRate, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->checkBoundaryLayer, &QCheckBox::toggled, this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinLayerCount, qOverload<int>(&QSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
}

MeshVolumeControlWidget::~MeshVolumeControlWidget()
{
    delete ui;
}

void MeshVolumeControlWidget::beforeLoad()
{
    refreshSummary();
}

void MeshVolumeControlWidget::beforeUnload()
{
}

void MeshVolumeControlWidget::onAddControl()
{
    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("体控制"), err);
        return;
    }

    const VolumeControlItem item = collectFormData();
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    ui->tableControls->clearSelection();
    onResetForm();
    refreshSummary();
}

void MeshVolumeControlWidget::onUpdateControl()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        return;
    }

    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("体控制"), err);
        return;
    }

    const VolumeControlItem item = collectFormData();
    m_items[row] = item;
    appendOrUpdateTableRow(row, item);
    refreshSummary();
}

void MeshVolumeControlWidget::onDeleteControl()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        return;
    }

    m_items.removeAt(row);
    ui->tableControls->removeRow(row);
    onResetForm();
    refreshSummary();
}

void MeshVolumeControlWidget::onResetForm()
{
    ui->editVolumeIds->clear();
    ui->spinMinSize->setValue(0.5);
    ui->spinMaxSize->setValue(2.0);
    ui->spinGrowthRate->setValue(1.2);
    ui->checkBoundaryLayer->setChecked(false);
    ui->spinLayerCount->setValue(3);
    ui->tableControls->clearSelection();
    refreshButtons();
    refreshSummary();
}

void MeshVolumeControlWidget::onSelectionChanged()
{
    const int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
    {
        fillFormData(m_items[row]);
    }

    refreshButtons();
}

void MeshVolumeControlWidget::onParameterEdited()
{
    ui->spinLayerCount->setEnabled(ui->checkBoundaryLayer->isChecked());
    refreshButtons();
    refreshSummary();
}

void MeshVolumeControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(6);
    ui->tableControls->setHorizontalHeaderLabels({tr("体 ID"), tr("最小尺寸"), tr("最大尺寸"), tr("增长率"), tr("边界层"), tr("层数")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);

    onResetForm();
}

bool MeshVolumeControlWidget::validateInput(QString &err) const
{
    if (ui->editVolumeIds->text().trimmed().isEmpty())
    {
        err = tr("请先输入体 ID，支持如 1,2,7-9 的格式。");
        return false;
    }

    if (ui->spinMinSize->value() > ui->spinMaxSize->value())
    {
        err = tr("最小尺寸不能大于最大尺寸。");
        return false;
    }

    if (ui->spinGrowthRate->value() < 1.0)
    {
        err = tr("增长率不应小于 1.0。");
        return false;
    }

    return true;
}

MeshVolumeControlWidget::VolumeControlItem MeshVolumeControlWidget::collectFormData() const
{
    VolumeControlItem item;
    item.volumeIds = ui->editVolumeIds->text().trimmed();
    item.minSize = ui->spinMinSize->value();
    item.maxSize = ui->spinMaxSize->value();
    item.growthRate = ui->spinGrowthRate->value();
    item.boundaryLayer = ui->checkBoundaryLayer->isChecked();
    item.layerCount = ui->spinLayerCount->value();
    return item;
}

void MeshVolumeControlWidget::fillFormData(const VolumeControlItem &item)
{
    ui->editVolumeIds->setText(item.volumeIds);
    ui->spinMinSize->setValue(item.minSize);
    ui->spinMaxSize->setValue(item.maxSize);
    ui->spinGrowthRate->setValue(item.growthRate);
    ui->checkBoundaryLayer->setChecked(item.boundaryLayer);
    ui->spinLayerCount->setValue(item.layerCount);
}

void MeshVolumeControlWidget::appendOrUpdateTableRow(int row, const VolumeControlItem &item)
{
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(item.volumeIds);
        ui->tableControls->item(row, 1)->setText(QString::number(item.minSize, 'f', 3));
        ui->tableControls->item(row, 2)->setText(QString::number(item.maxSize, 'f', 3));
        ui->tableControls->item(row, 3)->setText(QString::number(item.growthRate, 'f', 2));
        ui->tableControls->item(row, 4)->setText(item.boundaryLayer ? tr("启用") : tr("关闭"));
        ui->tableControls->item(row, 5)->setText(item.boundaryLayer ? QString::number(item.layerCount) : "-");
        return;
    }

    const int newRow = ui->tableControls->rowCount();
    ui->tableControls->insertRow(newRow);
    ui->tableControls->setItem(newRow, 0, new QTableWidgetItem(item.volumeIds));
    ui->tableControls->setItem(newRow, 1, new QTableWidgetItem(QString::number(item.minSize, 'f', 3)));
    ui->tableControls->setItem(newRow, 2, new QTableWidgetItem(QString::number(item.maxSize, 'f', 3)));
    ui->tableControls->setItem(newRow, 3, new QTableWidgetItem(QString::number(item.growthRate, 'f', 2)));
    ui->tableControls->setItem(newRow, 4, new QTableWidgetItem(item.boundaryLayer ? tr("启用") : tr("关闭")));
    ui->tableControls->setItem(newRow, 5, new QTableWidgetItem(item.boundaryLayer ? QString::number(item.layerCount) : "-"));
}

void MeshVolumeControlWidget::refreshButtons()
{
    const bool hasSelection = ui->tableControls->currentRow() >= 0;
    const bool hasInput = !ui->editVolumeIds->text().trimmed().isEmpty();

    ui->btnAdd->setEnabled(hasInput);
    ui->btnUpdate->setEnabled(hasSelection && hasInput);
    ui->btnDelete->setEnabled(hasSelection);
}

void MeshVolumeControlWidget::refreshSummary()
{
    const int count = m_items.size();
    const QString layerHint = ui->checkBoundaryLayer->isChecked()
                                  ? tr("已启用边界层，适合近壁面解析。")
                                  : tr("未启用边界层，网格规模更可控。");

    ui->labelSummary->setText(tr("已配置 %1 条体控制规则。%2").arg(count).arg(layerHint));
}
