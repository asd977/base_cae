#include "FillStorageCyclePage.h"

#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QWidget>
#include <cmath>

#include "ui_FillStorageCyclePage.h"

#include "SolverUiLayoutHelper.h"

namespace
{
constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr int MAX_MATERIAL_RELATIONS = 300;

int currentDomainDimension()
{
    const int meshDimension =
        PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return meshDimension == 3 ? 3 : 2;
}

DomainKey materialDomainKey(int matype)
{
    if (matype < FIRST_COMPUTABLE_MATYPE)
        return DomainKey{};
    return DomainKey{QStringLiteral("MATYPE"), currentDomainDimension(), matype};
}
}

FillStorageCyclePage::FillStorageCyclePage(QWidget* parent)
    : BasePage("FillStorageCycle"), ui(new Ui::FillStorageCyclePage)
{
    ui->setupUi(this);
    initValueEditors();
    SolverUiLayoutHelper::polishPage(this);
    updatePreview();
}

FillStorageCyclePage::~FillStorageCyclePage()
{
    delete ui;
}

void FillStorageCyclePage::beforeLoad()
{
    m_materialHighlightActive = true;
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Domain);
    refreshMaterialMappingOptions(m_wetTable);
    refreshMaterialMappingOptions(m_dynamicMaterialTable);

    const bool wetEnabled = valueOf(QStringLiteral("IFWET")) == QStringLiteral("1");
    QTableWidget* table = nullptr;
    if (wetEnabled && m_wetTable)
        table = m_wetTable;
    else if (m_dynamicOptionsAvailable && m_dynamicMaterialTable)
        table = m_dynamicMaterialTable;
    if (table && table->rowCount() > 0)
    {
        table->setCurrentCell(qMax(0, table->currentRow()), 0);
        refreshMaterialMappingHighlight(table, table->currentRow());
    }
    else
    {
        clearMaterialMappingHighlight();
    }

    // 结果目录可能刚由渗流计算生成，打开页面时重新扫描可用结果。
    emit requirementsChanged();
}

void FillStorageCyclePage::beforeUnload()
{
    clearMaterialMappingHighlight();
    PickManager::instance().clearDomainSelection(getPageType(), m_uuid);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Domain);
    m_materialHighlightActive = false;
}

void FillStorageCyclePage::initValueEditors()
{
    auto bindLine = [this](const QString& key, QLineEdit* edit)
    {
        m_editors.insert(key, edit);
        connect(edit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    };
    auto bindCombo = [this](const QString& key, QComboBox* combo)
    {
        m_editors.insert(key, combo);
        connect(combo, &QComboBox::currentTextChanged, this, [this]() { updatePreview(); });
    };

    bindCombo("IFIIWL_SEEP", ui->comboIFIIWL_SEEP);
    bindCombo("IFIIWL_BODY", ui->comboIFIIWL_BODY);
    bindCombo("IFIIWL_FACE", ui->comboIFIIWL_FACE);
    bindCombo("IFWET", ui->comboIFWET);
    bindCombo("IFDROP", ui->comboIFDROP);
    bindCombo("IFMODE", ui->comboIFMODE);
    bindCombo("IFWAVE", ui->comboIFWAVE);
    bindCombo("IFSETT", ui->comboIFSETT);
    bindLine("DT", ui->editDT);
    bindLine("TSTA", ui->editTSTA);
    bindLine("TEND", ui->editTEND);
    bindLine("WOMI_1", ui->editWOMI1);

    // 当前产品仅提供均质坝。求解器会把 IROCKFILLTYPE=4 的
    // IFIIWL_SEEP 强制改为 0，因此前端固定显示且只导出 0。
    ui->comboIFIIWL_SEEP->clear();
    ui->comboIFIIWL_SEEP->addItem(
        QStringLiteral("均质坝固定不单独施加"), QStringLiteral("0"));
    ui->comboIFIIWL_SEEP->setEnabled(false);
    ui->comboIFIIWL_BODY->setItemData(0, "0");
    ui->comboIFIIWL_BODY->setItemData(1, "2");
    ui->comboIFIIWL_FACE->setItemData(0, "0");
    ui->comboIFIIWL_FACE->setItemData(1, "1");
    ui->comboIFWET->setItemData(0, "0");
    ui->comboIFWET->setItemData(1, "1");
    ui->comboIFDROP->setItemData(0, "0");
    ui->comboIFDROP->setItemData(1, "1");
    ui->comboIFMODE->setItemData(0, "0");
    ui->comboIFMODE->setItemData(1, "1");
    ui->comboIFWAVE->setItemData(0, "0");
    ui->comboIFWAVE->setItemData(1, "1");
    ui->comboIFSETT->setItemData(0, "0");
    ui->comboIFSETT->setItemData(1, "1");
    ui->groupWet->setChecked(
        ui->comboIFWET->currentData().toString() == QStringLiteral("1"));
    ui->groupDrop->setChecked(
        ui->comboIFDROP->currentData().toString() == QStringLiteral("1"));
    ui->comboIFIIWL_BODY->setItemText(
        1, QStringLiteral("读取渗流结果（水头 + 节点体力）"));
    ui->comboSeepageSource->setItemData(0, QString());
    ui->comboSeepageResultType->setItemData(0, QStringLiteral("steady"));
    ui->comboSeepageResultType->setItemData(1, QStringLiteral("transient"));
    ui->comboSeepageTransientStep->setItemData(0, QString());
    ui->labelWOMI1->setText(QStringLiteral("一阶圆频率 ω1（无模态结果时）"));
    ui->editWOMI1->setToolTip(QStringLiteral("仅在执行地震波时程且未同时进行模态分析时必填。"));

    connect(ui->groupWet, &QGroupBox::toggled, this,
            [this](bool checked)
            {
                const int index = checked ? 1 : 0;
                if (ui->comboIFWET->currentIndex() != index)
                    ui->comboIFWET->setCurrentIndex(index);
            });
    connect(ui->groupDrop, &QGroupBox::toggled, this,
            [this](bool checked)
            {
                const int index = checked ? 1 : 0;
                if (ui->comboIFDROP->currentIndex() != index)
                    ui->comboIFDROP->setCurrentIndex(index);
            });
    connect(ui->comboIFWET, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                const bool checked =
                    ui->comboIFWET->currentData().toString() == QStringLiteral("1");
                if (ui->groupWet->isChecked() != checked)
                    ui->groupWet->setChecked(checked);
                updateConditionalVisibility();
            });
    connect(ui->comboIFDROP, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                const bool checked =
                    ui->comboIFDROP->currentData().toString() == QStringLiteral("1");
                if (ui->groupDrop->isChecked() != checked)
                    ui->groupDrop->setChecked(checked);
                updateConditionalVisibility();
            });
    connect(ui->comboIFMODE, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateConditionalVisibility(); });
    connect(ui->comboIFWAVE, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateConditionalVisibility(); });
    connect(ui->comboIFSETT, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateConditionalVisibility(); });
    for (QComboBox* combo : {ui->comboIFIIWL_SEEP, ui->comboIFIIWL_BODY,
                             ui->comboIFIIWL_FACE, ui->comboIFMODE,
                             ui->comboIFWAVE, ui->comboIFSETT})
    {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]() { emit requirementsChanged(); });
    }
    for (QComboBox* combo : {ui->comboIFIIWL_SEEP, ui->comboIFIIWL_BODY})
    {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]() { updateConditionalVisibility(); });
    }
    connect(ui->comboSeepageSource, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                const QString sourceUuid =
                    ui->comboSeepageSource->currentData().toString().trimmed();
                if (sourceUuid != m_savedSeepageSourceUuid)
                {
                    m_savedSeepageTransientStep.clear();
                    const QSignalBlocker blocker(ui->comboSeepageTransientStep);
                    ui->comboSeepageTransientStep->setCurrentIndex(0);
                }
                m_savedSeepageSourceUuid = sourceUuid;
                m_savedSeepageSourceName = ui->comboSeepageSource->currentText().trimmed();
                updateConditionalVisibility();
                emit requirementsChanged();
            });
    connect(ui->comboSeepageResultType,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                updateConditionalVisibility();
                emit requirementsChanged();
            });
    connect(ui->comboSeepageTransientStep,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                m_savedSeepageTransientStep =
                    ui->comboSeepageTransientStep->currentData().toString().trimmed();
                emit requirementsChanged();
            });

    auto mkTable = [this](const QStringList& headers, bool dataOnly = true)
    {
        auto* t = new QTableWidget(this);
        t->setColumnCount(headers.size());
        t->setHorizontalHeaderLabels(headers);
        if (dataOnly)
            t->hide();
        return t;
    };

    m_wetTable = mkTable({"坝体当前材料", "浸润后材料"}, false);
    m_dynamicMaterialTable = mkTable({"网格静力材料", "动力本构材料"}, false);
    m_dropTable = mkTable({"骤降前水位", "骤降后水位"}, false);
    m_circleTable = mkTable({"填筑开始层", "填筑结束层", "填筑结束后流变时间", "初始蓄水高程", "本阶段蓄水高程",
                             "蓄水结束后流变时间", "是否填筑", "是否填筑期流变", "是否蓄水",
                             "是否蓄水期流变", "节点水头数据（可选）", "节点体力数据（可选）"});

    m_wetTable->setRowCount(0);
    m_wetTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_wetTable->verticalHeader()->hide();
    m_wetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_wetTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_wetTable->setToolTip(QStringLiteral("湿化计算时使用的材料对应关系"));
    m_wetTable->setAlternatingRowColors(true);
    m_dynamicMaterialTable->setRowCount(0);
    m_dynamicMaterialTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dynamicMaterialTable->verticalHeader()->hide();
    m_dynamicMaterialTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dynamicMaterialTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dynamicMaterialTable->setToolTip(
        QStringLiteral("仅在本阶段启用动力或地震波动计算时使用"));
    m_dynamicMaterialTable->setAlternatingRowColors(true);

    m_dropTable->setRowCount(1);
    m_dropTable->setItem(0, 0, new QTableWidgetItem("3200.000"));
    m_dropTable->setItem(0, 1, new QTableWidgetItem("3160.000"));
    m_dropTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dropTable->verticalHeader()->hide();
    m_dropTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dropTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dropTable->setAlternatingRowColors(true);
    m_dropTable->setMinimumHeight(108);
    m_dropTable->setMaximumHeight(180);

    m_circleTable->setRowCount(1);
    const QStringList defaults = {"2", "9", "0.0000", "0.0000", "28.0000", "0.0000", "1", "0",
                                  "1", "0", "'NONE'", "'NONE'"};
    for (int c = 0; c < defaults.size(); ++c)
        m_circleTable->setItem(0, c, new QTableWidgetItem(defaults.at(c)));
    m_circleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    connect(m_wetTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_dynamicMaterialTable, &QTableWidget::cellChanged, this,
            [this]() { updatePreview(); });
    connect(m_wetTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int)
            { refreshMaterialMappingHighlight(m_wetTable, currentRow); });
    connect(m_dynamicMaterialTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int)
            { refreshMaterialMappingHighlight(m_dynamicMaterialTable, currentRow); });
    connect(m_dropTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });
    connect(m_circleTable, &QTableWidget::cellChanged, this, [this]() { updatePreview(); });

    auto openEditor = [this](QTableWidget* src, const QString& title)
    {
        const bool isCircleTable = (src == m_circleTable);
        QDialog dlg(this);
        dlg.setWindowTitle(title);
        dlg.resize(isCircleTable ? 980 : 900, isCircleTable ? 520 : 420);
        auto* layout = new QVBoxLayout(&dlg);
        auto* table = new QTableWidget(&dlg);
        table->setColumnCount(src->columnCount());
        QStringList headers;
        for (int c = 0; c < src->columnCount(); ++c)
            headers << (src->horizontalHeaderItem(c) ? src->horizontalHeaderItem(c)->text() : QString());
        table->setHorizontalHeaderLabels(headers);
        table->setRowCount(src->rowCount());
        if (isCircleTable)
        {
            // CAHEAD/CABODY 由“坝体渗流体力结果来源”统一生成，
            // 不允许在施工阶段表里手工填写两个互相冲突的来源。
            table->setColumnHidden(10, true);
            table->setColumnHidden(11, true);
        }
        for (int r = 0; r < src->rowCount(); ++r)
        {
            for (int c = 0; c < src->columnCount(); ++c)
            {
                QString value = tableItemText(src, r, c, QString());
                if (isCircleTable && c >= 10 &&
                    (value.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) == 0 ||
                     value.compare(QStringLiteral("'NONE'"), Qt::CaseInsensitive) == 0))
                {
                    value.clear();
                }
                table->setItem(r, c, new QTableWidgetItem(value));
            }
        }
        if (isCircleTable)
        {
            QHeaderView* header = table->horizontalHeader();
            header->setSectionResizeMode(QHeaderView::Interactive);
            header->setStretchLastSection(false);
            header->setMinimumSectionSize(48);
            table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
            table->setWordWrap(false);
            table->setTextElideMode(Qt::ElideNone);

            // 只按完整表头计算列宽，并交给当前 Qt 样式补足内边距。
            const QFontMetrics metrics(header->font());
            for (int column = 0; column < headers.size(); ++column)
            {
                if (table->isColumnHidden(column))
                    continue;
                QStyleOptionHeader option;
                option.initFrom(header);
                option.text = headers.at(column);
                const QSize textSize(metrics.horizontalAdvance(option.text), metrics.height());
                const int headerWidth = header->style()->sizeFromContents(
                    QStyle::CT_HeaderSection, &option, textSize, header).width() + 2;
                table->setColumnWidth(column, qMax(header->minimumSectionSize(), headerWidth));
            }
        }
        else
        {
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        }
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);

        auto setCircleCombo = [table](int row, int col, const QString& val)
        {
            auto* cb = new QComboBox(table);
            cb->addItem("否", "0");
            cb->addItem("是", "1");
            const int idx = cb->findData(val.trimmed());
            cb->setCurrentIndex(idx >= 0 ? idx : 0);
            table->setCellWidget(row, col, cb);
        };

        if (isCircleTable)
        {
            for (int r = 0; r < table->rowCount(); ++r)
            {
                for (int c : {6, 7, 8, 9})
                    setCircleCombo(r, c, tableItemText(src, r, c, "0"));
            }
        }
        layout->addWidget(table);

        auto* btnRow = new QHBoxLayout();
        auto* btnAdd = new QPushButton(isCircleTable ? QStringLiteral("新增阶段")
                                                     : QStringLiteral("新增一行"),
                                       &dlg);
        auto* btnDel = new QPushButton(isCircleTable ? QStringLiteral("删除阶段")
                                                     : QStringLiteral("删除选中行"),
                                       &dlg);
        btnRow->addWidget(btnAdd);
        btnRow->addWidget(btnDel);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        connect(btnAdd, &QPushButton::clicked, &dlg, [this, table, isCircleTable, setCircleCombo]() {
            const int row = table->rowCount();
            table->insertRow(row);
            if (isCircleTable)
            {
                int firstLayer = 2;
                double initialElevation = 0.0;
                if (row > 0)
                {
                    bool layerOk = false;
                    const int previousEnd = tableItemText(
                        table, row - 1, 1, QStringLiteral("1")).toInt(&layerOk);
                    firstLayer = layerOk ? qMax(2, previousEnd + 1) : 2;
                    initialElevation = tableItemText(
                        table, row - 1, 4, QStringLiteral("0")).toDouble();
                }
                const QStringList defaults = {
                    QString::number(firstLayer), QString::number(firstLayer),
                    QStringLiteral("0.0000"), QString::number(initialElevation, 'f', 4),
                    QString::number(initialElevation, 'f', 4), QStringLiteral("0.0000"),
                    QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("1"),
                    QStringLiteral("0"), QString(), QString()};
                for (int column = 0; column < defaults.size(); ++column)
                    table->setItem(row, column, new QTableWidgetItem(defaults.at(column)));
                for (int c : {6, 7, 8, 9})
                    setCircleCombo(row, c, defaults.at(c));
            }
        });
        connect(btnDel, &QPushButton::clicked, &dlg, [table, isCircleTable, &dlg]() {
            if (isCircleTable && table->rowCount() <= 1)
            {
                QMessageBox::information(&dlg, QStringLiteral("无法删除"),
                                         QStringLiteral("至少需要保留一个填筑蓄水阶段。"));
                return;
            }
            const auto rows = table->selectionModel()->selectedRows();
            for (int i = rows.size() - 1; i >= 0; --i)
                table->removeRow(rows.at(i).row());
        });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dlg,
                [this, table, isCircleTable, &dlg]()
                {
                    if (isCircleTable)
                    {
                        for (int row = 0; row < table->rowCount(); ++row)
                        {
                            bool firstOk = false;
                            bool lastOk = false;
                            const int first = tableItemText(table, row, 0, QString()).toInt(&firstOk);
                            const int last = tableItemText(table, row, 1, QString()).toInt(&lastOk);
                            if (!firstOk || !lastOk || first < 2 || last < first)
                            {
                                QMessageBox::warning(
                                    &dlg, QStringLiteral("请完善施工阶段"),
                                    QStringLiteral("第 %1 个阶段的填筑层范围无效，请设置为从第 2 层开始且结束层不小于开始层。")
                                        .arg(row + 1));
                                return;
                            }
                        }
                    }
                    dlg.accept();
                });
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted)
            return;

        src->setRowCount(table->rowCount());
        for (int r = 0; r < table->rowCount(); ++r)
        {
            for (int c = 0; c < table->columnCount(); ++c)
            {
                QString val = tableItemText(table, r, c, QString());
                if (isCircleTable && c >= 10)
                {
                    if (val.isEmpty() || val == QStringLiteral("无"))
                        val = QStringLiteral("'NONE'");
                    else if (!val.startsWith(QLatin1Char('\'')))
                        val = QStringLiteral("'%1'").arg(val);
                }
                if (src->item(r, c))
                    src->item(r, c)->setText(val);
                else
                    src->setItem(r, c, new QTableWidgetItem(val));
            }
        }
        updatePreview();
    };

    ui->btnEditWet->setText(QStringLiteral("新增关系"));
    m_removeWetMaterialButton = new QPushButton(QStringLiteral("删除选中"), this);
    ui->layoutWet->insertWidget(1, m_removeWetMaterialButton);
    connect(ui->btnEditWet, &QPushButton::clicked, this,
            [this]() { addMaterialMappingRow(m_wetTable); });
    connect(m_removeWetMaterialButton, &QPushButton::clicked, this,
            [this]() { removeSelectedMaterialMapping(m_wetTable); });
    ui->layoutWetMapping->addWidget(m_wetTable);
    ui->btnEditDrop->setText(QStringLiteral("新增水位"));
    m_removeDropButton = new QPushButton(QStringLiteral("删除选中"), this);
    ui->layoutDrop->insertWidget(1, m_removeDropButton);
    ui->layoutDropMain->addWidget(m_dropTable);
    connect(ui->btnEditDrop, &QPushButton::clicked, this,
            [this]()
            {
                const int row = m_dropTable->rowCount();
                m_dropTable->insertRow(row);
                QString before = QStringLiteral("3200.000");
                QString after = QStringLiteral("3160.000");
                if (row > 0)
                {
                    after = tableItemText(m_dropTable, row - 1, 1, after);
                    before = after;
                }
                m_dropTable->setItem(row, 0, new QTableWidgetItem(before));
                m_dropTable->setItem(row, 1, new QTableWidgetItem(after));
                m_dropTable->setCurrentCell(row, 0);
                m_dropTable->editItem(m_dropTable->item(row, 0));
                updatePreview();
                updateConditionalVisibility();
            });
    connect(m_removeDropButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_dropTable->rowCount() <= 1)
                {
                    QMessageBox::information(this, QStringLiteral("无法删除"),
                                             QStringLiteral("至少保留一组水位。"));
                    return;
                }
                int row = m_dropTable->currentRow();
                if (row < 0)
                    row = m_dropTable->rowCount() - 1;
                m_dropTable->removeRow(row);
                updatePreview();
                updateConditionalVisibility();
            });
    connect(ui->btnEditCircle, &QPushButton::clicked, this,
            [openEditor, this]() { openEditor(m_circleTable, "编辑填筑与蓄水周期表"); });

    m_dynamicMaterialGroup =
        new QGroupBox(QStringLiteral("动力材料对应"), this);
    auto* dynamicMaterialGroupLayout = new QVBoxLayout(m_dynamicMaterialGroup);
    dynamicMaterialGroupLayout->setContentsMargins(9, 9, 9, 9);
    dynamicMaterialGroupLayout->setSpacing(6);
    m_dynamicMaterialHint = new QLabel(
        QStringLiteral("提示：需启用模态分析或地震波时程分析。"),
        m_dynamicMaterialGroup);
    m_dynamicMaterialHint->setWordWrap(true);
    dynamicMaterialGroupLayout->addWidget(m_dynamicMaterialHint);
    m_dynamicMaterialControls = new QWidget(m_dynamicMaterialGroup);
    auto* dynamicMappingLayout = new QHBoxLayout(m_dynamicMaterialControls);
    dynamicMappingLayout->setContentsMargins(0, 0, 0, 0);
    m_addDynamicMaterialButton = new QPushButton(QStringLiteral("新增关系"), m_dynamicMaterialControls);
    m_removeDynamicMaterialButton = new QPushButton(QStringLiteral("删除选中"), m_dynamicMaterialControls);
    m_dynamicMaterialSummary = new QLabel(m_dynamicMaterialControls);
    dynamicMappingLayout->addWidget(m_addDynamicMaterialButton);
    dynamicMappingLayout->addWidget(m_removeDynamicMaterialButton);
    dynamicMappingLayout->addWidget(m_dynamicMaterialSummary);
    dynamicMappingLayout->addStretch();
    dynamicMaterialGroupLayout->addWidget(m_dynamicMaterialControls);
    dynamicMaterialGroupLayout->addWidget(m_dynamicMaterialTable);
    const int dynamicGroupIndex = ui->verticalLayoutContent->indexOf(ui->groupDynamic);
    ui->verticalLayoutContent->insertWidget(
        dynamicGroupIndex >= 0 ? dynamicGroupIndex + 1 : ui->verticalLayoutContent->count(),
        m_dynamicMaterialGroup);
    connect(m_addDynamicMaterialButton, &QPushButton::clicked, this,
            [this]() { addMaterialMappingRow(m_dynamicMaterialTable); });
    connect(m_removeDynamicMaterialButton, &QPushButton::clicked, this,
            [this]() { removeSelectedMaterialMapping(m_dynamicMaterialTable); });

    updateMaterialTableHeight(m_wetTable);
    updateMaterialTableHeight(m_dynamicMaterialTable);
    updateConditionalVisibility();
}

void FillStorageCyclePage::updateConditionalVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);
    const bool needsAssociation = needsSeepageAssociation();
    const bool transientResult =
        ui->comboSeepageResultType->currentData().toString() == QStringLiteral("transient");
    const bool hasSource =
        m_availableSeepageSourceUuids.contains(seepageSourceUuid());

    // 关联区域始终显示；不适用时整体置灰，避免页面布局跳动。
    ui->groupSeepageAssociation->setEnabled(needsAssociation);
    ui->groupSeepageTransientStep->setEnabled(
        needsAssociation && transientResult && hasSource);
    ui->groupDynamic->setVisible(true);
    ui->labelDynamicHint->setVisible(true);
    ui->labelDynamicHint->setText(
        m_dynamicOptionsAvailable
            ? QStringLiteral("当前计算模式包含动力计算，可设置以下参数。")
            : QStringLiteral("当前为仅静力计算，动力参数保留显示，但不参与本次计算。"));
    ui->groupDynamic->setEnabled(m_dynamicOptionsAvailable);

    const bool wetEnabled = ui->groupWet->isChecked();
    const bool dropEnabled = ui->groupDrop->isChecked();
    ui->btnEditWet->setEnabled(wetEnabled);
    if (m_removeWetMaterialButton)
        m_removeWetMaterialButton->setEnabled(wetEnabled && m_wetTable &&
                                              m_wetTable->rowCount() > 0);
    ui->labelWetRows->setEnabled(wetEnabled);
    if (m_wetTable)
    {
        m_wetTable->setVisible(true);
        m_wetTable->setEnabled(wetEnabled);
    }
    ui->btnEditDrop->setVisible(true);
    ui->btnEditDrop->setEnabled(dropEnabled);
    if (m_removeDropButton)
        m_removeDropButton->setEnabled(dropEnabled && m_dropTable &&
                                       m_dropTable->rowCount() > 1);
    ui->labelDropRows->setVisible(true);
    ui->labelDropRows->setEnabled(dropEnabled);
    if (m_dropTable)
    {
        m_dropTable->setVisible(true);
        m_dropTable->setEnabled(dropEnabled);
    }

    const bool modeEnabled = ui->comboIFMODE->currentData().toString() == QStringLiteral("1");
    const bool waveEnabled = ui->comboIFWAVE->currentData().toString() == QStringLiteral("1");
    if (!waveEnabled && ui->comboIFSETT->currentData().toString() == QStringLiteral("1"))
    {
        const QSignalBlocker blocker(ui->comboIFSETT);
        const int offIndex = ui->comboIFSETT->findData(QStringLiteral("0"));
        if (offIndex >= 0)
            ui->comboIFSETT->setCurrentIndex(offIndex);
    }
    const bool settlementEnabled =
        ui->comboIFSETT->currentData().toString() == QStringLiteral("1");
    ui->labelIFSETT->setEnabled(m_dynamicOptionsAvailable && waveEnabled);
    ui->comboIFSETT->setEnabled(m_dynamicOptionsAvailable && waveEnabled);
    ui->comboIFSETT->setToolTip(
        waveEnabled ? QStringLiteral("基于本次地震波响应结果计算永久沉降。")
                    : QStringLiteral("需先启用地震波时程分析。"));

    const bool dynamicMaterialMappingEnabled =
        m_dynamicOptionsAvailable && (modeEnabled || waveEnabled);
    const bool timeEnabled = m_dynamicOptionsAvailable && (waveEnabled || settlementEnabled);
    // 动力参数固定显示，只通过启用/禁用表达依赖关系，避免切换选项时页面跳动。
    ui->labelDT->setVisible(true);
    ui->editDT->setVisible(true);
    ui->labelTSTA->setVisible(true);
    ui->editTSTA->setVisible(true);
    ui->labelTEND->setVisible(true);
    ui->editTEND->setVisible(true);
    ui->labelDT->setEnabled(timeEnabled);
    ui->editDT->setEnabled(timeEnabled);
    ui->labelTSTA->setEnabled(timeEnabled);
    ui->editTSTA->setEnabled(timeEnabled);
    ui->labelTEND->setEnabled(timeEnabled);
    ui->editTEND->setEnabled(timeEnabled);

    const bool needFallbackFrequency = m_dynamicOptionsAvailable && waveEnabled && !modeEnabled;
    ui->labelWOMI1->setVisible(true);
    ui->editWOMI1->setVisible(true);
    ui->labelWOMI1->setEnabled(needFallbackFrequency);
    ui->editWOMI1->setEnabled(needFallbackFrequency);
    if (m_dynamicMaterialGroup)
    {
        m_dynamicMaterialGroup->setVisible(true);
        m_dynamicMaterialGroup->setEnabled(true);
    }
    if (m_dynamicMaterialHint)
    {
        m_dynamicMaterialHint->setVisible(true);
        m_dynamicMaterialHint->setText(
            dynamicMaterialMappingEnabled
                ? QStringLiteral("动力材料关系用于模态/地震波计算。")
                : (m_dynamicOptionsAvailable
                       ? QStringLiteral("未启用模态或地震波计算，动力材料关系暂不参与本次计算。")
                       : QStringLiteral("当前为仅静力计算，动力材料关系保留但不参与本次计算。")));
    }
    if (m_dynamicMaterialControls)
    {
        m_dynamicMaterialControls->setVisible(true);
        m_dynamicMaterialControls->setEnabled(dynamicMaterialMappingEnabled);
    }
    if (m_dynamicMaterialTable)
    {
        m_dynamicMaterialTable->setVisible(true);
        m_dynamicMaterialTable->setEnabled(dynamicMaterialMappingEnabled);
    }

    if (m_materialHighlightActive)
    {
        if (m_activeMaterialTable == m_wetTable && !wetEnabled)
            m_activeMaterialTable = nullptr;
        if (m_activeMaterialTable == m_dynamicMaterialTable &&
            !dynamicMaterialMappingEnabled)
            m_activeMaterialTable = nullptr;
        if (!m_activeMaterialTable)
        {
            if (wetEnabled && m_wetTable && m_wetTable->rowCount() > 0)
                m_activeMaterialTable = m_wetTable;
            else if (dynamicMaterialMappingEnabled && m_dynamicMaterialTable &&
                     m_dynamicMaterialTable->rowCount() > 0)
                m_activeMaterialTable = m_dynamicMaterialTable;
        }

        if (m_activeMaterialTable)
        {
            const int row = qMax(0, m_activeMaterialTable->currentRow());
            m_activeMaterialTable->setCurrentCell(row, 0);
            refreshMaterialMappingHighlight(m_activeMaterialTable, row);
        }
        else
        {
            clearMaterialMappingHighlight();
        }
    }
}

void FillStorageCyclePage::setDynamicOptionsAvailable(bool available)
{
    if (m_dynamicOptionsAvailable == available)
        return;
    m_dynamicOptionsAvailable = available;
    updateConditionalVisibility();
}

bool FillStorageCyclePage::usesDamBaseUplift() const
{
    // IROCKFILLTYPE=4（均质坝）时，求解器会固定 IFIIWL_SEEP=0。
    return false;
}

bool FillStorageCyclePage::usesLinearDamBaseUplift() const
{
    return false;
}

bool FillStorageCyclePage::usesDamFacePressure() const
{
    return valueOf(QStringLiteral("IFIIWL_FACE")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesImportedSeepageHead() const
{
    // FILLFBLOAD2 会同时读取 CAHEAD 与 CABODY，并依据水头判断湿润节点。
    // 因此选择节点渗流体力时，节点水头是必需的伴随输入。
    return usesImportedSeepageBody();
}

bool FillStorageCyclePage::usesImportedSeepageBody() const
{
    return valueOf(QStringLiteral("IFIIWL_BODY")) == QStringLiteral("2");
}

bool FillStorageCyclePage::needsSeepageAssociation() const
{
    return usesImportedSeepageBody();
}

QString FillStorageCyclePage::seepageSourceUuid() const
{
    return ui->comboSeepageSource->currentData().toString().trimmed();
}

QString FillStorageCyclePage::seepageSourceName() const
{
    const QString uuid = seepageSourceUuid();
    if (!uuid.isEmpty() && !m_availableSeepageSourceUuids.contains(uuid) &&
        !m_savedSeepageSourceName.isEmpty())
        return m_savedSeepageSourceName;
    return ui->comboSeepageSource->currentText().trimmed();
}

QString FillStorageCyclePage::seepageResultType() const
{
    const QString type =
        ui->comboSeepageResultType->currentData().toString().trimmed();
    return type == QStringLiteral("transient")
               ? QStringLiteral("transient")
               : QStringLiteral("steady");
}

QString FillStorageCyclePage::seepageTransientStep() const
{
    return ui->comboSeepageTransientStep->currentData().toString().trimmed();
}

void FillStorageCyclePage::setAvailableSeepageSources(
    const QVector<QPair<QString, QString>>& sources)
{
    QString selectedUuid = seepageSourceUuid();
    if (selectedUuid.isEmpty())
        selectedUuid = m_savedSeepageSourceUuid;
    const QString selectedName = m_savedSeepageSourceName;

    const QSignalBlocker blocker(ui->comboSeepageSource);
    ui->comboSeepageSource->clear();
    ui->comboSeepageSource->addItem(QStringLiteral("未选择渗流工况"), QString());
    m_availableSeepageSourceUuids.clear();
    for (const auto& source : sources)
    {
        const QString uuid = source.first.trimmed();
        const QString name = source.second.trimmed();
        if (uuid.isEmpty())
            continue;
        ui->comboSeepageSource->addItem(
            name.isEmpty() ? QStringLiteral("渗流工况") : name, uuid);
        m_availableSeepageSourceUuids.insert(uuid);
    }

    int index = selectedUuid.isEmpty()
                    ? 0
                    : ui->comboSeepageSource->findData(selectedUuid);
    if (!selectedUuid.isEmpty() && index < 0)
    {
        const QString missingName =
            selectedName.isEmpty() ? selectedUuid : selectedName;
        ui->comboSeepageSource->addItem(
            QStringLiteral("原关联工况已删除：%1").arg(missingName),
            selectedUuid);
        index = ui->comboSeepageSource->count() - 1;
    }
    ui->comboSeepageSource->setCurrentIndex(qMax(0, index));
    m_savedSeepageSourceUuid =
        ui->comboSeepageSource->currentData().toString().trimmed();
    if (m_availableSeepageSourceUuids.contains(m_savedSeepageSourceUuid))
        m_savedSeepageSourceName =
            ui->comboSeepageSource->currentText().trimmed();
    updateConditionalVisibility();
}

void FillStorageCyclePage::setAvailableTransientResults(
    const QVector<int>& steps, bool hasFinalResult)
{
    QString selectedStep = seepageTransientStep();
    if (selectedStep.isEmpty())
        selectedStep = m_savedSeepageTransientStep;

    const QSignalBlocker blocker(ui->comboSeepageTransientStep);
    ui->comboSeepageTransientStep->clear();
    ui->comboSeepageTransientStep->addItem(
        QStringLiteral("请选择非稳态结果步"), QString());

    m_availableTransientSteps.clear();
    m_hasTransientFinalResult = hasFinalResult;
    if (hasFinalResult)
    {
        ui->comboSeepageTransientStep->addItem(
            QStringLiteral("最终结果"), QStringLiteral("final"));
    }
    for (int step : steps)
    {
        if (step < 0)
            continue;
        m_availableTransientSteps.insert(step);
        ui->comboSeepageTransientStep->addItem(
            QStringLiteral("STEP%1").arg(step), QString::number(step));
    }

    int index = selectedStep.isEmpty()
                    ? 0
                    : ui->comboSeepageTransientStep->findData(selectedStep);
    ui->comboSeepageTransientStep->setCurrentIndex(index >= 0 ? index : 0);
    m_savedSeepageTransientStep =
        ui->comboSeepageTransientStep->currentData().toString().trimmed();

    ui->labelSeepageTransientHint->setText(
        ui->comboSeepageTransientStep->count() > 1
            ? QStringLiteral("所选结果仅在本工况最后一个实际蓄水阶段作为坝体渗流场施加；前序阶段不重复施加。")
            : QStringLiteral("关联工况尚未生成可用的非稳态结果。"));
}

void FillStorageCyclePage::setPreparedSeepageFiles(
    const QString& headFile, const QString& bodyFile)
{
    m_preparedHeadFile = headFile;
    m_preparedBodyFile = bodyFile;
}

void FillStorageCyclePage::clearPreparedSeepageFiles()
{
    m_preparedHeadFile.clear();
    m_preparedBodyFile.clear();
}

bool FillStorageCyclePage::hasCreepCalculation() const
{
    if (!m_circleTable)
        return false;
    for (int row = 0; row < m_circleTable->rowCount(); ++row)
    {
        if (tableItemText(m_circleTable, row, 7, QStringLiteral("0")) == QStringLiteral("1")
            || tableItemText(m_circleTable, row, 9, QStringLiteral("0")) == QStringLiteral("1"))
            return true;
    }
    return false;
}

bool FillStorageCyclePage::usesModalAnalysis() const
{
    return m_dynamicOptionsAvailable
           && valueOf(QStringLiteral("IFMODE")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesWaveAnalysis() const
{
    return m_dynamicOptionsAvailable
           && valueOf(QStringLiteral("IFWAVE")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesPermanentSettlement() const
{
    return m_dynamicOptionsAvailable
           && valueOf(QStringLiteral("IFSETT")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesWaterDrop() const
{
    return valueOf(QStringLiteral("IFDROP")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesWetting() const
{
    return valueOf(QStringLiteral("IFWET")) == QStringLiteral("1");
}

int FillStorageCyclePage::cycleCount() const
{
    return m_circleTable ? m_circleTable->rowCount() : 0;
}

QString FillStorageCyclePage::finalStaticStateDirectoryName(int caseIndex,
                                                             int maximumDamLayer) const
{
    auto twoDigits = [](int value)
    {
        return value > 0 && value < 10
                   ? QStringLiteral("0%1").arg(value)
                   : QString::number(value);
    };

    if (caseIndex <= 0 || caseIndex > 99 || !m_circleTable)
        return {};

    const QString caseCode = twoDigits(caseIndex);
    QString lastDirectory = QStringLiteral("DISK00");
    constexpr double solverTolerance = 1.0e-3;

    for (int row = 0; row < m_circleTable->rowCount(); ++row)
    {
        const int circle = row + 1;
        if (circle > 99)
            return {};
        const QString circleCode = twoDigits(circle);

        bool packStartOk = false;
        bool packEndOk = false;
        bool packCreepOk = false;
        bool waterStartOk = false;
        bool waterEndOk = false;
        bool fillCreepOk = false;
        const int packStart =
            tableItemText(m_circleTable, row, 0, QString()).toInt(&packStartOk);
        const int packEnd =
            tableItemText(m_circleTable, row, 1, QString()).toInt(&packEndOk);
        const double packCreep =
            tableItemText(m_circleTable, row, 2, QString()).toDouble(&packCreepOk);
        const double waterStart =
            tableItemText(m_circleTable, row, 3, QString()).toDouble(&waterStartOk);
        const double waterEnd =
            tableItemText(m_circleTable, row, 4, QString()).toDouble(&waterEndOk);
        const double fillCreep =
            tableItemText(m_circleTable, row, 5, QString()).toDouble(&fillCreepOk);

        const bool packEnabled =
            tableItemText(m_circleTable, row, 6, QStringLiteral("0")) == QStringLiteral("1");
        const bool packCreepEnabled =
            tableItemText(m_circleTable, row, 7, QStringLiteral("0")) == QStringLiteral("1");
        const bool fillEnabled =
            tableItemText(m_circleTable, row, 8, QStringLiteral("0")) == QStringLiteral("1");
        const bool fillCreepEnabled =
            tableItemText(m_circleTable, row, 9, QStringLiteral("0")) == QStringLiteral("1");

        const bool packDirectoryExists =
            packStartOk && packEndOk
            && !(maximumDamLayer > 0 && packStart == packEnd
                 && packStart == maximumDamLayer);
        if (packEnabled && packDirectoryExists)
            lastDirectory = QStringLiteral("CASE%1PACK%2").arg(caseCode, circleCode);
        if (packCreepEnabled && packCreepOk
            && std::abs(packCreep) >= solverTolerance)
            lastDirectory = QStringLiteral("CASE%1PLIU%2").arg(caseCode, circleCode);
        if (fillEnabled && waterStartOk && waterEndOk
            && std::abs(waterStart - waterEnd) >= solverTolerance)
            lastDirectory = QStringLiteral("CASE%1FILL%2").arg(caseCode, circleCode);
        if (fillCreepEnabled && fillCreepOk
            && std::abs(fillCreep) >= solverTolerance)
            lastDirectory = QStringLiteral("CASE%1FLIU%2").arg(caseCode, circleCode);
    }

    if (usesWetting() && m_wetTable && m_wetTable->rowCount() > 0)
        lastDirectory = QStringLiteral("CASE%1WETT").arg(caseCode);
    if (usesWaterDrop() && m_dropTable && m_dropTable->rowCount() > 0)
    {
        if (m_dropTable->rowCount() > 99)
            return {};
        lastDirectory = QStringLiteral("CASE%1DROP%2")
                            .arg(caseCode, twoDigits(m_dropTable->rowCount()));
    }
    return lastDirectory;
}

bool FillStorageCyclePage::hasEffectiveWaterFill(int oneBasedCircle) const
{
    if (!m_circleTable || oneBasedCircle <= 0 || oneBasedCircle > m_circleTable->rowCount())
        return false;
    const int row = oneBasedCircle - 1;
    if (tableItemText(m_circleTable, row, 8, QStringLiteral("0")) != QStringLiteral("1"))
        return false;
    bool startOk = false;
    bool endOk = false;
    const double start = tableItemText(m_circleTable, row, 3, QString()).toDouble(&startOk);
    const double end = tableItemText(m_circleTable, row, 4, QString()).toDouble(&endOk);
    return startOk && endOk && std::isfinite(start) && std::isfinite(end)
           && std::abs(start - end) >= 1.0e-3;
}

bool FillStorageCyclePage::lastCycleHasReservoirCreep() const
{
    return m_circleTable && m_circleTable->rowCount() > 0
           && tableItemText(m_circleTable, m_circleTable->rowCount() - 1, 9,
                            QStringLiteral("0")) == QStringLiteral("1");
}

void FillStorageCyclePage::setPreparedDynamicStateDirectory(const QString& directory)
{
    m_preparedDynamicStateDirectory = directory.trimmed();
}

void FillStorageCyclePage::clearPreparedDynamicStateDirectory()
{
    m_preparedDynamicStateDirectory.clear();
}

bool FillStorageCyclePage::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (needsSeepageAssociation())
    {
        const QString sourceUuid = seepageSourceUuid();
        if (sourceUuid.isEmpty())
            return fail(QStringLiteral("已选择读取渗流结果，请先关联一个渗流工况。"));
        if (!m_availableSeepageSourceUuids.contains(sourceUuid))
            return fail(QStringLiteral("原关联渗流工况已被删除，请重新选择渗流工况。"));

        if (seepageResultType() == QStringLiteral("transient"))
        {
            const QString step = seepageTransientStep();
            if (step.isEmpty())
                return fail(QStringLiteral("请选择一个明确的非稳态结果步。"));
            if (step == QStringLiteral("final"))
            {
                if (!m_hasTransientFinalResult)
                    return fail(QStringLiteral("关联工况没有可用的非稳态最终结果。"));
            }
            else
            {
                bool stepOk = false;
                const int stepNumber = step.toInt(&stepOk);
                if (!stepOk || !m_availableTransientSteps.contains(stepNumber))
                    return fail(QStringLiteral("所选非稳态结果步已不存在，请重新选择。"));
            }
        }
    }

    if (cycleCount() > 99)
        return fail(QStringLiteral("当前求解器的施工循环目录编号仅支持 01～99；单个工况最多设置 99 个填筑蓄水循环。"));
    if (m_dropTable && m_dropTable->rowCount() > 99)
        return fail(QStringLiteral("当前求解器的水位骤降目录编号仅支持 01～99；最多设置 99 组水位骤降。"));

    const bool modeEnabled = usesModalAnalysis();
    const bool waveEnabled = usesWaveAnalysis();
    const bool settlementEnabled = usesPermanentSettlement();
    if (settlementEnabled && !waveEnabled)
        return fail(QStringLiteral("地震永久沉降必须与本次地震波时程分析同时启用。"));

    const bool hasTimedTask = waveEnabled || settlementEnabled;
    if (hasTimedTask)
    {
        bool dtOk = false;
        bool startOk = false;
        bool endOk = false;
        const double dt = valueOf(QStringLiteral("DT")).toDouble(&dtOk);
        const double start = valueOf(QStringLiteral("TSTA")).toDouble(&startOk);
        const double end = valueOf(QStringLiteral("TEND")).toDouble(&endOk);
        if (!dtOk || dt <= 0.0)
            return fail(QStringLiteral("计算时步必须为正数。"));
        if (!startOk || !endOk || end <= start)
            return fail(QStringLiteral("终止时间必须大于起始时间。"));
    }

    if (waveEnabled && !modeEnabled)
    {
        bool omegaOk = false;
        const double omega = valueOf(QStringLiteral("WOMI_1")).toDouble(&omegaOk);
        if (!omegaOk || !std::isfinite(omega) || omega < 0.001)
            return fail(QStringLiteral(
                "未启用模态分析时，地震波时程分析必须指定一阶圆频率 ω1（不小于 0.001）。"));
    }

    if (valueOf(QStringLiteral("IFWET")) == QStringLiteral("1"))
    {
        QString mappingError;
        if (!validateMaterialMappings(m_wetTable, QStringLiteral("浸润材料关系"), true,
                                      &mappingError))
            return fail(mappingError);
        // 求解器的浸润分支固定从 CASE##FILL##(NCIRCLE) 读取初始状态。
        if (cycleCount() <= 0 || !hasEffectiveWaterFill(cycleCount()))
            return fail(QStringLiteral(
                "启用浸润变形时，最后一个填筑蓄水循环必须实际执行蓄水并形成 FILL 结果。"));
        if (tableItemText(m_circleTable, cycleCount() - 1, 9, QStringLiteral("0"))
            == QStringLiteral("1"))
            return fail(QStringLiteral(
                "当前求解器的浸润分支固定读取最后一次蓄水 FILL 状态，不能与最后循环的蓄水期流变连续组合；"
                "请二选一进行测试。"));
    }
    if (valueOf(QStringLiteral("IFDROP")) == QStringLiteral("1"))
    {
        if (!m_dropTable || m_dropTable->rowCount() == 0)
            return fail(QStringLiteral("启用水位骤降后，至少需要一组骤降前后水位。"));
        for (int row = 0; row < m_dropTable->rowCount(); ++row)
        {
            bool startOk = false;
            bool endOk = false;
            const double startLevel = tableItemText(m_dropTable, row, 0, QString()).toDouble(&startOk);
            const double endLevel = tableItemText(m_dropTable, row, 1, QString()).toDouble(&endOk);
            if (!startOk || !endOk || !std::isfinite(startLevel) || !std::isfinite(endLevel))
                return fail(QStringLiteral("水位骤降第 %1 组必须填写有效水位。").arg(row + 1));
            if (startLevel < endLevel)
                return fail(QStringLiteral(
                    "水位骤降第 %1 组的骤降前水位不能低于骤降后水位。").arg(row + 1));
        }
    }
    if (!m_circleTable || m_circleTable->rowCount() == 0)
        return fail(QStringLiteral("至少需要一个填筑蓄水阶段。"));

    // IFIIWL_BODY=2 是工况级开关，而 CAHEAD/CABODY 是逐施工循环给出的文件名。
    // 当前界面只允许明确选择一组渗流结果，因此只在最后一个实际蓄水阶段施加一次。
    // 求解器的 WATER DROP(ITASK=5) 也会再次进入 FILLFBLOAD2，但没有独立的骤降后
    // 渗流场输入；与单一关联场组合会重复使用同一场，不能可靠闭环。
    if (usesImportedSeepageBody())
    {
        const int finalCycle = m_circleTable->rowCount();
        if (!hasEffectiveWaterFill(finalCycle))
            return fail(QStringLiteral(
                "读取渗流结果作为坝体渗流体力时，最后一个填筑蓄水阶段必须实际执行蓄水；"
                "所选节点水头和节点体力只在该阶段施加一次。"));
        if (usesWaterDrop())
            return fail(QStringLiteral(
                "当前求解器的水位骤降分支没有独立的骤降后渗流场输入，不能与“读取渗流结果（水头 + 节点体力）”可靠组合；"
                "请关闭其中一项分别测试。"));
    }

    for (int row = 0; row < m_circleTable->rowCount(); ++row)
    {
        bool packCreepOk = false;
        bool waterStartOk = false;
        bool waterEndOk = false;
        bool fillCreepOk = false;
        const double packCreepTime =
            tableItemText(m_circleTable, row, 2, QString()).toDouble(&packCreepOk);
        const double waterStart =
            tableItemText(m_circleTable, row, 3, QString()).toDouble(&waterStartOk);
        const double waterEnd =
            tableItemText(m_circleTable, row, 4, QString()).toDouble(&waterEndOk);
        const double fillCreepTime =
            tableItemText(m_circleTable, row, 5, QString()).toDouble(&fillCreepOk);
        if (!packCreepOk || !waterStartOk || !waterEndOk || !fillCreepOk
            || !std::isfinite(packCreepTime) || !std::isfinite(waterStart)
            || !std::isfinite(waterEnd) || !std::isfinite(fillCreepTime))
            return fail(QStringLiteral("第 %1 个填筑蓄水阶段包含无效数值。").arg(row + 1));

        int enabledOperations = 0;
        for (int column = 6; column <= 9; ++column)
        {
            const QString flag = tableItemText(m_circleTable, row, column, QStringLiteral("0"));
            if (flag != QStringLiteral("0") && flag != QStringLiteral("1"))
                return fail(QStringLiteral("第 %1 个填筑蓄水阶段的计算开关只能为 0 或 1。")
                                .arg(row + 1));
            if (flag == QStringLiteral("1"))
                ++enabledOperations;
        }
        if (enabledOperations == 0)
            return fail(QStringLiteral("第 %1 个填筑蓄水阶段至少应启用一种计算。")
                            .arg(row + 1));
        if (tableItemText(m_circleTable, row, 8, QStringLiteral("0")) == QStringLiteral("1"))
        {
            if (waterEnd < waterStart)
                return fail(QStringLiteral("第 %1 个阶段是蓄水计算，结束水位不能低于起始水位；降水请使用“水位骤降”。")
                                .arg(row + 1));
            if (std::abs(waterEnd - waterStart) < 1.0e-3)
                return fail(QStringLiteral("第 %1 个阶段启用蓄水计算后，起始水位与结束水位不能相同。")
                                .arg(row + 1));
        }
        if (tableItemText(m_circleTable, row, 7, QStringLiteral("0")) == QStringLiteral("1")
            && packCreepTime <= 0.0)
            return fail(QStringLiteral("第 %1 个阶段启用填筑期流变后，流变时间必须大于 0。")
                            .arg(row + 1));
        if (tableItemText(m_circleTable, row, 9, QStringLiteral("0")) == QStringLiteral("1")
            && fillCreepTime <= 0.0)
            return fail(QStringLiteral("第 %1 个阶段启用蓄水期流变后，流变时间必须大于 0。")
                            .arg(row + 1));
    }

    const bool dynamicEnabled = modeEnabled || waveEnabled;
    if (dynamicEnabled)
    {
        QString mappingError;
        if (!validateMaterialMappings(m_dynamicMaterialTable,
                                      QStringLiteral("动力材料关系"),
                                      false, &mappingError))
            return fail(mappingError);
    }

    QString layerError;
    if (fillLayerRanges(&layerError).isEmpty())
        return fail(layerError);

    return true;
}

QVector<QPair<int, int>> FillStorageCyclePage::fillLayerRanges(QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();

    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return QVector<QPair<int, int>>{};
    };

    if (!m_circleTable || m_circleTable->rowCount() <= 0)
        return fail(QStringLiteral("至少需要一个填筑蓄水阶段。"));

    QVector<QPair<int, int>> ranges;
    ranges.reserve(m_circleTable->rowCount());
    for (int row = 0; row < m_circleTable->rowCount(); ++row)
    {
        bool startOk = false;
        bool endOk = false;
        const int startLayer = tableItemText(m_circleTable, row, 0, QString()).toInt(&startOk);
        const int endLayer = tableItemText(m_circleTable, row, 1, QString()).toInt(&endOk);
        if (!startOk || !endOk)
        {
            return fail(QStringLiteral("第 %1 个阶段的填筑开始层和结束层必须为整数。")
                            .arg(row + 1));
        }
        if (startLayer < 2)
        {
            return fail(QStringLiteral("第 %1 个阶段的填筑开始层不能小于 2；第 1 层为坝基。")
                            .arg(row + 1));
        }
        if (endLayer < startLayer)
        {
            return fail(QStringLiteral("第 %1 个阶段的填筑结束层不能小于开始层。")
                            .arg(row + 1));
        }
        ranges.push_back(qMakePair(startLayer, endLayer));
    }
    return ranges;
}

void FillStorageCyclePage::setFillLayerRange(int firstLayer, int lastLayer)
{
    if (!m_circleTable || firstLayer < 2 || lastLayer < firstLayer)
        return;
    if (m_circleTable->rowCount() > 1)
        return;
    if (m_circleTable->rowCount() == 0)
        m_circleTable->setRowCount(1);

    auto setCell = [this](int column, int value)
    {
        if (QTableWidgetItem* item = m_circleTable->item(0, column))
            item->setText(QString::number(value));
        else
            m_circleTable->setItem(0, column, new QTableWidgetItem(QString::number(value)));
    };
    setCell(0, firstLayer);
    setCell(1, lastLayer);
    updatePreview();
    emit requirementsChanged();
}

QVector<QPair<int, int>> FillStorageCyclePage::materialMappings(QTableWidget* table) const
{
    QVector<QPair<int, int>> mappings;
    if (!table)
        return mappings;
    mappings.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); ++row)
    {
        bool sourceOk = false;
        bool targetOk = false;
        const int source = tableItemText(table, row, 0, QString()).toInt(&sourceOk);
        const int target = tableItemText(table, row, 1, QString()).toInt(&targetOk);
        mappings.push_back({sourceOk ? source : 0, targetOk ? target : 0});
    }
    return mappings;
}

void FillStorageCyclePage::setMaterialMappings(
    QTableWidget* table, const QVector<QPair<int, int>>& mappings)
{
    if (!table)
        return;
    table->setRowCount(0);
    for (const auto& mapping : mappings)
        addMaterialMappingRow(table, mapping.first, mapping.second);
    updateMaterialTableHeight(table);
}

void FillStorageCyclePage::addMaterialMappingRow(QTableWidget* table, int sourceMaterial,
                                                 int targetMaterial)
{
    if (!table)
        return;

    const bool userRequestedRow = sourceMaterial <= 0 || targetMaterial <= 0;
    if (userRequestedRow && table->rowCount() >= MAX_MATERIAL_RELATIONS)
    {
        QMessageBox::information(
            this, QStringLiteral("数量已达上限"),
            QStringLiteral("材料关系最多可设置 300 组。"));
        return;
    }

    const QMap<int, QString> sourceOptions = materialSourceOptions();
    if (sourceMaterial <= 0)
    {
        QSet<int> usedSources;
        for (const auto& mapping : materialMappings(table))
            usedSources.insert(mapping.first);
        for (auto it = sourceOptions.cbegin(); it != sourceOptions.cend(); ++it)
        {
            if (!usedSources.contains(it.key()))
            {
                sourceMaterial = it.key();
                break;
            }
        }
        if (sourceMaterial <= 0 && !sourceOptions.isEmpty())
            sourceMaterial = sourceOptions.cbegin().key();
    }

    const QMap<int, QString> targetOptions = materialTargetOptions(table, sourceMaterial);
    if (targetMaterial <= 0 && !targetOptions.isEmpty())
        targetMaterial = targetOptions.cbegin().key();

    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < 2; ++column)
    {
        auto* item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    }

    auto* sourceCombo = new QComboBox(table);
    auto* targetCombo = new QComboBox(table);
    populateMaterialCombo(sourceCombo, sourceOptions, sourceMaterial,
                          QStringLiteral("暂无已施加的坝体材料"), true);
    populateMaterialCombo(
        targetCombo, targetOptions, targetMaterial,
        table == m_wetTable ? QStringLiteral("请先准备浸润后材料")
                            : QStringLiteral("请先添加坝料动力本构材料"),
        true);
    table->setCellWidget(row, 0, sourceCombo);
    table->setCellWidget(row, 1, targetCombo);

    connect(sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, table, sourceCombo, targetCombo]()
            {
                int row = -1;
                for (int candidate = 0; candidate < table->rowCount(); ++candidate)
                {
                    if (table->cellWidget(candidate, 0) == sourceCombo)
                    {
                        row = candidate;
                        break;
                    }
                }

                const int source = sourceCombo->currentData().toInt();
                const int oldTarget = targetCombo->currentData().toInt();
                const QMap<int, QString> options = materialTargetOptions(table, source);
                const int target = options.contains(oldTarget)
                    ? oldTarget
                    : (options.isEmpty() ? 0 : options.cbegin().key());
                populateMaterialCombo(
                    targetCombo, options, target,
                    table == m_wetTable ? QStringLiteral("请先准备浸润后材料")
                                        : QStringLiteral("请先添加坝料动力本构材料"),
                    false);
                if (row >= 0)
                {
                    table->setCurrentCell(row, 0);
                    refreshMaterialMappingHighlight(table, row);
                }
                updatePreview();
            });
    connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, table, targetCombo]()
            {
                for (int row = 0; row < table->rowCount(); ++row)
                {
                    if (table->cellWidget(row, 1) == targetCombo)
                    {
                        table->setCurrentCell(row, 1);
                        refreshMaterialMappingHighlight(table, row);
                        break;
                    }
                }
                updatePreview();
            });

    auto activateRow = [this, table](QComboBox* combo, int column)
    {
        for (int candidate = 0; candidate < table->rowCount(); ++candidate)
        {
            if (table->cellWidget(candidate, column) == combo)
            {
                table->setCurrentCell(candidate, column);
                refreshMaterialMappingHighlight(table, candidate);
                break;
            }
        }
    };
    connect(sourceCombo, qOverload<int>(&QComboBox::activated), this,
            [activateRow, sourceCombo](int) { activateRow(sourceCombo, 0); });
    connect(targetCombo, qOverload<int>(&QComboBox::activated), this,
            [activateRow, targetCombo](int) { activateRow(targetCombo, 1); });

    table->setCurrentCell(row, 0);
    refreshMaterialMappingHighlight(table, row);
    updateMaterialTableHeight(table);
    updatePreview();
}

void FillStorageCyclePage::removeSelectedMaterialMapping(QTableWidget* table)
{
    if (!table)
        return;
    int row = table->currentRow();
    const auto selectedRows = table->selectionModel()->selectedRows();
    if (!selectedRows.isEmpty())
        row = selectedRows.first().row();
    if (row < 0)
        return;
    table->removeRow(row);
    if (table->rowCount() > 0)
    {
        const int nextRow = qMin(row, table->rowCount() - 1);
        table->setCurrentCell(nextRow, 0);
        refreshMaterialMappingHighlight(table, nextRow);
    }
    else
    {
        clearMaterialMappingHighlight();
    }
    updateMaterialTableHeight(table);
    updatePreview();
}

void FillStorageCyclePage::refreshMaterialMappingOptions(QTableWidget* table)
{
    if (!table)
        return;
    const auto mappings = materialMappings(table);
    setMaterialMappings(table, mappings);
}

QMap<int, QString> FillStorageCyclePage::materialSourceOptions() const
{
    if (auto* materialService =
            ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        return materialService->damStaticMaterialLabels();
    }
    return {};
}

QMap<int, QString> FillStorageCyclePage::materialTargetOptions(QTableWidget* table,
                                                               int sourceMaterial) const
{
    auto* materialService =
        ServiceRegistry::instance().getService<MaterialManagerService>();
    if (!materialService)
        return {};
    if (table == m_wetTable)
        return materialService->damWetMaterialLabels(sourceMaterial);
    if (table == m_dynamicMaterialTable)
        return materialService->damDynamicMaterialLabels(sourceMaterial);
    return {};
}

void FillStorageCyclePage::populateMaterialCombo(QComboBox* combo,
                                                  const QMap<int, QString>& options,
                                                  int currentMaterial,
                                                  const QString& emptyText,
                                                  bool retainUnavailable) const
{
    if (!combo)
        return;

    const QSignalBlocker blocker(combo);
    combo->clear();
    for (auto it = options.cbegin(); it != options.cend(); ++it)
    {
        combo->addItem(it.value(), it.key());
        combo->setItemData(combo->count() - 1, it.value(), Qt::ToolTipRole);
    }

    int currentIndex = combo->findData(currentMaterial);
    if (currentIndex < 0 && retainUnavailable && currentMaterial > 0)
    {
        const QString unavailable =
            QStringLiteral("编号 %1  当前不可用").arg(currentMaterial);
        combo->insertItem(0, unavailable, currentMaterial);
        combo->setItemData(0, unavailable, Qt::ToolTipRole);
        currentIndex = 0;
    }

    if (combo->count() == 0)
    {
        combo->addItem(emptyText, QVariant());
        combo->setItemData(0, emptyText, Qt::ToolTipRole);
        combo->setEnabled(false);
        SolverUiLayoutHelper::configureComboSizing(combo);
        return;
    }

    combo->setEnabled(true);
    combo->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);
    SolverUiLayoutHelper::configureComboSizing(combo);
}

void FillStorageCyclePage::refreshMaterialMappingHighlight(QTableWidget* table, int row)
{
    // 选中状态交给全局表格样式绘制，不再为单个下拉框写局部 QSS。
    // 三维材料域的预览/已应用颜色仍由 PickManager 维护。
    for (QTableWidget* candidateTable : {m_wetTable, m_dynamicMaterialTable})
    {
        if (candidateTable && candidateTable != table)
            candidateTable->clearSelection();
    }
    if (table && row >= 0 && row < table->rowCount())
        table->selectRow(row);

    m_activeMaterialTable = table;
    if (!m_materialHighlightActive || !table || row < 0 || row >= table->rowCount())
    {
        if (m_materialHighlightActive)
            clearMaterialMappingHighlight();
        return;
    }

    const int source = tableItemText(table, row, 0, QString()).toInt();
    const int target = tableItemText(table, row, 1, QString()).toInt();
    const DomainKey sourceKey = materialDomainKey(source);
    const DomainKey targetKey = materialDomainKey(target);

    QSet<DomainKey> applied;
    if (sourceKey.isValid())
        applied.insert(sourceKey);
    if (targetKey.isValid())
        applied.insert(targetKey);

    auto& pickManager = PickManager::instance();
    pickManager.activate(getPageType(), m_uuid, PickMode::Domain);
    pickManager.setAppliedDomains(getPageType(), m_uuid, applied);
    pickManager.setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    pickManager.setPreviewDomain(getPageType(), m_uuid, sourceKey);
    pickManager.setPreDeleteDomain(getPageType(), m_uuid, targetKey);
}

void FillStorageCyclePage::clearMaterialMappingHighlight()
{
    auto& pickManager = PickManager::instance();
    pickManager.setAppliedDomains(getPageType(), m_uuid, {});
    pickManager.setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    pickManager.setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    pickManager.setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
}

void FillStorageCyclePage::updateMaterialTableHeight(QTableWidget* table) const
{
    if (!table)
        return;
    const int visibleRows = qBound(2, table->rowCount(), 5);
    const int headerHeight = table->horizontalHeader()->sizeHint().height();
    const int rowHeight = table->verticalHeader()->defaultSectionSize();
    const int frameHeight = table->frameWidth() * 2 + 2;
    const int compactHeight = headerHeight + qMin(2, qMax(1, table->rowCount())) * rowHeight
                              + frameHeight;
    const int preferredHeight = headerHeight + visibleRows * rowHeight + frameHeight;
    table->setMinimumHeight(compactHeight);
    table->setMaximumHeight(preferredHeight);
}

bool FillStorageCyclePage::validateMaterialMappings(QTableWidget* table,
                                                    const QString& relationName,
                                                    bool required,
                                                    QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    const auto mappings = materialMappings(table);
    if (mappings.isEmpty())
    {
        if (!required)
            return true;
        if (errorMessage)
            *errorMessage = QStringLiteral("%1至少需要一组。").arg(relationName);
        return false;
    }
    if (mappings.size() > MAX_MATERIAL_RELATIONS)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1不能超过 300 组。").arg(relationName);
        return false;
    }

    const QMap<int, QString> sourceOptions = materialSourceOptions();

    QSet<int> usedSources;
    QSet<int> usedTargets;
    for (int row = 0; row < mappings.size(); ++row)
    {
        const int source = mappings.at(row).first;
        const int target = mappings.at(row).second;
        const QString prefix = QStringLiteral("%1第 %2 组：").arg(relationName).arg(row + 1);
        auto fail = [errorMessage, prefix](const QString& message)
        {
            if (errorMessage)
                *errorMessage = prefix + message;
            return false;
        };
        if (source <= 0 || target <= 0)
            return fail(QStringLiteral("材料选择不完整。"));
        if (source == target)
            return fail(QStringLiteral("前后材料不能相同。"));
        if (usedSources.contains(source))
            return fail(QStringLiteral("同一种当前材料不能重复设置。"));
        if (table == m_dynamicMaterialTable && usedTargets.contains(target))
            return fail(QStringLiteral("同一种动力及地震波动材料不能对应多个静力材料。"));
        if (!sourceOptions.contains(source))
            return fail(QStringLiteral("左侧材料不是已施加到坝体网格的静力材料，请重新选择。"));
        const QMap<int, QString> targetOptions = materialTargetOptions(table, source);
        if (!targetOptions.contains(target))
        {
            return fail(table == m_wetTable
                            ? QStringLiteral("右侧材料必须是已启用的坝体计算阶段材料。")
                            : QStringLiteral("右侧材料必须是已启用的坝料动力及地震波动计算阶段材料。"));
        }
        if (table == m_dynamicMaterialTable)
        {
            auto* materialService =
                ServiceRegistry::instance().getService<MaterialManagerService>();
            if (!materialService ||
                !materialService->setDamDynamicSourceMatype(target, source))
            {
                return fail(QStringLiteral("无法保存动力材料关系。请检查材料设置后重试。"));
            }
        }
        usedSources.insert(source);
        usedTargets.insert(target);
    }
    return true;
}

QString FillStorageCyclePage::valueOf(const QString& key) const
{
    auto it = m_editors.constFind(key);
    if (it == m_editors.constEnd())
        return QString();
    if (auto* combo = qobject_cast<QComboBox*>(it.value()))
        return combo->currentData().toString().trimmed();
    if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
        return edit->text().trimmed();
    return QString();
}

void FillStorageCyclePage::setValueOf(const QString& key, const QString& value)
{
    auto it = m_editors.find(key);
    if (it == m_editors.end())
        return;
    if (auto* combo = qobject_cast<QComboBox*>(it.value()))
    {
        const int idx = combo->findData(value.trimmed());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        return;
    }
    if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
        edit->setText(value);
}

void FillStorageCyclePage::updatePreview()
{
    ui->labelWetRows->setText(QStringLiteral("共 %1 组").arg(m_wetTable ? m_wetTable->rowCount() : 0));
    ui->labelDropRows->setText(QStringLiteral("共 %1 组").arg(m_dropTable ? m_dropTable->rowCount() : 0));
    ui->labelCircleRows->setText(
        QStringLiteral("共 %1 个阶段").arg(m_circleTable ? m_circleTable->rowCount() : 0));
    if (m_dynamicMaterialSummary)
        m_dynamicMaterialSummary->setText(
            QStringLiteral("共 %1 组").arg(m_dynamicMaterialTable
                                               ? m_dynamicMaterialTable->rowCount()
                                               : 0));
    const bool wetEnabled = ui->groupWet->isChecked();
    if (m_removeWetMaterialButton)
        m_removeWetMaterialButton->setEnabled(
            wetEnabled && m_wetTable && m_wetTable->rowCount() > 0);
    const bool dynamicMaterialMappingEnabled =
        m_dynamicOptionsAvailable &&
        (ui->comboIFMODE->currentData().toString() == QStringLiteral("1") ||
         ui->comboIFWAVE->currentData().toString() == QStringLiteral("1"));
    if (m_removeDynamicMaterialButton)
        m_removeDynamicMaterialButton->setEnabled(
            dynamicMaterialMappingEnabled && m_dynamicMaterialTable &&
            m_dynamicMaterialTable->rowCount() > 0);
    updateMaterialTableHeight(m_wetTable);
    updateMaterialTableHeight(m_dynamicMaterialTable);
}

QString FillStorageCyclePage::sectionText() const
{
    return QStringLiteral("1    NCASE\n"
                          " ----------------------------------------------\n")
           + sectionText(1);
}

QString FillStorageCyclePage::sectionText(int caseIndex) const
{
    QString text;
    text += QString("%1    ICASE--------------------------------------\n").arg(caseIndex);
    // IFIIWL_BODY=1 在现有求解器中会主动报错，始终只导出 0 或 2。
    const QString bodyMode = usesImportedSeepageBody()
                                 ? QStringLiteral("2")
                                 : QStringLiteral("0");
    text += QString("0 %1 %2     IFIIWL_SEEP IFIIWL_BODY IFIIWL_FACE\n")
                .arg(bodyMode, valueOf("IFIIWL_FACE"));
    const int numWet = valueOf("IFWET") == QStringLiteral("1") && m_wetTable
        ? m_wetTable->rowCount() : 0;
    text += QString("%1 %2     NUM_WET IFWET\n").arg(numWet).arg(valueOf("IFWET"));
    for (int i = 0; i < numWet; ++i)
        text += QString("%1 %2 %3\n")
                    .arg(tableItemText(m_wetTable, i, 0, "1"),
                         tableItemText(m_wetTable, i, 1, "1"))
                    .arg(i + 1);

    const int numDrop = valueOf("IFDROP") == QStringLiteral("1") && m_dropTable
        ? m_dropTable->rowCount() : 0;
    text += QString("%1 %2     NUM_DROP IFDROP\n").arg(numDrop).arg(valueOf("IFDROP"));
    for (int i = 0; i < numDrop; ++i)
        text += QString("%1 %2 %3\n")
                    .arg(tableItemText(m_dropTable, i, 0, "0.000"),
                         tableItemText(m_dropTable, i, 1, "0.000"))
                    .arg(i + 1);

    text += "0     NCHANGE\n";
    const int nCircle = m_circleTable ? m_circleTable->rowCount() : 0;
    text += QString("%1  NCIRCLE\n").arg(nCircle);
    text += "JPACK_STA JPACK_END TT_PACK HWJ_STA HWJ_END TT_FILL IF_PACK IF_PLIU IF_FILL IF_FLIU CAHEAD CABODY\n";
    for (int i = 0; i < nCircle; ++i)
    {
        // 一个工况只明确选择一组渗流结果，因此将它施加在本工况的
        // 最后一个实际蓄水循环；validateInputs() 已保证最后一行确实执行 FILL。
        // 前序阶段保持 NONE，避免同一渗流场在多个增量中被重复施加。
        const bool applySeepageResult =
            usesImportedSeepageBody() && i == nCircle - 1;
        text += QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13\n")
                    // 第一组：处理前 9 个占位符
                    .arg(tableItemText(m_circleTable, i, 0, "0"),
                         tableItemText(m_circleTable, i, 1, "0"),
                         tableItemText(m_circleTable, i, 2, "0.0000"),
                         tableItemText(m_circleTable, i, 3, "0.0000"),
                         tableItemText(m_circleTable, i, 4, "0.0000"),
                         tableItemText(m_circleTable, i, 5, "0.0000"),
                         tableItemText(m_circleTable, i, 6, "0"),
                         tableItemText(m_circleTable, i, 7, "0"),
                         tableItemText(m_circleTable, i, 8, "0"))
                    // 第二组：处理剩下的 4 个占位符 (%10, %11, %12, %13)
                    .arg(tableItemText(m_circleTable, i, 9, "0"),
                         applySeepageResult && !m_preparedHeadFile.isEmpty()
                             ? QStringLiteral("'%1'").arg(m_preparedHeadFile)
                             : QStringLiteral("'NONE'"),
                         applySeepageResult && !m_preparedBodyFile.isEmpty()
                             ? QStringLiteral("'%1'").arg(m_preparedBodyFile)
                             : QStringLiteral("'NONE'"),
                         QString::number(i + 1));
    }

    QString dynamicStateDirectory = m_preparedDynamicStateDirectory;
    if (dynamicStateDirectory.isEmpty())
        dynamicStateDirectory = finalStaticStateDirectoryName(caseIndex, 0);
    if (dynamicStateDirectory.isEmpty())
        dynamicStateDirectory = QStringLiteral("DISK00");
    text += QStringLiteral("'%1'     DIRDYNA\n").arg(dynamicStateDirectory);
    QVector<QPair<int, int>> dynamicMappings;
    if (valueOf("IFMODE") == QStringLiteral("1") ||
        valueOf("IFWAVE") == QStringLiteral("1"))
        dynamicMappings = materialMappings(m_dynamicMaterialTable);
    text += QString("%1   NUM_DYNA\n").arg(dynamicMappings.size());
    for (int i = 0; i < dynamicMappings.size(); ++i)
    {
        text += QString("%1 %2 %3   MACHAN MACHAN II DYNA\n")
                    .arg(dynamicMappings.at(i).first)
                    .arg(dynamicMappings.at(i).second)
                    .arg(i + 1);
    }

    text += QString("%1 %2 %3   IFMODE   IFWAVE     IFSETT\n")
                .arg(valueOf("IFMODE"), valueOf("IFWAVE"), valueOf("IFSETT"));
    text += QString("%1 %2 %3 %4    DT TSTA TEND WOMI_1\n")
                .arg(valueOf("DT"), valueOf("TSTA"), valueOf("TEND"), valueOf("WOMI_1"));
    text += "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ\n";
    return text;
}

QJsonObject FillStorageCyclePage::saveJson()
{
    QJsonObject obj;
    QJsonObject values;
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it)
    {
        if (auto* combo = qobject_cast<QComboBox*>(it.value()))
            values[it.key()] = combo->currentData().toString();
        else if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
            values[it.key()] = edit->text();
    }
    obj["values"] = values;
    obj["wet_table"] = tableToJson(m_wetTable);
    obj["dynamic_material_table"] = tableToJson(m_dynamicMaterialTable);
    obj["drop_table"] = tableToJson(m_dropTable);
    obj["circle_table"] = tableToJson(m_circleTable);
    obj["seepage_source_uuid"] = seepageSourceUuid();
    obj["seepage_source_name"] = seepageSourceName();
    obj["seepage_result_type"] = seepageResultType();
    obj["seepage_transient_step"] = seepageTransientStep();
    return obj;
}

void FillStorageCyclePage::loadJson(const QJsonObject& obj)
{
    const QJsonObject values = obj.value("values").toObject();
    for (auto it = m_editors.begin(); it != m_editors.end(); ++it)
    {
        QString current;
        if (auto* combo = qobject_cast<QComboBox*>(it.value()))
            current = combo->currentData().toString();
        else if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
            current = edit->text();
        QString savedValue = values.value(it.key()).toString(current);
        // 当前仅提供均质坝：坝基渗透荷载在求解器中固定为 0。
        if (it.key() == QStringLiteral("IFIIWL_SEEP"))
            savedValue = QStringLiteral("0");
        // 旧工程中的 IFIIWL_BODY=1 对应求解器已停用分支，按“不考虑”迁移。
        if (it.key() == QStringLiteral("IFIIWL_BODY") &&
            savedValue.trimmed() == QStringLiteral("1"))
            savedValue = QStringLiteral("0");
        setValueOf(it.key(), savedValue);
    }

    auto mappingsFromJson = [](const QJsonArray& rows)
    {
        QVector<QPair<int, int>> mappings;
        mappings.reserve(rows.size());
        for (const auto& value : rows)
        {
            int source = 0;
            int target = 0;
            if (value.isArray())
            {
                const QJsonArray columns = value.toArray();
                source = columns.size() > 0 ? columns.at(0).toVariant().toInt() : 0;
                target = columns.size() > 1 ? columns.at(1).toVariant().toInt() : 0;
            }
            else if (value.isObject())
            {
                const QJsonObject mapping = value.toObject();
                source = mapping.value("source").toInt();
                target = mapping.value("target").toInt();
            }
            if (source > 0 || target > 0)
                mappings.push_back({source, target});
        }
        return mappings;
    };
    setMaterialMappings(m_wetTable, mappingsFromJson(obj.value("wet_table").toArray()));
    setMaterialMappings(m_dynamicMaterialTable,
                        mappingsFromJson(obj.value("dynamic_material_table").toArray()));
    jsonToTable(obj.value("drop_table").toArray(), m_dropTable, 1);
    jsonToTable(obj.value("circle_table").toArray(), m_circleTable, 1);
    m_savedSeepageSourceUuid = obj.value("seepage_source_uuid").toString();
    m_savedSeepageSourceName = obj.value("seepage_source_name").toString();

    QString resultType =
        obj.value("seepage_result_type").toString().trimmed();
    m_savedSeepageTransientStep =
        obj.value("seepage_transient_step").toString().trimmed();

    // 兼容上一版工程，但不再保留“自动匹配”规则。
    if (resultType.isEmpty())
    {
        const QString legacyMode =
            obj.value("seepage_result_mode").toString().trimmed();
        resultType =
            legacyMode.isEmpty() || legacyMode == QStringLiteral("steady")
                ? QStringLiteral("steady")
                : QStringLiteral("transient");
        if (legacyMode == QStringLiteral("transient_final"))
            m_savedSeepageTransientStep = QStringLiteral("final");
        else if (legacyMode == QStringLiteral("auto")
                 || legacyMode == QStringLiteral("transient_stages"))
            m_savedSeepageTransientStep.clear();
    }

    const int resultTypeIndex =
        ui->comboSeepageResultType->findData(
            resultType == QStringLiteral("transient")
                ? QStringLiteral("transient")
                : QStringLiteral("steady"));
    ui->comboSeepageResultType->setCurrentIndex(
        resultTypeIndex >= 0 ? resultTypeIndex : 0);
    updateConditionalVisibility();
    updatePreview();
    emit requirementsChanged();
}

QString FillStorageCyclePage::tableItemText(QTableWidget* table, int row, int col,
                                            const QString& fallback) const
{
    if (!table)
        return fallback;
    if (auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, col)))
        return combo->currentData().toString().trimmed();
    auto* item = table->item(row, col);
    const QString t = item ? item->text().trimmed() : QString();
    return t.isEmpty() ? fallback : t;
}

QJsonArray FillStorageCyclePage::tableToJson(QTableWidget* table) const
{
    QJsonArray rows;
    if (!table)
        return rows;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        QJsonArray cols;
        for (int c = 0; c < table->columnCount(); ++c)
            cols.append(tableItemText(table, r, c, QString()));
        rows.append(cols);
    }
    return rows;
}

void FillStorageCyclePage::jsonToTable(const QJsonArray& arr, QTableWidget* table, int minRows)
{
    if (!table)
        return;
    const int rows = qMax(minRows, arr.size());
    table->setRowCount(rows);
    for (int r = 0; r < rows; ++r)
    {
        // Change .value(r) to .at(r)
        const QJsonArray cols = r < arr.size() ? arr.at(r).toArray() : QJsonArray();

        for (int c = 0; c < table->columnCount(); ++c)
        {
            // Note: QJsonArray also doesn't have .value(c) here!
            // Use .at(c).toString() instead.
            QString val = c < cols.size() ? cols.at(c).toString() : QString();

            if (table->item(r, c))
                table->item(r, c)->setText(val);
            else
                table->setItem(r, c, new QTableWidgetItem(val));
        }
    }
}
