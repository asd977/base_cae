#include "CaveExcavationBodyPage.h"

#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkUnstructuredGrid.h>

#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
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
    form->addRow("选组", m_groupCombo);
    form->addRow("", m_batchAddBtn);
    form->addRow("", m_clearBtn);
    root->addLayout(form);
    root->addStretch();

    connect(m_clearBtn, &QPushButton::clicked, this, &CaveExcavationBodyPage::onClearClicked);
    connect(m_batchAddBtn, &QPushButton::clicked, this,
            [this]() { onGroupChanged(m_groupCombo->currentIndex()); });

    setupBatchGroupCombo();
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
    obj["group_phys_id"] = m_groupCombo ? m_groupCombo->currentData().toInt() : -1;

    obj["pick_mode"] = (currentPickMode() == PickMode::Face) ? "face" : "edge";
    QJsonArray faceArr, edgeArr;
    const auto faceKeys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
    for (vtkIdType key : faceKeys)
        faceArr.append(static_cast<qint64>(key));
    const auto edgeKeys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid);
    for (vtkIdType key : edgeKeys)
        edgeArr.append(static_cast<qint64>(key));
    obj["face_solid"] = faceArr;
    obj["edge_solid"] = edgeArr;
    return obj;
}

void CaveExcavationBodyPage::loadJson(const QJsonObject& obj)
{
    if (m_groupCombo)
    {
        const int physId = obj.value("group_phys_id").toInt(-1);
        for (int i = 0; i < m_groupCombo->count(); ++i)
        {
            if (m_groupCombo->itemData(i).toInt() == physId)
            {
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
    const auto edgeKeys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid);

    std::set<int> uniq;
    for (vtkIdType key : faceKeys)
    {
        const vtkIdType cellId = PickManager::instance().unpackFaceCellId(key);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
        uniq.insert(ke);
    }
    for (vtkIdType key : edgeKeys)
    {
        const vtkIdType cellId = PickManager::instance().unpackEdgeCellId(key);
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
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
}

void CaveExcavationBodyPage::onGroupChanged(int index)
{
    if (index < 0 || !m_groupCombo)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = m_groupCombo->itemData(index).toInt();
    const int dim = map.physIdToDim.value(physId, -1);

    QVector<vtkIdType> faceKeys;
    QVector<vtkIdType> edgeKeys;
    if (dim == 2)
    {
        const auto src = map.bndFacePhysToFaceKeys.value(physId);
        faceKeys.reserve(src.size());
        edgeKeys.reserve(src.size());
        for (qint64 k : src)
        {
            const vtkIdType fk = static_cast<vtkIdType>(k);
            faceKeys.push_back(fk);
            const vtkIdType cid = PickManager::instance().unpackFaceCellId(fk);
            const int lid = PickManager::instance().unpackFaceLocalId(fk);
            edgeKeys.push_back(PickManager::instance().packEdgeKey(cid, lid));
        }
    }
    else if (dim == 3)
    {
        const auto src = map.volPhysToCellIds.value(physId);
        faceKeys.reserve(src.size());
        for (qint64 cid : src)
            faceKeys.push_back(PickManager::instance().packFaceKey(static_cast<vtkIdType>(cid), 15));
    }

    if (currentPickMode() == PickMode::Face)
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys, false);
    else
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, false);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
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

    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim != targetDim)
            continue;
        m_groupCombo->addItem(QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId),
                              physId);
    }
    m_groupCombo->blockSignals(false);

    disconnect(m_groupCombo, nullptr, this, nullptr);
    if (auto* groupCombo = qobject_cast<CustomComboBox*>(m_groupCombo))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this, targetDim](const int& row)
                {
                    if (row < 0 || !m_groupCombo)
                        return;
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;

                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const int physId = m_groupCombo->itemData(row).toInt();
                    QVector<vtkIdType> faceKeys;
                    QVector<vtkIdType> edgeKeys;
                    if (targetDim == 2)
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
                        for (qint64 cid : map.volPhysToCellIds.value(physId))
                            faceKeys.push_back(PickManager::instance().packFaceKey(static_cast<vtkIdType>(cid), 15));
                    }
                    if (currentPickMode() == PickMode::Face)
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid,
                                                                   GridKind::Solid, faceKeys);
                    else
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid,
                                                                   GridKind::Solid, edgeKeys);
                },
                Qt::UniqueConnection);

        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                                  currentPickMode());
                },
                Qt::UniqueConnection);
    }
}

PickMode CaveExcavationBodyPage::currentPickMode() const
{
    const int meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return (meshDim == 3) ? PickMode::Face : PickMode::Edge;
}
