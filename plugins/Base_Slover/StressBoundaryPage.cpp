#include "StressBoundaryPage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTextStream>
#include <algorithm>

#include "ui_StressBoundaryPage.h"

namespace
{

QString boundaryTagText(const MeshTagSet& set)
{
    const QString name = set.displayName();
    return QStringLiteral("%1  (dim=%2, id=%3)")
        .arg(name)
        .arg(set.key.dim)
        .arg(set.key.id);
}

MeshTagKey comboTagKey(QComboBox* combo, int index = -1)
{
    if (!combo)
        return MeshTagKey{};
    const QVariant v = (index >= 0) ? combo->itemData(index) : combo->currentData();
    if (!v.isValid())
        return MeshTagKey{};
    return v.value<MeshTagKey>();
}

QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& values)
{
    QVector<vtkIdType> out;
    out.reserve(values.size());
    for (MeshIdType v : values)
        out.push_back(static_cast<vtkIdType>(v));
    return out;
}

QVector<vtkIdType> edgeKeysOfTag(const MshPickMap& map, const MeshTagKey& key)
{
    const MeshTagSet set = map.tagSets.value(key);
    return toVtkIds(set.edgeKeys);
}

QVector<vtkIdType> faceKeysOfTag(const MshPickMap& map, const MeshTagKey& key)
{
    const MeshTagSet set = map.tagSets.value(key);
    return toVtkIds(set.faceKeys);
}

bool lessRow(const StressBoundaryPage::EdgeRow& a, const StressBoundaryPage::EdgeRow& b)
{
    if (a.ke1 != b.ke1)
        return a.ke1 < b.ke1;
    if (a.ke2 != b.ke2)
        return a.ke2 < b.ke2;
    if (a.jd1 != b.jd1)
        return a.jd1 < b.jd1;
    if (a.jd2 != b.jd2)
        return a.jd2 < b.jd2;
    if (a.jd3 != b.jd3)
        return a.jd3 < b.jd3;
    return a.jd4 < b.jd4;
}

StressBoundaryPage::EdgeRow makeRow(vtkUnstructuredGrid* grid, vtkDataArray* elemIdArr,
                                    vtkDataArray* nodeIdArr, vtkIdType cellId, vtkIdType pidA,
                                    vtkIdType pidB)
{
    StressBoundaryPage::EdgeRow row;
    QSet<int> keSet;
    const int pickedKe = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
    keSet.insert(pickedKe);

    vtkNew<vtkIdList> cellsA;
    vtkNew<vtkIdList> cellsB;
    grid->GetPointCells(pidA, cellsA);
    grid->GetPointCells(pidB, cellsB);

    QSet<vtkIdType> byA;
    for (vtkIdType i = 0; i < cellsA->GetNumberOfIds(); ++i)
        byA.insert(cellsA->GetId(i));

    for (vtkIdType i = 0; i < cellsB->GetNumberOfIds(); ++i)
    {
        const vtkIdType cid = cellsB->GetId(i);
        if (!byA.contains(cid))
            continue;
        const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cid)) : int(cid + 1);
        keSet.insert(ke);
        if (keSet.size() >= 2)
            break;
    }

    QVector<int> kes = keSet.values().toVector();
    std::sort(kes.begin(), kes.end());
    row.ke1 = kes.isEmpty() ? 0 : kes[0];
    row.ke2 = (kes.size() >= 2) ? kes[1] : 0;
    row.jd1 = int(nodeIdArr->GetTuple1(pidA));
    row.jd2 = int(nodeIdArr->GetTuple1(pidB));
    if (row.jd1 > row.jd2)
        std::swap(row.jd1, row.jd2);
    return row;
}

QVector<StressBoundaryPage::EdgeRow> collectRowsFromEdges(GridKind kind, const QString& domain,
                                                          const QString& uuid)
{
    QVector<StressBoundaryPage::EdgeRow> rows;
    PickManager& pm = PickManager::instance();
    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? pm.solidGrid() : pm.lineGrid();
    if (!grid)
        return rows;

    vtkDataArray* nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    if (!nodeIdArr)
        return rows;

    const auto edgeKeys = pm.getIdList(domain, uuid, PickMode::Edge, kind);
    rows.reserve(edgeKeys.size());

    for (vtkIdType edgeKey : edgeKeys)
    {
        const vtkIdType cellId = pm.unpackEdgeCellId(edgeKey);
        const int edgeLocalId = pm.unpackEdgeLocalId(edgeKey);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        vtkIdType pidA = -1;
        vtkIdType pidB = -1;
        if (cell->GetCellDimension() == 1 || edgeLocalId == 15)
        {
            vtkIdList* ids = cell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2)
                continue;
            pidA = ids->GetId(0);
            pidB = ids->GetId(ids->GetNumberOfIds() - 1);
        }
        else
        {
            if (edgeLocalId < 0 || edgeLocalId >= cell->GetNumberOfEdges())
                continue;
            vtkCell* edge = cell->GetEdge(edgeLocalId);
            if (!edge)
                continue;
            vtkIdList* ids = edge->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2)
                continue;
            pidA = ids->GetId(0);
            pidB = ids->GetId(ids->GetNumberOfIds() - 1);
        }

        if (pidA < 0 || pidB < 0)
            continue;
        rows.push_back(makeRow(grid, elemIdArr, nodeIdArr, cellId, pidA, pidB));
    }

    std::sort(rows.begin(), rows.end(), lessRow);
    rows.erase(std::unique(rows.begin(), rows.end(),
                           [](const StressBoundaryPage::EdgeRow& a,
                              const StressBoundaryPage::EdgeRow& b)
                           { return a.ke1 == b.ke1 && a.ke2 == b.ke2 && a.jd1 == b.jd1 && a.jd2 == b.jd2; }),
               rows.end());
    return rows;
}

QVector<StressBoundaryPage::EdgeRow> collectRowsFromFaces(GridKind kind, const QString& domain,
                                                          const QString& uuid)
{
    QVector<StressBoundaryPage::EdgeRow> rows;
    PickManager& pm = PickManager::instance();
    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? pm.solidGrid() : pm.lineGrid();
    if (!grid)
        return rows;

    vtkDataArray* nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    if (!nodeIdArr)
        return rows;

    const auto faceKeys = pm.getIdList(domain, uuid, PickMode::Face, kind);
    rows.reserve(faceKeys.size());

    for (vtkIdType faceKey : faceKeys)
    {
        const vtkIdType cellId = pm.unpackFaceCellId(faceKey);
        const int faceLocalId = pm.unpackFaceLocalId(faceKey);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        vtkIdList* ids = nullptr;
        if (cell->GetCellDimension() == 2)
            ids = cell->GetPointIds();
        else
        {
            if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
                continue;
            vtkCell* face = cell->GetFace(faceLocalId);
            if (!face)
                continue;
            ids = face->GetPointIds();
        }
        if (!ids || ids->GetNumberOfIds() < 3)
            continue;

        StressBoundaryPage::EdgeRow row;
        row.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
        row.ke2 = 0;
        const int cornerCount = std::min<int>(ids->GetNumberOfIds(), 4);
        if (cornerCount >= 1)
            row.jd1 = int(nodeIdArr->GetTuple1(ids->GetId(0)));
        if (cornerCount >= 2)
            row.jd2 = int(nodeIdArr->GetTuple1(ids->GetId(1)));
        if (cornerCount >= 3)
            row.jd3 = int(nodeIdArr->GetTuple1(ids->GetId(2)));
        if (cornerCount >= 4)
            row.jd4 = int(nodeIdArr->GetTuple1(ids->GetId(3)));
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), lessRow);
    rows.erase(std::unique(rows.begin(), rows.end(),
                           [](const StressBoundaryPage::EdgeRow& a,
                              const StressBoundaryPage::EdgeRow& b)
                           {
                               return a.ke1 == b.ke1 && a.ke2 == b.ke2 && a.jd1 == b.jd1 &&
                                      a.jd2 == b.jd2 && a.jd3 == b.jd3 && a.jd4 == b.jd4;
                           }),
               rows.end());
    return rows;
}

} // namespace

StressBoundaryPage::StressBoundaryPage(const QString& pageType, const QString& boundaryTitle,
                                       const QString& exportFileName, QWidget* parent)
    : BasePage(pageType), ui(new Ui::StressBoundaryPage), m_boundaryTitle(boundaryTitle),
      m_exportFileName(exportFileName)
{
    ui->setupUi(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->labelFileNameValue->setText(exportFileName);
    setupBatchGroupCombo();

    connect(ui->btnClear, &QPushButton::clicked, this, &StressBoundaryPage::onClearClicked);
}

StressBoundaryPage::~StressBoundaryPage()
{
    delete ui;
}

void StressBoundaryPage::beforeLoad()
{
    setupBatchGroupCombo();
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
}

void StressBoundaryPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QVector<StressBoundaryPage::EdgeRow> StressBoundaryPage::selectedEdgeRows()
{
    PickManager::instance().updateStructure();

    if (currentPickMode() == PickMode::Face)
    {
        QVector<EdgeRow> rows = collectRowsFromFaces(GridKind::Solid, getPageType(), m_uuid);
        if (rows.isEmpty())
            rows = collectRowsFromFaces(GridKind::Line, getPageType(), m_uuid);
        return rows;
    }

    QVector<EdgeRow> rows = collectRowsFromEdges(GridKind::Line, getPageType(), m_uuid);
    if (rows.isEmpty())
        rows = collectRowsFromEdges(GridKind::Solid, getPageType(), m_uuid);
    return rows;
}

bool StressBoundaryPage::exportDatFile()
{
    const QVector<EdgeRow> rows = selectedEdgeRows();
    const bool isFaceMode = (currentPickMode() == PickMode::Face);

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/" + m_exportFileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    if (isFaceMode)
    {
        os << QString("%1        6     NJX   MAKOD\n").arg(rows.size());
        os << "    KJX     KE1     KE2     JD1     JD2     JD3     JD4  JAT\n";
    }
    else
    {
        os << QString("%1        4     NJX   MAKOD\n").arg(rows.size());
        os << "    KJX     KE1     KE2     JD1     JD2  JAT\n";
    }

    for (int i = 0; i < rows.size(); ++i)
    {
        const EdgeRow& row = rows[i];
        if (isFaceMode)
        {
            os << QString("%1 %2 %3 %4 %5 %6 %7 %8\n")
            .arg(i + 1, 9)
                .arg(row.ke1, 9)
                .arg(0, 9)
                .arg(row.jd1, 9)
                .arg(row.jd2, 9)
                .arg(row.jd3, 9)
                .arg(row.jd4, 9)
                .arg(6, 9);
        }
        else
        {
            os << QString("%1 %2 %3 %4 %5 %6\n")
            .arg(i + 1, 9)
                .arg(row.ke1, 9)
                .arg(row.ke2, 9)
                .arg(row.jd1, 9)
                .arg(row.jd2, 9)
                .arg(10, 9);
        }
    }

    return true;
}

void StressBoundaryPage::onClearClicked()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
}

void StressBoundaryPage::setupBatchGroupCombo()
{
    if (!ui)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int targetDim = (currentPickMode() == PickMode::Face) ? 2 : 1;

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    const QVector<MeshTagSet> sets = map.boundaryTagSets(targetDim);
    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryTagText(set), QVariant::fromValue(set.key));

    ui->comboBoxGroup->blockSignals(false);

    disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    disconnect(ui->btnBatchAdd, nullptr, this, nullptr);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    if (row < 0 || !ui)
                        return;

                    auto* meshSvc =
                        ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;

                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);

                    if (currentPickMode() == PickMode::Face)
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid,
                                                                   GridKind::Solid,
                                                                   faceKeysOfTag(map, key));
                    else
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid,
                                                                   GridKind::Solid,
                                                                   edgeKeysOfTag(map, key));
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

    connect(ui->btnBatchAdd, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            },
            Qt::UniqueConnection);
}

void StressBoundaryPage::onGroupChanged(int index)
{
    if (index < 0 || !ui)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    if (!key.isValid())
        return;

    if (currentPickMode() == PickMode::Face)
    {
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    faceKeysOfTag(map, key), false);
    }
    else
    {
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    edgeKeysOfTag(map, key), false);
    }

    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
}

QJsonObject StressBoundaryPage::saveJson()
{
    QJsonObject obj;
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup);
    obj["group_category"] = key.category;
    obj["group_dim"] = key.dim;
    obj["group_id"] = key.id;

    auto toJson = [](const QVector<vtkIdType>& ids)
    {
        QJsonArray arr;
        for (auto id : ids)
            arr.append(static_cast<qint64>(id));
        return arr;
    };
    obj["pick_mode"] = (currentPickMode() == PickMode::Face) ? "face" : "edge";
    obj["edge_solid"] = toJson(
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid));
    obj["edge_line"] = toJson(
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Line));
    obj["face_solid"] = toJson(
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid));
    obj["face_line"] = toJson(
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Line));
    return obj;
}

void StressBoundaryPage::loadJson(const QJsonObject& obj)
{
    setupBatchGroupCombo();

    MeshTagKey savedKey;
    savedKey.category = obj.value("group_category").toString();
    savedKey.dim = obj.value("group_dim").toInt(-1);
    savedKey.id = obj.value("group_id").toInt(-1);

    if (savedKey.isValid())
    {
        for (int i = 0; i < ui->comboBoxGroup->count(); ++i)
        {
            if (comboTagKey(ui->comboBoxGroup, i) == savedKey)
            {
                const QSignalBlocker blocker(ui->comboBoxGroup);
                ui->comboBoxGroup->setCurrentIndex(i);
                break;
            }
        }
    }

    auto fromJson = [](const QJsonArray& arr)
    {
        QVector<vtkIdType> out;
        out.reserve(arr.size());
        for (const auto& v : arr)
            out.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
        return out;
    };

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("edge_solid").toArray()), true);
    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Line,
                                                fromJson(obj.value("edge_line").toArray()), true);
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("face_solid").toArray()), true);
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Line,
                                                fromJson(obj.value("face_line").toArray()), true);
}

PickMode StressBoundaryPage::currentPickMode() const
{
    const int dim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return dim == 3 ? PickMode::Face : PickMode::Edge;
}
