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

    // 默认 UI 状态初始化（避免刚打开界面时控件状态混乱）
    if (ui->modelComboBox->count() > 0 && ui->modelComboBox->currentIndex() < 0)
        ui->modelComboBox->setCurrentIndex(0);

    updateModelUI();
    updateTafelSubUI();
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

    // 1) 所有面物理组
    auto faceGroups = bc.getGroupsForDim(2);  // dim=2 -> face
    for (const auto &g : faceGroups)
    {
        QString text = QString::fromStdString(g.name)
        + QString(" (id=%1)").arg(g.physId);
        ui->comboBox->addItem(text, g.physId);
    }

    // 2) 先查二次，再查一次，优先使用二次
    const std::string bcId = m_uuid.toStdString();
    auto physIdsSecondary = bc.getPhysIdsForBc(
        bcId,
        SolverType::SecondaryCurrent,
        /*dim=*/2
        );
    auto physIdsPrimary = bc.getPhysIdsForBc(
        bcId,
        SolverType::PrimaryCurrent,
        /*dim=*/2
        );

    std::vector<int> physIds;
    bool hasSecondary = !physIdsSecondary.empty();
    bool hasPrimary   = !physIdsPrimary.empty();

    if (hasSecondary)
        physIds = physIdsSecondary;
    else
        physIds = physIdsPrimary;

    // 3) 加入 listWidget
    for (int pid : physIds)
    {
        QString name;
        for (const auto &g : faceGroups)
            if (g.physId == pid)
                name = QString::fromStdString(g.name);

        QListWidgetItem *item =
            new QListWidgetItem(name + QString(" (id=%1)").arg(pid));
        item->setData(Qt::UserRole, pid);
        ui->listWidget->addItem(item);
    }

    // 4) 高亮
    bc.highlightForPage(/*dim=*/2, physIds);

    // 5) 先清空 UI
    ui->phiLineEdit->clear();
    ui->EeqLineEdit->clear();
    ui->secondaryCheckBox->setChecked(false);
    if (ui->modelComboBox->count() > 0)
        ui->modelComboBox->setCurrentIndex(0);

    ui->useAnodicTafelCheckBox->setChecked(false);
    ui->useCathodicTafelCheckBox->setChecked(false);
    ui->betaALineEdit->clear();
    ui->i0ALineEdit->clear();
    ui->betaCLineEdit->clear();
    ui->i0CLineEdit->clear();
    ui->i0BVLineEdit->clear();
    ui->alphaALineEdit->clear();
    ui->alphaCLineEdit->clear();

    // 6) 根据已有的 solverParam 回填
    if (!physIds.empty())
    {
        int pid0 = physIds[0];

        if (hasSecondary)
        {
            // —— 有二次电流参数：勾选“二次”，从 SecondaryCurrent 读 ——
            BCSolverParam param;
            if (bc.getSolverParam(2, pid0,
                                  SolverType::SecondaryCurrent,
                                  param))
            {
                auto itPhi = param.scalars.find("phi");
                if (itPhi != param.scalars.end())
                    ui->phiLineEdit->setText(QString::number(itPhi->second));

                loadSecondaryParams(param);    // 里面会设置 secondaryCheckBox / model / tafel / BV 等
            }
        }
        else if (hasPrimary)
        {
            // —— 没有二次，有一次：当成“一次模式”，只读 PrimaryCurrent 里的 phi ——
            BCSolverParam param;
            if (bc.getSolverParam(2, pid0,
                                  SolverType::PrimaryCurrent,
                                  param))
            {
                auto itPhi = param.scalars.find("phi");
                if (itPhi != param.scalars.end())
                    ui->phiLineEdit->setText(QString::number(itPhi->second));
            }
            ui->secondaryCheckBox->setChecked(false);
        }
        else
        {
            // 完全没设置过，保持默认空
            ui->secondaryCheckBox->setChecked(false);
        }
    }
    else
    {
        // 没选任何面
        ui->secondaryCheckBox->setChecked(false);
    }

    updateModelUI();
    updateTafelSubUI();

    refreshRender();
}


void ElectrodeSurfaceSettingWidget::beforeUnload()
{
    BoundaryConditions &bc = BoundaryConditions::instance();

    const std::string bcId = m_uuid.toStdString();
    const int dim = 2; // 面

    // 先把本页面的一次 / 二次都清掉，避免残留
    bc.clearSolverParamsForPage(bcId, SolverType::PrimaryCurrent,   dim);
    bc.clearSolverParamsForPage(bcId, SolverType::SecondaryCurrent, dim);

    std::vector<BCSolverRecord> records;

    // 收集当前列表中所有 physId
    std::vector<int> physIds;
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        auto *item = ui->listWidget->item(i);
        int pid = item->data(Qt::UserRole).toInt();
        if (pid > 0)
            physIds.push_back(pid);
    }

    if (physIds.empty())
    {
        // 没有任何选面，直接清状态退出
        bc.previewSelection(BCSelectMode::Face, -1);
        bc.previewRemoveSelection(BCSelectMode::Face, -1);
        bc.clearHighlight();
        refreshRender();
        return;
    }

    const bool secondaryEnabled = ui->secondaryCheckBox->isChecked();

    if (!secondaryEnabled)
    {
        // ===== 情况 A：未勾选二次电流 ——> 写到一次电流分布（Dirichlet phi） =====
        std::map<std::string,double> scalars;
        scalars["phi"] = ui->phiLineEdit->text().toDouble();

        for (int physId : physIds)
        {
            BCSolverRecord rec;
            rec.dim     = dim;
            rec.physId  = physId;
            rec.solver  = SolverType::PrimaryCurrent; // 一次
            rec.bcId    = bcId;
            rec.scalars = scalars;

            records.push_back(std::move(rec));
        }
    }
    else
    {
        // ===== 情况 B：勾选了二次电流 ——> 写到 SecondaryCurrent，下次进来按二次模式读 =====
        std::map<std::string,double> scalars;
        scalars["phi"] = ui->phiLineEdit->text().toDouble();
        collectSecondaryScalars(scalars); // 这里面会写 secondary_enabled/model_type/Eeq/tafel/BV 等

        for (int physId : physIds)
        {
            BCSolverRecord rec;
            rec.dim     = dim;
            rec.physId  = physId;
            rec.solver  = SolverType::SecondaryCurrent; // 二次
            rec.bcId    = bcId;
            rec.scalars = scalars;

            records.push_back(std::move(rec));
        }
    }

    bc.setSolverParamsBulk(records, /*merge=*/true);

    bc.previewSelection(BCSelectMode::Face, -1);
    bc.previewRemoveSelection(BCSelectMode::Face, -1);
    bc.clearHighlight();
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
    Q_UNUSED(item);
    // 如需要 hover 删除预览，可在此开启
    // int physId = item->data(Qt::UserRole).toInt();
    // if (physId <= 0) return;
    // BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Face, physId);
    // refreshRender();
}

void ElectrodeSurfaceSettingWidget::onListItemSelectionChanged()
{
    // 简化：只要列表选择发生改变，就取消预删
    BoundaryConditions::instance().previewRemoveSelection(BCSelectMode::Face, -1);
    refreshRender();
}

// ================== 二次电流分布 开关 & 模型切换 ==================

void ElectrodeSurfaceSettingWidget::on_secondaryCheckBox_toggled(bool /*checked*/)
{
    updateModelUI();
    updateTafelSubUI();
}

void ElectrodeSurfaceSettingWidget::on_modelComboBox_currentIndexChanged(int /*index*/)
{
    updateModelUI();
    updateTafelSubUI();
}

// ================== Tafel 子支路启用/禁用 ==================

void ElectrodeSurfaceSettingWidget::on_useAnodicTafelCheckBox_toggled(bool /*checked*/)
{
    updateTafelSubUI();
}

void ElectrodeSurfaceSettingWidget::on_useCathodicTafelCheckBox_toggled(bool /*checked*/)
{
    updateTafelSubUI();
}

// ================== 刷新渲染 ==================

void ElectrodeSurfaceSettingWidget::refreshRender()
{
    PreProcessingService* w = ServiceRegistry::instance().getService<PreProcessingService>();
    if (w)
        w->render();
}

// ================== UI 状态更新辅助函数 ==================

void ElectrodeSurfaceSettingWidget::updateModelUI()
{
    const bool secondaryEnabled = ui->secondaryCheckBox->isChecked();
    ui->modelComboBox->setEnabled(secondaryEnabled);
    ui->EeqLineEdit->setEnabled(secondaryEnabled);

    int modelIndex = ui->modelComboBox->currentIndex();
    // 约定：0 = Tafel, 1 = BV
    const bool isTafel = (modelIndex == 0);
    const bool isBV    = (modelIndex == 1);

    ui->tafelGroupBox->setVisible(secondaryEnabled && isTafel);
    ui->bvGroupBox->setVisible(secondaryEnabled && isBV);

    // BV 分组内部控件 enable
    ui->i0BVLineEdit->setEnabled(secondaryEnabled && isBV);
    ui->alphaALineEdit->setEnabled(secondaryEnabled && isBV);
    ui->alphaCLineEdit->setEnabled(secondaryEnabled && isBV);
}

void ElectrodeSurfaceSettingWidget::updateTafelSubUI()
{
    const bool secondaryEnabled = ui->secondaryCheckBox->isChecked();
    int modelIndex = ui->modelComboBox->currentIndex();
    const bool isTafel = (modelIndex == 0);

    const bool anodicOn   = secondaryEnabled && isTafel && ui->useAnodicTafelCheckBox->isChecked();
    const bool cathodicOn = secondaryEnabled && isTafel && ui->useCathodicTafelCheckBox->isChecked();

    ui->betaALineEdit->setEnabled(anodicOn);
    ui->i0ALineEdit->setEnabled(anodicOn);

    ui->betaCLineEdit->setEnabled(cathodicOn);
    ui->i0CLineEdit->setEnabled(cathodicOn);
}

// ================== 从 BCSolverParam 回填二次电流 UI ==================

void ElectrodeSurfaceSettingWidget::loadSecondaryParams(const BCSolverParam &param)
{
    auto &s = param.scalars;

    // secondary_enabled
    bool secondaryEnabled = false;
    auto itSec = s.find("secondary_enabled");
    if (itSec != s.end())
        secondaryEnabled = (itSec->second != 0.0);
    ui->secondaryCheckBox->setChecked(secondaryEnabled);

    // model_type: 1 = Tafel, 2 = BV
    int modelType = 1;
    auto itModel = s.find("model_type");
    if (itModel != s.end())
        modelType = static_cast<int>(itModel->second);

    if (ui->modelComboBox->count() > 0) {
        if (modelType == 2 && ui->modelComboBox->count() > 1)
            ui->modelComboBox->setCurrentIndex(1); // BV
        else
            ui->modelComboBox->setCurrentIndex(0); // Tafel
    }

    // Eeq
    auto itEeq = s.find("Eeq");
    if (itEeq != s.end())
        ui->EeqLineEdit->setText(QString::number(itEeq->second));

    // ----- Tafel -----
    // 阳极
    auto itUseAn = s.find("use_anodic");
    bool useAnodic = (itUseAn != s.end() && itUseAn->second != 0.0);
    ui->useAnodicTafelCheckBox->setChecked(useAnodic);

    auto itBa = s.find("beta_a");
    if (itBa != s.end())
        ui->betaALineEdit->setText(QString::number(itBa->second));
    auto itI0a = s.find("i0_a");
    if (itI0a != s.end())
        ui->i0ALineEdit->setText(QString::number(itI0a->second));

    // 阴极
    auto itUseCa = s.find("use_cathodic");
    bool useCathodic = (itUseCa != s.end() && itUseCa->second != 0.0);
    ui->useCathodicTafelCheckBox->setChecked(useCathodic);

    auto itBc = s.find("beta_c");
    if (itBc != s.end())
        ui->betaCLineEdit->setText(QString::number(itBc->second));
    auto itI0c = s.find("i0_c");
    if (itI0c != s.end())
        ui->i0CLineEdit->setText(QString::number(itI0c->second));

    // ----- BV -----
    auto itI0 = s.find("i0");
    if (itI0 != s.end())
        ui->i0BVLineEdit->setText(QString::number(itI0->second));

    auto itAlphaA = s.find("alpha_a");
    if (itAlphaA != s.end())
        ui->alphaALineEdit->setText(QString::number(itAlphaA->second));

    auto itAlphaC = s.find("alpha_c");
    if (itAlphaC != s.end())
        ui->alphaCLineEdit->setText(QString::number(itAlphaC->second));
}

// ================== 从 UI 收集二次电流参数写入 scalars ==================

void ElectrodeSurfaceSettingWidget::collectSecondaryScalars(
    std::map<std::string, double> &scalars) const
{
    const bool secondaryEnabled = ui->secondaryCheckBox->isChecked();
    scalars["secondary_enabled"] = secondaryEnabled ? 1.0 : 0.0;

    if (!secondaryEnabled)
        return;

    // Eeq
    scalars["Eeq"] = ui->EeqLineEdit->text().toDouble();

    int modelIndex = ui->modelComboBox->currentIndex();
    // 约定：0 = Tafel, 1 = BV
    if (modelIndex == 0)
    {
        // Tafel
        scalars["model_type"] = 1.0;

        // 阳极
        bool useAnodic = ui->useAnodicTafelCheckBox->isChecked();
        scalars["use_anodic"] = useAnodic ? 1.0 : 0.0;
        if (useAnodic)
        {
            scalars["beta_a"] = ui->betaALineEdit->text().toDouble();
            scalars["i0_a"]   = ui->i0ALineEdit->text().toDouble();
        }

        // 阴极
        bool useCathodic = ui->useCathodicTafelCheckBox->isChecked();
        scalars["use_cathodic"] = useCathodic ? 1.0 : 0.0;
        if (useCathodic)
        {
            scalars["beta_c"] = ui->betaCLineEdit->text().toDouble();
            scalars["i0_c"]   = ui->i0CLineEdit->text().toDouble();
        }
    }
    else
    {
        // BV
        scalars["model_type"] = 2.0;

        scalars["i0"]      = ui->i0BVLineEdit->text().toDouble();
        scalars["alpha_a"] = ui->alphaALineEdit->text().toDouble();
        scalars["alpha_c"] = ui->alphaCLineEdit->text().toDouble();
        // 如果以后加温度，可再加 scalars["T"]
    }
}
