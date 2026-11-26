#include "DamDynamicAnalysisPage.h"

#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/PhysicalFieldService.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>
#include <cmath>
#include <limits>

#include "ui_DamDynamicAnalysisPage.h"
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

QVector<QPair<int, int>> mappingsFromJson(const QJsonArray& rows)
{
    QVector<QPair<int, int>> mappings;
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
    return mappings;
}
}

DamDynamicAnalysisPage::DamDynamicAnalysisPage(QWidget* parent)
    : BasePage(QStringLiteral("DamDynamicAnalysis")),
      ui(new Ui::DamDynamicAnalysisPage)
{
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    ui->comboIFMODE->setItemData(0, QStringLiteral("0"));
    ui->comboIFMODE->setItemData(1, QStringLiteral("1"));
    ui->comboIFWAVE->setItemData(0, QStringLiteral("0"));
    ui->comboIFWAVE->setItemData(1, QStringLiteral("1"));
    ui->comboIFWAVE->setToolTip(
        QStringLiteral("地震加速度时程在“约束与荷载 → 地震加速度”中统一设置。"));
    ui->labelWave->setToolTip(ui->comboIFWAVE->toolTip());
    ui->btnOpenSeismicAcceleration->setToolTip(
        QStringLiteral("打开“约束与荷载 → 地震加速度”设置；尚未创建时会自动创建。"));
    ui->comboIFSETT->setItemData(0, QStringLiteral("0"));
    ui->comboIFSETT->setItemData(1, QStringLiteral("1"));
    ui->editModalOrder->setValidator(
        new QIntValidator(1, std::numeric_limits<int>::max(), ui->editModalOrder));
    ui->editModalOrder->setToolTip(
        QStringLiteral("模态分析启用时提取的振型阶数。"));
    ui->editWOMI1->setToolTip(QStringLiteral("未执行模态分析但进行地震波分析时填写。"));

    m_materialTable = new QTableWidget(ui->groupMaterialMapping);
    m_materialTable->setObjectName(QStringLiteral("dynamicMaterialTable"));
    m_materialTable->setColumnCount(2);
    m_materialTable->setHorizontalHeaderLabels(
        {QStringLiteral("静力材料"), QStringLiteral("动力材料")});
    m_materialTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_materialTable->verticalHeader()->hide();
    m_materialTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_materialTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_materialTable->setAlternatingRowColors(true);
    m_materialTable->setToolTip(QStringLiteral("仅对当前土石坝工况的动力计算生效。"));
    ui->layoutMaterialMapping->addWidget(m_materialTable);

    m_addButton = ui->btnAddMapping;
    m_removeButton = ui->btnRemoveMapping;

    connect(m_addButton, &QPushButton::clicked, this,
            [this]() { addMaterialMappingRow(); });
    connect(m_removeButton, &QPushButton::clicked, this,
            &DamDynamicAnalysisPage::removeSelectedMaterialMapping);
    connect(ui->btnOpenSeismicAcceleration, &QPushButton::clicked, this,
            [this]()
            {
                auto* physicalField =
                    ServiceRegistry::instance().getService<PhysicalFieldService>();
                QString error;
                if (!physicalField || !physicalField->openSeismicAccelerationPage(&error))
                {
                    if (error.isEmpty())
                        error = QStringLiteral("约束与荷载服务不可用，无法打开地震加速度设置。");
                    QMessageBox::warning(this, QStringLiteral("地震加速度"), error);
                }
            });
    connect(m_materialTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int)
            { refreshMaterialMappingHighlight(currentRow); });
    connect(ui->comboIFMODE, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                updateConditionalState();
                emit requirementsChanged();
            });
    connect(ui->comboIFWAVE, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                updateConditionalState();
                emit requirementsChanged();
            });
    connect(ui->comboIFSETT, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                updateConditionalState();
                emit requirementsChanged();
            });
    connect(ui->editModalOrder, &QLineEdit::textChanged, this,
            [this](const QString& text)
            {
                bool ok = false;
                const int order = text.trimmed().toInt(&ok);
                if (ok && order > 0)
                    emit modalExtractionOrderChanged(order);
                emit requirementsChanged();
            });
    for (QLineEdit* edit : {ui->editDT, ui->editTSTA, ui->editTEND, ui->editWOMI1})
        connect(edit, &QLineEdit::textChanged, this,
                [this]() { emit requirementsChanged(); });

    updateTableHeight();
    updateConditionalState();
}

DamDynamicAnalysisPage::~DamDynamicAnalysisPage()
{
    delete ui;
}

void DamDynamicAnalysisPage::beforeLoad()
{
    m_materialHighlightActive = true;
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Domain);
    refreshMaterialMappingOptions();
    updateConditionalState();

    if (m_materialTable && m_materialTable->isEnabled() && m_materialTable->rowCount() > 0)
    {
        const int row = qBound(0, qMax(0, m_materialTable->currentRow()),
                               m_materialTable->rowCount() - 1);
        m_materialTable->setCurrentCell(row, 0);
        refreshMaterialMappingHighlight(row);
    }
    else
    {
        clearMaterialMappingHighlight();
    }
}

void DamDynamicAnalysisPage::beforeUnload()
{
    clearMaterialMappingHighlight();
    PickManager::instance().clearDomainSelection(getPageType(), m_uuid);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Domain);
    m_materialHighlightActive = false;
}

QString DamDynamicAnalysisPage::comboValue(QComboBox* combo) const
{
    return combo ? combo->currentData().toString().trimmed() : QStringLiteral("0");
}

void DamDynamicAnalysisPage::setComboValue(QComboBox* combo, const QString& value)
{
    if (!combo)
        return;
    const int index = combo->findData(value.trimmed());
    if (index >= 0)
        combo->setCurrentIndex(index);
}

bool DamDynamicAnalysisPage::usesModalAnalysis() const
{
    return m_analysisEnabled && comboValue(ui->comboIFMODE) == QStringLiteral("1");
}

bool DamDynamicAnalysisPage::usesWaveAnalysis() const
{
    return m_analysisEnabled && comboValue(ui->comboIFWAVE) == QStringLiteral("1");
}

bool DamDynamicAnalysisPage::usesPermanentSettlement() const
{
    return m_analysisEnabled && comboValue(ui->comboIFSETT) == QStringLiteral("1");
}

int DamDynamicAnalysisPage::modalExtractionOrder() const
{
    bool ok = false;
    const int order = ui->editModalOrder->text().trimmed().toInt(&ok);
    return ok && order > 0 ? order : 10;
}

void DamDynamicAnalysisPage::setModalExtractionOrder(int order)
{
    if (order <= 0)
        return;
    const QString value = QString::number(order);
    if (ui->editModalOrder->text().trimmed() == value)
        return;
    const QSignalBlocker blocker(ui->editModalOrder);
    ui->editModalOrder->setText(value);
}

QMap<int, QString> DamDynamicAnalysisPage::sourceMaterialOptions() const
{
    if (auto* service = ServiceRegistry::instance().getService<MaterialManagerService>())
        return service->damStaticMaterialLabels();
    return {};
}

QMap<int, QString> DamDynamicAnalysisPage::targetMaterialOptions() const
{
    if (auto* service = ServiceRegistry::instance().getService<MaterialManagerService>())
        return service->damDynamicMaterialLabels();
    return {};
}

void DamDynamicAnalysisPage::populateMaterialCombo(
    QComboBox* combo, const QMap<int, QString>& options, int currentMaterial,
    const QString& emptyText, bool retainUnavailable) const
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
        const QString unavailable = QStringLiteral("编号 %1  当前不可用").arg(currentMaterial);
        combo->insertItem(0, unavailable, currentMaterial);
        combo->setItemData(0, unavailable, Qt::ToolTipRole);
        currentIndex = 0;
    }
    if (combo->count() == 0)
    {
        combo->addItem(emptyText, QVariant());
        combo->setEnabled(false);
        SolverUiLayoutHelper::configureComboSizing(combo);
        return;
    }

    combo->setEnabled(true);
    combo->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);
    SolverUiLayoutHelper::configureComboSizing(combo);
}

QVector<QPair<int, int>> DamDynamicAnalysisPage::materialMappings() const
{
    QVector<QPair<int, int>> result;
    if (!m_materialTable)
        return result;
    result.reserve(m_materialTable->rowCount());
    for (int row = 0; row < m_materialTable->rowCount(); ++row)
    {
        auto* sourceCombo = qobject_cast<QComboBox*>(m_materialTable->cellWidget(row, 0));
        auto* targetCombo = qobject_cast<QComboBox*>(m_materialTable->cellWidget(row, 1));
        result.push_back(qMakePair(sourceCombo ? sourceCombo->currentData().toInt() : 0,
                                   targetCombo ? targetCombo->currentData().toInt() : 0));
    }
    return result;
}

QVector<QPair<int, int>> DamDynamicAnalysisPage::activeDynamicMaterialMappings() const
{
    if (!usesModalAnalysis() && !usesWaveAnalysis())
        return {};
    return materialMappings();
}

void DamDynamicAnalysisPage::setMaterialMappings(const QVector<QPair<int, int>>& mappings)
{
    if (!m_materialTable)
        return;
    m_materialTable->setRowCount(0);
    for (const auto& mapping : mappings)
        addMaterialMappingRow(mapping.first, mapping.second);
    updateTableHeight();
}

void DamDynamicAnalysisPage::addMaterialMappingRow(int sourceMaterial, int targetMaterial,
                                                   int insertAt)
{
    if (!m_materialTable)
        return;
    if (m_materialTable->rowCount() >= MAX_MATERIAL_RELATIONS)
    {
        QMessageBox::information(this, QStringLiteral("数量已达上限"),
                                 QStringLiteral("材料关系最多可设置 300 组。"));
        return;
    }

    const QMap<int, QString> sourceOptions = sourceMaterialOptions();
    const QMap<int, QString> targetOptions = targetMaterialOptions();
    if (sourceMaterial <= 0)
    {
        QSet<int> used;
        for (const auto& mapping : materialMappings())
            used.insert(mapping.first);
        for (auto it = sourceOptions.cbegin(); it != sourceOptions.cend(); ++it)
        {
            if (!used.contains(it.key()))
            {
                sourceMaterial = it.key();
                break;
            }
        }
    }
    if (targetMaterial <= 0)
    {
        QSet<int> used;
        for (const auto& mapping : materialMappings())
            used.insert(mapping.second);
        for (auto it = targetOptions.cbegin(); it != targetOptions.cend(); ++it)
        {
            if (!used.contains(it.key()))
            {
                targetMaterial = it.key();
                break;
            }
        }
    }

    const int row = insertAt < 0 ? m_materialTable->rowCount()
                                 : qBound(0, insertAt, m_materialTable->rowCount());
    m_materialTable->insertRow(row);
    for (int column = 0; column < 2; ++column)
    {
        auto* item = new QTableWidgetItem;
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_materialTable->setItem(row, column, item);
    }

    auto* sourceCombo = new QComboBox(m_materialTable);
    auto* targetCombo = new QComboBox(m_materialTable);
    populateMaterialCombo(sourceCombo, sourceOptions, sourceMaterial,
                          QStringLiteral("暂无已施加的坝体静力材料"), true);
    populateMaterialCombo(targetCombo, targetOptions, targetMaterial,
                          QStringLiteral("请先添加坝料动力本构材料"), true);
    m_materialTable->setCellWidget(row, 0, sourceCombo);
    m_materialTable->setCellWidget(row, 1, targetCombo);

    connect(sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, sourceCombo](int)
            {
                for (int candidate = 0; candidate < m_materialTable->rowCount(); ++candidate)
                {
                    if (m_materialTable->cellWidget(candidate, 0) == sourceCombo)
                    {
                        m_materialTable->setCurrentCell(candidate, 0);
                        refreshMaterialMappingHighlight(candidate);
                        break;
                    }
                }
                updateConditionalState();
                emit requirementsChanged();
            });
    connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, targetCombo](int)
            {
                for (int candidate = 0; candidate < m_materialTable->rowCount(); ++candidate)
                {
                    if (m_materialTable->cellWidget(candidate, 1) == targetCombo)
                    {
                        m_materialTable->setCurrentCell(candidate, 1);
                        refreshMaterialMappingHighlight(candidate);
                        break;
                    }
                }
                updateConditionalState();
                emit requirementsChanged();
            });

    auto activateRow = [this](QComboBox* combo, int column)
    {
        for (int candidate = 0; candidate < m_materialTable->rowCount(); ++candidate)
        {
            if (m_materialTable->cellWidget(candidate, column) == combo)
            {
                m_materialTable->setCurrentCell(candidate, column);
                refreshMaterialMappingHighlight(candidate);
                break;
            }
        }
    };
    connect(sourceCombo, qOverload<int>(&QComboBox::activated), this,
            [activateRow, sourceCombo](int) { activateRow(sourceCombo, 0); });
    connect(targetCombo, qOverload<int>(&QComboBox::activated), this,
            [activateRow, targetCombo](int) { activateRow(targetCombo, 1); });

    m_materialTable->setCurrentCell(row, 0);
    refreshMaterialMappingHighlight(row);
    updateTableHeight();
    updateConditionalState();
    emit requirementsChanged();
}

void DamDynamicAnalysisPage::removeSelectedMaterialMapping()
{
    if (!m_materialTable || m_materialTable->rowCount() <= 0)
        return;
    int row = m_materialTable->currentRow();
    if (row < 0)
        row = m_materialTable->rowCount() - 1;
    m_materialTable->removeRow(row);
    if (m_materialTable->rowCount() > 0)
    {
        const int nextRow = qMin(row, m_materialTable->rowCount() - 1);
        m_materialTable->setCurrentCell(nextRow, 0);
        refreshMaterialMappingHighlight(nextRow);
    }
    else
    {
        clearMaterialMappingHighlight();
    }
    updateTableHeight();
    updateConditionalState();
    emit requirementsChanged();
}

void DamDynamicAnalysisPage::refreshMaterialMappingOptions()
{
    if (!m_materialTable)
        return;
    const QVector<QPair<int, int>> mappings = materialMappings();
    setMaterialMappings(mappings);
}

void DamDynamicAnalysisPage::refreshMaterialMappingHighlight(int row)
{
    if (m_materialTable && row >= 0 && row < m_materialTable->rowCount())
        m_materialTable->selectRow(row);

    if (!m_materialHighlightActive || !m_materialTable || row < 0
        || row >= m_materialTable->rowCount() || !m_materialTable->isEnabled())
    {
        if (m_materialHighlightActive)
            clearMaterialMappingHighlight();
        return;
    }

    auto* sourceCombo = qobject_cast<QComboBox*>(m_materialTable->cellWidget(row, 0));
    auto* targetCombo = qobject_cast<QComboBox*>(m_materialTable->cellWidget(row, 1));
    const int source = sourceCombo ? sourceCombo->currentData().toInt() : 0;
    const int target = targetCombo ? targetCombo->currentData().toInt() : 0;
    const DomainKey sourceKey = materialDomainKey(source);
    const DomainKey targetKey = materialDomainKey(target);

    QSet<DomainKey> applied;
    if (sourceKey.isValid())
        applied.insert(sourceKey);
    if (targetKey.isValid())
        applied.insert(targetKey);

    // 与湿化材料转换保持同一交互：当前静力材料作为主预览高亮，
    // 转换后的动力材料使用第二高亮通道；若动力材料未直接分配到网格，
    // 则模型中只会高亮实际存在的静力材料区域。
    auto& pickManager = PickManager::instance();
    pickManager.activate(getPageType(), m_uuid, PickMode::Domain);
    pickManager.setAppliedDomains(getPageType(), m_uuid, applied);
    pickManager.setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    pickManager.setPreviewDomain(getPageType(), m_uuid, sourceKey);
    pickManager.setPreDeleteDomain(getPageType(), m_uuid, targetKey);
}

void DamDynamicAnalysisPage::clearMaterialMappingHighlight()
{
    auto& pickManager = PickManager::instance();
    pickManager.setAppliedDomains(getPageType(), m_uuid, {});
    pickManager.setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    pickManager.setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    pickManager.setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
}

void DamDynamicAnalysisPage::updateTableHeight()
{
    if (!m_materialTable)
        return;
    const int visibleRows = qBound(2, m_materialTable->rowCount(), 5);
    const int headerHeight = m_materialTable->horizontalHeader()->sizeHint().height();
    const int rowHeight = m_materialTable->verticalHeader()->defaultSectionSize();
    const int frameHeight = m_materialTable->frameWidth() * 2 + 2;
    m_materialTable->setMinimumHeight(headerHeight + qMin(2, qMax(1, m_materialTable->rowCount()))
                                                    * rowHeight + frameHeight);
    m_materialTable->setMaximumHeight(headerHeight + visibleRows * rowHeight + frameHeight);
}

void DamDynamicAnalysisPage::updateConditionalState()
{
    const bool modal = usesModalAnalysis();
    const bool wave = usesWaveAnalysis();
    if (m_analysisEnabled && !wave && comboValue(ui->comboIFSETT) == QStringLiteral("1"))
    {
        const QSignalBlocker blocker(ui->comboIFSETT);
        setComboValue(ui->comboIFSETT, QStringLiteral("0"));
    }
    const bool settlement = usesPermanentSettlement();
    const bool timed = m_analysisEnabled && (wave || settlement);
    const bool mappingEnabled = m_analysisEnabled && (modal || wave);

    ui->groupTasks->setEnabled(m_analysisEnabled);
    ui->groupMaterialMapping->setEnabled(m_analysisEnabled);
    ui->labelModalOrder->setEnabled(modal);
    ui->editModalOrder->setEnabled(modal);
    ui->labelSettlement->setEnabled(m_analysisEnabled && wave);
    ui->comboIFSETT->setEnabled(m_analysisEnabled && wave);
    ui->labelDT->setEnabled(timed);
    ui->editDT->setEnabled(timed);
    ui->labelTSTA->setEnabled(timed);
    ui->editTSTA->setEnabled(timed);
    ui->labelTEND->setEnabled(timed);
    ui->editTEND->setEnabled(timed);
    const bool fallbackFrequency = m_analysisEnabled && wave && !modal;
    ui->labelWOMI1->setEnabled(fallbackFrequency);
    ui->editWOMI1->setEnabled(fallbackFrequency);
    m_materialTable->setEnabled(mappingEnabled);
    m_addButton->setEnabled(mappingEnabled);
    m_removeButton->setEnabled(mappingEnabled && m_materialTable->rowCount() > 0);
    ui->labelMappingCount->setText(
        QStringLiteral("共 %1 组").arg(m_materialTable->rowCount()));
    ui->labelMappingHint->setText(
        mappingEnabled
            ? QStringLiteral("本工况进入动力计算时使用下列静力材料 → 动力材料关系；选中表格行可联动模型高亮。")
            : QStringLiteral("启用模态分析或地震波时程分析后设置材料转换关系。"));

    if (m_materialHighlightActive)
    {
        if (mappingEnabled && m_materialTable->rowCount() > 0)
        {
            const int row = qBound(0, qMax(0, m_materialTable->currentRow()),
                                   m_materialTable->rowCount() - 1);
            refreshMaterialMappingHighlight(row);
        }
        else
        {
            clearMaterialMappingHighlight();
        }
    }
}

void DamDynamicAnalysisPage::setAnalysisEnabled(bool enabled)
{
    if (m_analysisEnabled == enabled)
        return;
    m_analysisEnabled = enabled;
    updateConditionalState();
}

bool DamDynamicAnalysisPage::validateInputs(QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!m_analysisEnabled)
        return true;

    if (usesModalAnalysis())
    {
        bool ok = false;
        const int order = ui->editModalOrder->text().trimmed().toInt(&ok);
        if (!ok || order <= 0)
            return fail(QStringLiteral("启用模态分析时，模态提取阶数必须为正整数。"));
    }

    if (usesPermanentSettlement() && !usesWaveAnalysis())
        return fail(QStringLiteral("地震永久沉降必须与地震波时程分析同时启用。"));

    if (usesWaveAnalysis() || usesPermanentSettlement())
    {
        bool dtOk = false;
        bool startOk = false;
        bool endOk = false;
        const double dt = ui->editDT->text().trimmed().toDouble(&dtOk);
        const double start = ui->editTSTA->text().trimmed().toDouble(&startOk);
        const double end = ui->editTEND->text().trimmed().toDouble(&endOk);
        if (!dtOk || !std::isfinite(dt) || dt <= 0.0)
            return fail(QStringLiteral("计算时步 DT 必须为正数。"));
        if (!startOk || !endOk || !std::isfinite(start) || !std::isfinite(end)
            || end <= start)
            return fail(QStringLiteral("终止时间 TEND 必须大于起始时间 TSTA。"));
    }

    if (usesWaveAnalysis() && !usesModalAnalysis())
    {
        bool ok = false;
        const double omega = ui->editWOMI1->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(omega) || omega < 0.001)
            return fail(QStringLiteral("未做模态分析时，请填写一阶圆频率 ω1（≥ 0.001）。"));
    }

    const QVector<QPair<int, int>> mappings = activeDynamicMaterialMappings();
    if (mappings.size() > MAX_MATERIAL_RELATIONS)
        return fail(QStringLiteral("静力－动力材料转换不能超过 300 组。"));

    const QMap<int, QString> sourceOptions = sourceMaterialOptions();
    const QMap<int, QString> targetOptions = targetMaterialOptions();
    QSet<int> usedSources;
    QSet<int> usedTargets;
    for (int row = 0; row < mappings.size(); ++row)
    {
        const int source = mappings.at(row).first;
        const int target = mappings.at(row).second;
        const QString prefix = QStringLiteral("静力－动力材料转换第 %1 组：").arg(row + 1);
        if (source <= 0 || target <= 0)
            return fail(prefix + QStringLiteral("材料选择不完整。"));
        if (source == target)
            return fail(prefix + QStringLiteral("静力材料和动力材料不能相同。"));
        if (!sourceOptions.contains(source))
            return fail(prefix + QStringLiteral("请选择已施加到坝体的静力材料。"));
        if (!targetOptions.contains(target))
            return fail(prefix + QStringLiteral("请选择坝料动力本构材料。"));
        if (usedSources.contains(source))
            return fail(prefix + QStringLiteral("同一种静力材料不能重复设置。"));
        if (usedTargets.contains(target))
            return fail(prefix + QStringLiteral("同一种动力材料不能对应多个静力材料。"));
        usedSources.insert(source);
        usedTargets.insert(target);
    }
    return true;
}

QJsonObject DamDynamicAnalysisPage::solverState() const
{
    QJsonObject state = currentState();
    if (m_analysisEnabled)
        return state;

    QJsonObject values = state.value(QStringLiteral("values")).toObject();
    values[QStringLiteral("IFMODE")] = QStringLiteral("0");
    values[QStringLiteral("IFWAVE")] = QStringLiteral("0");
    values[QStringLiteral("IFSETT")] = QStringLiteral("0");
    state[QStringLiteral("values")] = values;
    state[QStringLiteral("dynamic_material_table")] = QJsonArray();
    return state;
}

QJsonObject DamDynamicAnalysisPage::normalizedState(const QJsonObject& obj) const
{
    QJsonObject result;
    QJsonObject values = obj.value(QStringLiteral("values")).toObject();
    auto copyLegacyValue = [&values, &obj](const QString& key, const QString& fallback)
    {
        if (values.contains(key))
            return;
        const QString direct = obj.value(key).toVariant().toString().trimmed();
        values[key] = direct.isEmpty() ? fallback : direct;
    };
    copyLegacyValue(QStringLiteral("IFMODE"), QStringLiteral("0"));
    copyLegacyValue(QStringLiteral("IFWAVE"), QStringLiteral("0"));
    copyLegacyValue(QStringLiteral("IFSETT"), QStringLiteral("0"));
    copyLegacyValue(QStringLiteral("DT"), QStringLiteral("0.020000"));
    copyLegacyValue(QStringLiteral("TSTA"), QStringLiteral("0.000000"));
    copyLegacyValue(QStringLiteral("TEND"), QStringLiteral("0.000000"));
    copyLegacyValue(QStringLiteral("WOMI_1"), QStringLiteral("0.000000"));
    result[QStringLiteral("values")] = values;
    const int modalOrder = obj.value(QStringLiteral("modal_order")).toInt(10);
    result[QStringLiteral("modal_order")] = modalOrder > 0 ? modalOrder : 10;
    result[QStringLiteral("dynamic_material_table")] =
        obj.value(QStringLiteral("dynamic_material_table")).toArray();
    return result;
}

QJsonObject DamDynamicAnalysisPage::currentState() const
{
    QJsonObject obj;
    QJsonObject values;
    values[QStringLiteral("IFMODE")] = comboValue(ui->comboIFMODE);
    values[QStringLiteral("IFWAVE")] = comboValue(ui->comboIFWAVE);
    values[QStringLiteral("IFSETT")] = comboValue(ui->comboIFSETT);
    values[QStringLiteral("DT")] = ui->editDT->text().trimmed();
    values[QStringLiteral("TSTA")] = ui->editTSTA->text().trimmed();
    values[QStringLiteral("TEND")] = ui->editTEND->text().trimmed();
    values[QStringLiteral("WOMI_1")] = ui->editWOMI1->text().trimmed();
    obj[QStringLiteral("values")] = values;
    obj[QStringLiteral("modal_order")] = modalExtractionOrder();

    QJsonArray rows;
    for (const auto& mapping : materialMappings())
    {
        QJsonArray row;
        row.append(QString::number(mapping.first));
        row.append(QString::number(mapping.second));
        rows.append(row);
    }
    obj[QStringLiteral("dynamic_material_table")] = rows;
    return obj;
}

QJsonObject DamDynamicAnalysisPage::saveJson()
{
    return currentState();
}

void DamDynamicAnalysisPage::loadJson(const QJsonObject& obj)
{
    const QJsonObject state = normalizedState(obj);
    const QJsonObject values = state.value(QStringLiteral("values")).toObject();
    setComboValue(ui->comboIFMODE, values.value(QStringLiteral("IFMODE")).toString(QStringLiteral("0")));
    setComboValue(ui->comboIFWAVE, values.value(QStringLiteral("IFWAVE")).toString(QStringLiteral("0")));
    setComboValue(ui->comboIFSETT, values.value(QStringLiteral("IFSETT")).toString(QStringLiteral("0")));
    ui->editDT->setText(values.value(QStringLiteral("DT")).toString(QStringLiteral("0.020000")));
    ui->editTSTA->setText(values.value(QStringLiteral("TSTA")).toString(QStringLiteral("0.000000")));
    ui->editTEND->setText(values.value(QStringLiteral("TEND")).toString(QStringLiteral("0.000000")));
    ui->editWOMI1->setText(values.value(QStringLiteral("WOMI_1")).toString(QStringLiteral("0.000000")));
    setModalExtractionOrder(state.value(QStringLiteral("modal_order")).toInt(10));
    setMaterialMappings(mappingsFromJson(state.value(QStringLiteral("dynamic_material_table")).toArray()));
    updateConditionalState();
}

void DamDynamicAnalysisPage::loadLegacyFillStorageState(const QJsonObject& legacyState)
{
    QJsonObject state;
    const QJsonObject oldValues = legacyState.value(QStringLiteral("values")).toObject();
    QJsonObject values;
    for (const QString& key : {QStringLiteral("IFMODE"), QStringLiteral("IFWAVE"),
                               QStringLiteral("IFSETT"), QStringLiteral("DT"),
                               QStringLiteral("TSTA"), QStringLiteral("TEND"),
                               QStringLiteral("WOMI_1")})
    {
        if (oldValues.contains(key))
            values[key] = oldValues.value(key);
    }
    state[QStringLiteral("values")] = values;
    state[QStringLiteral("dynamic_material_table")] =
        legacyState.value(QStringLiteral("dynamic_material_table")).toArray();
    state[QStringLiteral("modal_order")] = legacyState.value(QStringLiteral("modal_order")).toInt(10);
    loadJson(state);
}
