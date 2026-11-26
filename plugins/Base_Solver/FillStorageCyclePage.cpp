#include "FillStorageCyclePage.h"
#include <CompactNumberDelegate.h>

#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QIntValidator>
#include <QJsonArray>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkLineSource.h>
#include <vtkPlaneSource.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>

#include "ui_FillStorageCyclePage.h"

#include "SolverUiLayoutHelper.h"

namespace
{
constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr int MAX_MATERIAL_RELATIONS = 300;
constexpr double CYCLE_BRANCH_TOLERANCE = 1.0e-3;

QString cycleTableValue(QTableWidget* table, int row, int column,
                        const QString& fallback = QString())
{
    if (!table || row < 0 || row >= table->rowCount() || column < 0
        || column >= table->columnCount())
        return fallback;
    if (auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, column)))
    {
        const QString value = combo->currentData().toString().trimmed();
        return value.isEmpty() ? fallback : value;
    }
    const QTableWidgetItem* item = table->item(row, column);
    const QString value = item ? item->text().trimmed() : QString();
    return value.isEmpty() ? fallback : value;
}

void setCycleTableValue(QTableWidget* table, int row, int column, const QString& value)
{
    if (!table || row < 0 || row >= table->rowCount() || column < 0
        || column >= table->columnCount())
        return;
    if (auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, column)))
    {
        const QSignalBlocker blocker(combo);
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        return;
    }
    QTableWidgetItem* item = table->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem;
        table->setItem(row, column, item);
    }
    item->setText(value);
}

void synchronizeAutomaticCycleBranches(QTableWidget* table)
{
    if (!table)
        return;

    const QSignalBlocker blocker(table);
    for (int row = 0; row < table->rowCount(); ++row)
    {
        bool startLayerOk = false;
        bool endLayerOk = false;
        bool packCreepOk = false;
        bool waterStartOk = false;
        bool waterEndOk = false;
        bool fillCreepOk = false;

        const int startLayer = cycleTableValue(table, row, 0).toInt(&startLayerOk);
        const int endLayer = cycleTableValue(table, row, 1).toInt(&endLayerOk);
        const double packCreep = cycleTableValue(table, row, 2).toDouble(&packCreepOk);
        const double waterStart = cycleTableValue(table, row, 3).toDouble(&waterStartOk);
        const double waterEnd = cycleTableValue(table, row, 4).toDouble(&waterEndOk);
        const double fillCreep = cycleTableValue(table, row, 5).toDouble(&fillCreepOk);

        const bool packEnabled = startLayerOk && endLayerOk && startLayer >= 2
                                 && endLayer >= startLayer;
        const bool packCreepEnabled = packCreepOk && std::isfinite(packCreep)
                                      && packCreep >= CYCLE_BRANCH_TOLERANCE;
        const bool fillEnabled = waterStartOk && waterEndOk && std::isfinite(waterStart)
                                 && std::isfinite(waterEnd)
                                 && waterEnd - waterStart >= CYCLE_BRANCH_TOLERANCE;
        const bool fillCreepEnabled = fillCreepOk && std::isfinite(fillCreep)
                                      && fillCreep >= CYCLE_BRANCH_TOLERANCE;

        setCycleTableValue(table, row, 6, packEnabled ? QStringLiteral("1")
                                                      : QStringLiteral("0"));
        setCycleTableValue(table, row, 7, packCreepEnabled ? QStringLiteral("1")
                                                           : QStringLiteral("0"));
        setCycleTableValue(table, row, 8, fillEnabled ? QStringLiteral("1")
                                                      : QStringLiteral("0"));
        setCycleTableValue(table, row, 9, fillCreepEnabled ? QStringLiteral("1")
                                                           : QStringLiteral("0"));
    }
}

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

void addStagePreviewLine(VisualWidgetService* visual, const QString& groupName,
                         const QString& id, const QString& name,
                         const double point0[3], const double point1[3],
                         const std::array<double, 3>& color, double width)
{
    if (!visual)
        return;

    auto line = vtkSmartPointer<vtkLineSource>::New();
    line->SetPoint1(point0);
    line->SetPoint2(point1);
    line->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(line->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetColor(color[0], color[1], color[2]);
    actor->GetProperty()->SetLineWidth(width);

    visual->addProp(id, actor, groupName, name, false, false);
}

void addStagePreviewLabel(VisualWidgetService* visual, const QString& groupName,
                          const QString& id, const QString& name,
                          const QString& textValue, const double position[3],
                          const std::array<double, 3>& color, bool alignRight,
                          int fontSize, double backgroundOpacity)
{
    if (!visual)
        return;

    auto label = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    const QByteArray encoded = textValue.toUtf8();
    label->SetInput(encoded.constData());
    label->SetPosition(position[0], position[1], position[2]);
    label->PickableOff();

    vtkTextProperty* property = label->GetTextProperty();
    property->SetFontSize(fontSize);
    property->SetColor(color[0], color[1], color[2]);
    property->SetBackgroundColor(1.0, 1.0, 1.0);
    property->SetBackgroundOpacity(backgroundOpacity);
    property->SetVerticalJustificationToCentered();
    if (alignRight)
        property->SetJustificationToRight();
    else
        property->SetJustificationToLeft();

    visual->addProp(id, label, groupName, name, false, false);
}

bool stagePreviewLevelVisible(double level, const double modelBounds[6],
                              int verticalAxis, double tolerance)
{
    return std::isfinite(level)
           && level >= modelBounds[2 * verticalAxis] - tolerance
           && level <= modelBounds[2 * verticalAxis + 1] + tolerance;
}

void addStageWaterTick(VisualWidgetService* visual, const QString& groupName,
                       const QString& idPrefix, const QString& suffix,
                       const QString& name, const QString& labelText, double level,
                       const std::array<double, 3>& color, bool strong,
                       const double modelBounds[6], int verticalAxis, int sideAxis,
                       int depthAxis, double waterBase, double depthCoordinate,
                       double tickLength, double labelGap)
{
    double outerPoint[3] = {
        0.5 * (modelBounds[0] + modelBounds[1]),
        0.5 * (modelBounds[2] + modelBounds[3]),
        0.5 * (modelBounds[4] + modelBounds[5])};
    outerPoint[verticalAxis] = level;
    outerPoint[sideAxis] = waterBase;
    if (depthAxis >= 0)
        outerPoint[depthAxis] = depthCoordinate;

    double tickEnd[3] = {outerPoint[0], outerPoint[1], outerPoint[2]};
    tickEnd[sideAxis] -= tickLength;

    addStagePreviewLine(visual, groupName, idPrefix + suffix, name,
                        tickEnd, outerPoint, color, strong ? 3.2 : 2.0);

    double labelPosition[3] = {outerPoint[0], outerPoint[1], outerPoint[2]};
    labelPosition[sideAxis] += labelGap;
    addStagePreviewLabel(visual, groupName,
                         idPrefix + suffix + QStringLiteral(".label"),
                         name + QStringLiteral("标注"), labelText,
                         labelPosition, color, false, strong ? 15 : 13,
                         strong ? 0.90 : 0.72);
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
    m_pageActive = false;
    closeConstructionStageEditor();
    if (m_constructionStageEditor)
    {
        QWidget* editor = m_constructionStageEditor.data();
        m_constructionStageEditor = nullptr;
        editor->deleteLater();
    }
    clearConstructionStagePreview(false);
    delete ui;
}

void FillStorageCyclePage::beforeLoad()
{
    m_pageActive = true;
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

    QTimer::singleShot(0, this,
                       [this]()
                       {
                           if (m_pageActive)
                               openConstructionStageEditor();
                       });

    // 结果目录可能刚由渗流计算生成，打开页面时重新扫描可用结果。
    emit requirementsChanged();
}

void FillStorageCyclePage::beforeUnload()
{
    m_pageActive = false;
    closeConstructionStageEditor();
    clearConstructionStagePreview(false);
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

    // 均质坝（IROCKFILLTYPE=4）在求解器中固定忽略 IFIIWL_SEEP；
    // 混凝土面板堆石坝（IROCKFILLTYPE=1）支持 0/1/2 三种坝基扬压力模式。
    ui->comboIFIIWL_SEEP->clear();
    ui->comboIFIIWL_SEEP->addItem(QStringLiteral("不施加坝基扬压力"), QStringLiteral("0"));
    ui->comboIFIIWL_SEEP->addItem(QStringLiteral("按折减系数线性分布"), QStringLiteral("1"));
    ui->comboIFIIWL_SEEP->addItem(QStringLiteral("按渗流场节点水头计算"), QStringLiteral("2"));
    ui->comboIFIIWL_SEEP->setCurrentIndex(0);
    ui->comboIFIIWL_SEEP->setEnabled(false);
    ui->formLayoutStaticLoad->setRowVisible(ui->comboIFIIWL_SEEP, false);
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
    ui->labelWOMI1->setText(QStringLiteral("一阶圆频率 ω1"));
    ui->editModalOrder->setValidator(
        new QIntValidator(1, std::numeric_limits<int>::max(), ui->editModalOrder));
    ui->editModalOrder->setToolTip(
        QStringLiteral("模态阶数请在“动力分析”子节点设置。"));
    ui->editWOMI1->setToolTip(QStringLiteral("未做模态分析时填写。"));

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
    connect(ui->editModalOrder, &QLineEdit::textChanged, this,
            [this](const QString& text)
            {
                bool ok = false;
                const int order = text.trimmed().toInt(&ok);
                if (ok && order > 0)
                    emit modalExtractionOrderChanged(order);
            });
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
    m_dynamicMaterialTable = mkTable({"静力材料", "动力材料"}, false);
    m_dropTable = mkTable({"骤降前水位", "骤降后水位"}, false);
    m_circleTable = mkTable({"填筑起始层", "填筑结束层", "填筑后流变时间", "起始水位", "目标水位",
                             "蓄水后流变时间", "填筑", "填筑流变", "蓄水",
                             "蓄水流变", "节点水头数据（可选）", "节点体力数据（可选）"});
    auto* cycleNumbers=new CompactNumberDelegate(m_circleTable);
    for(int column=0;column<6;++column)m_circleTable->setItemDelegateForColumn(column,cycleNumbers);
    m_dropTable->setItemDelegate(new CompactNumberDelegate(m_dropTable));
    // 内部仍保留求解器需要的 12 列；界面只显示前 6 项施工参数。
    // IF_PACK / IF_PLIU / IF_FILL / IF_FLIU 始终由施工参数自动生成。

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
        QStringLiteral("当前工况使用的静力材料到动力材料转换关系。"));
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
    m_dynamicMaterialGroup =
        new QGroupBox(QStringLiteral("静力－动力材料转换"), this);
    auto* dynamicMaterialGroupLayout = new QVBoxLayout(m_dynamicMaterialGroup);
    dynamicMaterialGroupLayout->setContentsMargins(9, 9, 9, 9);
    dynamicMaterialGroupLayout->setSpacing(6);
    m_dynamicMaterialHint = new QLabel(
        QStringLiteral("仅对当前工况生效；启用模态或地震波分析后参与计算。"),
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
    // 兼容旧工程：原静动材料转换控件保留为隐藏求解器字段，用户入口已迁移到“动力分析”子节点。
    ui->formLayoutDynamic->addRow(m_dynamicMaterialGroup);

    // 本页只负责当前土石坝工况的填筑、蓄水及相关静力过程设置。
    ui->verticalLayoutContent->removeWidget(ui->groupCycle);
    ui->verticalLayoutContent->insertWidget(0, ui->groupCycle);
    connect(m_addDynamicMaterialButton, &QPushButton::clicked, this,
            [this]() { addMaterialMappingRow(m_dynamicMaterialTable); });
    connect(m_removeDynamicMaterialButton, &QPushButton::clicked, this,
            [this]() { removeSelectedMaterialMapping(m_dynamicMaterialTable); });

    auto installMaterialRowMenu = [this](QTableWidget* table)
    {
        SolverUiLayoutHelper::installTableRowContextMenu(
            table,
            [this, table](int insertAt, int sourceRow, bool duplicate)
            {
                int sourceMaterial = 0;
                int targetMaterial = 0;
                if (duplicate && sourceRow >= 0 && sourceRow < table->rowCount())
                {
                    sourceMaterial = tableItemText(table, sourceRow, 0, QString()).toInt();
                    targetMaterial = tableItemText(table, sourceRow, 1, QString()).toInt();
                }
                addMaterialMappingRow(table, sourceMaterial, targetMaterial, insertAt);
            },
            [this, table](int row)
            {
                if (row < 0 || row >= table->rowCount())
                    return;
                table->setCurrentCell(row, 0);
                removeSelectedMaterialMapping(table);
            });
    };
    installMaterialRowMenu(m_wetTable);
    installMaterialRowMenu(m_dynamicMaterialTable);

    SolverUiLayoutHelper::installTableRowContextMenu(
        m_dropTable,
        [this](int insertAt, int sourceRow, bool duplicate)
        {
            insertAt = qBound(0, insertAt, m_dropTable->rowCount());
            QString before = QStringLiteral("3200.000");
            QString after = QStringLiteral("3160.000");
            if (duplicate && sourceRow >= 0 && sourceRow < m_dropTable->rowCount())
            {
                before = tableItemText(m_dropTable, sourceRow, 0, before);
                after = tableItemText(m_dropTable, sourceRow, 1, after);
            }
            else if (insertAt > 0)
            {
                before = tableItemText(m_dropTable, insertAt - 1, 1, before);
                after = before;
            }
            m_dropTable->insertRow(insertAt);
            m_dropTable->setItem(insertAt, 0, new QTableWidgetItem(before));
            m_dropTable->setItem(insertAt, 1, new QTableWidgetItem(after));
            m_dropTable->setCurrentCell(insertAt, 0);
            updatePreview();
            updateConditionalVisibility();
        },
        [this](int row)
        {
            if (row < 0 || row >= m_dropTable->rowCount() || m_dropTable->rowCount() <= 1)
                return;
            m_dropTable->removeRow(row);
            if (m_dropTable->rowCount() > 0)
                m_dropTable->setCurrentCell(qMin(row, m_dropTable->rowCount() - 1), 0);
            updatePreview();
            updateConditionalVisibility();
        },
        1);

    updateMaterialTableHeight(m_wetTable);
    updateMaterialTableHeight(m_dynamicMaterialTable);
    updateConditionalVisibility();
}

void FillStorageCyclePage::openConstructionStageEditor()
{
    auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>();
    if (!uiFrame || !m_circleTable)
        return;

    if (m_constructionStageEditor)
    {
        if (m_constructionStageTable && m_circleTable)
        {
            const QSignalBlocker blocker(m_constructionStageTable);
            m_constructionStageTable->setRowCount(m_circleTable->rowCount());
            for (int r = 0; r < m_circleTable->rowCount(); ++r)
            {
                for (int c = 0; c < m_circleTable->columnCount(); ++c)
                {
                    const QString txt = tableItemText(m_circleTable, r, c, QString());
                    if (auto* item = m_constructionStageTable->item(r, c))
                        item->setText(txt);
                    else
                        m_constructionStageTable->setItem(r, c, new QTableWidgetItem(txt));
                }
            }
        }
        uiFrame->setWidgetVisible(m_constructionStageEditor, true);
        updateConstructionStageLabels();
        updateConstructionStageSummary();
        updateConstructionStagePreviewFromEditor();
        return;
    }

    auto* editor = new QWidget;
    editor->setObjectName(QStringLiteral("constructionStageEditor"));
    editor->setAttribute(Qt::WA_StyledBackground, true);
    m_constructionStageEditor = editor;

    auto* layout = new QVBoxLayout(editor);
    layout->setContentsMargins(8, 6, 8, 7);
    layout->setSpacing(5);

    // 模型显示使用下拉框，默认“截至当前阶段”，最符合施工过程查看习惯。
    auto* modeRow = new QHBoxLayout;
    modeRow->setSpacing(6);
    modeRow->addWidget(new QLabel(QStringLiteral("模型显示"), editor));
    auto* modeCombo = new QComboBox(editor);
    modeCombo->setObjectName(QStringLiteral("comboConstructionStagePreviewMode"));
    modeCombo->addItem(QStringLiteral("当前阶段"), 0);
    modeCombo->addItem(QStringLiteral("截至当前阶段"), 1);
    modeCombo->addItem(QStringLiteral("全部阶段"), 2);
    modeCombo->setCurrentIndex(1);
    modeCombo->setMinimumWidth(140);
    m_constructionStageModeCombo = modeCombo;
    modeRow->addWidget(modeCombo);
    modeRow->addStretch();
    layout->addLayout(modeRow);

    // 当前阶段摘要只保留用户真正需要核对的信息。
    auto* summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(16);
    auto* stageTitle = new QLabel(editor);
    QFont stageTitleFont = stageTitle->font();
    stageTitleFont.setBold(true);
    stageTitle->setFont(stageTitleFont);
    auto* fillSummary = new QLabel(editor);
    auto* waterSummary = new QLabel(editor);
    m_constructionStageTitleLabel = stageTitle;
    m_constructionStageFillLabel = fillSummary;
    m_constructionStageWaterLabel = waterSummary;
    summaryRow->addWidget(stageTitle);
    summaryRow->addWidget(fillSummary);
    summaryRow->addWidget(waterSummary);
    summaryRow->addStretch();
    layout->addLayout(summaryRow);

    auto* table = new QTableWidget(editor);
    m_constructionStageTable = table;
    table->setColumnCount(m_circleTable->columnCount());
    QStringList headers;
    for (int column = 0; column < m_circleTable->columnCount(); ++column)
    {
        headers << (m_circleTable->horizontalHeaderItem(column)
                        ? m_circleTable->horizontalHeaderItem(column)->text()
                        : QString());
    }
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(m_circleTable->rowCount());
    for (int row = 0; row < m_circleTable->rowCount(); ++row)
    {
        for (int column = 0; column < m_circleTable->columnCount(); ++column)
        {
            QString value = tableItemText(m_circleTable, row, column, QString());
            if (column >= 10
                && (value.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) == 0
                    || value.compare(QStringLiteral("'NONE'"), Qt::CaseInsensitive) == 0))
            {
                value.clear();
            }
            table->setItem(row, column, new QTableWidgetItem(value));
        }
    }

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(true);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);

    synchronizeAutomaticCycleBranches(table);
    for (int column = 6; column <= 11; ++column)
        table->setColumnHidden(column, true);

    QHeaderView* header = table->horizontalHeader();
    // 施工阶段表格需要随停靠窗口宽度实时伸缩，而不是只按内容宽度占据左侧一部分。
    // 可见列统一使用 Stretch：窗口变宽时自动铺满，变窄时按最小列宽限制并出现横向滚动。
    header->setMinimumSectionSize(90);
    header->setStretchLastSection(false);
    for (int column = 0; column < table->columnCount(); ++column)
    {
        if (!table->isColumnHidden(column))
            header->setSectionResizeMode(column, QHeaderView::Stretch);
    }
    layout->addWidget(table, 1);

    SolverUiLayoutHelper::installTableRowContextMenu(
        table,
        [this](int insertAt, int sourceRow, bool duplicate)
        {
            insertConstructionStageRow(insertAt, sourceRow, duplicate);
        },
        [this](int row)
        {
            removeConstructionStageRow(row);
        },
        1);

    auto* editButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(QStringLiteral("新增阶段"), editor);
    auto* removeButton = new QPushButton(QStringLiteral("删除阶段"), editor);
    editButtons->addWidget(addButton);
    editButtons->addWidget(removeButton);
    editButtons->addStretch();
    layout->addLayout(editButtons);

    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FillStorageCyclePage::onConstructionStageModeChanged);
    connect(table, &QTableWidget::currentCellChanged,
            this, &FillStorageCyclePage::onConstructionStageCurrentCellChanged);
    connect(table, &QTableWidget::cellChanged,
            this, &FillStorageCyclePage::onConstructionStageCellChanged);
    connect(addButton, &QPushButton::clicked,
            this, &FillStorageCyclePage::addConstructionStage);
    connect(removeButton, &QPushButton::clicked,
            this, &FillStorageCyclePage::removeCurrentConstructionStage);
    connect(editor, &QObject::destroyed,
            this, &FillStorageCyclePage::onConstructionStageEditorDestroyed);

    if (table->rowCount() > 0)
        table->setCurrentCell(0, 0);
    updateConstructionStageLabels();
    updateConstructionStageSummary();
    updateConstructionStagePreviewFromEditor();

    // 施工阶段标注与湿化/动力材料显示完全独立，可同时查看。
    uiFrame->showWidgetInLogArea(editor, QStringLiteral("施工阶段"));
}

int FillStorageCyclePage::constructionStagePreviewMode() const
{
    if (!m_constructionStageModeCombo)
        return 1;

    bool ok = false;
    const int mode = m_constructionStageModeCombo->currentData().toInt(&ok);
    return ok ? qBound(0, mode, 2) : 1;
}

void FillStorageCyclePage::updateConstructionStageLabels()
{
    if (!m_constructionStageTable)
        return;

    for (int row = 0; row < m_constructionStageTable->rowCount(); ++row)
    {
        m_constructionStageTable->setVerticalHeaderItem(
            row, new QTableWidgetItem(QStringLiteral("阶段 %1").arg(row + 1)));
    }
}

void FillStorageCyclePage::updateConstructionStageSummary()
{
    if (!m_constructionStageTable || !m_constructionStageTitleLabel
        || !m_constructionStageFillLabel || !m_constructionStageWaterLabel)
    {
        return;
    }

    int row = m_constructionStageTable->currentRow();
    if (row < 0 && m_constructionStageTable->rowCount() > 0)
        row = 0;
    if (row < 0)
    {
        m_constructionStageTitleLabel->setText(QStringLiteral("暂无阶段"));
        m_constructionStageFillLabel->clear();
        m_constructionStageWaterLabel->clear();
        return;
    }

    m_constructionStageTitleLabel->setText(QStringLiteral("阶段 %1").arg(row + 1));

    bool firstOk = false;
    bool lastOk = false;
    const int firstLayer = tableItemText(
        m_constructionStageTable, row, 0, QString()).toInt(&firstOk);
    const int lastLayer = tableItemText(
        m_constructionStageTable, row, 1, QString()).toInt(&lastOk);
    m_constructionStageFillLabel->setText(
        firstOk && lastOk
            ? QStringLiteral("填筑：%1–%2 层").arg(firstLayer).arg(lastLayer)
            : QStringLiteral("填筑：—"));

    bool startOk = false;
    bool endOk = false;
    const double startWater = tableItemText(
        m_constructionStageTable, row, 3, QString()).toDouble(&startOk);
    const double endWater = tableItemText(
        m_constructionStageTable, row, 4, QString()).toDouble(&endOk);
    m_constructionStageWaterLabel->setText(
        startOk && endOk && std::isfinite(startWater) && std::isfinite(endWater)
            ? QStringLiteral("水位：%1 → %2 m")
                  .arg(startWater, 0, 'g', 8)
                  .arg(endWater, 0, 'g', 8)
            : QStringLiteral("水位：—"));
}

void FillStorageCyclePage::updateConstructionStagePreviewFromEditor()
{
    if (!m_constructionStageTable)
        return;

    int row = m_constructionStageTable->currentRow();
    if (row < 0 && m_constructionStageTable->rowCount() > 0)
        row = 0;
    updateConstructionStagePreview(
        m_constructionStageTable, row, constructionStagePreviewMode());
}

void FillStorageCyclePage::syncConstructionStageEditorToSource()
{
    if (!m_constructionStageTable || !m_circleTable)
        return;

    synchronizeAutomaticCycleBranches(m_constructionStageTable);
    {
        const QSignalBlocker blocker(m_circleTable);
        m_circleTable->setRowCount(m_constructionStageTable->rowCount());
        for (int row = 0; row < m_constructionStageTable->rowCount(); ++row)
        {
            for (int column = 0; column < m_constructionStageTable->columnCount(); ++column)
            {
                QString value = tableItemText(
                    m_constructionStageTable, row, column, QString());
                if (column >= 10)
                {
                    if (value.isEmpty() || value == QStringLiteral("无"))
                        value = QStringLiteral("'NONE'");
                    else if (!value.startsWith(QLatin1Char('\'')))
                        value = QStringLiteral("'%1'").arg(value);
                }
                if (m_circleTable->item(row, column))
                    m_circleTable->item(row, column)->setText(value);
                else
                    m_circleTable->setItem(row, column, new QTableWidgetItem(value));
            }
        }
    }

    synchronizeAutomaticCycleBranches(m_circleTable);
    updatePreview();
    emit requirementsChanged();
}

void FillStorageCyclePage::insertConstructionStageRow(int insertAt, int sourceRow,
                                                       bool duplicate)
{
    if (!m_constructionStageTable)
        return;

    QTableWidget* table = m_constructionStageTable;
    insertAt = qBound(0, insertAt, table->rowCount());
    {
        const QSignalBlocker blocker(table);
        table->insertRow(insertAt);

        if (duplicate && sourceRow >= 0 && sourceRow < table->rowCount())
        {
            const int actualSource = sourceRow >= insertAt ? sourceRow + 1 : sourceRow;
            for (int column = 0; column < table->columnCount(); ++column)
            {
                table->setItem(insertAt, column,
                               new QTableWidgetItem(tableItemText(
                                   table, actualSource, column, QString())));
            }
        }
        else
        {
            int firstLayer = 2;
            double initialElevation = 0.0;
            if (insertAt > 0)
            {
                bool layerOk = false;
                const int previousEnd = tableItemText(
                    table, insertAt - 1, 1, QStringLiteral("1")).toInt(&layerOk);
                firstLayer = layerOk ? qMax(2, previousEnd + 1) : 2;

                bool waterOk = false;
                const double previousWater = tableItemText(
                    table, insertAt - 1, 4, QStringLiteral("0")).toDouble(&waterOk);
                initialElevation = waterOk ? previousWater : 0.0;
            }

            const bool firstHasWaterFill = insertAt > 0 &&
                tableItemText(table, 0, 8, QStringLiteral("0")) == QStringLiteral("1");
            const double targetElevation = firstHasWaterFill ? initialElevation + 1.0 : initialElevation;
            const QStringList defaults = {
                QString::number(firstLayer), QString::number(firstLayer),
                QStringLiteral("0.0000"), QString::number(initialElevation, 'f', 4),
                QString::number(targetElevation, 'f', 4), QStringLiteral("0.0000"),
                QStringLiteral("1"), QStringLiteral("0"),
                firstHasWaterFill ? QStringLiteral("1") : QStringLiteral("0"),
                QStringLiteral("0"), QString(), QString()};
            for (int column = 0; column < defaults.size(); ++column)
                table->setItem(insertAt, column, new QTableWidgetItem(defaults.at(column)));
        }

        synchronizeAutomaticCycleBranches(table);
        table->setCurrentCell(insertAt, 0);
    }

    updateConstructionStageLabels();
    syncConstructionStageEditorToSource();
    updateConstructionStageSummary();
    updateConstructionStagePreviewFromEditor();
}

void FillStorageCyclePage::removeConstructionStageRow(int row)
{
    if (!m_constructionStageTable || !m_constructionStageEditor)
        return;

    QTableWidget* table = m_constructionStageTable;
    if (table->rowCount() <= 1)
    {
        QMessageBox::information(m_constructionStageEditor.data(),
                                 QStringLiteral("无法删除"),
                                 QStringLiteral("至少需要保留一个施工阶段。"));
        return;
    }
    if (row < 0 || row >= table->rowCount())
        return;

    {
        const QSignalBlocker blocker(table);
        table->removeRow(row);
        table->setCurrentCell(qMin(row, table->rowCount() - 1), 0);
    }

    updateConstructionStageLabels();
    syncConstructionStageEditorToSource();
    updateConstructionStageSummary();
    updateConstructionStagePreviewFromEditor();
}

void FillStorageCyclePage::onConstructionStageCurrentCellChanged(
    int currentRow, int currentColumn, int previousRow, int previousColumn)
{
    Q_UNUSED(currentRow)
    Q_UNUSED(currentColumn)
    Q_UNUSED(previousRow)
    Q_UNUSED(previousColumn)
    updateConstructionStageSummary();
    updateConstructionStagePreviewFromEditor();
}

void FillStorageCyclePage::onConstructionStageCellChanged(int row, int column)
{
    Q_UNUSED(row)
    if (!m_constructionStageTable)
        return;

    if (column >= 0 && column <= 5)
        synchronizeAutomaticCycleBranches(m_constructionStageTable);
    syncConstructionStageEditorToSource();
    updateConstructionStageSummary();
    updateConstructionStagePreviewFromEditor();
}

void FillStorageCyclePage::onConstructionStageModeChanged(int index)
{
    Q_UNUSED(index)
    updateConstructionStagePreviewFromEditor();
}

void FillStorageCyclePage::addConstructionStage()
{
    if (!m_constructionStageTable)
        return;
    insertConstructionStageRow(
        m_constructionStageTable->rowCount(),
        m_constructionStageTable->rowCount() - 1,
        false);
}

void FillStorageCyclePage::removeCurrentConstructionStage()
{
    if (!m_constructionStageTable)
        return;

    int row = m_constructionStageTable->currentRow();
    if (row < 0)
        row = m_constructionStageTable->rowCount() - 1;
    removeConstructionStageRow(row);
}

void FillStorageCyclePage::onConstructionStageEditorDestroyed(QObject* object)
{
    Q_UNUSED(object)
    m_constructionStageEditor = nullptr;
    m_constructionStageTable = nullptr;
    m_constructionStageModeCombo = nullptr;
    m_constructionStageTitleLabel = nullptr;
    m_constructionStageFillLabel = nullptr;
    m_constructionStageWaterLabel = nullptr;
    clearConstructionStagePreview();
}

void FillStorageCyclePage::closeConstructionStageEditor()
{
    if (!m_constructionStageEditor)
        return;

    QWidget* editor = m_constructionStageEditor.data();
    clearConstructionStagePreview();
    if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
        uiFrame->setWidgetVisible(editor, false);

}

void FillStorageCyclePage::clearConstructionStagePreview(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    const QString prefix = QStringLiteral("dam.stage.preview.%1.").arg(m_uuid);
    // 兼容旧版预览对象，同时清理当前版本增加的阶段标签和水位标记。
    visual->removeProp(prefix + QStringLiteral("layers"), false);
    visual->removeProp(prefix + QStringLiteral("water.start"), false);
    visual->removeProp(prefix + QStringLiteral("water.end"), false);
    for (int stage = 0; stage < 256; ++stage)
    {
        const QString stagePrefix = prefix + QStringLiteral("stage.%1.").arg(stage);
        visual->removeProp(stagePrefix + QStringLiteral("layers"), false);
        visual->removeProp(stagePrefix + QStringLiteral("label"), false);
        visual->removeProp(stagePrefix + QStringLiteral("height.range"), false);
        visual->removeProp(stagePrefix + QStringLiteral("height.low"), false);
        visual->removeProp(stagePrefix + QStringLiteral("height.high"), false);
        visual->removeProp(stagePrefix + QStringLiteral("height.label"), false);
        visual->removeProp(stagePrefix + QStringLiteral("water.start"), false);
        visual->removeProp(stagePrefix + QStringLiteral("water.start.label"), false);
        visual->removeProp(stagePrefix + QStringLiteral("water.end"), false);
        visual->removeProp(stagePrefix + QStringLiteral("water.end.label"), false);
        visual->removeProp(stagePrefix + QStringLiteral("water.change"), false);
        visual->removeProp(stagePrefix + QStringLiteral("water.change.label"), false);
    }
    if (renderNow)
        visual->render();
}

void FillStorageCyclePage::updateConstructionStagePreview(QTableWidget* table, int row,
                                                           int previewMode)
{
    clearConstructionStagePreview(false);
    if (!table || row < 0 || row >= table->rowCount())
        return;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (!visual || !grid || grid->GetNumberOfCells() <= 0)
        return;

    // 施工阶段预览只使用模型外侧标尺，不改变模型本体颜色，
    // 也不修改湿化材料、动力材料等其它模型显示状态。

    previewMode = qBound(0, previewMode, 2);
    QVector<int> stages;
    if (previewMode == 0)
    {
        stages.push_back(row);
    }
    else if (previewMode == 1)
    {
        for (int stage = 0; stage <= row; ++stage)
            stages.push_back(stage);
    }
    else
    {
        for (int stage = 0; stage < table->rowCount(); ++stage)
            stages.push_back(stage);
    }

    const QString prefix = QStringLiteral("dam.stage.preview.%1.").arg(m_uuid);
    constexpr const char* groupName = "土石坝施工阶段标注";

    // 阶段标尺使用中性灰阶，避免与材料着色产生颜色语义冲突。
    // 当前阶段只通过线宽与文字强调，不再使用黄色/橙色表达。
    const std::array<double, 3> currentStageColor{0.16, 0.18, 0.20};
    const std::array<double, 3> otherStageColor{0.56, 0.59, 0.62};
    const std::array<double, 3> startWaterColor{0.35, 0.63, 0.80};
    const std::array<double, 3> targetWaterColor{0.08, 0.39, 0.67};

    double modelBounds[6];
    grid->GetBounds(modelBounds);

    int verticalAxis = meshService->verticalAxis() - 1;
    if (verticalAxis < 0 || verticalAxis > 2)
        verticalAxis = 1;

    const double spans[3] = {
        modelBounds[1] - modelBounds[0],
        modelBounds[3] - modelBounds[2],
        modelBounds[5] - modelBounds[4]};
    const double modelSpan = std::max({spans[0], spans[1], spans[2], 1.0});
    const double tolerance = modelSpan * 1.0e-8;
    const int targetDimension = currentDomainDimension();

    // 在两个非竖直方向中，跨度较大的方向用于把标尺放到模型左右两侧。
    int sideAxis = -1;
    int depthAxis = -1;
    double sideSpan = -1.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis == verticalAxis)
            continue;
        if (spans[axis] > sideSpan)
        {
            sideSpan = spans[axis];
            sideAxis = axis;
        }
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis != verticalAxis && axis != sideAxis)
        {
            depthAxis = axis;
            break;
        }
    }
    if (sideAxis < 0)
        return;

    sideSpan = std::max(sideSpan, modelSpan * 0.10);
    const double depthSpan =
        depthAxis >= 0 ? std::max(spans[depthAxis], modelSpan * 0.05) : modelSpan * 0.05;
    const double stageBase =
        modelBounds[2 * sideAxis] - std::max(sideSpan * 0.08, modelSpan * 0.035);
    const double stageStep = std::max(sideSpan * 0.055, modelSpan * 0.022);
    const double waterBase =
        modelBounds[2 * sideAxis + 1] + std::max(sideSpan * 0.08, modelSpan * 0.035);
    const double tickLength = std::max(sideSpan * 0.035, modelSpan * 0.016);
    const double labelGap = std::max(sideSpan * 0.018, modelSpan * 0.010);
    const double depthCoordinate =
        depthAxis >= 0
            ? modelBounds[2 * depthAxis + 1] + std::max(depthSpan * 0.025, modelSpan * 0.006)
            : 0.0;

    vtkDataArray* layerArray =
        grid->GetCellData() ? grid->GetCellData()->GetArray("LAYER") : nullptr;

    // 左侧：只画阶段的高程区间标尺。模型本体完全不着色。
    for (int displayIndex = 0; displayIndex < stages.size(); ++displayIndex)
    {
        const int stageRow = stages.at(displayIndex);
        bool firstOk = false;
        bool lastOk = false;
        const int firstLayer =
            tableItemText(table, stageRow, 0, QString()).toInt(&firstOk);
        const int lastLayer =
            tableItemText(table, stageRow, 1, QString()).toInt(&lastOk);
        if (!firstOk || !lastOk || firstLayer < 2 || lastLayer < firstLayer || !layerArray)
            continue;

        bool found = false;
        double elevationLow = 0.0;
        double elevationHigh = 0.0;
        for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
        {
            vtkCell* cell = grid->GetCell(cellId);
            if (!cell || cell->GetCellDimension() != targetDimension)
                continue;

            const int layer =
                static_cast<int>(std::llround(layerArray->GetTuple1(cellId)));
            if (layer < firstLayer || layer > lastLayer)
                continue;

            double cellBounds[6];
            cell->GetBounds(cellBounds);
            const double low = cellBounds[2 * verticalAxis];
            const double high = cellBounds[2 * verticalAxis + 1];
            if (!std::isfinite(low) || !std::isfinite(high))
                continue;

            if (!found)
            {
                elevationLow = low;
                elevationHigh = high;
                found = true;
            }
            else
            {
                elevationLow = std::min(elevationLow, low);
                elevationHigh = std::max(elevationHigh, high);
            }
        }
        if (!found)
            continue;

        const bool current = stageRow == row;
        const auto& color = current ? currentStageColor : otherStageColor;
        const double rulerCoordinate = stageBase - displayIndex * stageStep;
        const QString stagePrefix =
            prefix + QStringLiteral("stage.%1.").arg(stageRow);

        double lowPoint[3] = {
            0.5 * (modelBounds[0] + modelBounds[1]),
            0.5 * (modelBounds[2] + modelBounds[3]),
            0.5 * (modelBounds[4] + modelBounds[5])};
        double highPoint[3] = {lowPoint[0], lowPoint[1], lowPoint[2]};
        lowPoint[verticalAxis] = elevationLow;
        highPoint[verticalAxis] = elevationHigh;
        lowPoint[sideAxis] = highPoint[sideAxis] = rulerCoordinate;
        if (depthAxis >= 0)
            lowPoint[depthAxis] = highPoint[depthAxis] = depthCoordinate;

        addStagePreviewLine(visual, QString::fromUtf8(groupName),
                            stagePrefix + QStringLiteral("height.range"),
                            QStringLiteral("阶段 %1 高程范围").arg(stageRow + 1),
                            lowPoint, highPoint, color, current ? 3.2 : 1.8);

        double lowTickEnd[3] = {lowPoint[0], lowPoint[1], lowPoint[2]};
        double highTickEnd[3] = {highPoint[0], highPoint[1], highPoint[2]};
        lowTickEnd[sideAxis] += tickLength;
        highTickEnd[sideAxis] += tickLength;
        addStagePreviewLine(visual, QString::fromUtf8(groupName),
                            stagePrefix + QStringLiteral("height.low"),
                            QStringLiteral("阶段 %1 起始高程").arg(stageRow + 1),
                            lowPoint, lowTickEnd, color, current ? 3.2 : 1.8);
        addStagePreviewLine(visual, QString::fromUtf8(groupName),
                            stagePrefix + QStringLiteral("height.high"),
                            QStringLiteral("阶段 %1 结束高程").arg(stageRow + 1),
                            highPoint, highTickEnd, color, current ? 3.2 : 1.8);

        double labelPosition[3] = {
            0.5 * (lowPoint[0] + highPoint[0]),
            0.5 * (lowPoint[1] + highPoint[1]),
            0.5 * (lowPoint[2] + highPoint[2])};
        labelPosition[sideAxis] = rulerCoordinate - labelGap;

        const QString labelText =
            current
                ? QStringLiteral("阶段 %1\n填筑层 %2–%3\n高程 %4–%5 m")
                      .arg(stageRow + 1)
                      .arg(firstLayer)
                      .arg(lastLayer)
                      .arg(elevationLow, 0, 'g', 8)
                      .arg(elevationHigh, 0, 'g', 8)
                : QStringLiteral("阶段 %1  ·  填筑层 %2–%3  ·  高程 %4–%5 m")
                      .arg(stageRow + 1)
                      .arg(firstLayer)
                      .arg(lastLayer)
                      .arg(elevationLow, 0, 'g', 8)
                      .arg(elevationHigh, 0, 'g', 8);
        addStagePreviewLabel(visual, QString::fromUtf8(groupName),
                             stagePrefix + QStringLiteral("height.label"),
                             QStringLiteral("阶段 %1 高程标注").arg(stageRow + 1),
                             labelText, labelPosition, color, true,
                             current ? 15 : 13, current ? 0.90 : 0.72);
    }

    // 右侧：只标当前阶段的水位，不再把半透明水位面铺到模型内部。
    bool startOk = false;
    bool endOk = false;
    const double startWater =
        tableItemText(table, row, 3, QString()).toDouble(&startOk);
    const double endWater =
        tableItemText(table, row, 4, QString()).toDouble(&endOk);

    const bool hasStart = startOk
                          && stagePreviewLevelVisible(
                              startWater, modelBounds, verticalAxis, tolerance);
    const bool hasEnd = endOk
                        && stagePreviewLevelVisible(
                            endWater, modelBounds, verticalAxis, tolerance);
    const bool waterChanges =
        startOk && endOk && std::isfinite(startWater) && std::isfinite(endWater)
        && std::abs(endWater - startWater) >= CYCLE_BRANCH_TOLERANCE;
    const QString waterPrefix =
        prefix + QStringLiteral("stage.%1.").arg(row);

    if (waterChanges && hasStart)
    {
        addStageWaterTick(visual, QString::fromUtf8(groupName), waterPrefix,
                          QStringLiteral("water.start"),
                          QStringLiteral("阶段 %1 起始水位").arg(row + 1),
                          QStringLiteral("起始水位  %1 m").arg(startWater, 0, 'g', 8),
                          startWater, startWaterColor, false, modelBounds, verticalAxis,
                          sideAxis, depthAxis, waterBase, depthCoordinate, tickLength,
                          labelGap);
    }

    if (hasEnd)
    {
        addStageWaterTick(visual, QString::fromUtf8(groupName), waterPrefix,
                          QStringLiteral("water.end"),
                          QStringLiteral("阶段 %1 目标水位").arg(row + 1),
                          waterChanges
                              ? QStringLiteral("目标水位  %1 m").arg(endWater, 0, 'g', 8)
                              : QStringLiteral("当前水位  %1 m").arg(endWater, 0, 'g', 8),
                          endWater, targetWaterColor, true, modelBounds, verticalAxis,
                          sideAxis, depthAxis, waterBase, depthCoordinate, tickLength,
                          labelGap);
    }

    if (waterChanges && hasStart && hasEnd)
    {
        double startPoint[3] = {
            0.5 * (modelBounds[0] + modelBounds[1]),
            0.5 * (modelBounds[2] + modelBounds[3]),
            0.5 * (modelBounds[4] + modelBounds[5])};
        double endPoint[3] = {startPoint[0], startPoint[1], startPoint[2]};
        startPoint[verticalAxis] = startWater;
        endPoint[verticalAxis] = endWater;
        startPoint[sideAxis] = endPoint[sideAxis] = waterBase;
        if (depthAxis >= 0)
            startPoint[depthAxis] = endPoint[depthAxis] = depthCoordinate;

        addStagePreviewLine(visual, QString::fromUtf8(groupName),
                            waterPrefix + QStringLiteral("water.change"),
                            QStringLiteral("阶段 %1 水位变化范围").arg(row + 1),
                            startPoint, endPoint, targetWaterColor, 2.2);
    }

    visual->render();
}

void FillStorageCyclePage::updateConditionalVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);
    const bool needsAssociation = needsSeepageAssociation();
    const bool transientResult =
        ui->comboSeepageResultType->currentData().toString() == QStringLiteral("transient");
    const bool hasSource =
        m_availableSeepageSourceUuids.contains(seepageSourceUuid());

    // 当前页面只负责“填筑蓄水”及其静力过程设置。
    // 动力任务已拆到同一土石坝工况下的“动力分析”兄弟节点。
    // 仅动力计算时本节点仍保留，用于查看/匹配此前形成的静力状态，
    // 但求解流程不会重新执行静力填筑蓄水。
    ui->groupCycle->setVisible(m_staticOptionsAvailable);
    ui->groupStaticLoad->setVisible(m_staticOptionsAvailable);
    const bool allowBaseUplift = m_damType != 4 && m_staticOptionsAvailable;
    ui->comboIFIIWL_SEEP->setEnabled(allowBaseUplift);
    ui->formLayoutStaticLoad->setRowVisible(ui->comboIFIIWL_SEEP, allowBaseUplift);
    ui->groupSeepageAssociation->setVisible(m_staticOptionsAvailable);
    ui->groupSeepageTransientStep->setVisible(m_staticOptionsAvailable);
    ui->groupWet->setVisible(m_staticOptionsAvailable);
    ui->groupDrop->setVisible(m_staticOptionsAvailable);

    ui->groupSeepageAssociation->setEnabled(
        m_staticOptionsAvailable && needsAssociation);
    ui->groupSeepageTransientStep->setEnabled(
        m_staticOptionsAvailable && needsAssociation && transientResult && hasSource);

    // 动力控件仅作为兼容旧工程和生成 IFILL.DAT 的隐藏字段，不再出现在本页。
    ui->groupDynamic->setVisible(false);
    ui->groupDynamic->setEnabled(false);
    ui->labelDynamicHint->setVisible(false);

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
    ui->labelModalOrderInline->setEnabled(m_dynamicOptionsAvailable && modeEnabled);
    ui->editModalOrder->setEnabled(m_dynamicOptionsAvailable && modeEnabled);
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
        m_dynamicMaterialGroup->setVisible(false);
        m_dynamicMaterialGroup->setEnabled(false);
    }
    if (m_dynamicMaterialHint)
    {
        m_dynamicMaterialHint->setVisible(false);
        m_dynamicMaterialHint->setText(
            dynamicMaterialMappingEnabled
                ? QStringLiteral("当前工况将使用下列静力材料到动力材料转换关系。")
                : QStringLiteral("启用模态分析或地震波时程分析后使用下列材料转换关系。"));
    }
    if (m_dynamicMaterialControls)
    {
        m_dynamicMaterialControls->setVisible(false);
        m_dynamicMaterialControls->setEnabled(dynamicMaterialMappingEnabled);
    }
    if (m_dynamicMaterialTable)
    {
        m_dynamicMaterialTable->setVisible(false);
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

int FillStorageCyclePage::modalExtractionOrder() const
{
    bool ok = false;
    const int order = ui->editModalOrder->text().trimmed().toInt(&ok);
    return ok && order > 0 ? order : 10;
}

void FillStorageCyclePage::setModalExtractionOrder(int order)
{
    if (order <= 0)
        return;
    const QString value = QString::number(order);
    if (ui->editModalOrder->text().trimmed() == value)
        return;
    const QSignalBlocker blocker(ui->editModalOrder);
    ui->editModalOrder->setText(value);
}

void FillStorageCyclePage::setAnalysisMode(int calculationType)
{
    // “填筑蓄水”节点始终可查看。仅动力计算不会重新执行这些静力步骤，
    // 但需要保留本工况的静力设置来匹配/复用既有静力结果。
    const bool staticAvailable = true;
    const bool dynamicAvailable = calculationType == 2 || calculationType == 3;
    if (m_staticOptionsAvailable == staticAvailable
        && m_dynamicOptionsAvailable == dynamicAvailable)
        return;

    m_staticOptionsAvailable = staticAvailable;
    m_dynamicOptionsAvailable = dynamicAvailable;
    updateConditionalVisibility();
}

void FillStorageCyclePage::setDynamicOptionsAvailable(bool available)
{
    if (m_dynamicOptionsAvailable == available)
        return;
    m_dynamicOptionsAvailable = available;
    updateConditionalVisibility();
}

void FillStorageCyclePage::applyDynamicAnalysisState(const QJsonObject& state, bool dynamicAvailable)
{
    m_dynamicOptionsAvailable = dynamicAvailable;
    const QJsonObject values = state.value(QStringLiteral("values")).toObject();
    const QStringList keys = {QStringLiteral("IFMODE"), QStringLiteral("IFWAVE"),
                              QStringLiteral("IFSETT"), QStringLiteral("DT"),
                              QStringLiteral("TSTA"), QStringLiteral("TEND"),
                              QStringLiteral("WOMI_1")};
    for (const QString& key : keys)
    {
        QString value = values.value(key).toVariant().toString().trimmed();
        if (value.isEmpty())
        {
            if (key == QStringLiteral("DT"))
                value = QStringLiteral("0.020000");
            else if (key == QStringLiteral("IFMODE") || key == QStringLiteral("IFWAVE")
                     || key == QStringLiteral("IFSETT"))
                value = QStringLiteral("0");
            else
                value = QStringLiteral("0.000000");
        }
        if (!dynamicAvailable && (key == QStringLiteral("IFMODE")
                                  || key == QStringLiteral("IFWAVE")
                                  || key == QStringLiteral("IFSETT")))
            value = QStringLiteral("0");
        if (QWidget* editor = m_editors.value(key, nullptr))
        {
            const QSignalBlocker blocker(editor);
            setValueOf(key, value);
        }
        else
        {
            setValueOf(key, value);
        }
    }

    QVector<QPair<int, int>> mappings;
    if (dynamicAvailable)
    {
        const QJsonArray rows = state.value(QStringLiteral("dynamic_material_table")).toArray();
        mappings.reserve(rows.size());
        for (const QJsonValue& value : rows)
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
                source = mapping.value(QStringLiteral("source")).toInt();
                target = mapping.value(QStringLiteral("target")).toInt();
            }
            if (source > 0 || target > 0)
                mappings.push_back(qMakePair(source, target));
        }
    }
    setMaterialMappings(m_dynamicMaterialTable, mappings);

    const int modalOrder = state.value(QStringLiteral("modal_order")).toInt(10);
    setModalExtractionOrder(modalOrder > 0 ? modalOrder : 10);
    updateConditionalVisibility();
    updatePreview();
}

void FillStorageCyclePage::setDamType(int damType)
{
    if (m_damType == damType)
        return;
    m_damType = damType;
    updateConditionalVisibility();
    updatePreview();
}

bool FillStorageCyclePage::usesDamBaseUplift() const
{
    // IROCKFILLTYPE=4（均质坝）时，求解器会固定 IFIIWL_SEEP=0。
    return m_damType != 4 && valueOf(QStringLiteral("IFIIWL_SEEP")) != QStringLiteral("0");
}

bool FillStorageCyclePage::usesLinearDamBaseUplift() const
{
    return m_damType != 4 && valueOf(QStringLiteral("IFIIWL_SEEP")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesDamFacePressure() const
{
    return valueOf(QStringLiteral("IFIIWL_FACE")) == QStringLiteral("1");
}

bool FillStorageCyclePage::usesImportedSeepageHead() const
{
    // FILLFTSEEP2（IFIIWL_SEEP=2）读取 CAHEAD；
    // FILLFBLOAD2（IFIIWL_BODY=2）会同时读取 CAHEAD 与 CABODY。
    return (m_damType != 4 && valueOf(QStringLiteral("IFIIWL_SEEP")) == QStringLiteral("2"))
           || usesImportedSeepageBody();
}

bool FillStorageCyclePage::usesImportedSeepageBody() const
{
    return valueOf(QStringLiteral("IFIIWL_BODY")) == QStringLiteral("2");
}

bool FillStorageCyclePage::needsSeepageAssociation() const
{
    return usesImportedSeepageHead() || usesImportedSeepageBody();
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
    const QString data = ui->comboSeepageTransientStep->currentData().toString().trimmed();
    return data.isEmpty() ? m_savedSeepageTransientStep : data;
}

QString FillStorageCyclePage::seepageTransientResultLabel() const
{
    return ui->comboSeepageTransientStep->currentText().trimmed();
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
    const QVector<int>& steps, bool hasFinalResult, const QMap<int, QString>& labels)
{
    QString selectedStep = seepageTransientStep();
    if (selectedStep.isEmpty())
        selectedStep = m_savedSeepageTransientStep;

    const QSignalBlocker blocker(ui->comboSeepageTransientStep);
    ui->comboSeepageTransientStep->clear();
    ui->comboSeepageTransientStep->addItem(
        QStringLiteral("请选择非稳态计算时刻"), QString());

    m_availableTransientSteps.clear();
    m_hasTransientFinalResult = hasFinalResult;
    const int finalStep = hasFinalResult && !steps.isEmpty() ? steps.constLast() : -1;
    for (int step : steps)
    {
        if (step < 0)
            continue;
        m_availableTransientSteps.insert(step);
        if (step == finalStep)
            continue;
        ui->comboSeepageTransientStep->addItem(
            labels.value(step, QStringLiteral("计算时刻索引 %1").arg(step)),
            QString::number(step));
    }
    if (hasFinalResult)
    {
        QString finalLabel = labels.value(finalStep);
        if (finalLabel.isEmpty())
            finalLabel = QStringLiteral("最终计算时刻");
        else if (finalLabel.startsWith(QStringLiteral("计算时刻")))
            finalLabel.replace(0, QStringLiteral("计算时刻").size(),
                               QStringLiteral("最终时刻"));
        else
            finalLabel += QStringLiteral("（最终时刻）");
        ui->comboSeepageTransientStep->addItem(finalLabel, QStringLiteral("final"));
    }

    // 旧工程可能保存了最后一个内部编号；它与最终文件是同一时刻，加载时自动归一。
    if (hasFinalResult && selectedStep == QString::number(finalStep))
        selectedStep = QStringLiteral("final");

    int index = selectedStep.isEmpty()
                    ? 0
                    : ui->comboSeepageTransientStep->findData(selectedStep);
    ui->comboSeepageTransientStep->setCurrentIndex(index >= 0 ? index : 0);
    if (index >= 0 || selectedStep.isEmpty())
        m_savedSeepageTransientStep =
            ui->comboSeepageTransientStep->currentData().toString().trimmed();

    ui->labelSeepageTransientHint->setText(
        ui->comboSeepageTransientStep->count() > 1
            ? QStringLiteral("按实际计算时间选择；括号内为该时刻的边界水位。所选渗流场将用于最后一个蓄水阶段。")
            : QStringLiteral("关联工况暂无可用的非稳态时刻结果。"));
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

QVector<QPair<int, int>> FillStorageCyclePage::activeDynamicMaterialMappings() const
{
    if (!usesModalAnalysis() && !usesWaveAnalysis())
        return {};
    return materialMappings(m_dynamicMaterialTable);
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
    synchronizeAutomaticCycleBranches(m_circleTable);

    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (usesModalAnalysis())
    {
        bool orderOk = false;
        const int order = ui->editModalOrder->text().trimmed().toInt(&orderOk);
        if (!orderOk || order <= 0)
            return fail(QStringLiteral("启用模态分析时，模态提取阶数必须为正整数。"));
    }

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
                return fail(QStringLiteral("请选择一个明确的非稳态计算时刻。"));
            if (step == QStringLiteral("final"))
            {
                if (!m_hasTransientFinalResult)
                    return fail(QStringLiteral("关联工况没有可用的非稳态最终时刻结果。"));
            }
            else
            {
                bool stepOk = false;
                const int stepNumber = step.toInt(&stepOk);
                if (!stepOk || !m_availableTransientSteps.contains(stepNumber))
                    return fail(QStringLiteral("所选非稳态计算时刻已不存在，请重新选择。"));
            }
        }
    }

    if (cycleCount() > 99)
        return fail(QStringLiteral("单个工况最多设置 99 个施工阶段。"));
    if (m_dropTable && m_dropTable->rowCount() > 99)
        return fail(QStringLiteral("最多设置 99 组水位骤降。"));

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
                "未做模态分析时，请填写一阶圆频率 ω1（≥ 0.001）。"));
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
                "启用浸润变形时，最后一个填筑蓄水循环必须执行蓄水。"));
        if (tableItemText(m_circleTable, cycleCount() - 1, 9, QStringLiteral("0"))
            == QStringLiteral("1"))
            return fail(QStringLiteral(
                "浸润变形不能与最后一个循环的蓄水期流变同时启用。"));
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
        return fail(QStringLiteral("至少需要一个施工阶段。"));

    // IFIIWL_BODY=2 是工况级开关，而 CAHEAD/CABODY 是逐施工循环给出的文件名。
    // 当前界面只允许明确选择一组渗流结果，因此只在最后一个实际蓄水阶段施加一次。
    // 求解器的 WATER DROP(ITASK=5) 也会再次进入 FILLFBLOAD2，但没有独立的骤降后
    // 渗流场输入；与单一关联场组合会重复使用同一场，不能可靠闭环。
    if (usesImportedSeepageBody() || usesImportedSeepageHead())
    {
        const int finalCycle = m_circleTable->rowCount();
        if (!hasEffectiveWaterFill(finalCycle))
            return fail(QStringLiteral(
                "读取渗流结果时，最后一个施工阶段必须执行蓄水。"));
        if (usesWaterDrop())
            return fail(QStringLiteral(
                "读取渗流结果时，暂不能同时启用水位骤降。请关闭其中一项。"));
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
            return fail(QStringLiteral("第 %1 个阶段包含无效数值。").arg(row + 1));
        if (packCreepTime < 0.0 || fillCreepTime < 0.0)
            return fail(QStringLiteral("第 %1 个阶段的流变时间不能为负数。")
                            .arg(row + 1));
        if (waterEnd < waterStart)
            return fail(QStringLiteral("第 %1 个阶段：目标水位不能低于起始水位；降水请使用“水位骤降”。")
                            .arg(row + 1));

        int enabledOperations = 0;
        for (int column = 6; column <= 9; ++column)
        {
            const QString flag = tableItemText(m_circleTable, row, column, QStringLiteral("0"));
            if (flag != QStringLiteral("0") && flag != QStringLiteral("1"))
                return fail(QStringLiteral("第 %1 个阶段的计算选项无效。")
                                .arg(row + 1));
            if (flag == QStringLiteral("1"))
                ++enabledOperations;
        }
        if (enabledOperations == 0)
            return fail(QStringLiteral("第 %1 个阶段至少启用一项计算。")
                            .arg(row + 1));
        if (tableItemText(m_circleTable, row, 8, QStringLiteral("0")) == QStringLiteral("1")
            && std::abs(waterEnd - waterStart) < CYCLE_BRANCH_TOLERANCE)
            return fail(QStringLiteral("第 %1 个阶段启用蓄水计算后，起始水位与目标水位不能相同。")
                            .arg(row + 1));
        if (tableItemText(m_circleTable, row, 7, QStringLiteral("0")) == QStringLiteral("1")
            && packCreepTime <= 0.0)
            return fail(QStringLiteral("第 %1 个阶段启用填筑期流变后，流变时间必须大于 0。")
                            .arg(row + 1));
        if (tableItemText(m_circleTable, row, 9, QStringLiteral("0")) == QStringLiteral("1")
            && fillCreepTime <= 0.0)
            return fail(QStringLiteral("第 %1 个阶段启用蓄水期流变后，流变时间必须大于 0。")
                            .arg(row + 1));
    }
    if (m_circleTable->rowCount() >= 2)
    {
        const QString firstSig = QStringLiteral("%1|%2|%3|%4")
            .arg(tableItemText(m_circleTable, 0, 6, QStringLiteral("0")),
                 tableItemText(m_circleTable, 0, 7, QStringLiteral("0")),
                 tableItemText(m_circleTable, 0, 8, QStringLiteral("0")),
                 tableItemText(m_circleTable, 0, 9, QStringLiteral("0")));
        for (int row = 1; row < m_circleTable->rowCount(); ++row)
        {
            const QString sig = QStringLiteral("%1|%2|%3|%4")
                .arg(tableItemText(m_circleTable, row, 6, QStringLiteral("0")),
                     tableItemText(m_circleTable, row, 7, QStringLiteral("0")),
                     tableItemText(m_circleTable, row, 8, QStringLiteral("0")),
                     tableItemText(m_circleTable, row, 9, QStringLiteral("0")));
            if (sig != firstSig)
                return fail(QStringLiteral(
                    "多循环填筑蓄水时，求解器要求各阶段的填筑/流变/蓄水开关组合保持一致"
                    "（第 %1 个阶段与第 1 个阶段不一致）。").arg(row + 1));
        }
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
        return fail(QStringLiteral("至少需要一个施工阶段。"));

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
    if (m_constructionStageTable && m_constructionStageTable->rowCount() == 1)
    {
        const QSignalBlocker blocker(m_constructionStageTable);
        if (auto* it0 = m_constructionStageTable->item(0, 0)) it0->setText(QString::number(firstLayer));
        if (auto* it1 = m_constructionStageTable->item(0, 1)) it1->setText(QString::number(lastLayer));
    }
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
                                                 int targetMaterial, int insertAt)
{
    if (!table)
        return;

    if (table->rowCount() >= MAX_MATERIAL_RELATIONS)
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

    const int row = insertAt < 0 ? table->rowCount()
                                 : qBound(0, insertAt, table->rowCount());
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
        return materialService->damDynamicMaterialLabels();
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

    // 材料域高亮与施工阶段外侧标尺属于两套独立显示，可同时存在。

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
            return fail(QStringLiteral("同一种动力材料不能对应多个静力材料。"));
        if (!sourceOptions.contains(source))
            return fail(QStringLiteral("请选择已施加到坝体的静力材料。"));
        const QMap<int, QString> targetOptions = materialTargetOptions(table, source);
        if (!targetOptions.contains(target))
        {
            return fail(table == m_wetTable
                            ? QStringLiteral("请选择可用的浸润后材料。")
                            : QStringLiteral("请选择可用的动力材料。"));
        }
        // 这里只校验当前工况，不再把关系写回全局材料库。
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
    synchronizeAutomaticCycleBranches(m_circleTable);
    ui->labelWetRows->setText(QStringLiteral("共 %1 组").arg(m_wetTable ? m_wetTable->rowCount() : 0));
    ui->labelDropRows->setText(QStringLiteral("共 %1 组").arg(m_dropTable ? m_dropTable->rowCount() : 0));
    ui->labelCircleRows->setText(
        QStringLiteral("共 %1 个阶段")
            .arg(m_circleTable ? m_circleTable->rowCount() : 0));
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
    const QString seepMode = m_damType == 4
                                 ? QStringLiteral("0")
                                 : valueOf(QStringLiteral("IFIIWL_SEEP"));
    const QString bodyMode = usesImportedSeepageBody()
                                 ? QStringLiteral("2")
                                 : QStringLiteral("0");
    text += QString("%1 %2 %3     IFIIWL_SEEP IFIIWL_BODY IFIIWL_FACE\n")
                .arg(seepMode, bodyMode, valueOf("IFIIWL_FACE"));
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
    text += "JPACK_STA JPACK_END TT_PLIU HWJ_STA HWJ_END TT_FLIU IF_PACK IF_PLIU IF_FILL IF_FLIU CAHEAD CABODY\n";
    for (int i = 0; i < nCircle; ++i)
    {
        // 一个工况只明确选择一组渗流结果，因此将它施加在本工况的
        // 最后一个实际蓄水循环；validateInputs() 已保证最后一行确实执行 FILL。
        // 前序阶段保持 NONE，避免同一渗流场在多个增量中被重复施加。
        const bool applySeepageResult =
            (usesImportedSeepageBody() || usesImportedSeepageHead()) && i == nCircle - 1;
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
    const QVector<QPair<int, int>> dynamicMappings = activeDynamicMaterialMappings();
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
    synchronizeAutomaticCycleBranches(m_circleTable);
    QJsonObject obj;
    QJsonObject values;
    const QSet<QString> dynamicKeys = {
        QStringLiteral("IFMODE"), QStringLiteral("IFWAVE"), QStringLiteral("IFSETT"),
        QStringLiteral("DT"), QStringLiteral("TSTA"), QStringLiteral("TEND"),
        QStringLiteral("WOMI_1")};
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it)
    {
        if (dynamicKeys.contains(it.key()))
            continue;
        if (auto* combo = qobject_cast<QComboBox*>(it.value()))
            values[it.key()] = combo->currentData().toString();
        else if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
            values[it.key()] = edit->text();
    }
    obj["values"] = values;
    obj["wet_table"] = tableToJson(m_wetTable);
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

    // 计算分支由可见施工参数统一生成，避免隐藏开关与求解器目录链不一致。
    jsonToTable(obj.value("circle_table").toArray(), m_circleTable, 1);
    synchronizeAutomaticCycleBranches(m_circleTable);
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
