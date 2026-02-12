#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <QMessageBox>
#include <QTableWidgetItem>

MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget *parent)
    : BasePage("MeshEdgeControlWidget")
    , ui(new Ui::MeshEdgeControlWidget)
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAddControl);
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshEdgeControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshEdgeControlWidget::onDeleteControl);
    connect(ui->btnReset, &QPushButton::clicked, this, &MeshEdgeControlWidget::onResetForm);

    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshEdgeControlWidget::onSelectionChanged);
    connect(ui->editEdgeIds, &QLineEdit::textChanged, this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinDivisions, qOverload<int>(&QSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinBias, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->checkHardConstraint, &QCheckBox::toggled, this, &MeshEdgeControlWidget::onParameterEdited);
}

MeshEdgeControlWidget::~MeshEdgeControlWidget()
{
    delete ui;
}

void MeshEdgeControlWidget::beforeLoad()
{
    refreshSummary();
}

void MeshEdgeControlWidget::beforeUnload()
{
}

void MeshEdgeControlWidget::onAddControl()
{
    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("边控制"), err);
        return;
    }

    EdgeControlItem item = collectFormData();
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    ui->tableControls->clearSelection();
    onResetForm();
    refreshSummary();
}

void MeshEdgeControlWidget::onUpdateControl()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        return;
    }

    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("边控制"), err);
        return;
    }

    const EdgeControlItem item = collectFormData();
    m_items[row] = item;
    appendOrUpdateTableRow(row, item);
    refreshSummary();
}

void MeshEdgeControlWidget::onDeleteControl()
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

void MeshEdgeControlWidget::onResetForm()
{
    ui->editEdgeIds->clear();
    ui->spinSize->setValue(1.0);
    ui->spinDivisions->setValue(10);
    ui->spinBias->setValue(1.0);
    ui->checkHardConstraint->setChecked(false);
    ui->tableControls->clearSelection();
    refreshButtons();
    refreshSummary();
}

void MeshEdgeControlWidget::onSelectionChanged()
{
    const int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
    {
        fillFormData(m_items[row]);
    }

    refreshButtons();
}

void MeshEdgeControlWidget::onParameterEdited()
{
    refreshButtons();
    refreshSummary();
}

void MeshEdgeControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(5);
    ui->tableControls->setHorizontalHeaderLabels({tr("边 ID"), tr("目标尺寸"), tr("分段数"), tr("偏置比"), tr("硬约束")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);

    onResetForm();
}

bool MeshEdgeControlWidget::validateInput(QString &err) const
{
    if (ui->editEdgeIds->text().trimmed().isEmpty())
    {
        err = tr("请先输入边 ID，支持如 1,3,5-8 的格式。");
        return false;
    }

    if (ui->spinSize->value() <= 0.0)
    {
        err = tr("目标尺寸必须大于 0。");
        return false;
    }

    if (ui->spinBias->value() < 1.0)
    {
        err = tr("偏置比建议不小于 1.0。");
        return false;
    }

    return true;
}

MeshEdgeControlWidget::EdgeControlItem MeshEdgeControlWidget::collectFormData() const
{
    EdgeControlItem item;
    item.edgeIds = ui->editEdgeIds->text().trimmed();
    item.size = ui->spinSize->value();
    item.divisions = ui->spinDivisions->value();
    item.bias = ui->spinBias->value();
    item.hardConstraint = ui->checkHardConstraint->isChecked();
    return item;
}

void MeshEdgeControlWidget::fillFormData(const EdgeControlItem &item)
{
    ui->editEdgeIds->setText(item.edgeIds);
    ui->spinSize->setValue(item.size);
    ui->spinDivisions->setValue(item.divisions);
    ui->spinBias->setValue(item.bias);
    ui->checkHardConstraint->setChecked(item.hardConstraint);
}

void MeshEdgeControlWidget::appendOrUpdateTableRow(int row, const EdgeControlItem &item)
{
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(item.edgeIds);
        ui->tableControls->item(row, 1)->setText(QString::number(item.size, 'f', 3));
        ui->tableControls->item(row, 2)->setText(QString::number(item.divisions));
        ui->tableControls->item(row, 3)->setText(QString::number(item.bias, 'f', 2));
        ui->tableControls->item(row, 4)->setText(item.hardConstraint ? tr("是") : tr("否"));
        return;
    }

    const int newRow = ui->tableControls->rowCount();
    ui->tableControls->insertRow(newRow);
    ui->tableControls->setItem(newRow, 0, new QTableWidgetItem(item.edgeIds));
    ui->tableControls->setItem(newRow, 1, new QTableWidgetItem(QString::number(item.size, 'f', 3)));
    ui->tableControls->setItem(newRow, 2, new QTableWidgetItem(QString::number(item.divisions)));
    ui->tableControls->setItem(newRow, 3, new QTableWidgetItem(QString::number(item.bias, 'f', 2)));
    ui->tableControls->setItem(newRow, 4, new QTableWidgetItem(item.hardConstraint ? tr("是") : tr("否")));
}

void MeshEdgeControlWidget::refreshButtons()
{
    const bool hasSelection = ui->tableControls->currentRow() >= 0;
    const bool hasInput = !ui->editEdgeIds->text().trimmed().isEmpty();

    ui->btnAdd->setEnabled(hasInput);
    ui->btnUpdate->setEnabled(hasSelection && hasInput);
    ui->btnDelete->setEnabled(hasSelection);
}

void MeshEdgeControlWidget::refreshSummary()
{
    const int count = m_items.size();
    const QString editingHint = ui->tableControls->currentRow() >= 0
                                    ? tr("当前为“编辑模式”，保存将覆盖选中项。")
                                    : tr("当前为“新增模式”，点击“添加控制”可追加规则。");

    ui->labelSummary->setText(tr("已配置 %1 条边控制规则。%2").arg(count).arg(editingHint));
}
