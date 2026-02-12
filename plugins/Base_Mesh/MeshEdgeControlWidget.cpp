#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>

MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget *parent)
    : BasePage("MeshEdgeControlWidget")
    , ui(new Ui::MeshEdgeControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(4);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "边编号" << "最小尺寸" << "最大尺寸" << "偏置系数");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->btnAddOrUpdate, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAddOrUpdateRule);
    connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &MeshEdgeControlWidget::onRemoveSelectedRule);
    connect(ui->btnClearRules, &QPushButton::clicked, this, &MeshEdgeControlWidget::onClearRules);
    connect(ui->btnResetDefaults, &QPushButton::clicked, this, &MeshEdgeControlWidget::onResetDefaults);

    onResetDefaults();
}

MeshEdgeControlWidget::~MeshEdgeControlWidget()
{
    delete ui;
}

void MeshEdgeControlWidget::beforeLoad()
{
    updateSummary();
}

void MeshEdgeControlWidget::beforeUnload()
{
}

void MeshEdgeControlWidget::onAddOrUpdateRule()
{
    const int edgeId = ui->spEdgeId->value();
    const double minSize = ui->dsMinSize->value();
    const double maxSize = ui->dsMaxSize->value();
    const double bias = ui->dsBias->value();

    if (minSize > maxSize) {
        QMessageBox::warning(this, QStringLiteral("边控制"), QStringLiteral("最小尺寸不能大于最大尺寸。"));
        return;
    }

    int row = findRowByEdgeId(edgeId);
    if (row < 0) {
        row = ui->tableRules->rowCount();
        ui->tableRules->insertRow(row);
    }

    ui->tableRules->setItem(row, 0, new QTableWidgetItem(QString::number(edgeId)));
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(bias, 'f', 3)));

    updateSummary();
}

void MeshEdgeControlWidget::onRemoveSelectedRule()
{
    const int row = ui->tableRules->currentRow();
    if (row < 0) {
        return;
    }
    ui->tableRules->removeRow(row);
    updateSummary();
}

void MeshEdgeControlWidget::onClearRules()
{
    ui->tableRules->setRowCount(0);
    updateSummary();
}

void MeshEdgeControlWidget::onResetDefaults()
{
    ui->spEdgeId->setValue(1);
    ui->dsMinSize->setValue(0.5);
    ui->dsMaxSize->setValue(2.0);
    ui->dsBias->setValue(1.0);
}

int MeshEdgeControlWidget::findRowByEdgeId(int edgeId) const
{
    for (int row = 0; row < ui->tableRules->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableRules->item(row, 0);
        if (item && item->text().toInt() == edgeId) {
            return row;
        }
    }
    return -1;
}

void MeshEdgeControlWidget::updateSummary()
{
    ui->labelSummary->setText(QStringLiteral("当前边控制规则数：%1").arg(ui->tableRules->rowCount()));
}
