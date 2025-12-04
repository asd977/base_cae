#include "ElectrodeSurfaceSettingWidget.h"
#include "ui_ElectrodeSurfaceSettingWidget.h"

#include <ServiceRegistry.h>

#include <services/PreProcessingService.h>

ElectrodeSurfaceSettingWidget::ElectrodeSurfaceSettingWidget(QWidget *parent)
    : BasePage("ElectrodeSurfaceSettingWidget")
    , ui(new Ui::ElectrodeSurfaceSettingWidget)
{
    ui->setupUi(this);

    // ===== comboBox 预选（面 dim=2，BCSelectMode::Face）=====
    // 在 .ui 中已经把 comboBox 类型改成 CustomComboBox 了
    connect(ui->comboBox, &CustomComboBox::itemHovered,
            this, &ElectrodeSurfaceSettingWidget::onComboItemHovered);
    connect(ui->comboBox, &CustomComboBox::dropdownClosed,
            this, &ElectrodeSurfaceSettingWidget::onComboDropdownClosed);

    // ===== listWidget 预删 =====
    ui->listWidget->setMouseTracking(true);
    ui->listWidget->viewport()->setMouseTracking(true);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->listWidget, &QListWidget::itemEntered,
            this, &ElectrodeSurfaceSettingWidget::onListItemEntered);
    connect(ui->listWidget, &QListWidget::itemSelectionChanged,
            this, &ElectrodeSurfaceSettingWidget::onListItemSelectionChanged);
}

ElectrodeSurfaceSettingWidget::~ElectrodeSurfaceSettingWidget()
{
    delete ui;
}

// ================== BasePage 生命周期 ==================

void ElectrodeSurfaceSettingWidget::beforeLoad()
{
    ui->comboBox->clear();
    ui->listWidget->clear();

    BoundaryConditions &bc = BoundaryConditions::instance();

    // ==========================================
    // 1) 填充所有面物理组（可选范围）
    // ==========================================
    auto faceGroups = bc.getGroupsForDim(2);  // dim=2 -> face
    for (const auto &g : faceGroups)
    {
        QString text = QString::fromStdString(g.name)
        + QString(" (id=%1)").arg(g.physId);
        ui->comboBox->addItem(text, g.physId);
    }

    // ==========================================
    // 2) 查询本页面已选 physId
    // ==========================================
    auto physIds = bc.getPhysIdsForBc(
        m_uuid.toStdString(),           // 本页面唯一 ID
        SolverType::PrimaryCurrent,   // 电极表面：二次电流
        /*dim=*/2
        );

    // ==========================================
    // 3) 把已选 physId 加入 listWidget
    // ==========================================
    for (int pid : physIds)
    {
        // 查找 name
        QString name;
        for (const auto &g : faceGroups)
            if (g.physId == pid)
                name = QString::fromStdString(g.name);

        QListWidgetItem *item =
            new QListWidgetItem(name + QString(" (id=%1)").arg(pid));
        item->setData(Qt::UserRole, pid);
        ui->listWidget->addItem(item);
    }

    // ==========================================
    // 4) 用 BoundaryConditions 统一高亮本页面已选 physId
    // ==========================================
    bc.highlightForPage(/*dim=*/2, physIds);

    // ==========================================
    // 5) 回填参数（phi），取第一个 physId 代表值
    // ==========================================
    if (!physIds.empty())
    {
        BCSolverParam param;
        if (bc.getSolverParam(2, physIds[0],
                              SolverType::PrimaryCurrent,
                              param))
        {
            auto it = param.scalars.find("phi");
            if (it != param.scalars.end())
                ui->phiLineEdit->setText(QString::number(it->second));
        }
    }
    else
    {
        ui->phiLineEdit->clear();
    }

    // ==========================================
    // 6) 清除预选/预删状态（非常重要）
    // ==========================================
    // bc.previewSelection(BCSelectMode::Face, -1);
    // bc.previewRemoveSelection(BCSelectMode::Face, -1);

    refreshRender();
}

void ElectrodeSurfaceSettingWidget::beforeUnload()
{
    BoundaryConditions &bc = BoundaryConditions::instance();

    const std::string bcId = m_uuid.toStdString();
    const SolverType solver = SolverType::PrimaryCurrent;
    const int dim = 3;

    bc.clearSolverParamsForPage(bcId, solver, dim);

    std::vector<BCSolverRecord> records;

    // 假设本页面所有面共用同一组参数（示例）
    // double sigma = ui->sigmaLineEdit->text().toDouble();
    // double phi   = ui->phiLineEdit->text().toDouble();

    std::map<std::string,double> scalars;
    // scalars["sigma"] = 1;
    scalars["phi"]   = ui->phiLineEdit->text().toDouble();

    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *item = ui->listWidget->item(i);
        int physId = item->data(Qt::UserRole).toInt();
        if (physId <= 0)
            continue;

        BCSolverRecord rec;
        rec.dim    = 2;
        rec.physId = physId;
        rec.solver = SolverType::PrimaryCurrent;
        rec.bcId   = m_uuid.toStdString();  // <== 归属于本页面的组
        rec.scalars = scalars;

        records.push_back(std::move(rec));
    }

    BoundaryConditions::instance().setSolverParamsBulk(records, /*merge=*/true);

    BoundaryConditions::instance().previewSelection(BCSelectMode::Face, -1);
    BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Face, -1);
    BoundaryConditions::instance().clearHighlight();
    refreshRender();
}


// ================== “添加”按钮：确认选择面 ==================

void ElectrodeSurfaceSettingWidget::on_addPushButton_clicked()
{
    int physId = ui->comboBox->currentData().toInt();
    if (physId <= 0)
        return;

    // 1) 避免重复添加到 listWidget
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *it = ui->listWidget->item(i);
        if (it->data(Qt::UserRole).toInt() == physId)
            return; // 已经存在了
    }

    // 2) 加入已选集合（影响 exportForSolver）
    BoundaryConditions::instance().addSelection(BCSelectMode::Face, physId);

    // 3) 在 listWidget 中增加一条
    QString text = ui->comboBox->currentText();
    auto *item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, physId);
    ui->listWidget->addItem(item);

    // 4) 刷颜色：本页面所有面（这里可以重新收集一下 list 里的 pid）
    std::vector<int> physIds;
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *it = ui->listWidget->item(i);
        physIds.push_back(it->data(Qt::UserRole).toInt());
    }
    BoundaryConditions::instance().highlightForPage(2, physIds);

    refreshRender();
}

// ================== “删除”按钮：确认删除面 ==================

void ElectrodeSurfaceSettingWidget::on_removePushButton_clicked()
{
    auto *item = ui->listWidget->currentItem();
    if (!item)
        return;

    int physId = item->data(Qt::UserRole).toInt();

    // 从全局选中集合移除
    BoundaryConditions::instance().removeSelection(BCSelectMode::Face, physId);

    // 删 UI
    delete item;

    // 重算本页面剩下的高亮
    std::vector<int> physIds;
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *it = ui->listWidget->item(i);
        physIds.push_back(it->data(Qt::UserRole).toInt());
    }
    BoundaryConditions::instance().highlightForPage(2, physIds);

    refreshRender();
}


// ================== comboBox 预选（hover：黄） ==================

void ElectrodeSurfaceSettingWidget::onComboItemHovered(int row)
{
    if (row < 0 || row >= ui->comboBox->count())
        return;

    int physId = ui->comboBox->itemData(row).toInt();
    if (physId <= 0)
        return;

    // 预选面：黄色高亮
    BoundaryConditions::instance().previewSelection(BCSelectMode::Face, physId);
    refreshRender();
}

void ElectrodeSurfaceSettingWidget::onComboDropdownClosed()
{
    // 收起下拉框，取消预选
    BoundaryConditions::instance().previewSelection(BCSelectMode::Face, -1);
    refreshRender();
}

// ================== listWidget 预删（hover：红） ==================

void ElectrodeSurfaceSettingWidget::onListItemEntered(QListWidgetItem *item)
{
    // if (!item)
    //     return;

    // int physId = item->data(Qt::UserRole).toInt();
    // if (physId <= 0)
    //     return;

    // // 预删面：红色高亮
    // BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Face, physId);
    // refreshRender();
}

void ElectrodeSurfaceSettingWidget::onListItemSelectionChanged()
{
    // 简化：只要列表选择发生改变，就取消预删
    BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Face, -1);
    refreshRender();
}

// ================== 刷新渲染 ==================

void ElectrodeSurfaceSettingWidget::refreshRender()
{
    PreProcessingService* w = ServiceRegistry::instance().getService<PreProcessingService>();
    w->render();
}
