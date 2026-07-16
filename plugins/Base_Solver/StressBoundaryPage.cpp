#include "StressBoundaryPage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
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

#include "SolverUiLayoutHelper.h"
namespace
{
struct EdgeRaw
{
    int ke1 = 0;
    int ke2 = 0;
    int jdA = 0;
    int jdB = 0;
    double ax = 0.0;
    double ay = 0.0;
    double bx = 0.0;
    double by = 0.0;
};

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
    return row;
}

QVector<StressBoundaryPage::EdgeRow> collectRowsFromEdges(GridKind kind, const QString& domain,
                                                          const QString& uuid)
{
    QVector<StressBoundaryPage::EdgeRow> rows;
    QVector<EdgeRaw> rawEdges;
    bool keepChainOrder = false;
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

        double pa[3] = {0.0, 0.0, 0.0};
        double pb[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(pidA, pa);
        grid->GetPoint(pidB, pb);
        EdgeRaw raw;
        raw.ke1 = rows.back().ke1;
        raw.ke2 = rows.back().ke2;
        raw.jdA = rows.back().jd1;
        raw.jdB = rows.back().jd2;
        raw.ax = pa[0];
        raw.ay = pa[1];
        raw.bx = pb[0];
        raw.by = pb[1];
        rawEdges.push_back(raw);
    }

    // 优先按“连通链+几何起点”恢复旧流程的边界定向（而非按编号大小）。
    if (rawEdges.size() >= 2)
    {
        QHash<int, QVector<int>> nodeToEdges;
        QHash<int, QPair<double, double>> nodeXY;
        for (int i = 0; i < rawEdges.size(); ++i)
        {
            const auto& e = rawEdges[i];
            nodeToEdges[e.jdA].push_back(i);
            nodeToEdges[e.jdB].push_back(i);
            if (!nodeXY.contains(e.jdA))
                nodeXY.insert(e.jdA, qMakePair(e.ax, e.ay));
            if (!nodeXY.contains(e.jdB))
                nodeXY.insert(e.jdB, qMakePair(e.bx, e.by));
        }

        QVector<int> endpoints;
        for (auto it = nodeToEdges.constBegin(); it != nodeToEdges.constEnd(); ++it)
            if (it.value().size() == 1)
                endpoints.push_back(it.key());

        if (!endpoints.isEmpty())
        {
            auto lessNodeByXY = [&](int lhs, int rhs)
            {
                const auto l = nodeXY.value(lhs);
                const auto r = nodeXY.value(rhs);
                if (l.second != r.second)
                    return l.second < r.second; // 先按Y
                if (l.first != r.first)
                    return l.first < r.first; // 再按X
                return lhs < rhs;
            };
            std::sort(endpoints.begin(), endpoints.end(), lessNodeByXY);
            const int startNode = endpoints.first();

            QVector<StressBoundaryPage::EdgeRow> chained;
            chained.reserve(rawEdges.size());
            QSet<int> usedEdges;
            int prevNode = -1;
            int currNode = startNode;

            while (chained.size() < rawEdges.size())
            {
                const auto candidates = nodeToEdges.value(currNode);
                int chosen = -1;
                for (int ei : candidates)
                {
                    if (usedEdges.contains(ei))
                        continue;
                    const auto& e = rawEdges[ei];
                    const int other = (e.jdA == currNode) ? e.jdB : e.jdA;
                    if (other == prevNode)
                        continue;
                    chosen = ei;
                    break;
                }
                if (chosen < 0)
                {
                    for (int ei : candidates)
                    {
                        if (!usedEdges.contains(ei))
                        {
                            chosen = ei;
                            break;
                        }
                    }
                }
                if (chosen < 0)
                    break;

                usedEdges.insert(chosen);
                const auto& e = rawEdges[chosen];
                const int nextNode = (e.jdA == currNode) ? e.jdB : e.jdA;
                StressBoundaryPage::EdgeRow row;
                row.ke1 = e.ke1;
                row.ke2 = e.ke2;
                // 与旧 AutoCAD 结果一致：每段输出 (next, curr)
                row.jd1 = nextNode;
                row.jd2 = currNode;
                chained.push_back(row);
                prevNode = currNode;
                currNode = nextNode;
            }

            if (chained.size() == rawEdges.size())
            {
                rows = chained;
                keepChainOrder = true;
            }
        }
    }

    if (!keepChainOrder)
    {
        std::sort(rows.begin(), rows.end(), lessRow);
        rows.erase(std::unique(rows.begin(), rows.end(),
                               [](const StressBoundaryPage::EdgeRow& a,
                                  const StressBoundaryPage::EdgeRow& b)
                               { return a.ke1 == b.ke1 && a.ke2 == b.ke2 && a.jd1 == b.jd1 && a.jd2 == b.jd2; }),
                   rows.end());
    }
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
    SolverUiLayoutHelper::polishPage(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->label2->hide();
    ui->labelFileNameValue->hide();
    setupBatchGroupCombo();

    connect(ui->btnClear, &QPushButton::clicked, this, &StressBoundaryPage::onClearClicked);
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind)
            {
                if (domain == getPageType() && uuid == m_uuid && mode == currentPickMode())
                    updateSelectionSummary();
            });
    updateSelectionSummary();
}

StressBoundaryPage::~StressBoundaryPage()
{
    delete ui;
}

void StressBoundaryPage::beforeLoad()
{
    setupBatchGroupCombo();
    updateSelectionSummary();
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

int StressBoundaryPage::selectionCount()
{
    return selectedEdgeRows().size();
}

void StressBoundaryPage::updateSelectionSummary()
{
    const int count = selectionCount();
    const QString unit = currentPickMode() == PickMode::Face ? QStringLiteral("个面")
                                                             : QStringLiteral("条边");
    ui->labelBoundaryTitle->setText(
        count > 0 ? QStringLiteral("%1（已选择 %2 %3）").arg(m_boundaryTitle).arg(count).arg(unit)
                  : QStringLiteral("%1（未设置）").arg(m_boundaryTitle));
    emit selectionCountChanged(count);
}

bool StressBoundaryPage::exportDatFile()
{
    const QVector<EdgeRow> rows = selectedEdgeRows();
    const bool isFaceMode = (currentPickMode() == PickMode::Face);

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(m_exportFileName));
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
    updateSelectionSummary();
}

void StressBoundaryPage::setupBatchGroupCombo()
{
    if (!ui)
        return;

    const bool selectFaces = currentPickMode() == PickMode::Face;
    ui->labelPickHint->setText(
        selectFaces
            ? QStringLiteral("选择对象：三维网格面。可在视图中框选，或按边界面分组批量添加。")
            : QStringLiteral("选择对象：二维网格边。可在视图中框选，或按边界线分组批量添加。"));
    ui->btnClear->setText(selectFaces ? QStringLiteral("清空选面") : QStringLiteral("清空选边"));

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int targetDim = (currentPickMode() == PickMode::Face) ? 2 : 1;

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    const auto nameMap = map.physicalNameMap();
    for (auto it = nameMap.constBegin(); it != nameMap.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physicalDim(physId);
        if (dim != targetDim)
            continue;
        ui->comboBoxGroup->addItem(it.value(), physId);
    }

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
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();

                    if (currentPickMode() == PickMode::Face)
                    {
                        QVector<vtkIdType> faceKeys;
                        for (qint64 k : map.physicalFaceKeys(physId))
                            faceKeys.push_back(vtkIdType(k));
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid,
                                                                   GridKind::Solid, faceKeys);
                    }
                    else
                    {
                        QVector<vtkIdType> edgeKeys;
                        for (qint64 k : map.physicalEdgeKeys(physId))
                            edgeKeys.push_back(vtkIdType(k));
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid,
                                                                   GridKind::Solid, edgeKeys);
                    }
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
    const int physId = ui->comboBoxGroup->itemData(index).toInt();

    if (currentPickMode() == PickMode::Face)
    {
        QVector<vtkIdType> faceKeys;
        const auto faceKeysInt = map.physicalFaceKeys(physId);
        faceKeys.reserve(faceKeysInt.size());
        for (qint64 k : faceKeysInt)
            faceKeys.push_back(static_cast<vtkIdType>(k));

        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    faceKeys, false);
    }
    else
    {
        QVector<vtkIdType> edgeKeys;
        const auto edgeKeysInt = map.physicalEdgeKeys(physId);
        edgeKeys.reserve(edgeKeysInt.size());
        for (qint64 k : edgeKeysInt)
            edgeKeys.push_back(static_cast<vtkIdType>(k));

        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    edgeKeys, false);
    }

    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    updateSelectionSummary();
}

QJsonObject StressBoundaryPage::saveJson()
{
    QJsonObject obj;
    obj["group_phys_id"] = ui->comboBoxGroup->currentData().toInt();

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
    const int physId = obj.value("group_phys_id").toInt(-1);
    for (int i = 0; i < ui->comboBoxGroup->count(); ++i)
    {
        if (ui->comboBoxGroup->itemData(i).toInt() == physId)
        {
            const QSignalBlocker blocker(ui->comboBoxGroup);
            ui->comboBoxGroup->setCurrentIndex(i);
            break;
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
    updateSelectionSummary();
}

PickMode StressBoundaryPage::currentPickMode() const
{
    const int dim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return dim == 3 ? PickMode::Face : PickMode::Edge;
}
