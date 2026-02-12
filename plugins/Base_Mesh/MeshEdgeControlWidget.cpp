#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QSignalBlocker>

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>
#include <PickManager.h>

MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget *parent)
    : BasePage("MeshEdgeControlWidget")
    , ui(new Ui::MeshEdgeControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(5);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "线组ID" << "组名" << "最小尺寸" << "最大尺寸" << "偏置系数");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEdgeControlWidget::onDimChanged);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEdgeControlWidget::onGroupChanged);

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
    PickManager::instance();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Edge);

    {
        QSignalBlocker blocker(ui->comboBoxDim);
        ui->comboBoxDim->clear();
        ui->comboBoxDim->addItem(QStringLiteral("dim1 线"), 1);
        ui->comboBoxDim->setCurrentIndex(0);
    }
    refreshGroupCombo();
    updateSummary();
}

void MeshEdgeControlWidget::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Edge);
}

void MeshEdgeControlWidget::onDimChanged()
{
    refreshGroupCombo();
}

void MeshEdgeControlWidget::onGroupChanged()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        return;
    }

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) {
        return;
    }

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(idx).toInt();

    const QVector<qint64> edgeKeys64 = map.bndEdgePhysToEdgeKeys.value(physId);
    QVector<vtkIdType> edgeKeys;
    edgeKeys.reserve(edgeKeys64.size());
    for (qint64 key : edgeKeys64) {
        edgeKeys.push_back(static_cast<vtkIdType>(key));
    }

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, true);

    ui->labelPickStatus->setText(QStringLiteral("当前高亮线组：%1（边数 %2）")
                                 .arg(ui->comboBoxGroup->currentText())
                                 .arg(edgeKeys.size()));
}

void MeshEdgeControlWidget::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) {
        ui->labelPickStatus->setText(QStringLiteral("未找到网格服务，无法读取线组。"));
        return;
    }

    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    QSignalBlocker blocker(ui->comboBoxGroup);
    ui->comboBoxGroup->clear();

    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it) {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dimFilter >= 0 && dim != dimFilter) {
            continue;
        }
        if (dim != 1) {
            continue;
        }
        ui->comboBoxGroup->addItem(QString("%1 (id=%2)").arg(it.value()).arg(physId), physId);
    }

    if (ui->comboBoxGroup->count() > 0) {
        ui->comboBoxGroup->setCurrentIndex(0);
        blocker.unblock();
        onGroupChanged();
    } else {
        ui->labelPickStatus->setText(QStringLiteral("暂无可用线组，请先完成整体剖分/导入带分组网格。"));
    }
}

void MeshEdgeControlWidget::onAddOrUpdateRule()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        QMessageBox::warning(this, QStringLiteral("边控制"), QStringLiteral("请先从下拉框选择线组。"));
        return;
    }

    const int edgeId = ui->comboBoxGroup->itemData(idx).toInt();
    const QString groupName = ui->comboBoxGroup->currentText();
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
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(groupName));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 4, new QTableWidgetItem(QString::number(bias, 'f', 3)));

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
