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
#include <algorithm>
#include <set>

#include "ui_CaveExcavationBodyPage.h"

CaveExcavationBodyPage::CaveExcavationBodyPage(QWidget* parent)
    : BasePage("CaveExcavationBody"), ui(new Ui::CaveExcavationBodyPage)
{
    Q_UNUSED(parent);

    ui->setupUi(this);

    connect(ui->addGroupBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onAddGroupClicked);
    connect(ui->removeGroupBtn, &QPushButton::clicked, this,
            &CaveExcavationBodyPage::onRemoveSelectedGroupsClicked);
    connect(ui->clearBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onClearClicked);

    if (auto* combo = qobject_cast<CustomComboBox*>(ui->groupCombo))
    {
        connect(combo, &CustomComboBox::itemHovered, this, &CaveExcavationBodyPage::onGroupHovered);
        connect(combo, &CustomComboBox::dropdownClosed, this,
                &CaveExcavationBodyPage::onGroupDropdownClosed);
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
    if (!ui || !ui->groupCombo || ui->groupCombo->currentIndex() < 0)
        return;
    const int gid = ui->groupCombo->currentData().toInt();
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
    if (!ui || !ui->groupList)
        return;
    const auto items = ui->groupList->selectedItems();
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
    if (!ui || !ui->groupCombo)
        return;

    if (m_lastHoveredIndex >= 0 && m_lastHoveredIndex < ui->groupCombo->count())
    {
        ui->groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        ui->groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::ForegroundRole);
    }

    if (index < 0 || index >= ui->groupCombo->count())
    {
        m_lastHoveredIndex = -1;
        // 参考 MaterialEntryWidget：hover 失效时立即取消 preview，仅保留已选组高亮。
        updateGroupDomainHighlight();
        return;
    }

    ui->groupCombo->setItemData(index, QColor(230, 247, 255), Qt::BackgroundRole);
    ui->groupCombo->setItemData(index, QColor(20, 70, 160), Qt::ForegroundRole);
    m_lastHoveredIndex = index;

    const int gid = ui->groupCombo->itemData(index).toInt();
    updateGroupDomainHighlight(gid);
}

void CaveExcavationBodyPage::onGroupDropdownClosed()
{
    if (ui && ui->groupCombo && m_lastHoveredIndex >= 0 && m_lastHoveredIndex < ui->groupCombo->count())
        ui->groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    m_lastHoveredIndex = -1;
    refreshGroupComboMarks();
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    // 关闭下拉后取消 hover 预览，仅保留“已选分组”的持久高亮。
    updateGroupDomainHighlight();
}

void CaveExcavationBodyPage::setupGroupCombo()
{
    if (!ui || !ui->groupCombo)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap map = meshSvc->mshPickMapSolid();

    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is3d = (meshDim == 3);
    if (!is3d)
    {
        ui->groupCombo->clear();
        ui->addGroupBtn->setEnabled(false);
        ui->removeGroupBtn->setEnabled(false);
        ui->groupList->setEnabled(false);
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
    ui->groupCombo->blockSignals(true);
    ui->groupCombo->clear();
    for (int id : ids)
        ui->groupCombo->addItem(QString::number(id), id);
    ui->groupCombo->blockSignals(false);

    ui->addGroupBtn->setEnabled(true);
    ui->removeGroupBtn->setEnabled(true);
    ui->groupList->setEnabled(true);
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
    if (!ui || !ui->groupList)
        return;
    ui->groupList->clear();
    QList<int> ids = m_selectedGroups.values();
    std::sort(ids.begin(), ids.end());
    for (int id : ids)
    {
        auto* item = new QListWidgetItem(QString::number(id), ui->groupList);
        item->setData(Qt::UserRole, id);
    }
}

void CaveExcavationBodyPage::refreshGroupComboMarks()
{
    if (!ui || !ui->groupCombo)
        return;
    for (int i = 0; i < ui->groupCombo->count(); ++i)
    {
        const int gid = ui->groupCombo->itemData(i).toInt();
        ui->groupCombo->setItemText(i, QString::number(gid));
        ui->groupCombo->setItemData(i, QVariant(), Qt::BackgroundRole);
        ui->groupCombo->setItemData(i, QVariant(), Qt::ForegroundRole);
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
    if (!meshSvc)
    {
        PickManager::instance().clearDomainHighlight(getPageType(), m_uuid);
        return;
    }
    const MshPickMap map = meshSvc->mshPickMapSolid();
    vtkUnstructuredGrid* grid = meshSvc->getVtkUnstructuredGridSolid();
    if (!grid)
        grid = PickManager::instance().solidGrid();
    const bool useLayer = (m_source == GroupSource::Layer);
    vtkDataArray* groupArr = (grid && grid->GetCellData())
                                 ? vtkDataArray::SafeDownCast(
                                       grid->GetCellData()->GetArray(useLayer ? "LAYER" : "MATYPE"))
                                 : nullptr;

    const auto collectGroupValues = [&](int gid) -> QSet<int>
    {
        QSet<int> out;
        const auto cids = groupCellIds(gid);
        for (vtkIdType cid : cids)
        {
            // LAYER 分组选 LAYER，其他分组选 MATYPE；缺失时回退 VTK 对应数组。
            const auto it = useLayer ? map.cellIdToLayer.constFind(static_cast<qint64>(cid))
                                     : map.cellIdToMatype.constFind(static_cast<qint64>(cid));
            if (it != (useLayer ? map.cellIdToLayer.constEnd() : map.cellIdToMatype.constEnd()))
            {
                out.insert(*it);
                continue;
            }
            if (groupArr && cid >= 0 && cid < groupArr->GetNumberOfTuples())
                out.insert(static_cast<int>(groupArr->GetTuple1(cid)));
        }
        return out;
    };

    QSet<int> selectedValues;
    for (int gid : m_selectedGroups)
        selectedValues.unite(collectGroupValues(gid));

    int previewValue = -1;
    if (previewGroup >= 0)
    {
        const QSet<int> previewValues = collectGroupValues(previewGroup);
        if (previewValues.size() == 1)
            previewValue = *previewValues.constBegin();
        else
            selectedValues.unite(previewValues);
    }

    if (useLayer)
    {
        PickManager::instance().highlightDomainsByLayer(getPageType(), m_uuid, selectedValues, -1,
                                                        previewValue, -1);
    }
    else
    {
        PickManager::instance().highlightDomainsByMatype(getPageType(), m_uuid, selectedValues, -1,
                                                         previewValue, -1);
    }
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
