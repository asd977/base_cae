#include "StressBoundaryPage.h"
#include "ui_StressBoundaryPage.h"

#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalBlocker>
#include <QTextStream>

#include <vtkCell.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkIdList.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>

namespace {

QVector<StressBoundaryPage::EdgeRow> collectRows(GridKind kind,
                                                 const QString& domain,
                                                 const QString& uuid)
{
    QVector<StressBoundaryPage::EdgeRow> rows;

    PickManager& pm = PickManager::instance();
    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? pm.solidGrid() : pm.lineGrid();
    if (!grid) return rows;

    vtkDataArray* nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    if (!nodeIdArr) return rows;

    const auto edgeKeys = pm.getIdList(domain, uuid, PickMode::Edge, kind);
    rows.reserve(edgeKeys.size());

    for (vtkIdType edgeKey : edgeKeys)
    {
        const vtkIdType cellId = pm.unpackEdgeCellId(edgeKey);
        const int edgeLocalId = pm.unpackEdgeLocalId(edgeKey);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells()) continue;

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell) continue;

        vtkIdType pidA = -1;
        vtkIdType pidB = -1;

        if (cell->GetCellDimension() == 1 || edgeLocalId == 15)
        {
            vtkIdList* ids = cell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2) continue;
            pidA = ids->GetId(0);
            pidB = ids->GetId(ids->GetNumberOfIds() - 1);
        }
        else
        {
            if (edgeLocalId < 0 || edgeLocalId >= cell->GetNumberOfEdges()) continue;
            vtkCell* edge = cell->GetEdge(edgeLocalId);
            if (!edge) continue;
            vtkIdList* ids = edge->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2) continue;
            pidA = ids->GetId(0);
            pidB = ids->GetId(ids->GetNumberOfIds() - 1);
        }

        if (pidA < 0 || pidB < 0) continue;

        QSet<int> keSet;
        const int pickedKe = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
        keSet.insert(pickedKe);

        // 通过边的两个端点，反查共享该边的单元，补齐 KE1/KE2。
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
            if (!byA.contains(cid)) continue;

            const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cid)) : int(cid + 1);
            keSet.insert(ke);
            if (keSet.size() >= 2) break;
        }

        QVector<int> kes = keSet.values().toVector();
        std::sort(kes.begin(), kes.end());

        StressBoundaryPage::EdgeRow row;
        row.ke1 = kes.isEmpty() ? 0 : kes[0];
        row.ke2 = (kes.size() >= 2) ? kes[1] : 0;
        row.jd1 = int(nodeIdArr->GetTuple1(pidA));
        row.jd2 = int(nodeIdArr->GetTuple1(pidB));
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const StressBoundaryPage::EdgeRow& a,
                                           const StressBoundaryPage::EdgeRow& b) {
        if (a.ke1 != b.ke1) return a.ke1 < b.ke1;
        if (a.ke2 != b.ke2) return a.ke2 < b.ke2;
        if (a.jd1 != b.jd1) return a.jd1 < b.jd1;
        return a.jd2 < b.jd2;
    });
    return rows;
}

} // namespace

StressBoundaryPage::StressBoundaryPage(const QString& pageType,
                                       const QString& boundaryTitle,
                                       const QString& exportFileName,
                                       QWidget* parent)
    : BasePage(pageType),
      ui(new Ui::StressBoundaryPage),
      m_boundaryTitle(boundaryTitle),
      m_exportFileName(exportFileName)
{
    ui->setupUi(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->labelFileNameValue->setText(exportFileName);
    setupBatchGroupCombo();

    connect(ui->btnClear, &QPushButton::clicked, this, &StressBoundaryPage::onClearClicked);
    connect(ui->comboBoxGroup,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &StressBoundaryPage::onGroupChanged,
            Qt::UniqueConnection);
}

StressBoundaryPage::~StressBoundaryPage()
{
    delete ui;
}

void StressBoundaryPage::beforeLoad()
{
    setupBatchGroupCombo();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Edge);
}

void StressBoundaryPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Edge);
}

QVector<StressBoundaryPage::EdgeRow> StressBoundaryPage::selectedEdgeRows() const
{
    PickManager::instance().updateStructure();

    QVector<EdgeRow> rows = collectRows(GridKind::Line, getPageType(), m_uuid);
    if (rows.isEmpty()) rows = collectRows(GridKind::Solid, getPageType(), m_uuid);
    return rows;
}

bool StressBoundaryPage::exportDatFile()
{
    const QVector<EdgeRow> rows = selectedEdgeRows();

    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP") && !wd.mkpath("PREP")) return false;

    QFile file(wd.filePath("PREP/" + m_exportFileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream os(&file);
    os << QString("%1        4     NJX   MAKOD\n").arg(rows.size());
    os << "    KJX     KE1     KE2     JD1     JD2  JAT\n";

    for (int i = 0; i < rows.size(); ++i)
    {
        const EdgeRow& row = rows[i];
        os << QString("%1 %2 %3 %4 %5 %6\n")
              .arg(i + 1, 9)
              .arg(row.ke1, 9)
              .arg(row.ke2, 9)
              .arg(row.jd1, 9)
              .arg(row.jd2, 9)
              .arg(10, 9);
    }

    return true;
}

void StressBoundaryPage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Edge);
}

void StressBoundaryPage::setupBatchGroupCombo()
{
    if (!ui) return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim != 1) continue;
        ui->comboBoxGroup->addItem(QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId), physId);
    }

    ui->comboBoxGroup->blockSignals(false);
}

void StressBoundaryPage::onGroupChanged(int index)
{
    if (index < 0 || !ui) return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();

    QVector<vtkIdType> edgeKeys;
    const auto edgeKeysInt = map.bndEdgePhysToEdgeKeys.value(physId);
    edgeKeys.reserve(edgeKeysInt.size());
    for (qint64 k : edgeKeysInt) edgeKeys.push_back(static_cast<vtkIdType>(k));

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, true);
}

QJsonObject StressBoundaryPage::saveJson()
{
    QJsonObject obj;
    obj["group_phys_id"] = ui->comboBoxGroup->currentData().toInt();

    auto toJson = [](const QVector<vtkIdType>& ids) {
        QJsonArray arr;
        for (auto id : ids) arr.append(static_cast<qint64>(id));
        return arr;
    };
    obj["edge_solid"] = toJson(PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid));
    obj["edge_line"] = toJson(PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Line));
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

    auto fromJson = [](const QJsonArray& arr) {
        QVector<vtkIdType> out;
        out.reserve(arr.size());
        for (const auto& v : arr) out.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
        return out;
    };

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("edge_solid").toArray()), true);
    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Line,
                                                fromJson(obj.value("edge_line").toArray()), true);
}
