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

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <limits>
#include <set>

CaveExcavationBodyPage::CaveExcavationBodyPage(QWidget* parent)
    : BasePage("CaveExcavationBody")
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("开挖体选择"), this);
    title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:600;color:#1f2937;"));
    root->addWidget(title);

    auto* hint = new QLabel(QStringLiteral("2D 工况只选择一个开挖面；3D 工况只选择一个开挖体分组。下拉框悬停可预览，点击“设为当前”后替换原选择。"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#6b7280;"));
    root->addWidget(hint);

    auto* pickGroup = new QGroupBox(QStringLiteral("1. 选择对象"), this);
    auto* pickForm = new QFormLayout(pickGroup);
    pickForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    pickForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_groupCombo = new CustomComboBox(pickGroup);
    m_addGroupBtn = new QPushButton(QStringLiteral("设为当前"), pickGroup);
    m_clearBtn = new QPushButton(QStringLiteral("清空"), pickGroup);

    auto* actionRow = new QWidget(pickGroup);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(m_addGroupBtn);
    actionLayout->addWidget(m_clearBtn);

    pickForm->addRow(QStringLiteral("分组"), m_groupCombo);
    pickForm->addRow(QString(), actionRow);
    root->addWidget(pickGroup);

    auto* currentGroup = new QGroupBox(QStringLiteral("2. 当前选择"), this);
    auto* currentLayout = new QVBoxLayout(currentGroup);
    currentLayout->setContentsMargins(10, 8, 10, 10);
    currentLayout->setSpacing(8);
    m_groupList = new QListWidget(currentGroup);
    m_groupList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_groupList->setMaximumHeight(86);
    m_removeGroupBtn = new QPushButton(QStringLiteral("移除当前选择"), currentGroup);
    currentLayout->addWidget(m_groupList);
    currentLayout->addWidget(m_removeGroupBtn);
    root->addWidget(currentGroup);
    root->addStretch();

    connect(m_addGroupBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onAddGroupClicked);
    connect(m_removeGroupBtn, &QPushButton::clicked, this,
            &CaveExcavationBodyPage::onRemoveSelectedGroupsClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onClearClicked);

    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind kind)
            {
                if (domain != getPageType() || uuid != m_uuid || mode != PickMode::Face ||
                    kind != GridKind::Solid)
                    return;

                const int meshDim =
                    PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
                if (meshDim != 2)
                    return;

                QVector<vtkIdType> keys = PickManager::instance().getIdList(
                    getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
                if (keys.size() <= 1)
                {
                    if (keys.isEmpty())
                        setProperty("single_face_key", QString());
                    else
                        setProperty("single_face_key", QString::number(static_cast<qint64>(keys.first())));
                    return;
                }

                bool ok = false;
                const vtkIdType lastKey =
                    static_cast<vtkIdType>(property("single_face_key").toString().toLongLong(&ok));
                vtkIdType keep = keys.first();
                if (ok)
                {
                    for (vtkIdType key : keys)
                    {
                        if (key != lastKey)
                        {
                            keep = key;
                            break;
                        }
                    }
                }

                setProperty("single_face_key", QString::number(static_cast<qint64>(keep)));
                PickManager::instance().setSelectedFaceKeys(
                    getPageType(), m_uuid, GridKind::Solid, QVector<vtkIdType>{keep}, true);
            });

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
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, false);

    if (mode == PickMode::Domain)
    {
        refreshSelectedGroupList();
        refreshGroupComboMarks();
        updateGroupDomainHighlight(0);
    }
    else
    {
        refreshSelectedGroupList();
        refreshGroupComboMarks();
        updatePersistentSelectionFaces();
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
    if (!faceKeys.isEmpty())
        faceArr.append(static_cast<qint64>(faceKeys.first()));
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
        const int gid = v.toInt(std::numeric_limits<int>::min());
        if (gid != std::numeric_limits<int>::min() && gid != 0)
        {
            domains.insert(DomainKey{"LAYER", layerDomainDim(), gid});
            break;
        }
    }
    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

    QVector<vtkIdType> faceKeys, edgeKeys;
    const auto faceArr = obj.value("face_solid").toArray();
    faceKeys.reserve(faceArr.size());
    for (const auto& v : faceArr)
    {
        faceKeys.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
        break;
    }
    const auto edgeArr = obj.value("edge_solid").toArray();
    edgeKeys.reserve(edgeArr.size());
    for (const auto& v : edgeArr)
        edgeKeys.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));

    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys, true);
    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, true);

    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
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
    updateGroupDomainHighlight(0);
}
void CaveExcavationBodyPage::onAddGroupClicked()
{
    if (!m_groupCombo || m_groupCombo->currentIndex() < 0)
        return;

    const int gid = m_groupCombo->currentData().toInt();

    QSet<DomainKey> domains;
    domains.insert(DomainKey{"LAYER", layerDomainDim(), gid});
    PickManager::instance().setAppliedDomains(getPageType(), m_uuid, domains);

           // UI 一律从 PickManager 回读
    refreshSelectedGroupList();
    refreshGroupComboMarks();
    updatePersistentSelectionFaces();
    updateGroupDomainHighlight(0);
}

void CaveExcavationBodyPage::onRemoveSelectedGroupsClicked()
{
    if (!m_groupList)
        return;

    QSet<DomainKey> domains = PickManager::instance().appliedDomains(getPageType(), m_uuid);

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
    if (currentPickMode() == PickMode::Face)
    {
        const QVector<vtkIdType> previewFaceKeys = visibleBoundaryFaceKeys(groupCellIds(gid));
        PickManager::instance().setPreviewFaceKeys(
            getPageType(), m_uuid, GridKind::Solid,
            previewFaceKeys.isEmpty() ? QVector<vtkIdType>{}
                                      : QVector<vtkIdType>{previewFaceKeys.first()});
        return;
    }
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

    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (meshDim == 2)
    {
        out.reserve(cellIds.size());
        for (vtkIdType cid : cellIds)
            out.push_back(PickManager::instance().packFaceKey(cid, 0));
        return out;
    }

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

           // 2D：将“已选分组”转为 Face 持久高亮，便于开挖体分组可视反馈
    if (meshDim == 2)
    {
        QVector<vtkIdType> singleFace;
        const QSet<int> groups = selectedLayerGroups();
        for (int gid : groups)
        {
            const QVector<vtkIdType> faceKeys = visibleBoundaryFaceKeys(groupCellIds(gid));
            if (!faceKeys.isEmpty())
            {
                singleFace.push_back(faceKeys.first());
                break;
            }
        }
        PickManager::instance().setSelectedFaceKeys(
            getPageType(), m_uuid, GridKind::Solid, singleFace, true);
        return;
    }

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
        previewGroup != 0 ? DomainKey{"LAYER", layerDomainDim(), previewGroup} : DomainKey{});
    PickManager::instance().setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
}

int CaveExcavationBodyPage::layerDomainDim() const
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return (meshDim == 2) ? 2 : 3;
}

PickMode CaveExcavationBodyPage::currentPickMode() const
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return (meshDim == 2) ? PickMode::Face : PickMode::Domain;
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
