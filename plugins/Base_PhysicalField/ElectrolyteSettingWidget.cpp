#include "ElectrolyteSettingWidget.h"
#include "ui_ElectrolyteSettingWidget.h"

#include <QListWidgetItem>

ElectrolyteSettingWidget::ElectrolyteSettingWidget(QWidget *parent)
    : BasePage("ElectrolyteSettingWidget")
    , ui(new Ui::ElectrolyteSettingWidget)
{
    ui->setupUi(this);

    // ===== ComboBox 预选：CustomComboBox 已经在 .ui 中使用 =====
    // ui->volumeComboBox 本身就是 CustomComboBox*
    connect(ui->volumeComboBox, &CustomComboBox::itemHovered,
            this, &ElectrolyteSettingWidget::onVolumeItemHovered);
    connect(ui->volumeComboBox, &CustomComboBox::dropdownClosed,
            this, &ElectrolyteSettingWidget::onVolumeDropdownClosed);

    // ===== listWidget 预删 =====
    ui->listWidget->setMouseTracking(true);
    ui->listWidget->viewport()->setMouseTracking(true);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->listWidget, &QListWidget::itemEntered,
            this, &ElectrolyteSettingWidget::onListItemEntered);
    connect(ui->listWidget, &QListWidget::itemSelectionChanged,
            this, &ElectrolyteSettingWidget::onListItemSelectionChanged);
}

ElectrolyteSettingWidget::~ElectrolyteSettingWidget()
{
    delete ui;
}

void ElectrolyteSettingWidget::beforeLoad()
{
    ui->volumeComboBox->clear();
    ui->listWidget->clear();

    auto volumeGroups = BoundaryConditions::instance().getGroupsForDim(3);
    QVector<PhysGroupInfo> tmp(volumeGroups.begin(), volumeGroups.end());
    qDebug() << "ElectrolyteSettingWidget volumes:" << tmp.size();

    for (int i = 0; i < tmp.size(); ++i)
    {
        const auto &g = tmp[i];
        QString text = QString::fromStdString(g.name)
                       + QString(" (id=%1)").arg(g.physId);
        // text 显示，UserRole 保存 physId
        ui->volumeComboBox->addItem(text, g.physId);
    }

    // 进入页面时：取消预选、预删
    BoundaryConditions::instance().previewSelection(BCSelectMode::Volume, -1);
    BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Volume, -1);

    refreshRender();
}

void ElectrolyteSettingWidget::beforeUnload()
{
    // 看你需求，一般不用特别处理
}

// ================== 添加 ==================

void ElectrolyteSettingWidget::on_addPushButton_clicked()
{
    if (ui->volumeComboBox->count() == 0)
        return;

    int physId = ui->volumeComboBox->currentData().toInt();
    if (physId <= 0)
        return;

    // 1) 通知 BoundaryConditions：确认选中这个体（变蓝）
    BoundaryConditions::instance().addSelection(BCSelectMode::Volume, physId);

    // 2) listWidget 里加一条，避免重复
    bool exists = false;
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *item = ui->listWidget->item(i);
        if (item->data(Qt::UserRole).toInt() == physId)
        {
            exists = true;
            break;
        }
    }

    if (!exists)
    {
        auto *item = new QListWidgetItem(ui->volumeComboBox->currentText());
        item->setData(Qt::UserRole, physId);
        ui->listWidget->addItem(item);
    }

    refreshRender();
}

// ================== 删除 ==================

void ElectrolyteSettingWidget::on_removePushButton_clicked()
{
    auto *item = ui->listWidget->currentItem();
    if (!item)
        return;

    int physId = item->data(Qt::UserRole).toInt();
    if (physId <= 0)
        return;

    // 1) 通知 BoundaryConditions：真正删除（从已选中移除，回灰）
    BoundaryConditions::instance().removeSelection(BCSelectMode::Volume, physId);

    // 2) 从列表移除
    int row = ui->listWidget->row(item);
    ui->listWidget->takeItem(row);
    delete item;

    // 3) 取消预删状态
    BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Volume, -1);

    refreshRender();
}

// ================== ComboBox 预选 ==================

void ElectrolyteSettingWidget::onVolumeItemHovered(int row)
{
    if (row < 0 || row >= ui->volumeComboBox->count())
        return;

    int physId = ui->volumeComboBox->itemData(row).toInt();
    if (physId <= 0)
        return;

    // 预选体：黄色
    BoundaryConditions::instance().previewSelection(BCSelectMode::Volume, physId);
    refreshRender();
}

void ElectrolyteSettingWidget::onVolumeDropdownClosed()
{
    // 下拉框收起时取消预选
    BoundaryConditions::instance().previewSelection(BCSelectMode::Volume, -1);
    refreshRender();
}

// ================== listWidget 预删 ==================

void ElectrolyteSettingWidget::onListItemEntered(QListWidgetItem *item)
{
    //先不做
}

void ElectrolyteSettingWidget::onListItemSelectionChanged()
{
    // 这里简单处理：只要选中变化，就取消预删
    BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Volume, -1);
    refreshRender();
}

// ================== 刷新渲染 ==================

void ElectrolyteSettingWidget::refreshRender()
{
    // 这里按你的架构来写，例如：
    // auto* w = ServiceRegistry::instance().getPreProcessingWidget();
    // if (w) w->render();
}
