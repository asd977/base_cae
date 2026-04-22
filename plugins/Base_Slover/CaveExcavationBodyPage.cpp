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
    connect(m_groupCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveExcavationBodyPage::onGroupCurrentChanged);

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
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight();
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool allowFacePick = (meshDim == 2);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, allowFacePick);
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, allowFacePick);
}

void CaveExcavationBodyPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearDomainHighlight(getPageType(), m_uuid);
}

QJsonObject CaveExcavationBodyPage::saveJson()
{
    QJsonObject obj;
    obj["group_source"] = sourceToString(m_source);

    QJsonArray selectedGroupsArr;
    QList<int> selected = m_selectedGroups.values();
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
    m_source = sourceFromString(obj.value("group_source").toString());
    setupGroupCombo();

    m_selectedGroups.clear();
    const auto selectedGroupsArr = obj.value("selected_groups").toArray();
    for (const auto& v : selectedGroupsArr)
        m_selectedGroups.insert(v.toInt());

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

    for (int g : m_selectedGroups)
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
    m_selectedGroups.clear();
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
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
    m_selectedGroups.insert(gid);
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
    const auto items = m_groupList->selectedItems();
    for (QListWidgetItem* item : items)
    {
        bool ok = false;
        const int gid = item->data(Qt::UserRole).toInt(&ok);
        if (ok)
            m_selectedGroups.remove(gid);
    }
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

void CaveExcavationBodyPage::onGroupCurrentChanged(int index)
{
    if (!m_groupCombo || index < 0 || index >= m_groupCombo->count())
    {
        updateGroupDomainHighlight();
        return;
    }
    if (m_lastHoveredIndex < 0 && !m_selectedGroups.isEmpty())
    {
        // 下拉关闭后，优先保持“已添加分组”的持久高亮，不用当前项覆盖为预览态。
        updateGroupDomainHighlight();
        return;
    }
    const int gid = m_groupCombo->itemData(index).toInt();
    updateGroupDomainHighlight(gid);
}

void CaveExcavationBodyPage::onGroupDropdownClosed()
{
    if (m_groupCombo && m_lastHoveredIndex >= 0 && m_lastHoveredIndex < m_groupCombo->count())
        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    m_lastHoveredIndex = -1;
    refreshGroupComboMarks();
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    if (!m_selectedGroups.isEmpty())
    {
        updateGroupDomainHighlight();
        return;
    }
    onGroupCurrentChanged(m_groupCombo ? m_groupCombo->currentIndex() : -1);
}

void CaveExcavationBodyPage::setupGroupCombo()
{
    if (!m_groupCombo)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap map = meshSvc->mshPickMapSolid();

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
        m_selectedGroups.clear();
        refreshSelectedGroupList();
        refreshGroupComboMarks();
        return;
    }

    if (is3d)
    {
        if (map.layerToCellIds.size() > 1)
            m_source = GroupSource::Layer;
        else if (!map.volPhysToCellIds.isEmpty())
            m_source = GroupSource::Physical;
        else
            m_source = GroupSource::Matype;
    }
    else
    {
        m_source = GroupSource::Physical;
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
    const MshPickMap map = meshSvc->mshPickMapSolid();

    QList<int> keys;
    if (m_source == GroupSource::Layer)
        keys = map.layerToCellIds.keys();
    else if (m_source == GroupSource::Matype)
        keys = map.matypeToCellIds.keys();
    else
    {
        for (auto it = map.physIdToDim.constBegin(); it != map.physIdToDim.constEnd(); ++it)
        {
            if (it.value() == 3)
                keys.push_back(it.key());
        }
    }

    std::sort(keys.begin(), keys.end());
    for (int id : keys)
    {
        if (!groupCellIds(id).isEmpty())
            out.push_back(id);
    }
    return out;
}

QVector<vtkIdType> CaveExcavationBodyPage::groupCellIds(int groupId) const
{
    QVector<vtkIdType> out;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return out;
    const MshPickMap map = meshSvc->mshPickMapSolid();
    vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
    if (!grid)
        return out;

    QVector<qint64> src;
    if (m_source == GroupSource::Layer)
        src = map.layerToCellIds.value(groupId);
    else if (m_source == GroupSource::Matype)
        src = map.matypeToCellIds.value(groupId);
    else
        src = map.volPhysToCellIds.value(groupId);

    out.reserve(src.size());
    for (qint64 c : src)
    {
        const vtkIdType cid = static_cast<vtkIdType>(c);
        if (cid < 0 || cid >= grid->GetNumberOfCells())
            continue;
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;
        if (PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 3 &&
            cell->GetCellDimension() != 3)
            continue;
        out.push_back(cid);
    }
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
    for (auto it = map.bndFacePhysToFaceKeys.constBegin(); it != map.bndFacePhysToFaceKeys.constEnd(); ++it)
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
    QList<int> ids = m_selectedGroups.values();
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
    if (meshDim == 3)
    {
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, {}, true);
        return;
    }

    QVector<vtkIdType> allFaces;
    for (int g : m_selectedGroups)
    {
        const auto faces = visibleBoundaryFaceKeys(groupCellIds(g));
        allFaces += faces;
    }
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, allFaces, true);
}

void CaveExcavationBodyPage::updateGroupDomainHighlight(int previewGroup)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGridSolid() : nullptr;
    if (!grid)
        grid = PickManager::instance().solidGrid();
    if (!grid || !grid->GetCellData())
    {
        PickManager::instance().clearDomainHighlight(getPageType(), m_uuid);
        return;
    }

    auto* matypeArr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));
    if (!matypeArr)
    {
        PickManager::instance().clearDomainHighlight(getPageType(), m_uuid);
        return;
    }

    const auto collectGroupMatypes = [&](int gid) -> QSet<int>
    {
        QSet<int> out;
        const auto cids = groupCellIds(gid);
        for (vtkIdType cid : cids)
        {
            if (cid < 0 || cid >= grid->GetNumberOfCells())
                continue;
            out.insert(matypeArr->GetValue(cid));
        }
        return out;
    };

    QSet<int> selectedMatypes;
    for (int gid : m_selectedGroups)
        selectedMatypes.unite(collectGroupMatypes(gid));

    int previewMatype = -1;
    if (previewGroup >= 0)
    {
        const QSet<int> previewMatypes = collectGroupMatypes(previewGroup);
        if (previewMatypes.size() == 1)
            previewMatype = *previewMatypes.constBegin();
        else
            selectedMatypes.unite(previewMatypes);
    }

    PickManager::instance().highlightDomainsByMatype(getPageType(), m_uuid, selectedMatypes, -1,
                                                     previewMatype, -1);
}

QString CaveExcavationBodyPage::sourceToString(GroupSource s) const
{
    if (s == GroupSource::Layer)
        return "layer";
    if (s == GroupSource::Matype)
        return "matype";
    return "physical";
}

CaveExcavationBodyPage::GroupSource CaveExcavationBodyPage::sourceFromString(const QString& s) const
{
    if (s == "layer")
        return GroupSource::Layer;
    if (s == "matype")
        return GroupSource::Matype;
    return GroupSource::Physical;
}

PickMode CaveExcavationBodyPage::currentPickMode() const
{
    return PickMode::Face;
}
