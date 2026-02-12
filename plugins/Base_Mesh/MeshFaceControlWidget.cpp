#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>

MeshFaceControlWidget::MeshFaceControlWidget(QWidget *parent)
    : BasePage("MeshFaceControlWidget")
    , ui(new Ui::MeshFaceControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(5);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "面编号" << "最小尺寸" << "最大尺寸" << "增长率" << "边界层");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->btnAddOrUpdate, &QPushButton::clicked, this, &MeshFaceControlWidget::onAddOrUpdateRule);
    connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &MeshFaceControlWidget::onRemoveSelectedRule);
    connect(ui->btnClearRules, &QPushButton::clicked, this, &MeshFaceControlWidget::onClearRules);
    connect(ui->btnResetDefaults, &QPushButton::clicked, this, &MeshFaceControlWidget::onResetDefaults);

    onResetDefaults();
}

MeshFaceControlWidget::~MeshFaceControlWidget()
{
    delete ui;
}

void MeshFaceControlWidget::beforeLoad()
{
    updateSummary();
}

void MeshFaceControlWidget::beforeUnload()
{
}

void MeshFaceControlWidget::onAddOrUpdateRule()
{
    const int faceId = ui->spFaceId->value();
    const double minSize = ui->dsMinSize->value();
    const double maxSize = ui->dsMaxSize->value();
    const double growth = ui->dsGrowthRate->value();
    const QString boundaryLayer = ui->chkBoundaryLayer->isChecked() ? QStringLiteral("启用") : QStringLiteral("禁用");

    if (minSize > maxSize) {
        QMessageBox::warning(this, QStringLiteral("面控制"), QStringLiteral("最小尺寸不能大于最大尺寸。"));
        return;
    }

    int row = findRowByFaceId(faceId);
    if (row < 0) {
        row = ui->tableRules->rowCount();
        ui->tableRules->insertRow(row);
    }

    ui->tableRules->setItem(row, 0, new QTableWidgetItem(QString::number(faceId)));
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(growth, 'f', 3)));
    ui->tableRules->setItem(row, 4, new QTableWidgetItem(boundaryLayer));

    updateSummary();
}

void MeshFaceControlWidget::onRemoveSelectedRule()
{
    const int row = ui->tableRules->currentRow();
    if (row < 0) {
        return;
    }
    ui->tableRules->removeRow(row);
    updateSummary();
}

void MeshFaceControlWidget::onClearRules()
{
    ui->tableRules->setRowCount(0);
    updateSummary();
}

void MeshFaceControlWidget::onResetDefaults()
{
    ui->spFaceId->setValue(1);
    ui->dsMinSize->setValue(0.8);
    ui->dsMaxSize->setValue(3.0);
    ui->dsGrowthRate->setValue(1.2);
    ui->chkBoundaryLayer->setChecked(false);
}

int MeshFaceControlWidget::findRowByFaceId(int faceId) const
{
    for (int row = 0; row < ui->tableRules->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableRules->item(row, 0);
        if (item && item->text().toInt() == faceId) {
            return row;
        }
    }
    return -1;
}

void MeshFaceControlWidget::updateSummary()
{
    ui->labelSummary->setText(QStringLiteral("当前面控制规则数：%1").arg(ui->tableRules->rowCount()));
}
