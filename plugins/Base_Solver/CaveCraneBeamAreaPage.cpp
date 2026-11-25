#include "CaveCraneBeamAreaPage.h"

#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkUnstructuredGrid.h>

#include <QColor>
#include <QDir>
#include <QSaveFile>
#include <QTextStream>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <set>

#include "SolverUiLayoutHelper.h"
#include "ui_CaveCraneBeamAreaPage.h"

CaveCraneBeamAreaPage::CaveCraneBeamAreaPage(QWidget* parent)
    : BasePage(QStringLiteral("CaveCraneBeamArea")),
      ui(new Ui::CaveCraneBeamAreaPage)
{
    ui->setupUi(this);
    Q_UNUSED(parent);
    SolverUiLayoutHelper::polishPage(this);

    m_groupCombo = ui->groupCombo;
    m_addGroupButton = ui->addGroupButton;
    m_removeGroupButton = ui->removeGroupButton;
    m_clearButton = ui->clearButton;
    m_groupList = ui->groupList;
    m_groupPreviewTimer = new QTimer(this);
    m_groupPreviewTimer->setSingleShot(true);
    m_groupPreviewTimer->setInterval(45);

    connect(m_addGroupButton, &QPushButton::clicked,
            this, &CaveCraneBeamAreaPage::onAddGroupClicked);
    connect(m_removeGroupButton, &QPushButton::clicked,
            this, &CaveCraneBeamAreaPage::onRemoveSelectedGroupsClicked);
    connect(m_clearButton, &QPushButton::clicked,
            this, &CaveCraneBeamAreaPage::onClearClicked);
    connect(m_groupCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &CaveCraneBeamAreaPage::onGroupChanged);
    connect(m_groupPreviewTimer, &QTimer::timeout, this,
            [this]() { updateGroupDomainHighlight(m_pendingPreviewGroup); });

    if (auto* combo = qobject_cast<CustomComboBox*>(m_groupCombo))
    {
        connect(combo, &CustomComboBox::itemHovered,
                this, &CaveCraneBeamAreaPage::onGroupHovered);
        connect(combo, &CustomComboBox::dropdownClosed,
                this, &CaveCraneBeamAreaPage::onGroupDropdownClosed);
    }

    setupGroupCombo();
    refreshSelectedGroupList();
}

CaveCraneBeamAreaPage::~CaveCraneBeamAreaPage()
{
    delete ui;
}

QString CaveCraneBeamAreaPage::pickDomain() const
{
    return QStringLiteral("CaveCraneBeamArea/%1").arg(m_uuid);
}


void CaveCraneBeamAreaPage::beforeLoad()
{
    setupGroupCombo();
    PickManager::instance().activate(pickDomain(), m_uuid, PickMode::Domain);
    PickManager::instance().setClickPickEnabled(pickDomain(), m_uuid, false);
    PickManager::instance().setBoxSelectEnabled(pickDomain(), m_uuid, false);
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
}

void CaveCraneBeamAreaPage::beforeUnload()
{
    m_groupPreviewTimer->stop();
    m_pendingPreviewGroup = 0;
    auto& pickManager = PickManager::instance();
    pickManager.setPreviewDomain(pickDomain(), m_uuid, DomainKey{});
    pickManager.setPreDeleteDomain(pickDomain(), m_uuid, DomainKey{});
    pickManager.setSelectedDomain(pickDomain(), m_uuid, DomainKey{});
    pickManager.deactivate(pickDomain(), m_uuid, PickMode::Domain);
}

QJsonObject CaveCraneBeamAreaPage::saveJson()
{
    QJsonObject object;
    QJsonArray domains;
    QList<int> ids = selectedGroups().values();
    std::sort(ids.begin(), ids.end());
    const QString category = selectionGroupCategory();
    const int dim = domainDimension();
    for (int id : ids)
    {
        QJsonObject domain;
        domain[QStringLiteral("category")] = category;
        domain[QStringLiteral("dim")] = dim;
        domain[QStringLiteral("id")] = id;
        domains.append(domain);
    }
    object[QStringLiteral("selected_domains")] = domains;
    return object;
}

void CaveCraneBeamAreaPage::loadJson(const QJsonObject& object)
{
    setupGroupCombo();
    QSet<DomainKey> domains;
    const QString category = selectionGroupCategory();
    const int dim = domainDimension();
    QSet<int> available;
    for (int id : availableGroupIds())
        available.insert(id);

    const QJsonArray values = object.value(QStringLiteral("selected_domains")).toArray();
    for (const QJsonValue& value : values)
    {
        const QJsonObject domain = value.toObject();
        const int id = domain.value(QStringLiteral("id")).toInt(0);
        if (id == 0 || (!available.isEmpty() && !available.contains(id)))
            continue;
        const QString savedCategory = domain.value(QStringLiteral("category")).toString();
        const int savedDim = domain.value(QStringLiteral("dim")).toInt(dim);
        if (!savedCategory.isEmpty()
            && savedCategory.compare(category, Qt::CaseInsensitive) != 0)
            continue;
        if (savedDim != dim)
            continue;
        domains.insert(DomainKey{category, dim, id});
    }

    // 兼容早期在主页面保存的区域数组。
    if (domains.isEmpty())
    {
        const QJsonArray groups = object.value(QStringLiteral("crane_beam_groups")).toArray();
        for (const QJsonValue& value : groups)
        {
            const QJsonObject group = value.toObject();
            const int id = group.value(QStringLiteral("id")).toInt(0);
            if (id != 0 && (available.isEmpty() || available.contains(id)))
                domains.insert(DomainKey{category, dim, id});
        }
    }

    PickManager::instance().setAppliedDomains(pickDomain(), m_uuid, domains);
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
    emit selectionChanged(selectionCount());
}

QVector<int> CaveCraneBeamAreaPage::selectedElementIds()
{
    std::set<int> unique;
    for (int groupId : selectedGroups())
    {
        const QVector<int> ids = elementIdsFromCells(groupCellIds(groupId));
        unique.insert(ids.cbegin(), ids.cend());
    }
    QVector<int> result;
    result.reserve(static_cast<int>(unique.size()));
    for (int id : unique)
        result.push_back(id);
    return result;
}

QVector<int> CaveCraneBeamAreaPage::selectedMaterialIds(QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return QVector<int>();
    };

    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return fail(QStringLiteral("无法读取当前网格。"));

    vtkUnstructuredGrid* grid = meshService->getVtkUnstructuredGrid();
    if (!grid)
        grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || !grid->GetCellData())
        return fail(QStringLiteral("当前网格没有可用于吊车梁设置的实体单元。"));

    vtkDataArray* materialArray = grid->GetCellData()->GetArray("MATYPE");
    if (!materialArray)
        return fail(QStringLiteral("当前网格缺少材料分区信息。请完成材料设置并重新生成网格。"));

    QSet<vtkIdType> selectedCells;
    QSet<int> materials;
    for (int groupId : selectedGroups())
    {
        for (vtkIdType cellId : groupCellIds(groupId))
        {
            selectedCells.insert(cellId);
            const int material = static_cast<int>(materialArray->GetTuple1(cellId));
            if (material > 0)
                materials.insert(material);
        }
    }

    if (selectedCells.isEmpty() || materials.isEmpty())
        return fail(QStringLiteral("请在“吊车梁区域”节点中选择有效实体区域。"));

    const int dim = domainDimension();
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != dim)
            continue;
        const int material = static_cast<int>(materialArray->GetTuple1(cellId));
        if (materials.contains(material) && !selectedCells.contains(cellId))
        {
            return fail(QStringLiteral(
                "吊车梁区域与其他区域共用了材料 %1。请为吊车梁设置独立材料。")
                            .arg(material));
        }
    }

    QVector<int> result = materials.values().toVector();
    std::sort(result.begin(), result.end());
    return result;
}


bool CaveCraneBeamAreaPage::exportMaterialSet(
    const QString& inputDirectory, QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    const QVector<int> materials = selectedMaterialIds(errorMessage);
    if (materials.isEmpty())
        return false;

    QDir directory(inputDirectory);
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath()))
        return fail(QStringLiteral("无法准备吊车梁计算数据。"));

    QSaveFile file(directory.filePath(QStringLiteral("MASET_DCL.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(QStringLiteral("无法准备吊车梁计算数据。"));
    QTextStream stream(&file);
    stream << materials.size() << "     NUM_DCL\n";
    for (int material : materials)
        stream << material << "\n";
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit())
        return fail(QStringLiteral("无法保存吊车梁计算数据。"));
    return true;
}

int CaveCraneBeamAreaPage::selectionCount()
{
    return selectedGroups().size();
}

void CaveCraneBeamAreaPage::onClearClicked()
{
    auto& pickManager = PickManager::instance();
    pickManager.setAppliedDomains(pickDomain(), m_uuid, {});
    pickManager.setSelectedDomain(pickDomain(), m_uuid, DomainKey{});
    pickManager.setPreviewDomain(pickDomain(), m_uuid, DomainKey{});
    pickManager.setPreDeleteDomain(pickDomain(), m_uuid, DomainKey{});
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
    emit selectionChanged(0);
}

void CaveCraneBeamAreaPage::onAddGroupClicked()
{
    if (!m_groupCombo || m_groupCombo->currentIndex() < 0)
        return;
    const int id = m_groupCombo->currentData().toInt();
    if (id == 0)
        return;
    QSet<DomainKey> domains =
        PickManager::instance().appliedDomains(pickDomain(), m_uuid);
    domains.insert(DomainKey{selectionGroupCategory(), domainDimension(), id});
    PickManager::instance().setAppliedDomains(pickDomain(), m_uuid, domains);
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
    emit selectionChanged(selectionCount());
}

void CaveCraneBeamAreaPage::onRemoveSelectedGroupsClicked()
{
    if (!m_groupList)
        return;
    QSet<DomainKey> domains =
        PickManager::instance().appliedDomains(pickDomain(), m_uuid);
    for (QListWidgetItem* item : m_groupList->selectedItems())
    {
        const int id = item->data(Qt::UserRole).toInt();
        domains.remove(DomainKey{selectionGroupCategory(), domainDimension(), id});
    }
    PickManager::instance().setAppliedDomains(pickDomain(), m_uuid, domains);
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
    emit selectionChanged(selectionCount());
}

void CaveCraneBeamAreaPage::onGroupChanged(int)
{
    m_groupPreviewTimer->stop();
    m_pendingPreviewGroup = 0;
    updateGroupDomainHighlight(0);
}

void CaveCraneBeamAreaPage::onGroupHovered(int index)
{
    if (!m_groupCombo || index < 0 || index >= m_groupCombo->count())
        return;
    if (m_lastHoveredIndex >= 0 && m_lastHoveredIndex < m_groupCombo->count())
    {
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::ForegroundRole);
    }
    m_groupCombo->setItemData(index, QColor(230, 247, 255), Qt::BackgroundRole);
    m_groupCombo->setItemData(index, QColor(20, 70, 160), Qt::ForegroundRole);
    m_lastHoveredIndex = index;
    m_pendingPreviewGroup = m_groupCombo->itemData(index).toInt();
    m_groupPreviewTimer->start();
}

void CaveCraneBeamAreaPage::onGroupDropdownClosed()
{
    if (m_groupCombo && m_lastHoveredIndex >= 0
        && m_lastHoveredIndex < m_groupCombo->count())
    {
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::ForegroundRole);
    }
    m_lastHoveredIndex = -1;
    m_groupPreviewTimer->stop();
    m_pendingPreviewGroup = 0;
    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
}

void CaveCraneBeamAreaPage::setupGroupCombo()
{
    if (!m_groupCombo)
        return;
    const QVariant previous = m_groupCombo->currentData();
    const QVector<int> ids = availableGroupIds();
    const QSignalBlocker blocker(m_groupCombo);
    m_groupCombo->clear();
    for (int id : ids)
    {
        const QString text = groupDisplayText(id);
        m_groupCombo->addItem(text, id);
        m_groupCombo->setItemData(m_groupCombo->count() - 1, text, Qt::ToolTipRole);
    }
    const int index = previous.isValid() ? m_groupCombo->findData(previous) : -1;
    if (index >= 0)
        m_groupCombo->setCurrentIndex(index);
    m_addGroupButton->setEnabled(!ids.isEmpty());
    refreshGroupComboMarks();
}

QVector<int> CaveCraneBeamAreaPage::availableGroupIds()
{
    QVector<int> ids;
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return ids;
    const QString category = selectionGroupCategory();
    const int dim = domainDimension();
    for (const MeshTagSet& set : meshService->domainTagSets())
    {
        if (set.key.category.compare(category, Qt::CaseInsensitive) == 0
            && set.key.dim == dim && set.key.id != 0
            && !meshService->cellIdsOfTag(set.key).isEmpty())
            ids.push_back(set.key.id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

QString CaveCraneBeamAreaPage::groupDisplayText(int groupId)
{
    const QString category = selectionGroupCategory();
    if (category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0)
    {
        if (auto* materialService =
                ServiceRegistry::instance().getService<MaterialManagerService>())
        {
            QMap<int, QString> labels = materialService->assignedMaterialLabels();
            if (!labels.contains(groupId))
                labels = materialService->calculationMaterialLabels();
            const QString label = labels.value(groupId).trimmed();
            if (!label.isEmpty())
                return label.section(QLatin1Char('/'), -1).trimmed();
        }
        return QStringLiteral("材料 %1").arg(groupId);
    }
    if (auto* meshService =
            ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        const QString text = meshService->domainDisplayText(
            DomainKey{category, domainDimension(), groupId}).trimmed();
        if (!text.isEmpty())
            return text;
    }
    return QStringLiteral("%1=%2").arg(category).arg(groupId);
}

QVector<vtkIdType> CaveCraneBeamAreaPage::groupCellIds(int groupId)
{
    QVector<vtkIdType> result;
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return result;
    vtkUnstructuredGrid* grid = meshService->getVtkUnstructuredGrid();
    if (!grid)
        grid = meshService->getVtkUnstructuredGridSolid();
    const MeshTagKey key{selectionGroupCategory(), domainDimension(), groupId};
    for (MeshIdType sourceId : meshService->cellIdsOfTag(key))
    {
        const vtkIdType cellId = static_cast<vtkIdType>(sourceId);
        if (grid)
        {
            if (cellId < 0 || cellId >= grid->GetNumberOfCells())
                continue;
            vtkCell* cell = grid->GetCell(cellId);
            if (!cell || cell->GetCellDimension() != domainDimension())
                continue;
        }
        result.push_back(cellId);
    }
    return result;
}

QVector<int> CaveCraneBeamAreaPage::elementIdsFromCells(
    const QVector<vtkIdType>& cellIds)
{
    QVector<int> result;
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    if (!grid && meshService)
        grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid)
        return result;
    vtkDataArray* elementIds = grid->GetCellData()
        ? grid->GetCellData()->GetArray("elem_id") : nullptr;
    for (vtkIdType cellId : cellIds)
    {
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        result.push_back(elementIds
            ? static_cast<int>(elementIds->GetTuple1(cellId))
            : static_cast<int>(cellId + 1));
    }
    return result;
}

void CaveCraneBeamAreaPage::refreshSelectedGroupList()
{
    m_groupList->clear();
    QList<int> ids = selectedGroups().values();
    std::sort(ids.begin(), ids.end());
    for (int id : ids)
    {
        auto* item = new QListWidgetItem(groupDisplayText(id), m_groupList);
        item->setData(Qt::UserRole, id);
    }
}

void CaveCraneBeamAreaPage::refreshGroupComboMarks()
{
    if (!m_groupCombo)
        return;
    for (int i = 0; i < m_groupCombo->count(); ++i)
    {
        m_groupCombo->setItemData(i, QVariant(), Qt::BackgroundRole);
        m_groupCombo->setItemData(i, QVariant(), Qt::ForegroundRole);
    }
}

void CaveCraneBeamAreaPage::updateGroupDomainHighlight(int previewGroup)
{
    const int current = m_groupCombo && m_groupCombo->currentData().isValid()
        ? m_groupCombo->currentData().toInt() : 0;
    const auto key = [this](int id)
    {
        return id == 0 ? DomainKey{}
                       : DomainKey{selectionGroupCategory(), domainDimension(), id};
    };
    auto& pickManager = PickManager::instance();
    pickManager.setSelectedDomain(pickDomain(), m_uuid, key(current));
    pickManager.setPreviewDomain(pickDomain(), m_uuid, key(previewGroup));
    pickManager.setPreDeleteDomain(pickDomain(), m_uuid, DomainKey{});
}

QSet<int> CaveCraneBeamAreaPage::selectedGroups()
{
    QSet<int> result;
    const QString category = selectionGroupCategory();
    const int dim = domainDimension();
    for (const DomainKey& key :
         PickManager::instance().appliedDomains(pickDomain(), m_uuid))
    {
        if (key.category.compare(category, Qt::CaseInsensitive) == 0
            && key.dim == dim && key.id != 0)
            result.insert(key.id);
    }
    return result;
}

QString CaveCraneBeamAreaPage::selectionGroupCategory()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return QStringLiteral("MATYPE");
    const QString category = meshService->domainCategoryForUse(MeshDomainUse::ExcavationRegion);
    return category.isEmpty() ? QStringLiteral("MATYPE") : category;
}

int CaveCraneBeamAreaPage::domainDimension()
{
    return PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2 ? 2 : 3;
}
