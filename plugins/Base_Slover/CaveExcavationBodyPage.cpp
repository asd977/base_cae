#include "CaveExcavationBodyPage.h"

#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkUnstructuredGrid.h>

#include <QColor>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>

#include "ui_CaveExcavationBodyPage.h"

#include <QSet>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

CaveExcavationBodyPage::CaveExcavationBodyPage(QWidget* parent)
    : BasePage("CaveExcavationBody"), ui(new Ui::CaveExcavationBodyPage)
{
    ui->setupUi(this);
    Q_UNUSED(parent);

    m_groupCombo = ui->groupCombo;
    m_addGroupBtn = ui->addGroupButton;
    m_clearBtn = ui->clearButton;
    m_groupList = ui->groupList;
    m_removeGroupBtn = ui->removeGroupButton;

    connect(m_addGroupBtn, &QPushButton::clicked,
            this, &CaveExcavationBodyPage::onAddGroupClicked);

    connect(m_removeGroupBtn, &QPushButton::clicked,
            this, &CaveExcavationBodyPage::onRemoveSelectedGroupsClicked);

    connect(m_clearBtn, &QPushButton::clicked,
            this, &CaveExcavationBodyPage::onClearClicked);

    if (auto* combo = qobject_cast<CustomComboBox*>(m_groupCombo))
    {
        connect(combo, &CustomComboBox::itemHovered,
                this, &CaveExcavationBodyPage::onGroupHovered);

        connect(combo, &CustomComboBox::dropdownClosed,
                this, &CaveExcavationBodyPage::onGroupDropdownClosed);
    }

    setupGroupCombo();
    refreshSelectedGroupList();
}

CaveExcavationBodyPage::~CaveExcavationBodyPage()
{
    delete ui;
}

void CaveExcavationBodyPage::beforeLoad()
{
    setupGroupCombo();

    const PickMode mode = currentPickMode();

    PickManager::instance().activate(getPageType(), m_uuid, mode);

    // 开挖体页面统一通过“分组”选择，不直接点选单个面。
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, false);

    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    PickManager::instance().setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setSelectedDomain(getPageType(), m_uuid, DomainKey{});

    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QJsonObject CaveExcavationBodyPage::saveJson()
{
    QJsonObject obj;

    QJsonArray selectedGroupsArr;

    QList<int> selected = selectedLayerGroups().values();
    std::sort(selected.begin(), selected.end());

    for (int g : selected)
        selectedGroupsArr.append(g);

    obj["selected_groups"] = selectedGroupsArr;

    // 当前版本以 selected_groups 为唯一业务保存来源。
    // face_solid / edge_solid 保留为空数组，避免旧逻辑误把高亮面当成业务数据。
    obj["face_solid"] = QJsonArray();
    obj["edge_solid"] = QJsonArray();

    return obj;
}

void CaveExcavationBodyPage::loadJson(const QJsonObject& obj)
{
    setupGroupCombo();

    QSet<DomainKey> domains;

    const auto selectedGroupsArr = obj.value("selected_groups").toArray();
    for (const auto& v : selectedGroupsArr)
    {
        const int gid = v.toInt(std::numeric_limits<int>::min());
        if (gid != std::numeric_limits<int>::min() && gid != 0)
            domains.insert(DomainKey{"LAYER", layerDomainDim(), gid});
    }

    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

    // 清理旧版可能残留的单面选择，当前页面以分组选择为准。
    PickManager::instance().setSelectedFaceKeys(
        getPageType(), m_uuid, GridKind::Solid, QVector<vtkIdType>{}, true);
    PickManager::instance().setSelectedEdgeKeys(
        getPageType(), m_uuid, GridKind::Solid, QVector<vtkIdType>{}, true);

    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
}

QVector<int> CaveExcavationBodyPage::selectedElementIds()
{
    std::set<int> uniq;

    const QSet<int> groups = selectedLayerGroups();
    for (int g : groups)
    {
        const QVector<int> elemIds = elementIdsFromCells(groupCellIds(g));
        for (int ke : elemIds)
            uniq.insert(ke);
    }

    QVector<int> out;
    out.reserve(int(uniq.size()));

    for (int ke : uniq)
        out.push_back(ke);

    return out;
}

void CaveExcavationBodyPage::onClearClicked()
{
    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, {});
    PickManager::instance().setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});

    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Face);

    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::onAddGroupClicked()
{
    if (!m_groupCombo || m_groupCombo->currentIndex() < 0)
        return;

    const int gid = m_groupCombo->currentData().toInt();
    if (gid == 0)
        return;

    QSet<DomainKey> domains =
        PickManager::instance().appliedDomains(getPageType(), m_uuid);

    domains.insert(DomainKey{"LAYER", layerDomainDim(), gid});

    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::onRemoveSelectedGroupsClicked()
{
    if (!m_groupList)
        return;

    QSet<DomainKey> domains =
        PickManager::instance().appliedDomains(getPageType(), m_uuid);

    const auto items = m_groupList->selectedItems();
    for (QListWidgetItem* item : items)
    {
        bool ok = false;
        const int gid = item->data(Qt::UserRole).toInt(&ok);
        if (ok)
            domains.remove(DomainKey{"LAYER", layerDomainDim(), gid});
    }

    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::onGroupHovered(int index)
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

    const int gid = m_groupCombo->itemData(index).toInt();
    if (gid == 0)
        return;

    const QVector<vtkIdType> previewFaceKeys =
        visibleBoundaryFaceKeys(groupCellIds(gid));

    PickManager::instance().setPreviewFaceKeys(
        getPageType(), m_uuid, GridKind::Solid, previewFaceKeys);

    // 只使用 FaceKey 做开挖体预览高亮，避免 DomainKey 把同分组里的杆/线单元一起高亮。
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::onGroupDropdownClosed()
{
    if (m_groupCombo && m_lastHoveredIndex >= 0 && m_lastHoveredIndex < m_groupCombo->count())
    {
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::ForegroundRole);
    }

    m_lastHoveredIndex = -1;

    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);

    refreshGroupComboMarks();
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::setupGroupCombo()
{
    if (!m_groupCombo)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const QVector<int> ids = availableGroupIds();

    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();

    for (int id : ids)
        m_groupCombo->addItem(QString::number(id), id);

    m_groupCombo->blockSignals(false);

    if (m_addGroupBtn)
        m_addGroupBtn->setEnabled(!ids.isEmpty());

    if (m_removeGroupBtn)
        m_removeGroupBtn->setEnabled(true);

    if (m_groupList)
        m_groupList->setEnabled(true);

    refreshGroupComboMarks();
}

QVector<int> CaveExcavationBodyPage::availableGroupIds() const
{
    QVector<int> out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return out;

    const QVector<MeshTagSet> sets = meshSvc->domainTagSets();

    for (const MeshTagSet& set : sets)
    {
        if (set.key.category.compare("LAYER", Qt::CaseInsensitive) != 0)
            continue;

        if (set.key.id == 0)
            continue;

        if (!meshSvc->cellIdsOfTag(set.key).isEmpty())
            out.push_back(set.key.id);
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());

    return out;
}

QVector<vtkIdType> CaveExcavationBodyPage::groupCellIds(int groupId) const
{
    QVector<vtkIdType> out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return out;

    MeshTagKey key;
    key.category = "LAYER";
    key.dim = layerDomainDim();
    key.id = groupId;

    const QVector<MeshIdType> src = meshSvc->cellIdsOfTag(key);
    if (src.isEmpty())
        return out;

    vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
    if (!grid)
        grid = PickManager::instance().solidGrid();

    const int targetDim = layerDomainDim();

    out.reserve(src.size());

    for (MeshIdType mid : src)
    {
        const vtkIdType cid = static_cast<vtkIdType>(mid);

        if (grid)
        {
            if (cid < 0 || cid >= grid->GetNumberOfCells())
                continue;

            vtkCell* cell = grid->GetCell(cid);
            if (!cell)
                continue;

            // 开挖体只取当前工况维度的实体/区域单元。
            // 2D 只取面/区域单元，3D 只取体单元；杆单元/线单元不参与高亮和开挖单元输出。
            if (cell->GetCellDimension() != targetDim)
                continue;
        }

        out.push_back(cid);
    }

    return out;
}

QVector<vtkIdType> CaveExcavationBodyPage::visibleBoundaryFaceKeys(
    const QVector<vtkIdType>& cellIds) const
{
    QVector<vtkIdType> out;

    if (cellIds.isEmpty())
        return out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();

    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGridSolid() : nullptr;
    if (!grid)
        grid = PickManager::instance().solidGrid();

    if (!grid)
        return out;

    const int meshDim =
        PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();

    // 2D：二维开挖区域本身就是要显示的单元区域。
    // 这里把每个二维 cell 打包成 face key，用已有的面高亮通道显示。
    if (meshDim == 2)
    {
        out.reserve(cellIds.size());

        for (vtkIdType cid : cellIds)
        {
            if (cid < 0 || cid >= grid->GetNumberOfCells())
                continue;

            vtkCell* cell = grid->GetCell(cid);
            if (!cell || cell->GetCellDimension() != 2)
                continue;

            out.push_back(PickManager::instance().packFaceKey(cid, 0));
        }

        return out;
    }

    // 3D：对选中的体单元提取外表面。
    // 同一分组内部两个单元共享的面不显示，只显示该开挖体的外轮廓面。
    std::map<std::vector<vtkIdType>, std::pair<vtkIdType, int>> firstFace;
    std::set<std::vector<vtkIdType>> sharedFace;

    for (vtkIdType cid : cellIds)
    {
        if (cid < 0 || cid >= grid->GetNumberOfCells())
            continue;

        vtkCell* cell = grid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != 3)
            continue;

        const int faceCount = cell->GetNumberOfFaces();

        for (int faceId = 0; faceId < faceCount; ++faceId)
        {
            vtkCell* face = cell->GetFace(faceId);
            if (!face || !face->GetPointIds())
                continue;

            std::vector<vtkIdType> key;
            key.reserve(face->GetNumberOfPoints());

            for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
                key.push_back(face->GetPointId(i));

            std::sort(key.begin(), key.end());

            auto it = firstFace.find(key);
            if (it == firstFace.end())
            {
                firstFace[key] = std::make_pair(cid, faceId);
            }
            else
            {
                sharedFace.insert(key);
            }
        }
    }

    out.reserve(int(firstFace.size()));

    for (const auto& item : firstFace)
    {
        if (sharedFace.find(item.first) != sharedFace.end())
            continue;

        const vtkIdType cid = item.second.first;
        const int faceId = item.second.second;

        out.push_back(PickManager::instance().packFaceKey(cid, faceId));
    }

    return out;
}

QVector<int> CaveExcavationBodyPage::elementIdsFromCells(
    const QVector<vtkIdType>& cellIds) const
{
    QVector<int> out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();

    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGridSolid() : nullptr;
    if (!grid)
        grid = PickManager::instance().solidGrid();

    if (!grid)
        return out;

    vtkDataArray* elemIdArr =
        vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

    const int targetDim = layerDomainDim();

    out.reserve(cellIds.size());

    for (vtkIdType cid : cellIds)
    {
        if (cid < 0 || cid >= grid->GetNumberOfCells())
            continue;

        vtkCell* cell = grid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != targetDim)
            continue;

        out.push_back(elemIdArr ? int(elemIdArr->GetTuple1(cid)) : int(cid + 1));
    }

    return out;
}

void CaveExcavationBodyPage::refreshSelectedGroupList()
{
    if (!m_groupList)
        return;

    m_groupList->clear();

    QList<int> ids = selectedLayerGroups().values();
    std::sort(ids.begin(), ids.end());

    for (int id : ids)
    {
        auto* item = new QListWidgetItem(QString::number(id), m_groupList);
        item->setData(Qt::UserRole, id);
    }
}

void CaveExcavationBodyPage::refreshGroupComboMarks()
{
    if (!m_groupCombo)
        return;

    for (int i = 0; i < m_groupCombo->count(); ++i)
    {
        const int gid = m_groupCombo->itemData(i).toInt();

        // ComboBox 中只显示原始分组编号，不添加勾选符号或其他标记。
        m_groupCombo->setItemText(i, QString::number(gid));
        m_groupCombo->setItemData(i, QVariant(), Qt::BackgroundRole);
        m_groupCombo->setItemData(i, QVariant(), Qt::ForegroundRole);
    }
}

void CaveExcavationBodyPage::updatePersistentSelectionFaces()
{
    QVector<vtkIdType> allFaceKeys;

    const QSet<int> groups = selectedLayerGroups();

    for (int gid : groups)
    {
        const QVector<vtkIdType> faceKeys =
            visibleBoundaryFaceKeys(groupCellIds(gid));

        for (vtkIdType key : faceKeys)
            allFaceKeys.push_back(key);
    }

    PickManager::instance().setSelectedFaceKeys(
        getPageType(), m_uuid, GridKind::Solid, allFaceKeys, true);
}

void CaveExcavationBodyPage::updateGroupDomainHighlight(int previewGroup)
{
    Q_UNUSED(previewGroup);

    // 不再使用 DomainKey 做开挖体高亮。
    // 原因：同一个 LAYER 分组中可能同时包含实体单元和杆/线单元，
    // DomainKey 高亮会把杆/线单元也带上；开挖体只应高亮和输出实体/区域单元。
    PickManager::instance().setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
}

int CaveExcavationBodyPage::layerDomainDim() const
{
    const int meshDim =
        PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();

    return (meshDim == 2) ? 2 : 3;
}

PickMode CaveExcavationBodyPage::currentPickMode() const
{
    // 洞室开挖体选择本质是“分组选择”。
    // 业务数据仍保存 LAYER 分组，但可视高亮和最终输出会过滤掉杆/线单元。
    return PickMode::Domain;
}

QSet<int> CaveExcavationBodyPage::selectedLayerGroups()
{
    QSet<int> out;

    const QSet<DomainKey> owned =
        PickManager::instance().appliedDomains(getPageType(), m_uuid);

    for (const DomainKey& key : owned)
    {
        if (key.category.compare("LAYER", Qt::CaseInsensitive) == 0 && key.id != 0)
            out.insert(key.id);
    }

    return out;
}
