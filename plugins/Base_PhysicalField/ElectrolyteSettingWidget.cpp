#include "ElectrolyteSettingWidget.h"
#include "ui_ElectrolyteSettingWidget.h"

#include <QListWidgetItem>
#include <ServiceRegistry.h>

#include <services/PreProcessingService.h>

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

    BoundaryConditions &bc = BoundaryConditions::instance();

    // 1. 加载所有体
    auto volumeGroups = bc.getGroupsForDim(3);
    for (const auto &g : volumeGroups)
    {
        ui->volumeComboBox->addItem(
            QString("%1 (id=%2)")
                .arg(QString::fromStdString(g.name))
                .arg(g.physId),
            g.physId
            );
    }

    // 2. 查询本 page 的已选 physId
    auto physIds =
        bc.getPhysIdsForBc(
            m_uuid.toStdString(),
            SolverType::PrimaryCurrent,
            3);

    // 3. 更新 listWidget
    for (int pid : physIds)
    {
        QString name;
        for (const auto &g : volumeGroups)
            if (g.physId == pid) name = QString::fromStdString(g.name);

        QListWidgetItem *item =
            new QListWidgetItem(name + QString(" (id=%1)").arg(pid));
        item->setData(Qt::UserRole, pid);
        ui->listWidget->addItem(item);
    }

    // 4. 由 BoundaryConditions 统一高亮颜色
    bc.highlightForPage(3, physIds);

    // 5. 预选/预删清理
    // bc.previewSelection(BCSelectMode::Volume, -1);
    // bc.previewRemoveSelection(BCSelectMode::Volume, -1);

    refreshRender();
}



void ElectrolyteSettingWidget::beforeUnload()
{
    BoundaryConditions &bc = BoundaryConditions::instance();

    const std::string bcId = m_uuid.toStdString();
    const SolverType solver = SolverType::PrimaryCurrent;
    const int dim = 3;

    bc.clearSolverParamsForPage(bcId, solver, dim);

    std::vector<BCSolverRecord> records;

    // 从界面拿电导率（假设只有一个电导率，对当前页面所有体共用）
    double sigma = ui->conductivityLineEdit->text().toDouble();

    std::map<std::string,double> scalars;
    scalars["sigma"] = sigma;   // 约定 key = "sigma"

    // 对 listWidget 中的每个体构建一条 solver 记录
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *item = ui->listWidget->item(i);
        int physId = item->data(Qt::UserRole).toInt();
        if (physId <= 0)
            continue;

        BCSolverRecord rec;
        rec.dim    = 3;                         // 体
        rec.physId = physId;
        rec.solver = SolverType::PrimaryCurrent;
        rec.bcId   = m_uuid.toStdString();             // 如果你给 Electrolyte 也做分组的话，这里改成 m_bcId
        rec.scalars = scalars;

        records.push_back(std::move(rec));
    }

    // 批量写入（merge = true：合并，不清空其它页面的配置）
    BoundaryConditions::instance().setSolverParamsBulk(records, /*merge=*/true);

    // 离开页面时，只清预选/预删，不动“已选”
    // BoundaryConditions::instance().previewSelection(BCSelectMode::Volume, -1);
    // BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Volume, -1);
    BoundaryConditions::instance().clearHighlight();
    refreshRender();
}
void ElectrolyteSettingWidget::refreshColorsFromBC()
{
    BoundaryConditions &bc = BoundaryConditions::instance();

    // 取当前 page 对应的 physId 集合
    auto physIds =
        bc.getPhysIdsForBc(
            m_uuid.toStdString(),
            SolverType::PrimaryCurrent,
            3);

    QSet<int> selected;
    for (int pid : physIds) selected.insert(pid);

    // 先把所有 item 重置为“未选中”样式
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        auto *item = ui->listWidget->item(i);
        item->setBackground(Qt::NoBrush);
        item->setForeground(Qt::black);
        item->setCheckState(Qt::Unchecked);
    }

    // 再根据 selected 里有的 physId，设置“选中”颜色
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        auto *item = ui->listWidget->item(i);
        int pid = item->data(Qt::UserRole).toInt();
        if (selected.contains(pid)) {
            item->setBackground(QColor("#3d6af2"));   // 你原来的高亮色
            item->setForeground(Qt::white);
            item->setCheckState(Qt::Checked);
        }
    }
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
    auto* w = ServiceRegistry::instance().getService<PreProcessingService>();
}
