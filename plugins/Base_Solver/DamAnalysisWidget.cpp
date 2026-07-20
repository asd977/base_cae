#include "DamAnalysisWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <QAbstractItemView>
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
#include <limits>

#include "ui_DamAnalysisWidget.h"

#include "SolverUiLayoutHelper.h"
DamAnalysisWidget::DamAnalysisWidget(QWidget* parent)
    : BasePage("DamAnalysis"), ui(new Ui::DamAnalysisWidget)
{
    ui->setupUi(this);
    initValueEditors();
    setupConstructionLayerUi();
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
    const QStringList preferredCategories = m_existingConstructionLayersValid
        ? QStringList{QStringLiteral("LAYER"), QStringLiteral("Physical"), QStringLiteral("MATYPE")}
        : QStringList{QStringLiteral("Physical"), QStringLiteral("MATYPE"), QStringLiteral("LAYER")};
    for (const QString& category : preferredCategories)
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

        auto* purpose = new QComboBox(m_constructionRegionTable);
        purpose->addItem(QStringLiteral("请选择"), 0);
        purpose->addItem(colorIcon(foundationColor), QStringLiteral("坝基"), 1);
        purpose->addItem(colorIcon(damColor), QStringLiteral("坝体"), 2);
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
        purpose->setCurrentIndex(purpose->findData(purposeValue));
        SolverUiLayoutHelper::configureComboSizing(purpose);
        purpose->setToolTip(QStringLiteral("蓝色表示坝基，橙色表示坝体"));
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
        QSet<MeshIdType>& target = purposeValue == 2 ? damCells : foundationCells;
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
        QSet<MeshIdType>& target = purposeValue == 2 ? damCells : foundationCells;
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
    m_comboAxisUp = ui->comboAxisUp;
    m_comboAxisDown = ui->comboAxisDown;
    m_comboIfRock = ui->comboIfRock;
    m_comboIfIso = ui->comboIfIso;

    auto* damTypeLabel = new QLabel(QStringLiteral("均质坝"), this);
    damTypeLabel->setObjectName(QStringLiteral("labelDamTypeValue"));
    ui->formLayoutMode->addRow(QStringLiteral("坝体类型"), damTypeLabel);

    m_groupFreezeThaw = new QGroupBox(QStringLiteral("冻融与结冰参数"), this);
    m_groupFreezeThaw->setObjectName(QStringLiteral("groupFreezeThaw"));
    m_groupFreezeThaw->setCheckable(true);
    m_groupFreezeThaw->setChecked(false);
    auto* freezeForm = new QFormLayout(m_groupFreezeThaw);
    auto addFreezeEdit = [this, freezeForm](const QString& label, const QString& value,
                                            const QString& objectName)
    {
        auto* edit = new QLineEdit(value, m_groupFreezeThaw);
        edit->setObjectName(objectName);
        freezeForm->addRow(label, edit);
        return edit;
    };
    m_editFreezeCycles = addFreezeEdit(QStringLiteral("冻融循环次数"), QStringLiteral("0"),
                                      QStringLiteral("editLine31"));
    m_editFreezeCycles->setValidator(new QIntValidator(0, 1000000, m_editFreezeCycles));
    m_editFreezeCoefficient =
        addFreezeEdit(QStringLiteral("冻融影响系数"), QStringLiteral("-0.206"),
                      QStringLiteral("editLine32"));
    m_editFreezeElevationMin =
        addFreezeEdit(QStringLiteral("影响高程下限"), QStringLiteral("2100.0"),
                      QStringLiteral("editFreezeElevationMin"));
    m_editFreezeElevationMax =
        addFreezeEdit(QStringLiteral("影响高程上限"), QStringLiteral("2105.0"),
                      QStringLiteral("editFreezeElevationMax"));
    m_editFreezeCompressionB =
        addFreezeEdit(QStringLiteral("抗压强度系数 b"), QStringLiteral("2.7110"),
                      QStringLiteral("editFreezeCompressionB"));
    m_editFreezeCompressionC =
        addFreezeEdit(QStringLiteral("抗压强度系数 c"), QStringLiteral("2.9480"),
                      QStringLiteral("editFreezeCompressionC"));
    m_editFreezeTensionB =
        addFreezeEdit(QStringLiteral("抗拉强度系数 b"), QStringLiteral("24.381"),
                      QStringLiteral("editFreezeTensionB"));
    m_editFreezeTensionC =
        addFreezeEdit(QStringLiteral("抗拉强度系数 c"), QStringLiteral("3.1310"),
                      QStringLiteral("editFreezeTensionC"));
    m_editIceExpansion =
        addFreezeEdit(QStringLiteral("冰线膨胀率"), QStringLiteral("0.0357"),
                      QStringLiteral("editLine37"));
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
    m_lineEditors[10] = nullptr;
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
    m_lineEditors[31] = m_editFreezeCycles;
    m_lineEditors[32] = m_editFreezeCoefficient;
    // 33~35 行在界面中拆成独立参数，导出时由 lineValue() 按原格式重新拼接。
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

    ui->comboLine19->setItemData(0, "1");
    ui->comboLine19->setItemData(1, "2");
    ui->comboLine19->setItemData(2, "3");
    ui->comboLine19->setItemText(0, QStringLiteral("仅静力计算"));
    ui->comboLine19->setItemText(1, QStringLiteral("仅动力计算"));
    ui->comboLine19->setItemText(2, QStringLiteral("先静力后动力计算"));

    ui->comboLine29->setItemData(0, "0");
    ui->comboLine29->setItemData(1, "1");
    ui->comboLine29->setItemText(0, QStringLiteral("不计算"));
    ui->comboLine29->setItemText(1, QStringLiteral("计算"));

    m_comboMethodIter->setItemData(0, "1");
    m_comboMethodIter->setItemData(1, "2");
    m_comboMethodIter->setItemData(2, "3");
    m_comboMethodIter->setItemData(3, "4");
    m_comboMethodIter->setItemText(0, QStringLiteral("常刚度法（初应变法）"));
    m_comboMethodIter->setItemText(1, QStringLiteral("常刚度法（初应力法）"));
    m_comboMethodIter->setItemText(2, QStringLiteral("变刚度法（切线刚度法）"));
    m_comboMethodIter->setItemText(3, QStringLiteral("中点增量法（土坝填筑）"));
    m_comboMethodIter->setCurrentIndex(3);

    m_comboAxisUp->setItemData(0, "1");
    m_comboAxisUp->setItemData(1, "2");
    m_comboAxisUp->setItemData(2, "3");
    m_comboAxisDown->setItemData(0, "1");
    m_comboAxisDown->setItemData(1, "2");
    m_comboAxisDown->setItemData(2, "3");
    for (QComboBox* combo : {m_comboAxisUp, m_comboAxisDown})
    {
        combo->setItemText(0, QStringLiteral("X 轴"));
        combo->setItemText(1, QStringLiteral("Y 轴"));
        combo->setItemText(2, QStringLiteral("Z 轴"));
    }
    m_comboAxisUp->setCurrentIndex(0);
    m_comboAxisDown->setCurrentIndex(2);

    m_comboIfRock->setItemData(0, "0");
    m_comboIfRock->setItemData(1, "1");
    m_comboIfRock->setItemText(0, QStringLiteral("优先复用已有基岩状态"));
    m_comboIfRock->setItemText(1, QStringLiteral("本次重新计算基岩自重"));
    m_comboIfRock->setCurrentIndex(1);
    m_comboIfRock->setToolTip(
        QStringLiteral("首次计算默认重新建立；选择复用但结果不完整时会自动重新计算。"));
    m_comboIfIso->setItemData(0, "0");
    m_comboIfIso->setItemData(1, "1");
    m_comboIfIso->setItemText(0, QStringLiteral("按建模材料计算"));
    m_comboIfIso->setItemText(1, QStringLiteral("按替代材料计算"));

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
    connect(m_editFreezeCycles, &QLineEdit::textChanged, this,
            [this]() { updateFreezeThawOptionState(); });
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
    const bool enabled = freezeThawEnabled();
    for (QLineEdit* edit : {m_editFreezeCycles, m_editFreezeCoefficient,
                            m_editFreezeElevationMin, m_editFreezeElevationMax,
                            m_editFreezeCompressionB, m_editFreezeCompressionC,
                            m_editFreezeTensionB, m_editFreezeTensionC, m_editIceExpansion})
    {
        if (edit)
            edit->setEnabled(enabled);
    }
}

bool DamAnalysisWidget::freezeThawEnabled() const
{
    return m_groupFreezeThaw && m_groupFreezeThaw->isChecked();
}

bool DamAnalysisWidget::dynamicAnalysisEnabled() const
{
    const int type = ui->comboLine19->currentData().toString().toInt();
    return type == 2 || type == 3;
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
    const bool showStatic = (type == 1 || type == 3);
    const bool showDynamic = (type == 2 || type == 3);

    ui->groupStatic->setVisible(showStatic);
    ui->groupDynamic->setVisible(showDynamic);

    updateDynamicOptionVisibility();
}

void DamAnalysisWidget::updateDynamicOptionVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int type = ui->comboLine19->currentData().toString().toInt();
    const bool showDynamic = (type == 2 || type == 3);
    const bool rayleigh = showDynamic &&
                          ui->comboLine16->currentData().toString() == QStringLiteral("1");

    // 次级开关采用“禁用”而不是反复插入/删除表单行，避免页面高度跳动。
    ui->labelLine17->setEnabled(rayleigh);
    ui->editLine17->setEnabled(rayleigh);
    ui->labelLine18->setEnabled(rayleigh);
    ui->editLine18->setEnabled(rayleigh);

    const bool eigen = showDynamic &&
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
        "2", "4   10", "0.000", "1.962", "1.308", "0.1", "0.2", "2.0", "0.9", "7.0", "0",
        "10", "0", "-0.206", "2100.0 2105.0", "2.7110 2.9480", "24.381 3.1310",
        "JMFW.DAT", "0.0357", "---", "---", "---"};
    return defaults.value(lineNo - 1);
}

QString DamAnalysisWidget::descriptionByLine(int lineNo) const
{
    static const QStringList descriptions = {
        "计算默认文件目录", "上游水头边界集文件", "下游水头边界集文件", "心墙建基面文件",
        "坝体中心面文件（已停用，占位参数）", "基岩材料集（层号为1）", "上游材料集（面板坝不需要）",
        "下游材料集", "心墙材料集（面板坝不需要）",
        "坝体类型：均质坝",
        "面板材料集文件", "特征节点集文件", "特征单元集文件", "特征时刻文件",
        "阻尼：0系统阻尼 / 1分材料阻尼", "瑞雷阻尼：0否 / 1是", "质量阻尼系数",
        "刚度阻尼系数", "计算类型：1静力 / 2动力 / 3静动力", "迭代方法与迭代次数",
        "蠕变计算时间步长 DT_CREEP", "水平最大地震加速度 AHMAX", "竖向最大地震加速度 AVMAX",
        "反应谱特征周期 TG1", "反应谱特征周期 TG2", "反应谱 BETAMAX", "反应谱指数 EXPN",
        "地震震级 QUAKE_LEVEL", "是否计算特征值特征向量：0否 / 1是", "特征值数",
        "冻融循环次数", "冻融影响系数", "冻融影响高程范围", "冻融抗压强度系数",
        "冻融抗拉强度系数", "结冰节点集合（内部参数）", "冰线膨胀率", "保留参数", "保留参数",
        "保留参数"};
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
    if (lineNo == 31 && !freezeThawEnabled())
        return QStringLiteral("0");
    if (lineNo == 33)
        return QStringLiteral("%1 %2")
            .arg(m_editFreezeElevationMin->text().trimmed(),
                 m_editFreezeElevationMax->text().trimmed());
    if (lineNo == 34)
        return QStringLiteral("%1 %2")
            .arg(m_editFreezeCompressionB->text().trimmed(),
                 m_editFreezeCompressionC->text().trimmed());
    if (lineNo == 35)
        return QStringLiteral("%1 %2")
            .arg(m_editFreezeTensionB->text().trimmed(),
                 m_editFreezeTensionC->text().trimmed());

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
    if (lineNo >= 33 && lineNo <= 35)
    {
        const QStringList parts =
            value.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        auto setPair = [&parts](QLineEdit* first, QLineEdit* second,
                                const QString& firstDefault, const QString& secondDefault)
        {
            first->setText(parts.value(0, firstDefault));
            second->setText(parts.value(1, secondDefault));
        };
        if (lineNo == 33)
            setPair(m_editFreezeElevationMin, m_editFreezeElevationMax,
                    QStringLiteral("2100.0"), QStringLiteral("2105.0"));
        else if (lineNo == 34)
            setPair(m_editFreezeCompressionB, m_editFreezeCompressionC,
                    QStringLiteral("2.7110"), QStringLiteral("2.9480"));
        else
            setPair(m_editFreezeTensionB, m_editFreezeTensionC,
                    QStringLiteral("24.381"), QStringLiteral("3.1310"));
        return;
    }

    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
    {
        const int idx = combo->findData(value.trimmed());
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
    static const QSet<int> fixedLines = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                         36, 38, 39, 40};
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

    static const QSet<int> oneTokenLines = {10, 15, 16, 17, 18, 19, 21, 22, 23, 24,
                                           25, 26, 27, 28, 29, 30, 31, 32, 37};
    static const QSet<int> twoTokenLines = {33, 34, 35};
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        if (!freezeThawEnabled() && lineNo >= 31 && lineNo <= 37)
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

    bool cyclesOk = false;
    const int freezeCycles = m_editFreezeCycles->text().toInt(&cyclesOk);
    if (freezeThawEnabled() && (!cyclesOk || freezeCycles <= 0))
    {
        setError("启用冻融结冰后，冻融循环次数必须为正整数。");
        return false;
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

    if (freezeThawEnabled() &&
        m_editFreezeElevationMin->text().toDouble() >
            m_editFreezeElevationMax->text().toDouble())
    {
        setError("冻融影响高程下限不能大于上限。");
        return false;
    }
    return true;
}

bool DamAnalysisWidget::exportIfillDatFile(int mcase, const QString& cycleSectionText) const
{
    QString error;
    if (!validateInputs(&error))
    {
        QMessageBox::warning(nullptr, "参数格式错误", error);
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    // 均质坝的坝体和基岩材料集合由求解器按当前网格层派生，避免复用旧模型结果。
    QFile::remove(inputDir.filePath(QStringLiteral("MA_ROCK.DAT")));
    QFile::remove(inputDir.filePath(QStringLiteral("MA_DAMUP.DAT")));
    QFile::remove(inputDir.filePath(QStringLiteral("MA_DAMDW.DAT")));

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
    os << QString("%1 %2 0.000000 0.000000 0.000000 0.000000")
              .arg(m_comboAxisUp->currentData().toString(),
                   m_comboAxisDown->currentData().toString())
              << "    IXUPDW IYUPDW XAMID YAMID XBMID YBMID\n";
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

    os << QString("%1 %2").arg(rockSelfWeightMode,
                               m_comboIfIso->currentData().toString())
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

QJsonObject DamAnalysisWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    obj["x_values"] = QJsonArray {m_editX1->text(), m_editX2->text(), m_editX3->text()};
    obj["alfa_values"] = QJsonArray {m_editAlfa1->text(), m_editAlfa2->text(), m_editAlfa3->text()};
    obj["axis_values"] = QJsonArray {m_comboAxisUp->currentData().toString(),
                                     m_comboAxisDown->currentData().toString()};
    obj["rock_values"] = QJsonArray {m_comboIfRock->currentData().toString(),
                                      m_comboIfIso->currentData().toString()};
    obj["freeze_thaw_enabled"] = freezeThawEnabled();
    return obj;
}

void DamAnalysisWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        const QString value = (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }
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

    const QJsonValue axisVal = obj.value("axis_values");
    const QJsonArray axisArr = axisVal.toArray();
    if (!axisArr.isEmpty())
    {
        const int upIdx = m_comboAxisUp->findData(axisArr.size() > 0 ? axisArr.at(0).toString() : "");
        const int downIdx =
            m_comboAxisDown->findData(axisArr.size() > 1 ? axisArr.at(1).toString() : "");
        if (upIdx >= 0)
            m_comboAxisUp->setCurrentIndex(upIdx);
        if (downIdx >= 0)
            m_comboAxisDown->setCurrentIndex(downIdx);
    }
    else if (axisVal.isString())
    {
        const QStringList parts =
            axisVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const int upIdx = m_comboAxisUp->findData(parts.value(0));
        const int downIdx = m_comboAxisDown->findData(parts.value(1));
        if (upIdx >= 0)
            m_comboAxisUp->setCurrentIndex(upIdx);
        if (downIdx >= 0)
            m_comboAxisDown->setCurrentIndex(downIdx);
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

    const bool freezeEnabled = obj.contains("freeze_thaw_enabled")
                                   ? obj.value("freeze_thaw_enabled").toBool(false)
                                   : m_editFreezeCycles->text().toInt() > 0;
    m_groupFreezeThaw->setChecked(freezeEnabled);
    updateAnalysisModeVisibility();
    updateFreezeThawOptionState();
    updateUpliftOptionState();
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
