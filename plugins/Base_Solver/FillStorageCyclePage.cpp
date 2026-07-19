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

    ui->comboIFIIWL_SEEP->setItemData(0, "0");
    ui->comboIFIIWL_SEEP->setItemData(1, "1");
    ui->comboIFIIWL_SEEP->setItemData(2, "2");
    ui->comboIFIIWL_BODY->setItemData(0, "0");
    ui->comboIFIIWL_BODY->setItemData(1, "1");
    ui->comboIFIIWL_BODY->setItemData(2, "2");
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
    ui->comboIFIIWL_SEEP->setItemText(2, QStringLiteral("读取节点水头并施加面力"));
    ui->comboIFIIWL_BODY->setItemText(2, QStringLiteral("读取节点体力"));
    ui->labelWOMI1->setText(QStringLiteral("流变初始参数"));

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
                                    &dlg, QStringLiteral("阶段设置不完整"),
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
        QStringLiteral("提示：需启用拟静力或地震波动。"),
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
    ui->groupDynamic->setVisible(true);
    ui->labelDynamicHint->setVisible(!m_dynamicOptionsAvailable);
    if (!m_dynamicOptionsAvailable)
        ui->labelDynamicHint->setText(QStringLiteral("提示：需先启用动力计算。"));
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
    const bool creepEnabled = ui->comboIFSETT->currentData().toString() == QStringLiteral("1");
    const bool dynamicMaterialMappingEnabled =
        m_dynamicOptionsAvailable && (modeEnabled || waveEnabled);
    const bool showTime = m_dynamicOptionsAvailable &&
                          (modeEnabled || waveEnabled || creepEnabled);
    ui->labelDT->setVisible(showTime);
    ui->editDT->setVisible(showTime);
    ui->labelTSTA->setVisible(showTime);
    ui->editTSTA->setVisible(showTime);
    ui->labelTEND->setVisible(showTime);
    ui->editTEND->setVisible(showTime);
    ui->labelWOMI1->setVisible(m_dynamicOptionsAvailable && creepEnabled);
    ui->editWOMI1->setVisible(m_dynamicOptionsAvailable && creepEnabled);
    if (m_dynamicMaterialGroup)
    {
        m_dynamicMaterialGroup->setVisible(true);
        m_dynamicMaterialGroup->setEnabled(true);
    }
    if (m_dynamicMaterialHint)
    {
        m_dynamicMaterialHint->setVisible(!dynamicMaterialMappingEnabled);
        if (!dynamicMaterialMappingEnabled)
        {
            m_dynamicMaterialHint->setText(
                m_dynamicOptionsAvailable
                    ? QStringLiteral("提示：需启用拟静力或地震波动。")
                    : QStringLiteral("提示：需先启用动力计算。"));
        }
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
    return valueOf(QStringLiteral("IFIIWL_SEEP")).toInt() > 0;
}

bool FillStorageCyclePage::usesLinearDamBaseUplift() const
{
    return valueOf(QStringLiteral("IFIIWL_SEEP")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesDamFacePressure() const
{
    return valueOf(QStringLiteral("IFIIWL_FACE")) == QStringLiteral("1");
}

bool FillStorageCyclePage::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    const bool hasTimedTask = m_dynamicOptionsAvailable &&
                              (valueOf(QStringLiteral("IFMODE")) == QStringLiteral("1") ||
                               valueOf(QStringLiteral("IFWAVE")) == QStringLiteral("1") ||
                               valueOf(QStringLiteral("IFSETT")) == QStringLiteral("1"));
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

    if (valueOf(QStringLiteral("IFWET")) == QStringLiteral("1"))
    {
        QString mappingError;
        if (!validateMaterialMappings(m_wetTable, QStringLiteral("浸润材料关系"), true,
                                      &mappingError))
            return fail(mappingError);
    }
    if (valueOf(QStringLiteral("IFDROP")) == QStringLiteral("1") &&
        (!m_dropTable || m_dropTable->rowCount() == 0))
        return fail(QStringLiteral("启用水位骤降后，至少需要一组骤降前后水位。"));
    if (!m_circleTable || m_circleTable->rowCount() == 0)
        return fail(QStringLiteral("至少需要一个填筑蓄水阶段。"));

    const bool dynamicEnabled = valueOf(QStringLiteral("IFMODE")) == QStringLiteral("1") ||
                                valueOf(QStringLiteral("IFWAVE")) == QStringLiteral("1");
    if (dynamicEnabled)
    {
        QString mappingError;
        if (!validateMaterialMappings(m_dynamicMaterialTable,
                                      QStringLiteral("动力及地震波动材料关系"),
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
            QStringLiteral("求解器最多支持 300 组材料关系，不能继续新增。"));
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
                return fail(QStringLiteral("无法更新动力材料的静力母材料编号。"));
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
    return sectionText(1);
}

QString FillStorageCyclePage::sectionText(int caseIndex) const
{
    QString text;
    text += "1    NCASE\n";
    text += " ----------------------------------------------\n";
    text += QString("%1    ICASE--------------------------------------\n").arg(caseIndex);
    text += QString("%1 %2 %3     IFIIWL_SEEP IFIIWL_BODY IFIIWL_FACE\n")
                .arg(valueOf("IFIIWL_SEEP"), valueOf("IFIIWL_BODY"), valueOf("IFIIWL_FACE"));
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
                         tableItemText(m_circleTable, i, 10, "'NONE'"),
                         tableItemText(m_circleTable, i, 11, "'NONE'"),
                         QString::number(i + 1)); // 这里的 i+1 建议显式转为 QString
    }

    text += "'CASE01FILL01'     DIRDYNA\n";
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
        setValueOf(it.key(), values.value(it.key()).toString(current));
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
