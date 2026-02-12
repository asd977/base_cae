#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

#include <QMessageBox>
#include <QTableWidgetItem>

MeshFaceControlWidget::MeshFaceControlWidget(QWidget *parent)
    : BasePage("MeshFaceControlWidget")
    , ui(new Ui::MeshFaceControlWidget)
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshFaceControlWidget::onAddControl);
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshFaceControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshFaceControlWidget::onDeleteControl);
    connect(ui->btnReset, &QPushButton::clicked, this, &MeshFaceControlWidget::onResetForm);

    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshFaceControlWidget::onSelectionChanged);
    connect(ui->editFaceIds, &QLineEdit::textChanged, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinMinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinMaxSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->checkCurvature, &QCheckBox::toggled, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinCurvatureLevel, qOverload<int>(&QSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
}

MeshFaceControlWidget::~MeshFaceControlWidget()
{
    delete ui;
}

void MeshFaceControlWidget::beforeLoad()
{
    refreshSummary();
}

void MeshFaceControlWidget::beforeUnload()
{
}

void MeshFaceControlWidget::onAddControl()
{
    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("面控制"), err);
        return;
    }

    const FaceControlItem item = collectFormData();
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    ui->tableControls->clearSelection();
    onResetForm();
    refreshSummary();
}

void MeshFaceControlWidget::onUpdateControl()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        return;
    }

    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("面控制"), err);
        return;
    }

    const FaceControlItem item = collectFormData();
    m_items[row] = item;
    appendOrUpdateTableRow(row, item);
    refreshSummary();
}

void MeshFaceControlWidget::onDeleteControl()
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

void MeshFaceControlWidget::onResetForm()
{
    ui->editFaceIds->clear();
    ui->spinMinSize->setValue(0.2);
    ui->spinMaxSize->setValue(1.0);
    ui->checkCurvature->setChecked(true);
    ui->spinCurvatureLevel->setValue(2);
    ui->tableControls->clearSelection();
    refreshButtons();
    refreshSummary();
}

void MeshFaceControlWidget::onSelectionChanged()
{
    const int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
    {
        fillFormData(m_items[row]);
    }

    refreshButtons();
}

void MeshFaceControlWidget::onParameterEdited()
{
    ui->spinCurvatureLevel->setEnabled(ui->checkCurvature->isChecked());
    refreshButtons();
    refreshSummary();
}

void MeshFaceControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(5);
    ui->tableControls->setHorizontalHeaderLabels({tr("面 ID"), tr("最小尺寸"), tr("最大尺寸"), tr("曲率加密"), tr("加密等级")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);

    onResetForm();
}

bool MeshFaceControlWidget::validateInput(QString &err) const
{
    if (ui->editFaceIds->text().trimmed().isEmpty())
    {
        err = tr("请先输入面 ID，支持如 2,4,10-16 的格式。");
        return false;
    }

    if (ui->spinMinSize->value() > ui->spinMaxSize->value())
    {
        err = tr("最小尺寸不能大于最大尺寸。");
        return false;
    }

    return true;
}

MeshFaceControlWidget::FaceControlItem MeshFaceControlWidget::collectFormData() const
{
    FaceControlItem item;
    item.faceIds = ui->editFaceIds->text().trimmed();
    item.minSize = ui->spinMinSize->value();
    item.maxSize = ui->spinMaxSize->value();
    item.curvatureRefine = ui->checkCurvature->isChecked();
    item.curvatureLevel = ui->spinCurvatureLevel->value();
    return item;
}

void MeshFaceControlWidget::fillFormData(const FaceControlItem &item)
{
    ui->editFaceIds->setText(item.faceIds);
    ui->spinMinSize->setValue(item.minSize);
    ui->spinMaxSize->setValue(item.maxSize);
    ui->checkCurvature->setChecked(item.curvatureRefine);
    ui->spinCurvatureLevel->setValue(item.curvatureLevel);
}

void MeshFaceControlWidget::appendOrUpdateTableRow(int row, const FaceControlItem &item)
{
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(item.faceIds);
        ui->tableControls->item(row, 1)->setText(QString::number(item.minSize, 'f', 3));
        ui->tableControls->item(row, 2)->setText(QString::number(item.maxSize, 'f', 3));
        ui->tableControls->item(row, 3)->setText(item.curvatureRefine ? tr("启用") : tr("关闭"));
        ui->tableControls->item(row, 4)->setText(item.curvatureRefine ? QString::number(item.curvatureLevel) : "-");
        return;
    }

    const int newRow = ui->tableControls->rowCount();
    ui->tableControls->insertRow(newRow);
    ui->tableControls->setItem(newRow, 0, new QTableWidgetItem(item.faceIds));
    ui->tableControls->setItem(newRow, 1, new QTableWidgetItem(QString::number(item.minSize, 'f', 3)));
    ui->tableControls->setItem(newRow, 2, new QTableWidgetItem(QString::number(item.maxSize, 'f', 3)));
    ui->tableControls->setItem(newRow, 3, new QTableWidgetItem(item.curvatureRefine ? tr("启用") : tr("关闭")));
    ui->tableControls->setItem(newRow, 4, new QTableWidgetItem(item.curvatureRefine ? QString::number(item.curvatureLevel) : "-"));
}

void MeshFaceControlWidget::refreshButtons()
{
    const bool hasSelection = ui->tableControls->currentRow() >= 0;
    const bool hasInput = !ui->editFaceIds->text().trimmed().isEmpty();

    ui->btnAdd->setEnabled(hasInput);
    ui->btnUpdate->setEnabled(hasSelection && hasInput);
    ui->btnDelete->setEnabled(hasSelection);
}

void MeshFaceControlWidget::refreshSummary()
{
    const int count = m_items.size();
    const QString modeHint = ui->checkCurvature->isChecked()
                                 ? tr("已开启曲率加密，可提升圆角/小特征处质量。")
                                 : tr("已关闭曲率加密，适合追求速度的粗网格。");

    ui->labelSummary->setText(tr("已配置 %1 条面控制规则。%2").arg(count).arg(modeHint));
}
