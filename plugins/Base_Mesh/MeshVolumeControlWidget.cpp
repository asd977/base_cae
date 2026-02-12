#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget *parent)
    : BasePage("MeshVolumeControlWidget")
    , ui(new Ui::MeshVolumeControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(5);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "体编号" << "最小尺寸" << "最大尺寸" << "增长率" << "单元阶次");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->btnAddOrUpdate, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddOrUpdateRule);
    connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &MeshVolumeControlWidget::onRemoveSelectedRule);
    connect(ui->btnClearRules, &QPushButton::clicked, this, &MeshVolumeControlWidget::onClearRules);
    connect(ui->btnResetDefaults, &QPushButton::clicked, this, &MeshVolumeControlWidget::onResetDefaults);

    onResetDefaults();
}

MeshVolumeControlWidget::~MeshVolumeControlWidget()
{
    delete ui;
}

void MeshVolumeControlWidget::beforeLoad()
{
    updateSummary();
}

void MeshVolumeControlWidget::beforeUnload()
{
}

void MeshVolumeControlWidget::onAddOrUpdateRule()
{
    const int volumeId = ui->spVolumeId->value();
    const double minSize = ui->dsMinSize->value();
    const double maxSize = ui->dsMaxSize->value();
    const double growth = ui->dsGrowthRate->value();
    const int order = ui->spElementOrder->value();

    if (minSize > maxSize) {
        QMessageBox::warning(this, QStringLiteral("体控制"), QStringLiteral("最小尺寸不能大于最大尺寸。"));
        return;
    }

    int row = findRowByVolumeId(volumeId);
    if (row < 0) {
        row = ui->tableRules->rowCount();
        ui->tableRules->insertRow(row);
    }

    ui->tableRules->setItem(row, 0, new QTableWidgetItem(QString::number(volumeId)));
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(growth, 'f', 3)));
    ui->tableRules->setItem(row, 4, new QTableWidgetItem(QString::number(order)));

    updateSummary();
}

void MeshVolumeControlWidget::onRemoveSelectedRule()
{
    const int row = ui->tableRules->currentRow();
    if (row < 0) {
        return;
    }
    ui->tableRules->removeRow(row);
    updateSummary();
}

void MeshVolumeControlWidget::onClearRules()
{
    ui->tableRules->setRowCount(0);
    updateSummary();
}

void MeshVolumeControlWidget::onResetDefaults()
{
    ui->spVolumeId->setValue(1);
    ui->dsMinSize->setValue(1.0);
    ui->dsMaxSize->setValue(5.0);
    ui->dsGrowthRate->setValue(1.3);
    ui->spElementOrder->setValue(1);
}

int MeshVolumeControlWidget::findRowByVolumeId(int volumeId) const
{
    for (int row = 0; row < ui->tableRules->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableRules->item(row, 0);
        if (item && item->text().toInt() == volumeId) {
            return row;
        }
    }
    return -1;
}

void MeshVolumeControlWidget::updateSummary()
{
    ui->labelSummary->setText(QStringLiteral("当前体控制规则数：%1").arg(ui->tableRules->rowCount()));
}
