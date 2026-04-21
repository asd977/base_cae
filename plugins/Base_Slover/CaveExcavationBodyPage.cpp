#include "CaveExcavationBodyPage.h"

#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <vtkCellData.h>
#include <vtkCell.h>
#include <vtkDataArray.h>
#include <vtkUnstructuredGrid.h>

#include <QComboBox>
#include <QColor>
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
    m_batchAddBtn = new QPushButton("添加分组", this);
    m_clearBtn = new QPushButton("清空所选", this);
    m_elemList = new QListWidget(this);
    m_removeSelectedBtn = new QPushButton("删除选中单元", this);
    form->addRow("选组", m_groupCombo);
    form->addRow("", m_batchAddBtn);
    form->addRow("已选单元", m_elemList);
    form->addRow("", m_removeSelectedBtn);
    form->addRow("", m_clearBtn);
    root->addLayout(form);
    root->addStretch();

    connect(m_clearBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onClearClicked);
    connect(m_removeSelectedBtn, &QPushButton::clicked, this,
            &CaveExcavationBodyPage::onRemoveSelectedClicked);
    connect(m_batchAddBtn, &QPushButton::clicked, this,
            [this]() { onGroupChanged(m_groupCombo->currentIndex()); });

    setupBatchGroupCombo();
    refreshElementListWidget();
}

void CaveExcavationBodyPage::beforeLoad()
{
    setupBatchGroupCombo();
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
}

void CaveExcavationBodyPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QJsonObject CaveExcavationBodyPage::saveJson()
{
    QJsonObject obj;
    const QString key = groupKeyForIndex(m_groupCombo ? m_groupCombo->currentIndex() : -1);
    bool ok = false;
    const GroupOption option = parseGroupKey(key, &ok);
    obj["group_key"] = key;
    obj["group_phys_id"] = ok ? option.id : -1; // 兼容旧存档（仅保存编号）

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
    QJsonArray batchElemArr;
    for (int ke : m_batchElementIds)
        batchElemArr.append(ke);
    obj["batch_elem_ids"] = batchElemArr;
    return obj;
}

void CaveExcavationBodyPage::loadJson(const QJsonObject& obj)
{
    if (m_groupCombo)
    {
        const QString groupKey = obj.value("group_key").toString();
        if (!groupKey.trimmed().isEmpty() && setCurrentGroupByKey(groupKey))
        {
            // 已按新字段恢复
        }
        else
        {
            const int legacyId = obj.value("group_phys_id").toInt(-1);
            for (int i = 0; i < m_groupCombo->count(); ++i)
            {
                bool ok = false;
                const GroupOption option = parseGroupKey(m_groupCombo->itemData(i).toString(), &ok);
                if (!ok || option.id != legacyId)
                    continue;
                const QSignalBlocker blocker(m_groupCombo);
                m_groupCombo->setCurrentIndex(i);
                break;
            }
        }
    }

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

    m_batchElementIds.clear();
    const auto batchElemArr = obj.value("batch_elem_ids").toArray();
    for (const auto& v : batchElemArr)
        m_batchElementIds.insert(v.toInt());
    refreshElementListWidget();
}

QVector<int> CaveExcavationBodyPage::selectedElementIds()
{
    QVector<int> out;

    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return out;

    vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    const auto faceKeys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);

    std::set<int> uniq;
    for (int ke : m_batchElementIds)
        uniq.insert(ke);
    for (vtkIdType key : faceKeys)
    {
        const vtkIdType cellId = PickManager::instance().unpackFaceCellId(key);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
        uniq.insert(ke);
    }
    out.reserve(int(uniq.size()));
    for (int ke : uniq)
        out.push_back(ke);
    return out;
}

void CaveExcavationBodyPage::onClearClicked()
{
    m_batchElementIds.clear();
    refreshElementListWidget();
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
}

void CaveExcavationBodyPage::onGroupChanged(int index)
{
    if (index < 0 || !m_groupCombo)
        return;

    const QVector<vtkIdType> cellIds = resolveCellIdsForIndex(index);
    const QVector<int> elemIds = toElementIds(cellIds);
    for (int ke : elemIds)
        m_batchElementIds.insert(ke);
    refreshElementListWidget();

    QVector<vtkIdType> faceKeys;
    QVector<vtkIdType> edgeKeys;
    faceKeys = visibleFaceKeysForCellIds(cellIds);

    if (currentPickMode() == PickMode::Face)
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys, false);
    else
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, false);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
}

void CaveExcavationBodyPage::onRemoveSelectedClicked()
{
    if (!m_elemList)
        return;

    const auto selectedItems = m_elemList->selectedItems();
    if (selectedItems.isEmpty())
        return;

    for (QListWidgetItem* item : selectedItems)
    {
        if (!item)
            continue;
        bool ok = false;
        const int elemId = item->text().toInt(&ok);
        if (ok)
            m_batchElementIds.remove(elemId);
    }
    refreshElementListWidget();
}

void CaveExcavationBodyPage::setupBatchGroupCombo()
{
    if (!m_groupCombo)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const int targetDim = (meshDim == 3) ? 3 : 2;
    vtkUnstructuredGrid* solidGrid = nullptr;
    const auto gridHolder = meshSvc->getVtkUnstructuredGridSolid();
    if (gridHolder)
        solidGrid = gridHolder;
    const auto hasVolumeCell = [solidGrid](const QVector<qint64>& cells) -> bool
    {
        if (!solidGrid)
            return !cells.isEmpty();
        for (qint64 cid64 : cells)
        {
            const vtkIdType cid = static_cast<vtkIdType>(cid64);
            if (cid < 0 || cid >= solidGrid->GetNumberOfCells())
                continue;
            vtkCell* cell = solidGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == 3)
                return true;
        }
        return false;
    };

    QVector<GroupOption> options;
    if (targetDim == 3)
    {
        const auto appendLayerOptions = [&]()
        {
            QList<int> layerIds = map.layerToCellIds.keys();
            std::sort(layerIds.begin(), layerIds.end());
            for (int layerId : layerIds)
            {
                const auto cells = map.layerToCellIds.value(layerId);
                if (cells.isEmpty() || !hasVolumeCell(cells))
                    continue;
                GroupOption o;
                o.source = GroupOption::Source::Layer;
                o.id = layerId;
                o.label = QString::number(layerId);
                options.push_back(o);
            }
        };

        const auto appendPhysicalOptions = [&]()
        {
            for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
            {
                const int physId = it.key();
                const int dim = map.physIdToDim.value(physId, -1);
                if (dim != 3)
                    continue;
                if (!hasVolumeCell(map.volPhysToCellIds.value(physId)))
                    continue;
                GroupOption o;
                o.source = GroupOption::Source::Physical;
                o.id = physId;
                o.label = QString::number(physId);
                options.push_back(o);
            }
        };

        const bool preferLayer = map.layerToCellIds.size() > 1;
        if (preferLayer)
            appendLayerOptions();
        else if (!map.volPhysToCellIds.isEmpty())
            appendPhysicalOptions();
        else if (!map.layerToCellIds.isEmpty())
            appendLayerOptions();
        else if (!map.matypeToCellIds.isEmpty())
        {
            QList<int> matypeIds = map.matypeToCellIds.keys();
            std::sort(matypeIds.begin(), matypeIds.end());
            for (int matypeId : matypeIds)
            {
                const auto cells = map.matypeToCellIds.value(matypeId);
                if (cells.isEmpty() || !hasVolumeCell(cells))
                    continue;
                GroupOption o;
                o.source = GroupOption::Source::Matype;
                o.id = matypeId;
                o.label = QString::number(matypeId);
                options.push_back(o);
            }
        }
    }
    else
    {
        for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
        {
            const int physId = it.key();
            const int dim = map.physIdToDim.value(physId, -1);
            if (dim != targetDim)
                continue;
            GroupOption o;
            o.source = GroupOption::Source::Physical;
            o.id = physId;
            o.label = QString::number(physId);
            options.push_back(o);
        }
    }

    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();
    for (const GroupOption& o : options)
        m_groupCombo->addItem(o.label, makeGroupKey(o));
    m_groupCombo->blockSignals(false);

    disconnect(m_groupCombo, nullptr, this, nullptr);
    if (auto* groupCombo = qobject_cast<CustomComboBox*>(m_groupCombo))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this, targetDim](const int& row)
                {
                    if (row < 0 || !m_groupCombo)
                        return;
                    if (m_lastHoveredIndex >= 0 && m_lastHoveredIndex < m_groupCombo->count())
                    {
                        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
                    }
                    m_groupCombo->setItemData(row, QColor(230, 247, 255), Qt::BackgroundRole);
                    m_lastHoveredIndex = row;

                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;

                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    bool parseOk = false;
                    const GroupOption option =
                        parseGroupKey(m_groupCombo->itemData(row).toString(), &parseOk);
                    if (!parseOk)
                        return;

                    const int physId = option.id;
                    QVector<vtkIdType> faceKeys;
                    QVector<vtkIdType> edgeKeys;
                    if (targetDim == 2 && option.source == GroupOption::Source::Physical)
                    {
                        for (qint64 k : map.bndFacePhysToFaceKeys.value(physId))
                        {
                            const vtkIdType fk = static_cast<vtkIdType>(k);
                            faceKeys.push_back(fk);
                            const vtkIdType cid = PickManager::instance().unpackFaceCellId(fk);
                            const int lid = PickManager::instance().unpackFaceLocalId(fk);
                            edgeKeys.push_back(PickManager::instance().packEdgeKey(cid, lid));
                        }
                    }
                    else
                    {
                        QVector<vtkIdType> cids;
                        if (option.source == GroupOption::Source::Layer)
                        {
                            const auto src = map.layerToCellIds.value(physId);
                            cids.reserve(src.size());
                            for (qint64 cid : src)
                                cids.push_back(static_cast<vtkIdType>(cid));
                        }
                        else if (option.source == GroupOption::Source::Matype)
                        {
                            const auto src = map.matypeToCellIds.value(physId);
                            cids.reserve(src.size());
                            for (qint64 cid : src)
                                cids.push_back(static_cast<vtkIdType>(cid));
                        }
                        else
                        {
                            const auto src = map.volPhysToCellIds.value(physId);
                            cids.reserve(src.size());
                            for (qint64 cid : src)
                                cids.push_back(static_cast<vtkIdType>(cid));
                        }

                        faceKeys = visibleFaceKeysForCellIds(cids);
                    }
                    if (currentPickMode() == PickMode::Face)
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                                   faceKeys);
                    else
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                                   edgeKeys);
                },
                Qt::UniqueConnection);

        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    if (m_groupCombo && m_lastHoveredIndex >= 0 &&
                        m_lastHoveredIndex < m_groupCombo->count())
                    {
                        m_groupCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
                    }
                    m_lastHoveredIndex = -1;
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                                  currentPickMode());
                },
                Qt::UniqueConnection);
    }
}

PickMode CaveExcavationBodyPage::currentPickMode() const
{
    return PickMode::Face;
}

QVector<vtkIdType> CaveExcavationBodyPage::resolveCellIdsForIndex(int index) const
{
    QVector<vtkIdType> out;
    if (index < 0 || !m_groupCombo)
        return out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return out;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    vtkUnstructuredGrid* grid = nullptr;
    const auto gridHolder = meshSvc->getVtkUnstructuredGridSolid();
    if (gridHolder)
        grid = gridHolder;
    bool ok = false;
    const GroupOption option = parseGroupKey(m_groupCombo->itemData(index).toString(), &ok);
    if (!ok)
        return out;

    if (option.source == GroupOption::Source::Layer)
    {
        const auto src = map.layerToCellIds.value(option.id);
        out.reserve(src.size());
        for (qint64 cid : src)
        {
            const vtkIdType vtkCid = static_cast<vtkIdType>(cid);
            if (!grid || vtkCid < 0 || vtkCid >= grid->GetNumberOfCells() ||
                grid->GetCell(vtkCid)->GetCellDimension() != 3)
                continue;
            out.push_back(vtkCid);
        }
        return out;
    }
    if (option.source == GroupOption::Source::Matype)
    {
        const auto src = map.matypeToCellIds.value(option.id);
        out.reserve(src.size());
        for (qint64 cid : src)
        {
            const vtkIdType vtkCid = static_cast<vtkIdType>(cid);
            if (!grid || vtkCid < 0 || vtkCid >= grid->GetNumberOfCells() ||
                grid->GetCell(vtkCid)->GetCellDimension() != 3)
                continue;
            out.push_back(vtkCid);
        }
        return out;
    }

    const int dim = map.physIdToDim.value(option.id, -1);
    if (dim == 3)
    {
        const auto src = map.volPhysToCellIds.value(option.id);
        out.reserve(src.size());
        for (qint64 cid : src)
        {
            const vtkIdType vtkCid = static_cast<vtkIdType>(cid);
            if (!grid || vtkCid < 0 || vtkCid >= grid->GetNumberOfCells() ||
                grid->GetCell(vtkCid)->GetCellDimension() != 3)
                continue;
            out.push_back(vtkCid);
        }
    }
    return out;
}

QVector<vtkIdType> CaveExcavationBodyPage::visibleFaceKeysForCellIds(const QVector<vtkIdType>& cellIds) const
{
    QVector<vtkIdType> out;
    if (cellIds.isEmpty())
        return out;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return out;
    const MshPickMap map = meshSvc->mshPickMapSolid();

    QSet<vtkIdType> cellIdSet;
    cellIdSet.reserve(cellIds.size());
    for (vtkIdType cid : cellIds)
        cellIdSet.insert(cid);

    QSet<vtkIdType> uniq;
    for (auto it = map.bndFacePhysToFaceKeys.constBegin(); it != map.bndFacePhysToFaceKeys.constEnd(); ++it)
    {
        const auto& keys = it.value();
        for (qint64 k : keys)
        {
            const vtkIdType fk = static_cast<vtkIdType>(k);
            const vtkIdType cid = PickManager::instance().unpackFaceCellId(fk);
            if (cellIdSet.contains(cid))
                uniq.insert(fk);
        }
    }

    out.reserve(uniq.size());
    for (vtkIdType fk : uniq)
        out.push_back(fk);
    return out;
}

QVector<int> CaveExcavationBodyPage::toElementIds(const QVector<vtkIdType>& cellIds) const
{
    QVector<int> out;
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
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

void CaveExcavationBodyPage::refreshElementListWidget()
{
    if (!m_elemList)
        return;

    QList<int> ids = m_batchElementIds.values();
    std::sort(ids.begin(), ids.end());

    m_elemList->clear();
    for (int id : ids)
        m_elemList->addItem(QString::number(id));
}

QString CaveExcavationBodyPage::groupKeyForIndex(int index) const
{
    if (!m_groupCombo || index < 0 || index >= m_groupCombo->count())
        return QString();
    return m_groupCombo->itemData(index).toString();
}

bool CaveExcavationBodyPage::setCurrentGroupByKey(const QString& key)
{
    if (!m_groupCombo || key.trimmed().isEmpty())
        return false;
    for (int i = 0; i < m_groupCombo->count(); ++i)
    {
        if (m_groupCombo->itemData(i).toString() != key)
            continue;
        const QSignalBlocker blocker(m_groupCombo);
        m_groupCombo->setCurrentIndex(i);
        return true;
    }
    return false;
}

QString CaveExcavationBodyPage::makeGroupKey(const GroupOption& option)
{
    QString source = "physical";
    if (option.source == GroupOption::Source::Layer)
        source = "layer";
    else if (option.source == GroupOption::Source::Matype)
        source = "matype";
    return QString("%1:%2").arg(source).arg(option.id);
}

CaveExcavationBodyPage::GroupOption CaveExcavationBodyPage::parseGroupKey(const QString& key, bool* ok)
{
    GroupOption option;
    if (ok)
        *ok = false;
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty())
        return option;

    const QStringList parts = trimmed.split(':');
    if (parts.size() != 2)
        return option;

    bool idOk = false;
    const int id = parts[1].toInt(&idOk);
    if (!idOk)
        return option;

    option.id = id;
    if (parts[0] == "layer")
        option.source = GroupOption::Source::Layer;
    else if (parts[0] == "matype")
        option.source = GroupOption::Source::Matype;
    else
        option.source = GroupOption::Source::Physical;
    if (ok)
        *ok = true;
    return option;
}
