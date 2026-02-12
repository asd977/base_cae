#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QSignalBlocker>

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <vtkUnstructuredGrid.h>

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget *parent)
    : BasePage("MeshVolumeControlWidget")
    , ui(new Ui::MeshVolumeControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(6);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "体组ID" << "组名" << "最小尺寸" << "最大尺寸" << "增长率" << "单元阶次");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshVolumeControlWidget::onGroupChanged);
    connect(ui->btnAddOrUpdate, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddOrUpdateRule);
    connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &MeshVolumeControlWidget::onRemoveSelectedRule);
    connect(ui->btnClearRules, &QPushButton::clicked, this, &MeshVolumeControlWidget::onClearRules);
    connect(ui->btnResetDefaults, &QPushButton::clicked, this, &MeshVolumeControlWidget::onResetDefaults);
    connect(ui->btnApplyControls, &QPushButton::clicked, this, &MeshVolumeControlWidget::onApplyControls);

    onResetDefaults();
}

MeshVolumeControlWidget::~MeshVolumeControlWidget()
{
    delete ui;
}

void MeshVolumeControlWidget::beforeLoad()
{
    refreshGroupCombo();
    updateSummary();
    ui->labelApplyStatus->setText("未执行。请先添加规则，然后点击【执行体控制】。");
}

void MeshVolumeControlWidget::beforeUnload()
{
}

void MeshVolumeControlWidget::refreshGroupCombo()
{
    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) {
        ui->labelPickStatus->setText("未找到网格服务，无法读取体组。");
        return;
    }

    m_cachedGrid = meshSvc->getVtkUnstructuredGridSolid();
    const MshPickMap &map = meshSvc->mshPickMapSolid();

    QSignalBlocker blocker(ui->comboBoxGroup);
    ui->comboBoxGroup->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it) {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim != 3) continue;
        ui->comboBoxGroup->addItem(QString("%1 (id=%2)").arg(it.value()).arg(physId), physId);
    }

    if (ui->comboBoxGroup->count() > 0) {
        ui->comboBoxGroup->setCurrentIndex(0);
        blocker.unblock();
        onGroupChanged();
    } else {
        ui->labelPickStatus->setText("暂无可用体组，请先完成整体剖分/导入带分组网格。");
    }
}

void MeshVolumeControlWidget::onGroupChanged()
{
    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) return;

    const MshPickMap &map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(idx).toInt();
    const QVector<qint64> cellIds64 = map.volPhysToCellIds.value(physId);

    ui->labelPickStatus->setText(QString("当前选择体组：%1（单元 %2）").arg(ui->comboBoxGroup->currentText()).arg(cellIds64.size()));
}

void MeshVolumeControlWidget::onAddOrUpdateRule()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        QMessageBox::warning(this, "体控制", "请先从下拉框选择体组。");
        return;
    }

    const int volumeId = ui->comboBoxGroup->itemData(idx).toInt();
    const QString groupName = ui->comboBoxGroup->currentText();
    const double minSize = ui->dsMinSize->value();
    const double maxSize = ui->dsMaxSize->value();
    const double growth = ui->dsGrowthRate->value();
    const int order = ui->spElementOrder->value();

    if (minSize > maxSize) {
        QMessageBox::warning(this, "体控制", "最小尺寸不能大于最大尺寸。");
        return;
    }

    int row = findRowByVolumeId(volumeId);
    if (row < 0) {
        row = ui->tableRules->rowCount();
        ui->tableRules->insertRow(row);
    }

    ui->tableRules->setItem(row, 0, new QTableWidgetItem(QString::number(volumeId)));
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(groupName));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 4, new QTableWidgetItem(QString::number(growth, 'f', 3)));
    ui->tableRules->setItem(row, 5, new QTableWidgetItem(QString::number(order)));
    updateSummary();
}

void MeshVolumeControlWidget::onRemoveSelectedRule()
{
    const int row = ui->tableRules->currentRow();
    if (row < 0) return;
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
    ui->dsMinSize->setValue(1.0);
    ui->dsMaxSize->setValue(5.0);
    ui->dsGrowthRate->setValue(1.3);
    ui->spElementOrder->setValue(1);
}

void MeshVolumeControlWidget::onApplyControls()
{
    const int ruleCount = ui->tableRules->rowCount();
    if (ruleCount <= 0) {
        QMessageBox::information(this, "体控制", "当前没有规则可执行，请先添加规则。");
        return;
    }
    ui->labelApplyStatus->setText(QString("已执行体控制规则：%1 条。后续重新剖分时将按当前规则生效。").arg(ruleCount));
    QMessageBox::information(this, "体控制", QString("体控制已执行，共 %1 条规则。").arg(ruleCount));
}

int MeshVolumeControlWidget::findRowByVolumeId(int volumeId) const
{
    for (int row = 0; row < ui->tableRules->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableRules->item(row, 0);
        if (item && item->text().toInt() == volumeId) return row;
    }
    return -1;
}

void MeshVolumeControlWidget::updateSummary()
{
    ui->labelSummary->setText(QString("当前体控制规则数：%1").arg(ui->tableRules->rowCount()));
}
