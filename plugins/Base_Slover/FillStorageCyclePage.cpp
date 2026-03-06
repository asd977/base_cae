#include "FillStorageCyclePage.h"

#include "ui_FillStorageCyclePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

FillStorageCyclePage::FillStorageCyclePage(QWidget* parent)
    : BasePage("FillStorageCycle"), ui(new Ui::FillStorageCyclePage)
{
    ui->setupUi(this);

    ui->comboSeep->addItem("0 不记坝基渗透荷载", 0);
    ui->comboSeep->addItem("1 线性折减面力", 1);
    ui->comboSeep->addItem("2 CASEEP 节点水头压力", 2);

    ui->comboBody->addItem("0 不记坝体渗透体力", 0);
    ui->comboBody->addItem("1 按饱和/湿容重差", 1);
    ui->comboBody->addItem("2 CABODY 节点体力", 2);

    ui->comboFace->addItem("0 不记坝体水面压力", 0);
    ui->comboFace->addItem("1 记入坝体水面压力", 1);
    ui->comboFace->setCurrentIndex(1);

    ui->comboIfWet->addItem("0 不考虑", 0);
    ui->comboIfWet->addItem("1 考虑", 1);

    ui->comboIfMode->addItem("0 不拟静力", 0);
    ui->comboIfMode->addItem("1 拟静力", 1);
    ui->comboIfWave->addItem("0 不地震波动力", 0);
    ui->comboIfWave->addItem("1 地震波动力", 1);
    ui->comboIfWave->setCurrentIndex(1);

    ui->comboIxupdw->addItem("0: 轴由上游指向下游", 0);
    ui->comboIxupdw->addItem("1: 轴由下游指向上游", 1);
    ui->comboIfrock->addItem("0 不计算（已完成）", 0);
    ui->comboIfrock->addItem("1 进行岩体自重计算", 1);
    ui->comboIfisoMode->addItem("按建模材料计算(IFISO=0)", 0);
    ui->comboIfisoMode->addItem("按指定材料号计算(IFISO!=1)", 1);

    auto initTable = [](QTableWidget* t, const QStringList& headers)
    {
        t->setColumnCount(headers.size());
        t->setHorizontalHeaderLabels(headers);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        t->setAlternatingRowColors(true);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
    };

    initTable(ui->tableWetMap, {"M_dry", "M_wet"});
    initTable(ui->tableDrop, {"HW_DROP_STA", "HW_DROP_END"});
    initTable(ui->tableChange, {"M_old", "M_new"});
    initTable(ui->tableCycle,
              {"JPACK_STA", "JPACK_END", "TT_PACK", "HWJ_STA", "HWJ_END", "TT_FILL",
               "IF_PACK", "IF_PLIU", "IF_FILL", "IF_FLIU", "CAHEAD", "CABODY"});
    initTable(ui->tableDyna, {"M_STA", "M_DYNA"});

    ensureTableRows(ui->tableWetMap, ui->spinNumWet->value());
    ensureTableRows(ui->tableDrop, ui->spinNumDrop->value());
    ensureTableRows(ui->tableChange, ui->spinNchange->value());
    ensureTableRows(ui->tableCycle, ui->spinNcircle->value());
    ensureTableRows(ui->tableDyna, ui->spinNumDyna->value());

    ui->btnWetAdd->setToolTip("新增一条湿化映射");
    ui->btnWetRemove->setToolTip("删除选中湿化映射（未选中则删除最后一行）");
    ui->btnDropAdd->setToolTip("新增一条骤降记录");
    ui->btnDropRemove->setToolTip("删除选中骤降记录（未选中则删除最后一行）");
    ui->btnChangeAdd->setToolTip("新增一条材料变换");
    ui->btnChangeRemove->setToolTip("删除选中材料变换（未选中则删除最后一行）");
    ui->btnCycleAdd->setToolTip("新增一个填筑-蓄水周期行");
    ui->btnCycleRemove->setToolTip("删除选中周期行（未选中则删除最后一行）");
    ui->btnDynaAdd->setToolTip("新增一条静力-动力材料映射");
    ui->btnDynaRemove->setToolTip("删除选中静力-动力材料映射（未选中则删除最后一行）");

    ui->tableWetMap->setVisible(false);
    ui->tableDrop->setVisible(false);
    ui->tableChange->setVisible(false);
    ui->tableCycle->setVisible(false);
    ui->tableDyna->setVisible(false);

    ui->btnWetAdd->setVisible(false);
    ui->btnWetRemove->setVisible(false);
    ui->btnDropAdd->setVisible(false);
    ui->btnDropRemove->setVisible(false);
    ui->btnChangeAdd->setVisible(false);
    ui->btnChangeRemove->setVisible(false);
    ui->btnCycleAdd->setVisible(false);
    ui->btnCycleRemove->setVisible(false);
    ui->btnDynaAdd->setVisible(false);
    ui->btnDynaRemove->setVisible(false);

    auto* btnWetConfig = new QPushButton("设置映射...", this);
    auto* btnDropConfig = new QPushButton("设置骤降...", this);
    auto* btnChangeConfig = new QPushButton("设置变换...", this);
    auto* btnCycleConfig = new QPushButton("设置周期...", this);
    auto* btnDynaConfig = new QPushButton("设置材料映射...", this);

    ui->wetToolbar->addWidget(btnWetConfig);
    ui->dropToolbar->addWidget(btnDropConfig);
    ui->changeToolbar->addWidget(btnChangeConfig);
    ui->cycleToolbar->addWidget(btnCycleConfig);
    ui->dynaToolbar->addWidget(btnDynaConfig);

    connect(btnWetConfig, &QPushButton::clicked, this,
            [this]() { openTableEditor("湿化材料映射", ui->tableWetMap, ui->spinNumWet); });
    connect(btnDropConfig, &QPushButton::clicked, this,
            [this]() { openTableEditor("水位骤降", ui->tableDrop, ui->spinNumDrop); });
    connect(btnChangeConfig, &QPushButton::clicked, this,
            [this]() { openTableEditor("材料属性变换", ui->tableChange, ui->spinNchange); });
    connect(btnCycleConfig, &QPushButton::clicked, this,
            [this]() { openTableEditor("填筑-蓄水周期", ui->tableCycle, ui->spinNcircle); });
    connect(btnDynaConfig, &QPushButton::clicked, this,
            [this]() { openTableEditor("静力-动力材料映射", ui->tableDyna, ui->spinNumDyna); });

    auto refreshGlobalUi = [this]()
    {
        const bool panelDam = ui->checkPanelDam->isChecked();
        ui->spinXamid->setEnabled(!panelDam);
        ui->spinYamid->setEnabled(!panelDam);
        ui->spinXbmid->setEnabled(!panelDam);
        ui->spinYbmid->setEnabled(!panelDam);

        const bool specifiedIso = ui->comboIfisoMode->currentIndex() == 1;
        ui->spinIfisoValue->setEnabled(specifiedIso);
    };
    connect(ui->checkPanelDam, &QCheckBox::toggled, this, [refreshGlobalUi](bool) { refreshGlobalUi(); });
    connect(ui->comboIfisoMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [refreshGlobalUi](int) { refreshGlobalUi(); });
    refreshGlobalUi();

    setupConnections();
    updatePreview();
}

FillStorageCyclePage::~FillStorageCyclePage()
{
    delete ui;
}

void FillStorageCyclePage::beforeLoad() {}

void FillStorageCyclePage::beforeUnload() {}

QString FillStorageCyclePage::sectionText() const
{
    return buildSectionText();
}

QJsonObject FillStorageCyclePage::saveJson()
{
    QJsonObject obj;
    obj["ncase"] = ui->spinNcase->value();
    obj["icase"] = ui->spinIcase->value();
    obj["ifiiwl_seep"] = ui->comboSeep->currentData().toInt();
    obj["ifiiwl_body"] = ui->comboBody->currentData().toInt();
    obj["ifiiwl_face"] = ui->comboFace->currentData().toInt();
    obj["ifwet"] = ui->comboIfWet->currentData().toInt();
    obj["num_wet"] = ui->spinNumWet->value();
    obj["wet_map"] = tableToJson(ui->tableWetMap);
    obj["num_drop"] = ui->spinNumDrop->value();
    obj["drop_rows"] = tableToJson(ui->tableDrop);
    obj["nchange"] = ui->spinNchange->value();
    obj["change_rows"] = tableToJson(ui->tableChange);
    obj["ncircle"] = ui->spinNcircle->value();
    obj["cycle_rows"] = tableToJson(ui->tableCycle);
    obj["dirdyna"] = ui->editDirDyna->text();
    obj["num_dyna"] = ui->spinNumDyna->value();
    obj["dyna_rows"] = tableToJson(ui->tableDyna);
    obj["ifmode"] = ui->comboIfMode->currentData().toInt();
    obj["ifwave"] = ui->comboIfWave->currentData().toInt();
    obj["dt"] = ui->spinDt->value();
    obj["tsta"] = ui->spinTsta->value();
    obj["tend"] = ui->spinTend->value();
    obj["wave_ax"] = ui->editWaveAx->text();
    obj["wave_ay"] = ui->editWaveAy->text();
    obj["wave_az"] = ui->editWaveAz->text();

    obj["x1"] = ui->spinX1->value();
    obj["x2"] = ui->spinX2->value();
    obj["x3"] = ui->spinX3->value();
    obj["a1"] = ui->spinA1->value();
    obj["a2"] = ui->spinA2->value();
    obj["a3"] = ui->spinA3->value();
    obj["ixupdw"] = ui->comboIxupdw->currentData().toInt();
    obj["panel_dam"] = ui->checkPanelDam->isChecked();
    obj["xamid"] = ui->spinXamid->value();
    obj["yamid"] = ui->spinYamid->value();
    obj["xbmid"] = ui->spinXbmid->value();
    obj["ybmid"] = ui->spinYbmid->value();
    obj["ifrock"] = ui->comboIfrock->currentData().toInt();
    obj["ifiso_mode"] = ui->comboIfisoMode->currentIndex();
    obj["ifiso_value"] = ui->spinIfisoValue->value();
    obj["mcase_ids"] = ui->editCaseIds->text();
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    auto setComboByData = [](QComboBox* combo, int val)
    {
        int idx = combo->findData(val);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    };

    ui->spinNcase->setValue(obj.value("ncase").toInt(1));
    ui->spinIcase->setValue(obj.value("icase").toInt(1));
    setComboByData(ui->comboSeep, obj.value("ifiiwl_seep").toInt(0));
    setComboByData(ui->comboBody, obj.value("ifiiwl_body").toInt(0));
    setComboByData(ui->comboFace, obj.value("ifiiwl_face").toInt(1));
    setComboByData(ui->comboIfWet, obj.value("ifwet").toInt(0));

    ui->spinNumWet->setValue(obj.value("num_wet").toInt(0));
    ensureTableRows(ui->tableWetMap, ui->spinNumWet->value());
    jsonToTable(obj.value("wet_map").toArray(), ui->tableWetMap);

    ui->spinNumDrop->setValue(obj.value("num_drop").toInt(0));
    ensureTableRows(ui->tableDrop, ui->spinNumDrop->value());
    jsonToTable(obj.value("drop_rows").toArray(), ui->tableDrop);

    ui->spinNchange->setValue(obj.value("nchange").toInt(0));
    ensureTableRows(ui->tableChange, ui->spinNchange->value());
    jsonToTable(obj.value("change_rows").toArray(), ui->tableChange);

    ui->spinNcircle->setValue(obj.value("ncircle").toInt(1));
    ensureTableRows(ui->tableCycle, ui->spinNcircle->value());
    jsonToTable(obj.value("cycle_rows").toArray(), ui->tableCycle);

    ui->editDirDyna->setText(obj.value("dirdyna").toString("CASE01FILL01"));
    ui->spinNumDyna->setValue(obj.value("num_dyna").toInt(0));
    ensureTableRows(ui->tableDyna, ui->spinNumDyna->value());
    jsonToTable(obj.value("dyna_rows").toArray(), ui->tableDyna);

    setComboByData(ui->comboIfMode, obj.value("ifmode").toInt(0));
    setComboByData(ui->comboIfWave, obj.value("ifwave").toInt(1));
    ui->spinDt->setValue(obj.value("dt").toDouble(0.02));
    ui->spinTsta->setValue(obj.value("tsta").toDouble(0.0));
    ui->spinTend->setValue(obj.value("tend").toDouble(28.0));
    ui->editWaveAx->setText(obj.value("wave_ax").toString("WAVE-AX.DAT"));
    ui->editWaveAy->setText(obj.value("wave_ay").toString("WAVE-AY.DAT"));
    ui->editWaveAz->setText(obj.value("wave_az").toString("WAVE-AZ.DAT"));

    setComboByData(ui->comboIxupdw, obj.value("ixupdw").toInt(0));
    ui->spinX1->setValue(obj.value("x1").toDouble(0.0));
    ui->spinX2->setValue(obj.value("x2").toDouble(0.0));
    ui->spinX3->setValue(obj.value("x3").toDouble(0.0));
    ui->spinA1->setValue(obj.value("a1").toDouble(1.0));
    ui->spinA2->setValue(obj.value("a2").toDouble(1.0));
    ui->spinA3->setValue(obj.value("a3").toDouble(1.0));
    ui->checkPanelDam->setChecked(obj.value("panel_dam").toBool(false));
    ui->spinXamid->setValue(obj.value("xamid").toDouble(0.0));
    ui->spinYamid->setValue(obj.value("yamid").toDouble(0.0));
    ui->spinXbmid->setValue(obj.value("xbmid").toDouble(0.0));
    ui->spinYbmid->setValue(obj.value("ybmid").toDouble(0.0));
    setComboByData(ui->comboIfrock, obj.value("ifrock").toInt(1));
    ui->comboIfisoMode->setCurrentIndex(obj.value("ifiso_mode").toInt(0));
    ui->spinIfisoValue->setValue(obj.value("ifiso_value").toInt(0));
    ui->editCaseIds->setText(obj.value("mcase_ids").toString());

    updatePreview();
}

void FillStorageCyclePage::setupConnections()
{
    auto onValue = [this]() { updatePreview(); };

    connect(ui->spinNumWet, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, onValue](int v)
            {
                ensureTableRows(ui->tableWetMap, v);
                onValue();
            });
    connect(ui->spinNumDrop, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, onValue](int v)
            {
                ensureTableRows(ui->tableDrop, v);
                onValue();
            });
    connect(ui->spinNchange, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, onValue](int v)
            {
                ensureTableRows(ui->tableChange, v);
                onValue();
            });
    connect(ui->spinNcircle, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, onValue](int v)
            {
                ensureTableRows(ui->tableCycle, v);
                onValue();
            });
    connect(ui->spinNumDyna, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, onValue](int v)
            {
                ensureTableRows(ui->tableDyna, v);
                onValue();
            });

    connect(ui->btnWetAdd, &QPushButton::clicked, this,
            [this]() { addTableRow(ui->tableWetMap, ui->spinNumWet); });
    connect(ui->btnWetRemove, &QPushButton::clicked, this,
            [this]() { removeTableRow(ui->tableWetMap, ui->spinNumWet); });
    connect(ui->btnDropAdd, &QPushButton::clicked, this,
            [this]() { addTableRow(ui->tableDrop, ui->spinNumDrop); });
    connect(ui->btnDropRemove, &QPushButton::clicked, this,
            [this]() { removeTableRow(ui->tableDrop, ui->spinNumDrop); });
    connect(ui->btnChangeAdd, &QPushButton::clicked, this,
            [this]() { addTableRow(ui->tableChange, ui->spinNchange); });
    connect(ui->btnChangeRemove, &QPushButton::clicked, this,
            [this]() { removeTableRow(ui->tableChange, ui->spinNchange); });
    connect(ui->btnCycleAdd, &QPushButton::clicked, this,
            [this]() { addTableRow(ui->tableCycle, ui->spinNcircle); });
    connect(ui->btnCycleRemove, &QPushButton::clicked, this,
            [this]() { removeTableRow(ui->tableCycle, ui->spinNcircle); });
    connect(ui->btnDynaAdd, &QPushButton::clicked, this,
            [this]() { addTableRow(ui->tableDyna, ui->spinNumDyna); });
    connect(ui->btnDynaRemove, &QPushButton::clicked, this,
            [this]() { removeTableRow(ui->tableDyna, ui->spinNumDyna); });

    connect(ui->spinNcase, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->spinIcase, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboSeep, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboBody, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboFace, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboIfWet, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboIfMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboIfWave, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });

    connect(ui->spinDt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinTsta, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinTend, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });

    connect(ui->editDirDyna, &QLineEdit::textChanged, this,
            [onValue](const QString&) { onValue(); });
    connect(ui->editWaveAx, &QLineEdit::textChanged, this,
            [onValue](const QString&) { onValue(); });
    connect(ui->editWaveAy, &QLineEdit::textChanged, this,
            [onValue](const QString&) { onValue(); });
    connect(ui->editWaveAz, &QLineEdit::textChanged, this,
            [onValue](const QString&) { onValue(); });


    connect(ui->spinX1, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinX2, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinX3, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinA1, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinA2, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinA3, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->comboIxupdw, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->checkPanelDam, &QCheckBox::toggled, this, [onValue](bool) { onValue(); });
    connect(ui->spinXamid, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinYamid, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinXbmid, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->spinYbmid, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [onValue](double) { onValue(); });
    connect(ui->comboIfrock, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->comboIfisoMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->spinIfisoValue, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [onValue](int) { onValue(); });
    connect(ui->editCaseIds, &QLineEdit::textChanged, this,
            [onValue](const QString&) { onValue(); });
    connect(ui->tableWetMap, &QTableWidget::itemChanged, this, [onValue]() { onValue(); });
    connect(ui->tableDrop, &QTableWidget::itemChanged, this, [onValue]() { onValue(); });
    connect(ui->tableChange, &QTableWidget::itemChanged, this, [onValue]() { onValue(); });
    connect(ui->tableCycle, &QTableWidget::itemChanged, this, [onValue]() { onValue(); });
    connect(ui->tableDyna, &QTableWidget::itemChanged, this, [onValue]() { onValue(); });
}

void FillStorageCyclePage::ensureTableRows(QTableWidget* table, int rows) const
{
    table->setRowCount(rows);
}

void FillStorageCyclePage::syncSpinWithTableRows(QSpinBox* spin, QTableWidget* table) const
{
    const QSignalBlocker blocker(spin);
    spin->setValue(table->rowCount());
}

void FillStorageCyclePage::addTableRow(QTableWidget* table, QSpinBox* spin)
{
    table->insertRow(table->rowCount());
    syncSpinWithTableRows(spin, table);
    updatePreview();
}

void FillStorageCyclePage::removeTableRow(QTableWidget* table, QSpinBox* spin)
{
    int row = table->currentRow();
    if (row < 0)
        row = table->rowCount() - 1;
    if (row < 0)
        return;

    table->removeRow(row);
    syncSpinWithTableRows(spin, table);
    updatePreview();
}

void FillStorageCyclePage::openTableEditor(const QString& title, QTableWidget* table, QSpinBox* spin)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(900, 480);

    auto* layout = new QVBoxLayout(&dialog);
    auto* editorTable = new QTableWidget(&dialog);
    editorTable->setColumnCount(table->columnCount());
    QStringList headers;
    for (int c = 0; c < table->columnCount(); ++c)
        headers << table->horizontalHeaderItem(c)->text();
    editorTable->setHorizontalHeaderLabels(headers);
    editorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    editorTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    editorTable->setSelectionMode(QAbstractItemView::SingleSelection);
    editorTable->setAlternatingRowColors(true);
    editorTable->setRowCount(table->rowCount());
    for (int r = 0; r < table->rowCount(); ++r)
        for (int c = 0; c < table->columnCount(); ++c)
            if (auto* src = table->item(r, c))
                editorTable->setItem(r, c, src->clone());

    auto* tool = new QHBoxLayout();
    auto* btnAdd = new QPushButton("添加行", &dialog);
    auto* btnRemove = new QPushButton("删除行", &dialog);
    tool->addStretch();
    tool->addWidget(btnAdd);
    tool->addWidget(btnRemove);

    connect(btnAdd, &QPushButton::clicked, &dialog,
            [editorTable]() { editorTable->insertRow(editorTable->rowCount()); });
    connect(btnRemove, &QPushButton::clicked, &dialog,
            [editorTable]()
            {
                int row = editorTable->currentRow();
                if (row < 0)
                    row = editorTable->rowCount() - 1;
                if (row >= 0)
                    editorTable->removeRow(row);
            });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addLayout(tool);
    layout->addWidget(editorTable);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    table->setRowCount(editorTable->rowCount());
    for (int r = 0; r < editorTable->rowCount(); ++r)
        for (int c = 0; c < editorTable->columnCount(); ++c)
            if (auto* src = editorTable->item(r, c))
                table->setItem(r, c, src->clone());

    syncSpinWithTableRows(spin, table);
    updatePreview();
}

QJsonArray FillStorageCyclePage::tableToJson(QTableWidget* table) const
{
    QJsonArray rows;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        QJsonArray cols;
        for (int c = 0; c < table->columnCount(); ++c)
            cols.append(tableItemText(table, r, c));
        rows.append(cols);
    }
    return rows;
}

void FillStorageCyclePage::jsonToTable(const QJsonArray& arr, QTableWidget* table) const
{
    for (int r = 0; r < table->rowCount() && r < arr.size(); ++r)
    {
        const QJsonArray cols = arr.at(r).toArray();
        for (int c = 0; c < table->columnCount() && c < cols.size(); ++c)
            table->setItem(r, c, new QTableWidgetItem(cols.at(c).toString()));
    }
}

QString FillStorageCyclePage::tableItemText(QTableWidget* table, int row, int col,
                                            const QString& defaultValue) const
{
    const auto* item = table->item(row, col);
    if (!item)
        return defaultValue;
    const QString text = item->text().trimmed();
    return text.isEmpty() ? defaultValue : text;
}

QString FillStorageCyclePage::buildSectionText() const
{
    QString out;
    out += QString("%1    NCASE\n").arg(ui->spinNcase->value());
    out += "----------------------------------------------\n";
    out += QString("%1    ICASE\n").arg(ui->spinIcase->value());
    out += QString("%1 %2 %3\n")
               .arg(ui->comboSeep->currentData().toInt())
               .arg(ui->comboBody->currentData().toInt())
               .arg(ui->comboFace->currentData().toInt());

    out += QString("%1 %2\n").arg(ui->spinNumWet->value()).arg(
        ui->comboIfWet->currentData().toInt());
    for (int i = 0; i < ui->tableWetMap->rowCount(); ++i)
        out += QString("%1 %2\n")
                   .arg(tableItemText(ui->tableWetMap, i, 0, "1"))
                   .arg(tableItemText(ui->tableWetMap, i, 1, "1"));

    out += QString("%1 %1\n").arg(ui->spinNumDrop->value());
    for (int i = 0; i < ui->tableDrop->rowCount(); ++i)
        out += QString("%1 %2 %3\n")
                   .arg(tableItemText(ui->tableDrop, i, 0, "0.0"))
                   .arg(tableItemText(ui->tableDrop, i, 1, "0.0"))
                   .arg(i + 1);

    out += QString("%1\n").arg(ui->spinNchange->value());
    for (int i = 0; i < ui->tableChange->rowCount(); ++i)
        out += QString("%1 %2\n")
                   .arg(tableItemText(ui->tableChange, i, 0, "1"))
                   .arg(tableItemText(ui->tableChange, i, 1, "1"));

    out += QString("%1\n").arg(ui->spinNcircle->value());
    for (int i = 0; i < ui->tableCycle->rowCount(); ++i)
        out += QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 '%11' '%12' %13\n")
                   .arg(tableItemText(ui->tableCycle, i, 0, "1"))
                   .arg(tableItemText(ui->tableCycle, i, 1, "1"))
                   .arg(tableItemText(ui->tableCycle, i, 2, "0.0"))
                   .arg(tableItemText(ui->tableCycle, i, 3, "0.0"))
                   .arg(tableItemText(ui->tableCycle, i, 4, "0.0"))
                   .arg(tableItemText(ui->tableCycle, i, 5, "0.0"))
                   .arg(tableItemText(ui->tableCycle, i, 6, "1"))
                   .arg(tableItemText(ui->tableCycle, i, 7, "0"))
                   .arg(tableItemText(ui->tableCycle, i, 8, "1"))
                   .arg(tableItemText(ui->tableCycle, i, 9, "0"))
                   .arg(tableItemText(ui->tableCycle, i, 10, "NONE"))
                   .arg(tableItemText(ui->tableCycle, i, 11, "NONE"))
                   .arg(i + 1);

    out += QString("'%1'\n").arg(ui->editDirDyna->text().trimmed());
    out += QString("%1\n").arg(ui->spinNumDyna->value());
    for (int i = 0; i < ui->tableDyna->rowCount(); ++i)
        out += QString("%1 %2 %3\n")
                   .arg(tableItemText(ui->tableDyna, i, 0, "1"))
                   .arg(tableItemText(ui->tableDyna, i, 1, "1"))
                   .arg(i + 1);

    out += QString("1 %1 %2\n")
               .arg(ui->comboIfMode->currentData().toInt())
               .arg(ui->comboIfWave->currentData().toInt());
    out += QString("%1 %2 %3\n")
               .arg(ui->spinDt->value(), 0, 'f', 6)
               .arg(ui->spinTsta->value(), 0, 'f', 6)
               .arg(ui->spinTend->value(), 0, 'f', 6);
    out += QString("'%1' '%2' '%3'\n")
               .arg(ui->editWaveAx->text().trimmed())
               .arg(ui->editWaveAy->text().trimmed())
               .arg(ui->editWaveAz->text().trimmed());
    return out;
}

void FillStorageCyclePage::updatePreview()
{
    ui->plainTextPreview->setPlainText(buildSectionText());
}
