#include "GeoStressOuterForcePage.h"

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
    int jd1 = 0;
    int jd2 = 0;
};

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

        auto appendRow = [&](vtkIdType a, vtkIdType b)
        {
            EdgeRow r;
            r.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
            r.jd1 = int(nodeIdArr->GetTuple1(a));
            r.jd2 = int(nodeIdArr->GetTuple1(b));
            rows.push_back(r);
        };

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
                appendRow(ids->GetId(i), ids->GetId((i + 1) % ids->GetNumberOfIds()));
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
            appendRow(a, b);
        }
    }
    return rows;
}
} // namespace

GeoStressOuterForcePage::GeoStressOuterForcePage(QWidget* parent)
    : BasePage("GeoStressRegressionOuterForce")
{
    Q_UNUSED(parent);
    auto* form = new QFormLayout(this);
    m_roofFileEdit = new QLineEdit("FAROOF.DAT", this);
    m_roofFileEdit->setReadOnly(true);
    m_forceFileEdit = new QLineEdit("FTLOAD.DAT", this);
    m_forceFileEdit->setReadOnly(true);
    m_pickModeCombo = new QComboBox(this);
    m_pickModeCombo->addItem("2D选线", "edge");
    m_pickModeCombo->addItem("3D选面", "face");
    m_clearBtn = new QPushButton("清空所选", this);
    m_forceEnabledCombo = new QComboBox(this);
    m_forceEnabledCombo->addItem("0 - 不考虑", 0);
    m_forceEnabledCombo->addItem("1 - 考虑", 1);
    m_fxEdit = new QLineEdit("0", this);
    m_fyEdit = new QLineEdit("0", this);
    m_fzEdit = new QLineEdit("0", this);

    form->addRow("FAROOF文件", m_roofFileEdit);
    form->addRow("外面力文件", m_forceFileEdit);
    form->addRow("拾取模式", m_pickModeCombo);
    form->addRow("", m_clearBtn);
    form->addRow("是否考虑外面力", m_forceEnabledCombo);
    form->addRow("FX", m_fxEdit);
    form->addRow("FY", m_fyEdit);
    form->addRow("FZ", m_fzEdit);

    connect(m_clearBtn, &QPushButton::clicked, this, &GeoStressOuterForcePage::onClearClicked);
}

void GeoStressOuterForcePage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
}

void GeoStressOuterForcePage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QJsonObject GeoStressOuterForcePage::saveJson()
{
    QJsonObject obj;
    obj["pick_mode"] = m_pickModeCombo->currentData().toString();
    obj["force_enabled"] = m_forceEnabledCombo->currentData().toInt();
    obj["fx"] = m_fxEdit->text().trimmed();
    obj["fy"] = m_fyEdit->text().trimmed();
    obj["fz"] = m_fzEdit->text().trimmed();

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

void GeoStressOuterForcePage::loadJson(const QJsonObject& obj)
{
    const int modeIdx = m_pickModeCombo->findData(obj.value("pick_mode").toString("edge"));
    if (modeIdx >= 0)
        m_pickModeCombo->setCurrentIndex(modeIdx);

    const int enabled = obj.value("force_enabled").toInt(m_forceEnabledCombo->currentData().toInt());
    const int idx = m_forceEnabledCombo->findData(enabled);
    if (idx >= 0)
        m_forceEnabledCombo->setCurrentIndex(idx);
    m_fxEdit->setText(obj.value("fx").toString(m_fxEdit->text()));
    m_fyEdit->setText(obj.value("fy").toString(m_fyEdit->text()));
    m_fzEdit->setText(obj.value("fz").toString(m_fzEdit->text()));

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

QString GeoStressOuterForcePage::forceFile() const
{
    return m_forceFileEdit->text().trimmed();
}

int GeoStressOuterForcePage::forceEnabled() const
{
    return m_forceEnabledCombo->currentData().toInt();
}

bool GeoStressOuterForcePage::exportDatFiles() const
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

    QFile roofFile(wd.filePath("PREP/" + m_roofFileEdit->text().trimmed()));
    if (!roofFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream roofOs(&roofFile);
    roofOs << QString("%1        4     NJX   MAKOD\n").arg(rows.size());
    roofOs << "    KJX     KE1     KE2     JD1     JD2  JAT\n";
    for (int i = 0; i < rows.size(); ++i)
    {
        const EdgeRow& row = rows[i];
        roofOs << QString("%1 %2 %3 %4 %5 %6\n")
                      .arg(i + 1, 9)
                      .arg(row.ke1, 9)
                      .arg(-1, 9)
                      .arg(row.jd1, 9)
                      .arg(row.jd2, 9)
                      .arg(3, 9);
    }

    const QString forceName = forceFile();
    QFile forceDat(wd.filePath("PREP/" + forceName));
    if (!forceDat.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream forceOs(&forceDat);
    forceOs << QString("%1       0.000       0.000   0.000000E+00  %2\n")
                   .arg(rows.size())
                   .arg(forceName);
    for (const auto& row : rows)
    {
        forceOs << QString("%1     2     1  KE  NFAJD  NFT\n").arg(row.ke1, 9);
        forceOs << QString("%1 %2 %3 %4    P=\n")
                       .arg(row.jd1, 9)
                       .arg(m_fxEdit->text().trimmed(), 14)
                       .arg(m_fyEdit->text().trimmed(), 14)
                       .arg(m_fzEdit->text().trimmed(), 14);
        forceOs << QString("%1 %2 %3 %4    P=\n")
                       .arg(row.jd2, 9)
                       .arg(m_fxEdit->text().trimmed(), 14)
                       .arg(m_fyEdit->text().trimmed(), 14)
                       .arg(m_fzEdit->text().trimmed(), 14);
    }

    return true;
}

void GeoStressOuterForcePage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
}

PickMode GeoStressOuterForcePage::currentPickMode() const
{
    return m_pickModeCombo->currentData().toString() == "face" ? PickMode::Face : PickMode::Edge;
}
