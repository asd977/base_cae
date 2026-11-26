#include "DamAnalysisWidget.h"
#include "DamInputProtocol.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringConverter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "ui_DamAnalysisWidget.h"

#include "SolverUiLayoutHelper.h"
#include "SolverQuietBoundary.h"
#include "QuietBoundarySelector.h"
DamAnalysisWidget::DamAnalysisWidget(QWidget* parent)
    : BasePage("DamAnalysis"), ui(new Ui::DamAnalysisWidget)
{
    ui->setupUi(this);
    initValueEditors();
    setupConstructionLayerUi();
    m_quietBoundaryCheck = new QCheckBox(QStringLiteral("考虑粘弹性边界"), ui->groupDynamic);
    m_quietBoundaryCheck->setObjectName(QStringLiteral("quietBoundaryCheck"));
    m_quietBoundaryCheck->setToolTip(QStringLiteral("用于吸收人工截断边界的波动；启用后在模型中选择边界区域。"));
    m_quietBoundarySelection = new QuietBoundarySelector(ui->groupDynamic);
    m_quietBoundaryStatus = m_quietBoundarySelection->statusLabel();
    ui->formLayoutDynamic->addRow(QString(),m_quietBoundaryCheck);
    ui->formLayoutDynamic->addRow(m_quietBoundarySelection);
    m_quietBoundarySelection->setEnabled(false);
    connect(m_quietBoundaryCheck,&QCheckBox::toggled,this,[this](bool enabled){
        m_quietBoundarySelection->setEnabled(enabled);
        if(!enabled)m_quietBoundarySelection->stop();
    });
    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);
    connect(ui->btnCompute, &QPushButton::clicked, this, &DamAnalysisWidget::onComputeClicked);
}

DamAnalysisWidget::~DamAnalysisWidget()
{
    clearConstructionLayerHighlights(false);
    delete ui;
}

void DamAnalysisWidget::beforeLoad()
{
    m_constructionLayerUiActive = true;
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (m_constructionLayerUiDirty && grid &&
        m_constructionPanelGridMTime == qulonglong(grid->GetMTime()))
    {
        updateConstructionLayerHighlights();
    }
    else
    {
        rebuildConstructionLayerPanel();
    }
}

void DamAnalysisWidget::beforeUnload()
{
    if(m_quietBoundarySelection)m_quietBoundarySelection->stop();
    m_constructionLayerUiActive = false;
    clearConstructionLayerHighlights();
}

QPair<int, int> DamAnalysisWidget::constructionLayerRange() const
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (!grid || grid->GetNumberOfCells() <= 0 || !grid->GetCellData())
        return {};

    vtkDataArray* layerArray = grid->GetCellData()->GetArray("LAYER");
    if (!layerArray)
        return {};
    vtkDataArray* materialArray = grid->GetCellData()->GetArray("MATYPE");

    const int targetDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    int minimumLayer = std::numeric_limits<int>::max();
    int maximumLayer = std::numeric_limits<int>::min();
    bool hasFoundation = false;
    QSet<int> damLayers;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != targetDim)
            continue;
        if (materialArray && int(materialArray->GetTuple1(cellId)) == 1)
            continue;
        const int layer = int(layerArray->GetTuple1(cellId));
        if (layer == 1)
        {
            hasFoundation = true;
            continue;
        }
        if (layer < 1)
            return {};
        damLayers.insert(layer);
        minimumLayer = qMin(minimumLayer, layer);
        maximumLayer = qMax(maximumLayer, layer);
    }
    if (!hasFoundation || minimumLayer != 2 || maximumLayer < minimumLayer)
        return {};
    for (int layer = 2; layer <= maximumLayer; ++layer)
    {
        if (!damLayers.contains(layer))
            return {};
    }
    return qMakePair(minimumLayer, maximumLayer);
}

void DamAnalysisWidget::setupConstructionLayerUi()
{
    m_groupConstructionLayers = new QGroupBox(QStringLiteral("坝体施工分层"), this);
    m_groupConstructionLayers->setObjectName(QStringLiteral("groupConstructionLayers"));
    auto* layout = new QVBoxLayout(m_groupConstructionLayers);
    m_labelConstructionLayerStatus = new QLabel(m_groupConstructionLayers);
    m_labelConstructionLayerStatus->setObjectName(QStringLiteral("labelConstructionLayerStatus"));
    m_labelConstructionLayerStatus->setWordWrap(true);
    layout->addWidget(m_labelConstructionLayerStatus);

    m_constructionRegionTable = new QTableWidget(m_groupConstructionLayers);
    m_constructionRegionTable->setObjectName(QStringLiteral("constructionRegionTable"));
    m_constructionRegionTable->setMinimumHeight(240);
    m_constructionRegionTable->setMaximumHeight(360);
    m_constructionRegionTable->setColumnCount(4);
    m_constructionRegionTable->setHorizontalHeaderLabels(
        {QStringLiteral("网格区域"), QStringLiteral("当前识别"),
         QStringLiteral("单元数"), QStringLiteral("区域用途")});
    m_constructionRegionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_constructionRegionTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_constructionRegionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_constructionRegionTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_constructionRegionTable->verticalHeader()->setVisible(false);
    m_constructionRegionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_constructionRegionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_constructionRegionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_constructionRegionTable->setAlternatingRowColors(true);
    layout->addWidget(m_constructionRegionTable);

    auto* controls = new QHBoxLayout();
    auto* countLabel = new QLabel(QStringLiteral("坝体填筑层数"), m_groupConstructionLayers);
    m_fillLayerCount = new QSpinBox(m_groupConstructionLayers);
    m_fillLayerCount->setObjectName(QStringLiteral("fillLayerCount"));
    m_fillLayerCount->setRange(1, 200);
    m_fillLayerCount->setValue(8);
    controls->addWidget(countLabel);
    controls->addWidget(m_fillLayerCount);
    controls->addStretch();
    layout->addLayout(controls);
    ui->contentLayout->insertWidget(0, m_groupConstructionLayers);

    connect(m_fillLayerCount, qOverload<int>(&QSpinBox::valueChanged),
            this, &DamAnalysisWidget::markConstructionLayerDirty);
    connect(m_constructionRegionTable, &QTableWidget::currentCellChanged, this,
            [this]() { updateConstructionLayerHighlights(); });

    rebuildConstructionLayerPanel();
}

QString DamAnalysisWidget::constructionRegionDisplayText(const MeshTagKey& key) const
{
    if (key.category.compare(QStringLiteral("LAYER"), Qt::CaseInsensitive) == 0)
    {
        if (key.id == 1)
            return QStringLiteral("坝基（现有分层）");
        if (key.id > 1)
            return QStringLiteral("填筑第 %1 层（现有分层）").arg(key.id - 1);
        return QStringLiteral("待修正区域");
    }

    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return QStringLiteral("网格区域 %1").arg(key.id);
    if (key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0)
    {
        const QString materialName = meshService->matypePhysicalName(key.id, key.dim).trimmed();
        if (!materialName.isEmpty())
            return materialName;
        for (const MeshTagSet& tag : meshService->domainTagSets())
        {
            if (tag.key == key && tag.name.startsWith(QStringLiteral("几何区域")))
                return tag.name;
        }
        return QStringLiteral("材料区域 %1").arg(key.id);
    }
    return meshService->displayTextOfTag(key);
}

void DamAnalysisWidget::rebuildConstructionLayerPanel()
{
    if (!m_labelConstructionLayerStatus || !m_constructionRegionTable || !m_fillLayerCount)
        return;

    m_constructionLayerUiUpdating = true;
    m_constructionLayerUiDirty = false;
    m_constructionRegionKeys.clear();
    m_constructionRegionCells.clear();
    m_constructionRegionTable->clearContents();
    m_constructionRegionTable->setRowCount(0);

    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (!meshService || !grid || grid->GetNumberOfCells() <= 0 || !grid->GetCellData())
    {
        m_existingConstructionLayersValid = false;
        m_constructionPanelGridMTime = 0;
        m_labelConstructionLayerStatus->setText(QStringLiteral("尚未生成或导入网格"));
        m_constructionRegionTable->setEnabled(false);
        m_fillLayerCount->setEnabled(false);
        m_constructionLayerUiUpdating = false;
        clearConstructionLayerHighlights();
        return;
    }

    m_constructionRegionTable->setEnabled(true);
    m_fillLayerCount->setEnabled(true);
    m_constructionPanelGridMTime = qulonglong(grid->GetMTime());
    const int targetDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    vtkDataArray* layerArray = grid->GetCellData()->GetArray("LAYER");
    vtkDataArray* materialArray = grid->GetCellData()->GetArray("MATYPE");
    QSet<MeshIdType> requiredCells;
    QMap<int, int> layerCounts;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != targetDim)
            continue;
        if (materialArray && int(materialArray->GetTuple1(cellId)) == 1)
            continue;
        requiredCells.insert(static_cast<MeshIdType>(cellId));
        if (layerArray)
            ++layerCounts[int(layerArray->GetTuple1(cellId))];
    }

    const QPair<int, int> currentRange = constructionLayerRange();
    m_existingConstructionLayersValid = currentRange.first == 2;
    bool hasAssignedDamLayer = false;
    for (auto it = layerCounts.constBegin(); it != layerCounts.constEnd(); ++it)
    {
        if (it.key() > 1)
        {
            hasAssignedDamLayer = true;
            break;
        }
    }
    struct RegionChoice
    {
        MeshTagKey key;
        QVector<MeshIdType> cells;
    };
    auto collectRegions = [&](const QString& category)
    {
        QVector<RegionChoice> regions;
        for (const MeshTagSet& tag : meshService->domainTagSets())
        {
            if (tag.key.category.compare(category, Qt::CaseInsensitive) != 0 ||
                tag.key.dim != targetDim)
                continue;
            QVector<MeshIdType> cells;
            for (MeshIdType cellId : meshService->cellIdsOfTag(tag.key))
            {
                if (requiredCells.contains(cellId))
                    cells.push_back(cellId);
            }
            if (!cells.isEmpty())
                regions.push_back({tag.key, cells});
        }
        return regions;
    };
    auto isExactPartition = [&](const QVector<RegionChoice>& regions)
    {
        if (regions.size() < 2)
            return false;
        QHash<MeshIdType, int> occurrences;
        for (const RegionChoice& region : regions)
        {
            for (MeshIdType cellId : region.cells)
                ++occurrences[cellId];
        }
        if (occurrences.size() != requiredCells.size())
            return false;
        for (MeshIdType cellId : requiredCells)
        {
            if (occurrences.value(cellId) != 1)
                return false;
        }
        return true;
    };

    QVector<RegionChoice> regions;
    // A fill LAYER is a calculation stage, not an original foundation/dam region.
    // Keep the same source-region identity before and after assigning fill layers.
    const QString sourceCategory =
        meshService->domainCategoryForUse(MeshDomainUse::MaterialRegion);
    for (const QString& category : QStringList{sourceCategory})
    {
        QVector<RegionChoice> candidate = collectRegions(category);
        if (isExactPartition(candidate))
        {
            regions = std::move(candidate);
            break;
        }
    }

    m_constructionRegionTable->setRowCount(regions.size());
    const QColor foundationColor(QStringLiteral("#2F8FB5"));
    const QColor damColor(QStringLiteral("#E28A2B"));
    auto colorIcon = [](const QColor& color)
    {
        QPixmap swatch(14, 14);
        swatch.fill(color);
        return QIcon(swatch);
    };

    for (int row = 0; row < regions.size(); ++row)
    {
        const RegionChoice& region = regions.at(row);
        m_constructionRegionKeys.push_back(region.key);
        m_constructionRegionCells.push_back(region.cells);

        int foundationCount = 0;
        int damCount = 0;
        for (MeshIdType rawCellId : region.cells)
        {
            const vtkIdType cellId = static_cast<vtkIdType>(rawCellId);
            const int layer = layerArray ? int(layerArray->GetTuple1(cellId)) : 0;
            if (layer == 1)
                ++foundationCount;
            else if (layer > 1)
                ++damCount;
        }

        QString recognizedText = QStringLiteral("待指定");
        if (hasAssignedDamLayer && foundationCount == region.cells.size())
            recognizedText = QStringLiteral("坝基");
        else if (hasAssignedDamLayer && damCount == region.cells.size())
            recognizedText = QStringLiteral("坝体");
        else if (hasAssignedDamLayer && foundationCount + damCount > 0)
            recognizedText = QStringLiteral("混合区域");

        auto* nameItem = new QTableWidgetItem(constructionRegionDisplayText(region.key));
        auto* recognizedItem = new QTableWidgetItem(recognizedText);
        auto* countItem = new QTableWidgetItem(QString::number(region.cells.size()));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        recognizedItem->setFlags(recognizedItem->flags() & ~Qt::ItemIsEditable);
        countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
        m_constructionRegionTable->setItem(row, 0, nameItem);
        m_constructionRegionTable->setItem(row, 1, recognizedItem);
        m_constructionRegionTable->setItem(row, 2, countItem);

        const int currentDamType = damType();
        auto* purpose = new QComboBox(m_constructionRegionTable);
        purpose->addItem(QStringLiteral("请选择"), 0);
        purpose->addItem(colorIcon(foundationColor), QStringLiteral("坝基"), 1);
        purpose->addItem(colorIcon(damColor), QStringLiteral("坝体"), 2);
        if (currentDamType == 1)
        {
            purpose->addItem(QStringLiteral("混凝土面板"), 3);
            purpose->addItem(QStringLiteral("附属结构（趾板、接缝等）"), 4);
        }
        else if (currentDamType == 2 || currentDamType == 3)
        {
            purpose->addItem(QStringLiteral("防渗心墙"), 5);
        }
        const QString name = nameItem->text().toCaseFolded();
        const bool nameLooksLikeDam = name.contains(QStringLiteral("坝体")) ||
                                      name.contains(QStringLiteral("填筑")) ||
                                      name.contains(QStringLiteral("dam")) ||
                                      name.contains(QStringLiteral("fill"));
        const bool nameLooksLikeFoundation = name.contains(QStringLiteral("坝基")) ||
                                             name.contains(QStringLiteral("基岩")) ||
                                             name.contains(QStringLiteral("foundation")) ||
                                             name.contains(QStringLiteral("rock"));
        int purposeValue = 0;
        if (hasAssignedDamLayer && foundationCount == region.cells.size())
            purposeValue = 1;
        else if (hasAssignedDamLayer && damCount == region.cells.size())
            purposeValue = 2;
        else if (nameLooksLikeDam && !nameLooksLikeFoundation)
            purposeValue = 2;
        else if (nameLooksLikeFoundation && !nameLooksLikeDam)
            purposeValue = 1;
        const QString roleKey = QStringLiteral("%1/%2/%3")
            .arg(region.key.category).arg(region.key.dim).arg(region.key.id);
        if (m_constructionRegionRoles.contains(roleKey))
            purposeValue = m_constructionRegionRoles.value(roleKey);
        const int roleIdx = purpose->findData(purposeValue);
        if (roleIdx >= 0)
            purpose->setCurrentIndex(roleIdx);
        else
        {
            purpose->setCurrentIndex(0);
            m_constructionLayerUiDirty = true;
        }
        SolverUiLayoutHelper::configureComboSizing(purpose);
        purpose->setToolTip(QStringLiteral("蓝色表示坝基，橙色表示填筑区域。面板坝需区分面板与附属结构材料。"));
        connect(purpose, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &DamAnalysisWidget::markConstructionLayerDirty);
        m_constructionRegionTable->setCellWidget(row, 3, purpose);
    }

    m_fillLayerCount->setValue(m_existingConstructionLayersValid ? currentRange.second - 1 : 8);
    if (m_existingConstructionLayersValid)
    {
        int damCellCount = 0;
        for (auto it = layerCounts.constBegin(); it != layerCounts.constEnd(); ++it)
        {
            if (it.key() > 1)
                damCellCount += it.value();
        }
        m_labelConstructionLayerStatus->setText(
            QStringLiteral("已识别现有分层：坝基 %1 个单元，坝体 %2 个单元，共 %3 个填筑层")
                .arg(layerCounts.value(1))
                .arg(damCellCount)
                .arg(currentRange.second - 1));
    }
    else if (!regions.isEmpty() && !hasAssignedDamLayer)
    {
        m_labelConstructionLayerStatus->setText(
            QStringLiteral("已识别 %1 个网格区域，请逐项指定坝基或坝体")
                .arg(regions.size()));
    }
    else if (layerArray && !layerCounts.isEmpty())
    {
        m_labelConstructionLayerStatus->setText(
            QStringLiteral("现有分层不完整，请在区域用途中修正"));
    }
    else
    {
        m_labelConstructionLayerStatus->setText(QStringLiteral("尚未设置坝体施工分层"));
    }
    if (regions.isEmpty())
    {
        m_labelConstructionLayerStatus->setText(
            m_labelConstructionLayerStatus->text() +
            QStringLiteral("；网格缺少可独立识别的坝基和坝体区域"));
    }
    m_fillLayerCount->setEnabled(!regions.isEmpty());

    if (m_constructionRegionTable->rowCount() > 0)
        m_constructionRegionTable->setCurrentCell(0, 0);
    m_constructionLayerUiUpdating = false;
    updateConstructionLayerHighlights();
}

void DamAnalysisWidget::markConstructionLayerDirty()
{
    if (m_constructionLayerUiUpdating)
        return;
    m_constructionLayerUiDirty = true;
    captureConstructionRegionRoles();
    updateConstructionLayerHighlights();
    applyConstructionLayerChanges();
}

void DamAnalysisWidget::applyConstructionLayerChanges()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService || !m_fillLayerCount || m_constructionRegionCells.isEmpty())
        return;

    QSet<MeshIdType> foundationCells;
    QSet<MeshIdType> damCells;
    int firstUnassignedRow = -1;
    for (int row = 0; row < m_constructionRegionCells.size(); ++row)
    {
        auto* purpose = qobject_cast<QComboBox*>(m_constructionRegionTable->cellWidget(row, 3));
        const int purposeValue = purpose ? purpose->currentData().toInt() : 0;
        if (purposeValue == 0)
        {
            if (firstUnassignedRow < 0)
                firstUnassignedRow = row;
            continue;
        }
        QSet<MeshIdType>& target = purposeValue >= 2 ? damCells : foundationCells;
        for (MeshIdType cellId : m_constructionRegionCells.at(row))
            target.insert(cellId);
    }

    if (firstUnassignedRow >= 0)
    {
        m_constructionRegionTable->setCurrentCell(firstUnassignedRow, 0);
        m_labelConstructionLayerStatus->setText(
            QStringLiteral("还有区域未指定用途，黄色高亮处需要选择坝基或坝体"));
        updateConstructionLayerHighlights();
        return;
    }

    clearConstructionLayerHighlights(false);
    QString error;
    if (!meshService->assignDamConstructionLayers(
            foundationCells, damCells, m_fillLayerCount->value(), &error))
    {
        m_labelConstructionLayerStatus->setText(error);
        updateConstructionLayerHighlights();
        return;
    }

    rebuildConstructionLayerPanel();
    const QPair<int, int> range = constructionLayerRange();
    if (range.first == 2)
        emit constructionLayersChanged(range.first, range.second);
}

void DamAnalysisWidget::clearConstructionLayerHighlights(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    const QString prefix = QStringLiteral("dam.construction.%1.").arg(m_uuid);
    visual->removeProp(prefix + QStringLiteral("foundation"), false);
    visual->removeProp(prefix + QStringLiteral("body"), false);
    visual->removeProp(prefix + QStringLiteral("preview"), false);
    m_constructionBaseHighlightSignature.clear();
    if (renderNow)
        visual->render();
}

void DamAnalysisWidget::updateConstructionLayerHighlights()
{
    if (!m_constructionLayerUiActive || m_constructionLayerUiUpdating)
        return;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (!visual || !grid || grid->GetNumberOfCells() <= 0)
        return;

    QSet<MeshIdType> foundationCells;
    QSet<MeshIdType> damCells;
    for (int row = 0; row < m_constructionRegionCells.size(); ++row)
    {
        auto* purpose = qobject_cast<QComboBox*>(m_constructionRegionTable->cellWidget(row, 3));
        const int purposeValue = purpose ? purpose->currentData().toInt() : 0;
        if (purposeValue == 0)
            continue;
        QSet<MeshIdType>& target = purposeValue >= 2 ? damCells : foundationCells;
        for (MeshIdType cellId : m_constructionRegionCells.at(row))
            target.insert(cellId);
    }

    if (m_constructionRegionCells.isEmpty() && grid->GetCellData())
    {
        vtkDataArray* layerArray = grid->GetCellData()->GetArray("LAYER");
        vtkDataArray* materialArray = grid->GetCellData()->GetArray("MATYPE");
        const int targetDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
        if (layerArray)
        {
            for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (!cell || cell->GetCellDimension() != targetDim ||
                    (materialArray && int(materialArray->GetTuple1(cellId)) == 1))
                    continue;
                if (int(layerArray->GetTuple1(cellId)) == 1)
                    foundationCells.insert(static_cast<MeshIdType>(cellId));
                else if (int(layerArray->GetTuple1(cellId)) > 1)
                    damCells.insert(static_cast<MeshIdType>(cellId));
            }
        }
    }

    QSet<MeshIdType> previewCells;
    const int row = m_constructionRegionTable->currentRow();
    if (row >= 0 && row < m_constructionRegionCells.size())
    {
        for (MeshIdType cellId : m_constructionRegionCells.at(row))
            previewCells.insert(cellId);
    }

    const QString prefix = QStringLiteral("dam.construction.%1.").arg(m_uuid);
    QStringList roleSignature;
    roleSignature.reserve(m_constructionRegionCells.size());
    for (int row = 0; row < m_constructionRegionCells.size(); ++row)
    {
        auto* purpose = qobject_cast<QComboBox*>(m_constructionRegionTable->cellWidget(row, 3));
        roleSignature.push_back(purpose ? purpose->currentData().toString() : QStringLiteral("0"));
    }
    const QString baseSignature =
        QStringLiteral("%1:%2:%3")
            .arg(quintptr(grid))
            .arg(qulonglong(grid->GetMTime()))
            .arg(roleSignature.join(QLatin1Char(',')));
    const bool rebuildBaseHighlights =
        baseSignature != m_constructionBaseHighlightSignature;
    if (rebuildBaseHighlights)
    {
        visual->removeProp(prefix + QStringLiteral("foundation"), false);
        visual->removeProp(prefix + QStringLiteral("body"), false);
    }
    visual->removeProp(prefix + QStringLiteral("preview"), false);

    auto addHighlight = [&](const QString& suffix, const QString& name,
                            const QSet<MeshIdType>& cells, const QColor& color,
                            double opacity, double lineWidth)
    {
        if (cells.isEmpty())
            return;
        auto ids = vtkSmartPointer<vtkIdList>::New();
        ids->SetNumberOfIds(cells.size());
        int index = 0;
        for (MeshIdType cellId : cells)
            ids->SetId(index++, static_cast<vtkIdType>(cellId));
        auto extract = vtkSmartPointer<vtkExtractCells>::New();
        extract->SetInputData(grid);
        extract->SetCellList(ids);
        auto geometry = vtkSmartPointer<vtkGeometryFilter>::New();
        geometry->SetInputConnection(extract->GetOutputPort());
        geometry->Update();
        auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
        mapper->SetInputData(geometry->GetOutput());
        mapper->ScalarVisibilityOff();
        mapper->SetResolveCoincidentTopologyToPolygonOffset();
        mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -1.0);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
        actor->GetProperty()->SetOpacity(opacity);
        actor->GetProperty()->EdgeVisibilityOn();
        actor->GetProperty()->SetEdgeColor(color.redF(), color.greenF(), color.blueF());
        actor->GetProperty()->SetLineWidth(lineWidth);
        visual->addProp(prefix + suffix, actor, QStringLiteral("土石坝"), name, false, false);
    };

    if (rebuildBaseHighlights)
    {
        addHighlight(QStringLiteral("foundation"), QStringLiteral("坝基区域"), foundationCells,
                     QColor(QStringLiteral("#2F8FB5")), 0.34, 1.5);
        addHighlight(QStringLiteral("body"), QStringLiteral("坝体区域"), damCells,
                     QColor(QStringLiteral("#E28A2B")), 0.34, 1.5);
        m_constructionBaseHighlightSignature = baseSignature;
    }
    addHighlight(QStringLiteral("preview"), QStringLiteral("当前查看区域"), previewCells,
                 QColor(QStringLiteral("#F2D13D")), 0.76, 2.5);
    visual->render();
}

void DamAnalysisWidget::initValueEditors()
{
    m_comboMethodIter = ui->comboMethodIter;
    m_editIter = ui->editIter;
    m_editIter->setValidator(new QIntValidator(1, 1000000, m_editIter));
    m_editX1 = ui->editX1;
    m_editX2 = ui->editX2;
    m_editX3 = ui->editX3;
    m_editAlfa1 = ui->editAlfa1;
    m_editAlfa2 = ui->editAlfa2;
    m_editAlfa3 = ui->editAlfa3;
    m_comboUpstreamAxis = ui->comboAxisUp;
    m_comboPgen = ui->comboPgen;
    m_comboIfRock = ui->comboIfRock;
    m_comboIfIso = ui->comboIfIso;

    m_damTypeCombo = new QComboBox(this);
    m_damTypeCombo->setObjectName(QStringLiteral("comboDamType"));
    m_damTypeCombo->addItem(QStringLiteral("均质坝"), QStringLiteral("4"));
    m_damTypeCombo->addItem(QStringLiteral("混凝土面板堆石坝"), QStringLiteral("1"));
    m_damTypeCombo->addItem(QStringLiteral("粘土心墙堆石坝"), QStringLiteral("2"));
    m_damTypeCombo->addItem(QStringLiteral("沥青心墙堆石坝"), QStringLiteral("3"));
    ui->formLayoutMode->addRow(QStringLiteral("坝体类型"), m_damTypeCombo);
    connect(m_damTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (m_loadingSettings) return;
        captureConstructionRegionRoles();
        rebuildConstructionLayerPanel();
        emit requirementsChanged();
    });

    m_groupFreezeThaw = new QGroupBox(QStringLiteral("结冰变形"), this);
    m_groupFreezeThaw->setObjectName(QStringLiteral("groupFreezeThaw"));
    m_groupFreezeThaw->setCheckable(true);
    m_groupFreezeThaw->setChecked(false);
    auto* freezeForm = new QFormLayout(m_groupFreezeThaw);
    auto* iceHint = new QLabel(
        QStringLiteral("启用后，请在“结冰区域”选择受冰冻作用的节点。"),
        m_groupFreezeThaw);
    iceHint->setWordWrap(true);
    freezeForm->addRow(iceHint);
    m_editIceExpansion = new QLineEdit(QStringLiteral("0.0357"), m_groupFreezeThaw);
    m_editIceExpansion->setObjectName(QStringLiteral("editLine37"));
    freezeForm->addRow(QStringLiteral("冰线膨胀率"), m_editIceExpansion);
    ui->contentLayout->insertWidget(ui->contentLayout->indexOf(ui->btnCompute),
                                    m_groupFreezeThaw);

    m_lineEditors[1] = nullptr;
    m_lineEditors[2] = nullptr;
    m_lineEditors[3] = nullptr;
    m_lineEditors[4] = nullptr;
    m_lineEditors[5] = nullptr;
    m_lineEditors[6] = nullptr;
    m_lineEditors[7] = nullptr;
    m_lineEditors[8] = nullptr;
    m_lineEditors[9] = nullptr;
    m_lineEditors[10] = m_damTypeCombo;
    m_lineEditors[11] = nullptr;
    m_lineEditors[12] = nullptr;
    m_lineEditors[13] = nullptr;
    m_lineEditors[14] = nullptr;
    m_lineEditors[15] = ui->comboLine15;
    m_lineEditors[16] = ui->comboLine16;
    m_lineEditors[17] = ui->editLine17;
    m_lineEditors[18] = ui->editLine18;
    m_lineEditors[19] = ui->comboLine19;
    m_lineEditors[20] = nullptr;
    m_lineEditors[21] = ui->editLine21;
    m_lineEditors[22] = ui->editLine22;
    m_lineEditors[23] = ui->editLine23;
    m_lineEditors[24] = ui->editLine24;
    m_lineEditors[25] = ui->editLine25;
    m_lineEditors[26] = ui->editLine26;
    m_lineEditors[27] = ui->editLine27;
    m_lineEditors[28] = ui->editLine28;
    m_lineEditors[29] = ui->comboLine29;
    m_lineEditors[30] = ui->editLine30;
    // 均质坝不使用 31~35 行的面板坝冻融材料退化参数，固定输出安全默认值。
    m_lineEditors[31] = nullptr;
    m_lineEditors[32] = nullptr;
    m_lineEditors[33] = nullptr;
    m_lineEditors[34] = nullptr;
    m_lineEditors[35] = nullptr;
    m_lineEditors[36] = nullptr;
    m_lineEditors[37] = m_editIceExpansion;
    m_lineEditors[38] = nullptr;
    m_lineEditors[39] = nullptr;
    m_lineEditors[40] = nullptr;

    ui->comboLine15->setItemData(0, "0");
    ui->comboLine15->setItemData(1, "1");
    ui->comboLine15->setItemText(0, QStringLiteral("系统统一阻尼"));
    ui->comboLine15->setItemText(1, QStringLiteral("按材料分别设置阻尼"));

    ui->comboLine16->setItemData(0, "0");
    ui->comboLine16->setItemData(1, "1");
    ui->comboLine16->setItemText(0, QStringLiteral("非瑞雷阻尼"));
    ui->comboLine16->setItemText(1, QStringLiteral("瑞雷阻尼"));

    ui->comboLine19->clear();
    ui->comboLine19->addItem(QStringLiteral("仅静力计算"), QStringLiteral("1"));
    ui->comboLine19->addItem(QStringLiteral("仅动力计算（使用本工况已有静力结果）"),
                             QStringLiteral("2"));
    ui->comboLine19->addItem(QStringLiteral("先静力后动力计算"), QStringLiteral("3"));
    ui->comboLine19->setToolTip(
        QStringLiteral("使用“仅动力计算”前，请先用相同工况完成一次静力计算。"));

    ui->comboLine29->setItemData(0, "0");
    ui->comboLine29->setItemData(1, "1");
    ui->comboLine29->setItemText(0, QStringLiteral("不计算"));
    ui->comboLine29->setItemText(1, QStringLiteral("计算"));
    ui->comboLine29->setToolTip(
        QStringLiteral("模态分析在“动力分析”子节点中设置。"));
    ui->comboLine29->setEnabled(false);
    // 是否执行模态分析由“动力分析”子节点的 IFMODE 决定。
    // 根页面不再重复显示派生开关。
    ui->formLayoutDynamic->setRowVisible(ui->comboLine29, false);
    // P030 仍按求解器格式保存在土石坝总参数中，但用户入口与模态开关统一放在“动力分析”子节点。
    ui->formLayoutDynamic->setRowVisible(ui->editLine30, false);
    ui->editLine30->setValidator(
        new QIntValidator(1, std::numeric_limits<int>::max(), ui->editLine30));

    // SOLVER AUTOPACK branches for METHOD_ITER1=1/2/3 have uninitialized HA1/HA2/ILAYER
    // and zero out reservoir water loads; lock dam fill to METHOD_ITER1=4.
    m_comboMethodIter->clear();
    m_comboMethodIter->addItem(QStringLiteral("中点增量法（土坝填筑）"), QStringLiteral("4"));
    m_comboMethodIter->setCurrentIndex(0);

    m_comboUpstreamAxis->setItemData(0, "1");
    m_comboUpstreamAxis->setItemData(1, "2");
    m_comboUpstreamAxis->setItemData(2, "3");
    m_comboUpstreamAxis->setItemText(0, QStringLiteral("X 轴"));
    m_comboUpstreamAxis->setItemText(1, QStringLiteral("Y 轴"));
    m_comboUpstreamAxis->setItemText(2, QStringLiteral("Z 轴"));
    m_comboUpstreamAxis->setToolTip(
        QStringLiteral("选择模型的上下游方向，不能与竖向方向相同。"));

    m_comboPgen->setItemData(0, "1");
    m_comboPgen->setItemData(1, "2");
    m_comboPgen->setItemData(2, "3");
    m_comboPgen->setItemData(3, "4");
    m_comboPgen->setItemText(0, QStringLiteral("三向平均"));
    m_comboPgen->setItemText(1, QStringLiteral("中小主应力平均"));
    m_comboPgen->setItemText(2, QStringLiteral("两种平均再平均"));
    m_comboPgen->setItemText(3, QStringLiteral("按主应力条件切换"));
    m_comboPgen->setToolTip(
        QStringLiteral("平均应力的计算方式，通常保持默认设置。"));
    m_comboUpstreamAxis->setCurrentIndex(0);
    m_comboPgen->setCurrentIndex(2);

    m_comboIfRock->setItemData(0, "0");
    m_comboIfRock->setItemData(1, "1");
    m_comboIfRock->setItemText(0, QStringLiteral("优先复用已有基岩状态"));
    m_comboIfRock->setItemText(1, QStringLiteral("本次重新计算基岩自重"));
    m_comboIfRock->setCurrentIndex(1);
    m_comboIfRock->setToolTip(
        QStringLiteral("可重新计算，也可复用已有基岩状态。"));
    // SOLVER PACKIKEY interprets IFISO > 0 as a substitute material ID; IFISO=1 is VOID.
    m_comboIfIso->clear();
    m_comboIfIso->addItem(QStringLiteral("按建模材料计算"), QStringLiteral("0"));
    m_comboIfIso->setCurrentIndex(0);

    connect(ui->comboLine19, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                updateAnalysisModeVisibility();
                emit requirementsChanged();
            });
    connect(ui->comboLine16, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateDynamicOptionVisibility(); });
    connect(ui->comboLine29, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateDynamicOptionVisibility(); });
    connect(m_groupFreezeThaw, &QGroupBox::toggled, this,
            [this]()
            {
                updateFreezeThawOptionState();
                emit requirementsChanged();
            });
    updateAnalysisModeVisibility();
    updateFreezeThawOptionState();
    updateUpliftOptionState();
}

void DamAnalysisWidget::updateFreezeThawOptionState()
{
    if (m_editIceExpansion)
        m_editIceExpansion->setEnabled(freezeThawEnabled());
}

bool DamAnalysisWidget::freezeThawEnabled() const
{
    return m_groupFreezeThaw && m_groupFreezeThaw->isChecked();
}

double DamAnalysisWidget::creepTimeStep(bool* ok) const
{
    bool localOk = false;
    const double value = ui->editLine21->text().trimmed().toDouble(&localOk);
    if (ok)
        *ok = localOk;
    return value;
}

bool DamAnalysisWidget::dynamicAnalysisEnabled() const
{
    const int type = ui->comboLine19->currentData().toString().toInt();
    return type == 2 || type == 3;
}

bool DamAnalysisWidget::dynamicOnlyEnabled() const
{
    return ui->comboLine19->currentData().toString().toInt() == 2;
}

int DamAnalysisWidget::modalExtractionOrder() const
{
    bool ok = false;
    const int order = ui->editLine30->text().trimmed().toInt(&ok);
    return ok && order > 0 ? order : 10;
}

void DamAnalysisWidget::setModalExtractionOrder(int order)
{
    if (order <= 0)
        return;
    const QString value = QString::number(order);
    if (ui->editLine30->text().trimmed() == value)
        return;
    const QSignalBlocker blocker(ui->editLine30);
    ui->editLine30->setText(value);
}

void DamAnalysisWidget::setModalAnalysisRequired(bool required)
{
    const QString value = required ? QStringLiteral("1") : QStringLiteral("0");
    const int index = ui->comboLine29->findData(value);
    if (index >= 0 && ui->comboLine29->currentIndex() != index)
    {
        const QSignalBlocker blocker(ui->comboLine29);
        ui->comboLine29->setCurrentIndex(index);
    }
    ui->comboLine29->setEnabled(false);
    ui->labelLine29->setEnabled(dynamicAnalysisEnabled());
    updateDynamicOptionVisibility();
}

void DamAnalysisWidget::setUpliftReductionRequired(bool required)
{
    if (m_upliftReductionRequired == required)
        return;
    m_upliftReductionRequired = required;
    updateUpliftOptionState();
}

void DamAnalysisWidget::updateUpliftOptionState()
{
    if (!ui || !ui->formLayoutStatic)
        return;
    for (QLineEdit* edit : {m_editX1, m_editX2, m_editX3,
                            m_editAlfa1, m_editAlfa2, m_editAlfa3})
    {
        if (edit)
            ui->formLayoutStatic->setRowVisible(edit, m_upliftReductionRequired);
    }
}

void DamAnalysisWidget::updateAnalysisModeVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int type = ui->comboLine19->currentData().toString().toInt();
    const bool staticEnabled = (type == 1 || type == 3);
    const bool dynamicEnabled = (type == 2 || type == 3);

    // 仅静力时只显示静力设置；仅动力时只显示动力设置；联合计算同时显示。
    ui->groupStatic->setVisible(staticEnabled);
    ui->groupDynamic->setVisible(dynamicEnabled);
    ui->groupStatic->setEnabled(staticEnabled);
    ui->groupDynamic->setEnabled(dynamicEnabled);

    // 坝体施工分层属于静力阶段模型准备；仅动力计算直接复用已有静力结果。
    if (m_groupConstructionLayers)
        m_groupConstructionLayers->setVisible(staticEnabled);
    if (!staticEnabled)
        clearConstructionLayerHighlights();

    if (m_quietBoundaryStatus)
        m_quietBoundaryStatus->setText(m_quietBoundarySelection->summary());
    updateDynamicOptionVisibility();
}

void DamAnalysisWidget::updateDynamicOptionVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int type = ui->comboLine19->currentData().toString().toInt();
    const bool dynamicEnabled = (type == 2 || type == 3);
    const bool rayleigh = dynamicEnabled &&
                          ui->comboLine16->currentData().toString() == QStringLiteral("1");

    // 次级开关采用“禁用”而不是反复插入/删除表单行，避免页面高度跳动。
    ui->labelLine17->setEnabled(rayleigh);
    ui->editLine17->setEnabled(rayleigh);
    ui->labelLine18->setEnabled(rayleigh);
    ui->editLine18->setEnabled(rayleigh);

    const bool eigen = dynamicEnabled &&
                       ui->comboLine29->currentData().toString() == QStringLiteral("1");
    ui->labelLine30->setEnabled(eigen);
    ui->editLine30->setEnabled(eigen);
}

QString DamAnalysisWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString DamAnalysisWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {
        "", "FA_DAMUP.DAT", "FA_DAMDW.DAT", "FA_DAMBASE.DAT",
        "NONE", "MA_ROCK.DAT", "MA_DAMUP.DAT", "MA_DAMDW.DAT", "MA_CORE.DAT", "4",
        "MA_PLATE.DAT", "JDTRACE.DAT", "KETRACE.DAT", "TDTRACE.DAT", "0", "0", "0.05", "0.0",
        "1", "4   10", "0.000", "1.962", "1.308", "0.1", "0.2", "2.0", "0.9", "7.0", "0",
        "10", "0", "-0.206", "2100.0 2105.0", "2.7110 2.9480", "24.381 3.1310",
        "JMFW.DAT", "0.0357", "0", "FA_DYNA.DAT", "---"};
    return defaults.value(lineNo - 1);
}

QString DamAnalysisWidget::descriptionByLine(int lineNo) const
{
    static const QStringList descriptions = {
        "计算设置", "上游水压力作用面", "下游水压力作用面", "心墙建基面",
        "坝体中心面", "基岩材料区域", "上游坝体材料区域",
        "下游坝体材料区域", "心墙材料区域",
        "坝体类型",
        "面板材料区域", "特征节点", "特征单元", "特征时刻",
        "阻尼设置", "瑞雷阻尼设置", "质量阻尼系数",
        "刚度阻尼系数", "计算类型", "迭代方法与最大迭代次数",
        "蠕变计算时间步长", "水平最大地震加速度", "竖向最大地震加速度",
        "反应谱第一特征周期", "反应谱第二特征周期", "反应谱最大放大系数",
        "反应谱衰减指数", "地震震级", "模态特征值求解", "模态阶数",
        "面板坝冻融循环次数（均质坝不用）", "面板坝冻融影响系数（均质坝不用）",
        "面板坝冻融影响高程（均质坝不用）", "面板坝冻融抗压系数（均质坝不用）",
        "面板坝冻融抗拉系数（均质坝不用）", "结冰区域", "冰线膨胀率", "动力粘弹性边界开关", "动力粘弹性边界面集",
        "系统参数"};
    return descriptions.value(lineNo - 1);
}

QString DamAnalysisWidget::normalizeEnumValue(const QString& rawValue) const
{
    const QString trimmed = rawValue.trimmed();
    const int quoteStart = trimmed.indexOf('\'');
    if (quoteStart < 0)
        return trimmed;
    const int quoteEnd = trimmed.lastIndexOf('\'');
    if (quoteEnd <= quoteStart)
        return trimmed;
    return trimmed.mid(quoteStart + 1, quoteEnd - quoteStart - 1).trimmed();
}

QString DamAnalysisWidget::lineValue(int lineNo) const
{
    if (lineNo == 20)
        return QString("%1 %2").arg(m_comboMethodIter->currentData().toString(),
                                    m_editIter->text().trimmed());
    if (lineNo >= 31 && lineNo <= 35)
        return defaultValueByLine(lineNo);
    if (lineNo == 38)
        return m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked()
            && (ui->comboLine19->currentData().toString() == QStringLiteral("2")
                || ui->comboLine19->currentData().toString() == QStringLiteral("3"))
            ? QStringLiteral("1") : QStringLiteral("0");

    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
        return combo->currentData().toString();
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        return edit->text().trimmed();
    if (auto* label = qobject_cast<QLabel*>(w))
        return label->text().trimmed();
    return defaultValueByLine(lineNo);
}

void DamAnalysisWidget::setLineValue(int lineNo, const QString& value)
{
    if (lineNo == 20)
    {
        const QStringList parts =
            value.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const QString method = parts.value(0, "4");
        const QString iter = parts.value(1, "10");
        const int idx = m_comboMethodIter->findData(method);
        if (idx >= 0)
            m_comboMethodIter->setCurrentIndex(idx);
        m_editIter->setText(iter);
        return;
    }
    if (lineNo >= 31 && lineNo <= 35)
        return;
    if (lineNo == 38)
    {
        if (m_quietBoundaryCheck)
            m_quietBoundaryCheck->setChecked(value.trimmed() == QStringLiteral("1"));
        return;
    }


    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
    {
        const QString normalizedValue = value.trimmed();
        const int idx = combo->findData(normalizedValue);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        return;
    }
    if (auto* edit = qobject_cast<QLineEdit*>(w))
    {
        edit->setText(value);
        return;
    }
}

bool DamAnalysisWidget::isFixedFileLine(int lineNo) const
{
    static const QSet<int> fixedLines = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14,
                                         31, 32, 33, 34, 35, 36, 39, 40};
    return fixedLines.contains(lineNo);
}

bool DamAnalysisWidget::validateInputs(QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    auto setError = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
    };

    if (m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked()
        && (ui->comboLine19->currentData().toString() == QStringLiteral("2")
            || ui->comboLine19->currentData().toString() == QStringLiteral("3"))
        && !m_quietBoundarySelection->generate())
    {
        setError(QStringLiteral("请在模型中选择粘弹性边界区域；当前支持线性边界面（二维选择边）。"));
        return false;
    }
    const int modeCode = ui->comboLine19->currentData().toString().toInt();
    const bool staticActive = (modeCode == 1 || modeCode == 3);
    const bool dynamicActive = (modeCode == 2 || modeCode == 3);
    const bool rayleighActive = dynamicActive && ui->comboLine16->currentData().toString() == QStringLiteral("1");
    if (staticActive)
    {
        if (m_constructionLayerUiDirty)
        {
            setError(QStringLiteral("尚未完成坝体施工分层，请为每个区域指定坝基或坝体。"));
            return false;
        }
        if (constructionLayerRange().first != 2)
        {
            setError(QStringLiteral("尚未完成坝体施工分层，请先指定坝基和坝体。"));
            return false;
        }
    }

    static const QSet<int> oneTokenLines = {10, 15, 16, 17, 18, 19, 21, 22, 23, 24,
                                           25, 26, 27, 28, 29, 30, 37};
    static const QSet<int> twoTokenLines;
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        if (!dynamicActive && ((lineNo >= 15 && lineNo <= 18) || (lineNo >= 22 && lineNo <= 30)))
            continue;
        if (!rayleighActive && (lineNo == 17 || lineNo == 18))
            continue;
        if (!freezeThawEnabled() && lineNo == 37)
            continue;
        if (!staticActive && (lineNo == 20 || lineNo == 21))
            continue;
        const QString text = normalizeEnumValue(lineValue(lineNo));
        if (text.trimmed().isEmpty())
        {
            setError(QString("%1不能为空。").arg(descriptionByLine(lineNo)));
            return false;
        }
        if (oneTokenLines.contains(lineNo))
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() != 1)
            {
                setError(QString("%1只能填写一个值。").arg(descriptionByLine(lineNo)));
                return false;
            }
        }
        if (twoTokenLines.contains(lineNo))
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() != 2)
            {
                setError(QString("%1需要填写两个数值，以空格分隔。").arg(descriptionByLine(lineNo)));
                return false;
            }
        }

        if ((oneTokenLines.contains(lineNo) || twoTokenLines.contains(lineNo)) && lineNo >= 10)
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (const QString& part : parts)
            {
                bool numberOk = false;
                part.toDouble(&numberOk);
                if (!numberOk)
                {
                    setError(QString("%1必须填写数值。").arg(descriptionByLine(lineNo)));
                    return false;
                }
            }
        }
    }

    bool iceExpansionOk = false;
    const double iceExpansion = m_editIceExpansion->text().toDouble(&iceExpansionOk);
    if (freezeThawEnabled() && (!iceExpansionOk || iceExpansion < 0.0))
    {
        setError("冰线膨胀率必须为不小于零的数值。");
        return false;
    }

    bool iterOk = false;
    const int iterations = m_editIter->text().toInt(&iterOk);
    if (!iterOk || iterations <= 0)
    {
        setError("最大迭代次数必须为正整数。");
        return false;
    }

    bool axisOk = false;
    const int upstreamAxis = m_comboUpstreamAxis->currentData().toString().toInt(&axisOk);
    if (!axisOk || upstreamAxis < 1 || upstreamAxis > 3)
    {
        setError("上下游轴向必须选择 X、Y、Z 中的一个。");
        return false;
    }
    if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        const int verticalAxis = meshSvc->verticalAxis();
        if (verticalAxis >= 1 && verticalAxis <= 3 && upstreamAxis == verticalAxis)
        {
            setError(QStringLiteral("上下游轴向不能与当前网格竖向轴相同。"));
            return false;
        }
    }

    bool pgenOk = false;
    const int pgen = m_comboPgen->currentData().toString().toInt(&pgenOk);
    if (!pgenOk || pgen < 1 || pgen > 4)
    {
        setError(QStringLiteral("请选择有效的平均应力方式。"));
        return false;
    }

    auto isNumber = [](const QLineEdit* edit)
    {
        bool ok = false;
        edit->text().trimmed().toDouble(&ok);
        return ok;
    };
    if (m_upliftReductionRequired &&
        (!isNumber(m_editX1) || !isNumber(m_editX2) || !isNumber(m_editX3)))
    {
        setError("扬压力折减位置必须填写数值。");
        return false;
    }
    if (m_upliftReductionRequired &&
        (!isNumber(m_editAlfa1) || !isNumber(m_editAlfa2) || !isNumber(m_editAlfa3)))
    {
        setError("扬压力折减系数必须填写数值。");
        return false;
    }

    return true;
}

bool DamAnalysisWidget::exportIfillDatFile(int mcase, const QString& cycleSectionText) const
{
    QString error;
    if (!validateInputs(&error))
    {
        QMessageBox::warning(nullptr, QStringLiteral("请完善土石坝设置"), error);
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    // Refresh homogeneous-dam derived sets here, after obsolete-file cleanup.
    // The native fallback attempts to inspect arrays already released by AUTOPACK.
    const int type = damType();
    for (const QString& obsoleteFile : {
             QStringLiteral("MA_ROCK.DAT"),
             QStringLiteral("MA_DAMUP.DAT"),
             QStringLiteral("MA_DAMDW.DAT"),
             QStringLiteral("MA_CORE.DAT"),
             QStringLiteral("MA_PLATE.DAT"),
             QStringLiteral("MA_YA.DAT")})
    {
        QFile::remove(inputDir.filePath(obsoleteFile));
    }
    if (type == 4)
        QFile::remove(inputDir.filePath(QStringLiteral("FA_DAMBASE.DAT")));

    bool x2Ok = false;
    const double x2Coord = m_editX2 ? m_editX2->text().trimmed().toDouble(&x2Ok) : 0.0;
    const int upstreamAxis = m_comboUpstreamAxis ? m_comboUpstreamAxis->currentData().toInt() : 1;
    QSet<int> plate, auxiliary, core;
    if ((type == 1 && !faceDamMaterialSets(plate, auxiliary, &error)) ||
        ((type == 2 || type == 3) && !coreDamMaterialSets(core, &error)) ||
        !DamInputProtocol::prepareMaterialSets(inputDir, type, plate, auxiliary, core,
                                               x2Ok ? x2Coord : 0.0, upstreamAxis, &error))
    {
        QMessageBox::warning(nullptr,QStringLiteral("请完善填筑材料"),error);
        return false;
    }

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    const QString inputDirAbs = ensureTrailingSep(inputDir.absolutePath());
    const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01FILL01"));

    QFile file(wd.filePath("IFILL.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setEncoding(QStringConverter::System);

    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1)
            value = inputDirAbs;
        else if (isFixedFileLine(lineNo))
            value = defaultValueByLine(lineNo);
        os << quoteValue(value) << "      " << lineNo << "\n";
    }

    os << QString("%1 %2 %3")
              .arg(m_editX1->text().trimmed())
              .arg(m_editX2->text().trimmed())
              .arg(m_editX3->text().trimmed())
              << "    X1  X2  X3\n";
    os << QString("%1 %2 %3")
              .arg(m_editAlfa1->text().trimmed())
              .arg(m_editAlfa2->text().trimmed())
              .arg(m_editAlfa3->text().trimmed())
              << "    ALFA1  ALFA2  ALFA3\n";
    double xA = x2Ok ? x2Coord : 0.0, yA = 0.0;
    double xB = xA, yB = 0.0;
    if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        if (auto grid = meshSvc->getVtkUnstructuredGrid(); grid && grid->GetNumberOfPoints() > 0)
        {
            double bounds[6] = {0, 0, 0, 0, 0, 0};
            grid->GetBounds(bounds);
            if (upstreamAxis == 2)
            {
                // Upstream/downstream along Y: dam axis runs along X (yMid = const)
                const double yMid = (x2Ok && (type == 2 || type == 3 || std::abs(x2Coord) >= 1.0e-9))
                    ? x2Coord : 0.5 * (bounds[2] + bounds[3]);
                xA = bounds[0]; yA = yMid;
                xB = bounds[1]; yB = yMid;
            }
            else
            {
                // Upstream/downstream along X: dam axis runs along Y (xMid = const)
                const double xMid = (x2Ok && (type == 2 || type == 3 || std::abs(x2Coord) >= 1.0e-9))
                    ? x2Coord : 0.5 * (bounds[0] + bounds[1]);
                xA = xMid; yA = bounds[2];
                xB = xMid; yB = bounds[3];
            }
        }
    }
    os << QString("%1 %2 %3 %4 %5 %6")
              .arg(m_comboUpstreamAxis->currentData().toString(),
                   m_comboPgen->currentData().toString(),
                   QString::number(xA, 'f', 6),
                   QString::number(yA, 'f', 6),
                   QString::number(xB, 'f', 6),
                   QString::number(yB, 'f', 6))
              << "    IXUPDW IPGEN XAMID YAMID XBMID YBMID\n";
    QString rockSelfWeightMode = m_comboIfRock->currentData().toString();
    if (rockSelfWeightMode == QStringLiteral("0"))
    {
        const QDir rockStateDir(wd.filePath(QStringLiteral("DISK00")));
        const QStringList requiredStateFiles = {
            QStringLiteral("IIDISP.TXT"),
            QStringLiteral("ISGMAR.TXT"),
            QStringLiteral("IIICMM.TXT")};
        const bool completeState =
            std::all_of(requiredStateFiles.cbegin(), requiredStateFiles.cend(),
                        [&rockStateDir](const QString& fileName)
                        {
                            const QFileInfo info(rockStateDir.filePath(fileName));
                            return info.isFile() && info.size() > 0;
                        });
        if (!completeState)
            rockSelfWeightMode = QStringLiteral("1");
    }

    const QString safeIfIso =
        m_comboIfIso && m_comboIfIso->currentData().toInt() >= 2
            ? m_comboIfIso->currentData().toString()
            : QStringLiteral("0");
    os << QString("%1 %2").arg(rockSelfWeightMode, safeIfIso)
              << "    IFROCK IFISO\n";

    const int safeCaseCount = qMax(1, mcase);
    os << safeCaseCount << "   MCASE\n";
    for (int i = 1; i <= safeCaseCount; ++i)
        os << i << "     " << i << "\n";

    QString dynamicSection = cycleSectionText.trimmed();
    if (dynamicSection.isEmpty())
    {
        dynamicSection =
            "1    NCASE\n"
            "----------------------------------------------\n"
            "1    ICASE--------------------------------------\n"
            "0     0     1     IFIIWL_SEEP   IFIIWL_BODY  IFIIWL_FACE\n"
            "0     0     NUM_WET        IFWET\n"
            "0     0     NUM_DROP       IFDROP\n"
            "0     NCHANGE\n"
            "1  NCIRCLE\n"
            "0 0 0.0000 0.0000 0.0000 0.0000 0 0 0 0 'NONE' 'NONE' 1\n"
            "'CASE01FILL01'     DIRDYNA\n"
            "0   NUM_DYNA\n"
            "0     0     0   IFMODE   IFWAVE     IFSETT\n"
            "0.020000    0.000000    0.000000    0.000000    DT TSTA TEND WOMI_1\n"
            "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ";
    }
    dynamicSection.replace("'CASE01FILL01'", "'" + caseDirAbs + "'");
    os << dynamicSection;
    if (!dynamicSection.endsWith('\n'))
        os << "\n";

    return true;
}

void DamAnalysisWidget::setMCaseCount(int mcase)
{
    Q_UNUSED(mcase);
}

int DamAnalysisWidget::damType() const
{
    return m_damTypeCombo ? m_damTypeCombo->currentData().toInt() : 4;
}

void DamAnalysisWidget::captureConstructionRegionRoles()
{
    if (m_loadingSettings || m_constructionLayerUiUpdating || !m_constructionRegionTable) return;
    for (int row = 0; row < m_constructionRegionKeys.size(); ++row)
    {
        auto* choice = qobject_cast<QComboBox*>(m_constructionRegionTable->cellWidget(row, 3));
        if (!choice) continue;
        const auto& key = m_constructionRegionKeys[row];
        m_constructionRegionRoles[QStringLiteral("%1/%2/%3").arg(key.category).arg(key.dim).arg(key.id)]
            = choice->currentData().toInt();
    }
}

bool DamAnalysisWidget::faceDamMaterialSets(QSet<int>& plate, QSet<int>& auxiliary,
                                           QString* error) const
{
    auto fail = [error](const QString& text) { if (error) *error = text; return false; };
    auto* mesh = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto grid = mesh ? mesh->getVtkUnstructuredGrid() : nullptr;
    auto* materials = grid && grid->GetCellData() ? grid->GetCellData()->GetArray("MATYPE") : nullptr;
    if (!materials) return fail(QStringLiteral("请先导入网格并指定面板坝的区域用途。"));
    QSet<int> body, rock;
    for (int row = 0; row < m_constructionRegionCells.size(); ++row)
    {
        auto* choice = qobject_cast<QComboBox*>(m_constructionRegionTable->cellWidget(row, 3));
        const int role = choice ? choice->currentData().toInt() : 0;
        if (role == 0) return fail(QStringLiteral("请为面板坝的每个区域指定用途。"));
        if (role != 1 && role != 2 && role != 3 && role != 4)
            return fail(QStringLiteral("面板坝区域用途包含不适用的心墙角色，请重新指定。"));
        QSet<int>* target = role == 3 ? &plate : role == 4 ? &auxiliary : role == 1 ? &rock : &body;
        for (auto cell : m_constructionRegionCells[row])
            if (cell >= 0 && cell < grid->GetNumberOfCells())
            {
                const int material = int(materials->GetTuple1(cell));
                if (material > 1) target->insert(material);
            }
    }
    if (plate.isEmpty()) return fail(QStringLiteral("请在区域用途中指定混凝土面板。"));
    if (!(plate & auxiliary).isEmpty() || !(plate & body).isEmpty() || !(plate & rock).isEmpty() ||
        !(auxiliary & body).isEmpty() || !(auxiliary & rock).isEmpty())
        return fail(QStringLiteral("面板、附属结构和坝体/坝基共用了材料号，请分别分配材料。"));
    return true;
}

bool DamAnalysisWidget::coreDamMaterialSets(QSet<int>& core, QString* error) const
{
    auto fail = [error](const QString& text) { if (error) *error = text; return false; };
    auto* mesh = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto grid = mesh ? mesh->getVtkUnstructuredGrid() : nullptr;
    auto* materials = grid && grid->GetCellData() ? grid->GetCellData()->GetArray("MATYPE") : nullptr;
    if (!materials) return fail(QStringLiteral("请先导入网格并指定心墙坝的区域用途。"));
    QSet<int> body, rock;
    for (int row = 0; row < m_constructionRegionCells.size(); ++row)
    {
        auto* choice = qobject_cast<QComboBox*>(m_constructionRegionTable->cellWidget(row, 3));
        const int role = choice ? choice->currentData().toInt() : 0;
        if (role == 0) return fail(QStringLiteral("请为心墙坝的每个区域指定用途。"));
        if (role != 1 && role != 2 && role != 5)
            return fail(QStringLiteral("心墙坝区域用途包含不适用的面板/附属结构角色，请重新指定。"));
        QSet<int>* target = role == 5 ? &core : role == 1 ? &rock : &body;
        for (auto cell : m_constructionRegionCells[row])
            if (cell >= 0 && cell < grid->GetNumberOfCells())
            {
                const int material = int(materials->GetTuple1(cell));
                if (material > 1) target->insert(material);
            }
    }
    if (core.isEmpty()) return fail(QStringLiteral("请在区域用途中指定防渗心墙。"));
    if (!(core & body).isEmpty() || !(core & rock).isEmpty())
        return fail(QStringLiteral("防渗心墙与坝壳/坝基共用了材料号，请分别分配材料。"));
    return true;
}

QJsonObject DamAnalysisWidget::saveJson()
{
    QJsonObject obj;
    captureConstructionRegionRoles();
    QJsonObject roles;
    for (auto it = m_constructionRegionRoles.cbegin(); it != m_constructionRegionRoles.cend(); ++it)
        roles.insert(it.key(), it.value());
    obj["construction_region_roles"] = roles;
    obj["quiet_boundary_selection"]=m_quietBoundarySelection?m_quietBoundarySelection->save():QJsonObject();
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    obj["quiet_boundary_enabled"] =
        m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked();
    obj["x_values"] = QJsonArray {m_editX1->text(), m_editX2->text(), m_editX3->text()};
    obj["alfa_values"] = QJsonArray {m_editAlfa1->text(), m_editAlfa2->text(), m_editAlfa3->text()};
    obj["upstream_axis_value"] = m_comboUpstreamAxis->currentData().toString();
    obj["pgen_value"] = m_comboPgen->currentData().toString();
    obj["axis_values"] = QJsonArray {m_comboUpstreamAxis->currentData().toString(),
                                     m_comboPgen->currentData().toString()};
    obj["rock_values"] = QJsonArray {m_comboIfRock->currentData().toString(),
                                      m_comboIfIso->currentData().toString()};
    obj["ice_deformation_enabled"] = freezeThawEnabled();
    // 兼容旧工程字段。
    obj["freeze_thaw_enabled"] = freezeThawEnabled();
    return obj;
}

void DamAnalysisWidget::loadJson(const QJsonObject& obj)
{
    m_loadingSettings = true;
    m_constructionRegionRoles.clear();
    const auto roles = obj.value("construction_region_roles").toObject();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        m_constructionRegionRoles.insert(it.key(), it.value().toInt());
    m_quietBoundarySelection->load(obj.value("quiet_boundary_selection").toObject(),obj.value("quiet_boundary_enabled").toBool(false));
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        const QString value = (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }
    if (obj.contains("quiet_boundary_enabled") && m_quietBoundaryCheck)
        m_quietBoundaryCheck->setChecked(
            obj.value("quiet_boundary_enabled").toBool(false));
    const QJsonValue xVal = obj.value("x_values");
    const QJsonArray xArr = xVal.toArray();
    if (!xArr.isEmpty())
    {
        if (xArr.size() > 0)
            m_editX1->setText(xArr.at(0).toString(m_editX1->text()));
        if (xArr.size() > 1)
            m_editX2->setText(xArr.at(1).toString(m_editX2->text()));
        if (xArr.size() > 2)
            m_editX3->setText(xArr.at(2).toString(m_editX3->text()));
    }
    else if (xVal.isString())
    {
        const QStringList parts =
            xVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        m_editX1->setText(parts.value(0, m_editX1->text()));
        m_editX2->setText(parts.value(1, m_editX2->text()));
        m_editX3->setText(parts.value(2, m_editX3->text()));
    }

    const QJsonValue alfaVal = obj.value("alfa_values");
    const QJsonArray alfaArr = alfaVal.toArray();
    if (!alfaArr.isEmpty())
    {
        if (alfaArr.size() > 0)
            m_editAlfa1->setText(alfaArr.at(0).toString(m_editAlfa1->text()));
        if (alfaArr.size() > 1)
            m_editAlfa2->setText(alfaArr.at(1).toString(m_editAlfa2->text()));
        if (alfaArr.size() > 2)
            m_editAlfa3->setText(alfaArr.at(2).toString(m_editAlfa3->text()));
    }
    else if (alfaVal.isString())
    {
        const QStringList parts =
            alfaVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        m_editAlfa1->setText(parts.value(0, m_editAlfa1->text()));
        m_editAlfa2->setText(parts.value(1, m_editAlfa2->text()));
        m_editAlfa3->setText(parts.value(2, m_editAlfa3->text()));
    }

    const QJsonValue newAxisVal = obj.value("upstream_axis_value");
    if (newAxisVal.isString())
    {
        const int idx = m_comboUpstreamAxis->findData(newAxisVal.toString());
        if (idx >= 0)
            m_comboUpstreamAxis->setCurrentIndex(idx);
    }
    const QJsonValue newPgenVal = obj.value("pgen_value");
    if (newPgenVal.isString())
    {
        const int idx = m_comboPgen->findData(newPgenVal.toString());
        if (idx >= 0)
            m_comboPgen->setCurrentIndex(idx);
    }

    const QJsonValue axisVal = obj.value("axis_values");
    const QJsonArray axisArr = axisVal.toArray();
    if (!axisArr.isEmpty() && !newAxisVal.isString() && !newPgenVal.isString())
    {
        const int upIdx =
            m_comboUpstreamAxis->findData(axisArr.size() > 0 ? axisArr.at(0).toString() : "");
        const int pgenIdx =
            m_comboPgen->findData(axisArr.size() > 1 ? axisArr.at(1).toString() : "");
        if (upIdx >= 0)
            m_comboUpstreamAxis->setCurrentIndex(upIdx);
        if (pgenIdx >= 0)
            m_comboPgen->setCurrentIndex(pgenIdx);
    }
    else if (axisVal.isString() && !newAxisVal.isString() && !newPgenVal.isString())
    {
        const QStringList parts =
            axisVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const int upIdx = m_comboUpstreamAxis->findData(parts.value(0));
        const int pgenIdx = m_comboPgen->findData(parts.value(1));
        if (upIdx >= 0)
            m_comboUpstreamAxis->setCurrentIndex(upIdx);
        if (pgenIdx >= 0)
            m_comboPgen->setCurrentIndex(pgenIdx);
    }

    const QJsonValue rockVal = obj.value("rock_values");
    const QJsonArray rockArr = rockVal.toArray();
    if (!rockArr.isEmpty())
    {
        const int rockIdx = m_comboIfRock->findData(rockArr.size() > 0 ? rockArr.at(0).toString() : "");
        const int isoIdx = m_comboIfIso->findData(rockArr.size() > 1 ? rockArr.at(1).toString() : "");
        if (rockIdx >= 0)
            m_comboIfRock->setCurrentIndex(rockIdx);
        if (isoIdx >= 0)
            m_comboIfIso->setCurrentIndex(isoIdx);
    }
    else if (rockVal.isString())
    {
        const QStringList parts =
            rockVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const int rockIdx = m_comboIfRock->findData(parts.value(0));
        const int isoIdx = m_comboIfIso->findData(parts.value(1));
        if (rockIdx >= 0)
            m_comboIfRock->setCurrentIndex(rockIdx);
        if (isoIdx >= 0)
            m_comboIfIso->setCurrentIndex(isoIdx);
    }

    const bool freezeEnabled = obj.contains("ice_deformation_enabled")
                                   ? obj.value("ice_deformation_enabled").toBool(false)
                                   : obj.value("freeze_thaw_enabled").toBool(false);
    m_groupFreezeThaw->setChecked(freezeEnabled);
    updateAnalysisModeVisibility();
    updateFreezeThawOptionState();
    updateUpliftOptionState();
    m_loadingSettings = false;
    rebuildConstructionLayerPanel();
}

void DamAnalysisWidget::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    emit computeRequested();
}

void DamAnalysisWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止计算")
                                      : QStringLiteral("数值法计算"));
}
