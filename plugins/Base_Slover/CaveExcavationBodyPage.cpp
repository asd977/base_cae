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
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <set>

CaveExcavationBodyPage::CaveExcavationBodyPage(QWidget* parent)
    : BasePage("CaveExcavationBody")
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel("开挖体（2D选面，3D选体）", this));

    auto* form = new QFormLayout();
    m_groupCombo = new CustomComboBox(this);
    m_addGroupBtn = new QPushButton("添加分组", this);
    m_removeGroupBtn = new QPushButton("删除选中组", this);
    m_clearBtn = new QPushButton("清空所选", this);
    m_groupList = new QListWidget(this);
    form->addRow("选组", m_groupCombo);
    form->addRow("", m_addGroupBtn);
    form->addRow("已选组", m_groupList);
    form->addRow("", m_removeGroupBtn);
    form->addRow("", m_clearBtn);
    root->addLayout(form);
    root->addStretch();

    connect(m_addGroupBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onAddGroupClicked);
    connect(m_removeGroupBtn, &QPushButton::clicked, this,
            &CaveExcavationBodyPage::onRemoveSelectedGroupsClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onClearClicked);

    if (auto* combo = qobject_cast<CustomComboBox*>(m_groupCombo))
    {
        connect(combo, &CustomComboBox::itemHovered, this, &CaveExcavationBodyPage::onGroupHovered);
        connect(combo, &CustomComboBox::dropdownClosed, this,
                &CaveExcavationBodyPage::onGroupDropdownClosed);
    }

    setupGroupCombo();
    refreshSelectedGroupList();
}

void CaveExcavationBodyPage::beforeLoad()
{
    setupGroupCombo();

    const PickMode mode = currentPickMode();
    PickManager::instance().activate(getPageType(), m_uuid, mode);

    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool allowFacePick = (meshDim == 2 && mode == PickMode::Face);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, allowFacePick);
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, allowFacePick);

    if (mode == PickMode::Domain)
    {
        refreshSelectedGroupList();
        refreshGroupComboMarks();
        updateGroupDomainHighlight();
    }
    else
    {
        refreshSelectedGroupList();
        refreshGroupComboMarks();
    }
}

void CaveExcavationBodyPage::beforeUnload()
{
    const PickMode mode = currentPickMode();
    if (mode == PickMode::Face)
        PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, mode);
    PickManager::instance().deactivate(getPageType(), m_uuid, mode);
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

    QJsonArray faceArr, edgeArr;
    const auto faceKeys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
    const auto edgeKeys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid);
    for (vtkIdType key : faceKeys)
        faceArr.append(static_cast<qint64>(key));
    for (vtkIdType key : edgeKeys)
        edgeArr.append(static_cast<qint64>(key));
    obj["face_solid"] = faceArr;
    obj["edge_solid"] = edgeArr;
    return obj;
}

void CaveExcavationBodyPage::loadJson(const QJsonObject& obj)
{
    setupGroupCombo();

    QSet<DomainKey> domains;
    const auto selectedGroupsArr = obj.value("selected_groups").toArray();
    for (const auto& v : selectedGroupsArr)
    {
        const int gid = v.toInt(-1);
        if (gid > 0)
            domains.insert(DomainKey{"LAYER", 3, gid});
    }
    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

    QVector<vtkIdType> faceKeys, edgeKeys;
    const auto faceArr = obj.value("face_solid").toArray();
    faceKeys.reserve(faceArr.size());
    for (const auto& v : faceArr)
        faceKeys.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
    const auto edgeArr = obj.value("edge_solid").toArray();
    edgeKeys.reserve(edgeArr.size());
    for (const auto& v : edgeArr)
        edgeKeys.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));

    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys, true);
    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, true);

    updatePersistentSelectionFaces();
    updateGroupDomainHighlight();
    refreshSelectedGroupList();
    refreshGroupComboMarks();
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

    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (grid)
    {
        const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
        if (meshDim == 2)
        {
            vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
            const auto faceKeys =
                PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
            for (vtkIdType key : faceKeys)
            {
                const vtkIdType cellId = PickManager::instance().unpackFaceCellId(key);
                if (cellId < 0 || cellId >= grid->GetNumberOfCells())
                    continue;
                const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
                uniq.insert(ke);
            }
        }
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

    refreshSelectedGroupList();
    refreshGroupComboMarks();

    if (currentPickMode() == PickMode::Face)
    {
        PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
        PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Face);
    }

    updatePersistentSelectionFaces();
    updateGroupDomainHighlight();
}
void CaveExcavationBodyPage::onAddGroupClicked()
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (meshDim != 3)
        return;
    if (!m_groupCombo || m_groupCombo->currentIndex() < 0)
        return;

    const int gid = m_groupCombo->currentData().toInt();

    QSet<DomainKey> domains = PickManager::instance().appliedDomains(getPageType(), m_uuid);
    domains.insert(DomainKey{"LAYER", 3, gid});
    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

           // UI 一律从 PickManager 回读
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight();
}

void CaveExcavationBodyPage::onRemoveSelectedGroupsClicked()
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (meshDim != 3)
        return;
    if (!m_groupList)
        return;

    QSet<DomainKey> domains = PickManager::instance().appliedDomains(getPageType(), m_uuid);

    const auto items = m_groupList->selectedItems();
    for (QListWidgetItem* item : items)
    {
        bool ok = false;
        const int gid = item->data(Qt::UserRole).toInt(&ok);
        if (ok)
            domains.remove(DomainKey{"LAYER", 3, gid});
    }

    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight();
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
    updateGroupDomainHighlight(gid);
}

void CaveExcavationBodyPage::onGroupDropdownClosed()
{
    if (m_groupCombo && m_lastHoveredIndex >= 0 && m_lastHoveredIndex < m_groupCombo->count())
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    m_lastHoveredIndex = -1;
    refreshGroupComboMarks();
    if (currentPickMode() == PickMode::Face)
        PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    updateGroupDomainHighlight();
}

void CaveExcavationBodyPage::setupGroupCombo()
{
    if (!m_groupCombo)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is3d = (meshDim == 3);
    if (!is3d)
    {
        if (m_groupCombo)
            m_groupCombo->clear();
        if (m_addGroupBtn)
            m_addGroupBtn->setEnabled(false);
        if (m_removeGroupBtn)
            m_removeGroupBtn->setEnabled(false);
        if (m_groupList)
            m_groupList->setEnabled(false);
        refreshSelectedGroupList();
        refreshGroupComboMarks();
        return;
    }

    const QVector<int> ids = availableGroupIds();
    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();
    for (int id : ids)
        m_groupCombo->addItem(QString::number(id), id);
    m_groupCombo->blockSignals(false);

    if (m_addGroupBtn)
        m_addGroupBtn->setEnabled(true);
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
        if (set.key.id <= 0)
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
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return {};

    MeshTagKey key;
    key.category = "LAYER";
    key.dim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    key.id = groupId;
    const QVector<MeshIdType> src = meshSvc->cellIdsOfTag(key);
    QVector<vtkIdType> out;
    out.reserve(src.size());
    for (MeshIdType cid : src)
        out.push_back(static_cast<vtkIdType>(cid));
    return out;
}

QVector<vtkIdType> CaveExcavationBodyPage::visibleBoundaryFaceKeys(const QVector<vtkIdType>& cellIds) const
{
    QVector<vtkIdType> out;
    if (cellIds.isEmpty())
        return out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return out;
    const MshPickMap map = meshSvc->mshPickMapSolid();

    QSet<vtkIdType> cset;
    for (vtkIdType cid : cellIds)
        cset.insert(cid);

    QSet<vtkIdType> uniq;
    const auto faceMap = map.physicalFaceKeyMap();
    for (auto it = faceMap.constBegin(); it != faceMap.constEnd(); ++it)
    {
        for (qint64 k : it.value())
        {
            const vtkIdType fk = static_cast<vtkIdType>(k);
            const vtkIdType cid = PickManager::instance().unpackFaceCellId(fk);
            if (cset.contains(cid))
                uniq.insert(fk);
        }
    }
    out.reserve(uniq.size());
    for (vtkIdType fk : uniq)
        out.push_back(fk);

    return out;
}

QVector<int> CaveExcavationBodyPage::elementIdsFromCells(const QVector<vtkIdType>& cellIds) const
{
    QVector<int> out;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGridSolid() : nullptr;
    if (!grid)
        grid = PickManager::instance().solidGrid();
    if (!grid)
        return out;

    vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    out.reserve(cellIds.size());
    for (vtkIdType cid : cellIds)
    {
        if (cid < 0 || cid >= grid->GetNumberOfCells())
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
        m_groupCombo->setItemText(i, QString::number(gid));
        m_groupCombo->setItemData(i, QVariant(), Qt::BackgroundRole);
        m_groupCombo->setItemData(i, QVariant(), Qt::ForegroundRole);
    }
}

void CaveExcavationBodyPage::updatePersistentSelectionFaces()
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();

           // 2D：面选择由 PickManager 的 Face 模式直接管理，这里不要覆盖
    if (meshDim == 2)
        return;

           // 3D：当前不使用 face 持久高亮
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, {}, true);
}
void CaveExcavationBodyPage::updateGroupDomainHighlight(int previewGroup)
{
    if (currentPickMode() != PickMode::Domain)
        return;

    PickManager::instance().setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    PickManager::instance().setPreviewDomain(
        getPageType(), m_uuid,
        previewGroup >= 0 ? DomainKey{"LAYER", 3, previewGroup} : DomainKey{});
    PickManager::instance().setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
}

PickMode CaveExcavationBodyPage::currentPickMode() const
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return (meshDim == 2) ? PickMode::Face : PickMode::Domain;
}

QSet<int> CaveExcavationBodyPage::selectedLayerGroups() const
{
    QSet<int> out;
    const QSet<DomainKey> owned =
        PickManager::instance().appliedDomains(getPageType(), m_uuid);

    for (const DomainKey& key : owned)
    {
        if (key.category.compare("LAYER", Qt::CaseInsensitive) == 0 && key.isValid())
            out.insert(key.id);
    }
    return out;
}
