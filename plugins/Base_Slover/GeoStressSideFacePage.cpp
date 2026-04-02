#include "GeoStressSideFacePage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTextStream>

namespace
{
struct EdgeRow
{
    int ke1 = 0;
    int ke2 = 0;
    int jd1 = 0;
    int jd2 = 0;
};

EdgeRow makeRow(vtkUnstructuredGrid* grid, vtkDataArray* elemIdArr, vtkDataArray* nodeIdArr,
                vtkIdType cellId, vtkIdType pidA, vtkIdType pidB)
{
    EdgeRow row;
    row.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
    row.ke2 = 0;
    row.jd1 = int(nodeIdArr->GetTuple1(pidA));
    row.jd2 = int(nodeIdArr->GetTuple1(pidB));
    return row;
}

QVector<EdgeRow> collectRows(vtkUnstructuredGrid* grid, const QString& domain, const QString& uuid,
                             PickMode mode)
{
    QVector<EdgeRow> rows;
    if (!grid)
        return rows;

    PickManager& pm = PickManager::instance();
    vtkDataArray* nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    vtkDataArray* elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));
    if (!nodeIdArr)
        return rows;

    const auto keys = pm.getIdList(domain, uuid, mode, GridKind::Solid);
    for (vtkIdType key : keys)
    {
        const vtkIdType cellId = (mode == PickMode::Face) ? pm.unpackFaceCellId(key) : pm.unpackEdgeCellId(key);
        const int localId = (mode == PickMode::Face) ? pm.unpackFaceLocalId(key) : pm.unpackEdgeLocalId(key);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        if (mode == PickMode::Face)
        {
            vtkIdList* ids = nullptr;
            if (cell->GetCellDimension() == 2)
                ids = cell->GetPointIds();
            else
            {
                if (localId < 0 || localId >= cell->GetNumberOfFaces())
                    continue;
                auto* face = cell->GetFace(localId);
                ids = face ? face->GetPointIds() : nullptr;
            }
            if (!ids || ids->GetNumberOfIds() < 2)
                continue;
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                const vtkIdType a = ids->GetId(i);
                const vtkIdType b = ids->GetId((i + 1) % ids->GetNumberOfIds());
                rows.push_back(makeRow(grid, elemIdArr, nodeIdArr, cellId, a, b));
            }
        }
        else
        {
            vtkIdType a = -1, b = -1;
            if (cell->GetCellDimension() == 1 || localId == 15)
            {
                auto* ids = cell->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                a = ids->GetId(0);
                b = ids->GetId(ids->GetNumberOfIds() - 1);
            }
            else
            {
                if (localId < 0 || localId >= cell->GetNumberOfEdges())
                    continue;
                auto* edge = cell->GetEdge(localId);
                auto* ids = edge ? edge->GetPointIds() : nullptr;
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                a = ids->GetId(0);
                b = ids->GetId(ids->GetNumberOfIds() - 1);
            }
            rows.push_back(makeRow(grid, elemIdArr, nodeIdArr, cellId, a, b));
        }
    }
    return rows;
}
} // namespace

GeoStressSideFacePage::GeoStressSideFacePage(QWidget* parent)
    : BasePage("GeoStressRegressionSideFace")
{
    Q_UNUSED(parent);
    auto* form = new QFormLayout(this);

    m_faceFileEdit = new QLineEdit("FASIDE.DAT", this);
    m_pickModeCombo = new QComboBox(this);
    m_pickModeCombo->addItem("2D选线", "edge");
    m_pickModeCombo->addItem("3D选面", "face");
    m_clearBtn = new QPushButton("清空所选", this);

    m_idirEdit = new QLineEdit("1", this);
    m_heightEdit = new QLineEdit("2000.0000", this);
    m_gamaEdit = new QLineEdit("2650.0000", this);
    m_pgroutEdit = new QLineEdit("0", this);
    m_iiEdit = new QLineEdit("1", this);

    form->addRow("侧立面文件", m_faceFileEdit);
    form->addRow("拾取模式", m_pickModeCombo);
    form->addRow("", m_clearBtn);
    form->addRow("剖切方向(IDIR)", m_idirEdit);
    form->addRow("侧压高程(m)", m_heightEdit);
    form->addRow("测压容重(kg/m3)", m_gamaEdit);
    form->addRow("附加矩形常压(Pa)", m_pgroutEdit);
    form->addRow("序号", m_iiEdit);

    connect(m_clearBtn, &QPushButton::clicked, this, &GeoStressSideFacePage::onClearClicked);
}

void GeoStressSideFacePage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
}

void GeoStressSideFacePage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QJsonObject GeoStressSideFacePage::saveJson()
{
    QJsonObject obj;
    obj["face_file"] = m_faceFileEdit->text().trimmed();
    obj["pick_mode"] = m_pickModeCombo->currentData().toString();
    obj["idir"] = m_idirEdit->text().trimmed();
    obj["height"] = m_heightEdit->text().trimmed();
    obj["gama"] = m_gamaEdit->text().trimmed();
    obj["pgrout"] = m_pgroutEdit->text().trimmed();
    obj["ii"] = m_iiEdit->text().trimmed();

    auto toJson = [](const QVector<vtkIdType>& ids)
    {
        QJsonArray arr;
        for (vtkIdType id : ids)
            arr.append(qint64(id));
        return arr;
    };

    obj["picked_edges"] = toJson(PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid));
    obj["picked_faces"] = toJson(PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid));
    return obj;
}

void GeoStressSideFacePage::loadJson(const QJsonObject& obj)
{
    m_faceFileEdit->setText(obj.value("face_file").toString(m_faceFileEdit->text()));
    const int modeIdx = m_pickModeCombo->findData(obj.value("pick_mode").toString("edge"));
    if (modeIdx >= 0)
        m_pickModeCombo->setCurrentIndex(modeIdx);
    m_idirEdit->setText(obj.value("idir").toString(m_idirEdit->text()));
    m_heightEdit->setText(obj.value("height").toString(m_heightEdit->text()));
    m_gamaEdit->setText(obj.value("gama").toString(m_gamaEdit->text()));
    m_pgroutEdit->setText(obj.value("pgrout").toString(m_pgroutEdit->text()));
    m_iiEdit->setText(obj.value("ii").toString(m_iiEdit->text()));

    auto fromJson = [](const QJsonArray& arr)
    {
        QVector<vtkIdType> ids;
        ids.reserve(arr.size());
        for (const auto& v : arr)
            ids.push_back(v.toVariant().toLongLong());
        return ids;
    };

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("picked_edges").toArray()), true);
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("picked_faces").toArray()), true);
}

QString GeoStressSideFacePage::faceFile() const
{
    return m_faceFileEdit->text().trimmed();
}

GeoStressSideFacePage::FaceAttr GeoStressSideFacePage::attr() const
{
    FaceAttr a;
    a.idir = m_idirEdit->text().trimmed();
    a.height = m_heightEdit->text().trimmed();
    a.gama = m_gamaEdit->text().trimmed();
    a.pgrout = m_pgroutEdit->text().trimmed();
    a.ii = m_iiEdit->text().trimmed();
    return a;
}

bool GeoStressSideFacePage::exportDatFile() const
{
    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return false;

    const QVector<EdgeRow> rows = collectRows(grid, getPageType(), m_uuid, currentPickMode());

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/" + faceFile()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << QString("%1        4     NJX   MAKOD\n").arg(rows.size());
    os << "    KJX     KE1     KE2     JD1     JD2  JAT\n";
    for (int i = 0; i < rows.size(); ++i)
    {
        const EdgeRow& row = rows[i];
        os << QString("%1 %2 %3 %4 %5 %6\n")
                  .arg(i + 1, 9)
                  .arg(row.ke1, 9)
                  .arg(0, 9)
                  .arg(row.jd1, 9)
                  .arg(row.jd2, 9)
                  .arg(0, 9);
    }
    return true;
}

void GeoStressSideFacePage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
}

PickMode GeoStressSideFacePage::currentPickMode() const
{
    return m_pickModeCombo->currentData().toString() == "face" ? PickMode::Face : PickMode::Edge;
}
