#include "GeoStressOuterForcePage.h"

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
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLocale>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTextStream>

#include "ui_GeoStressOuterForcePage.h"

#include "SolverUiLayoutHelper.h"
namespace
{
const QString kRoofFileName = "FAROOF.DAT";
const QString kForceFileName = "FTLOAD.DAT";

struct EdgeRow
{
    int ke1 = 0;
    int jd1 = 0;
    int jd2 = 0;
    int jd3 = 0;
    int jd4 = 0;
    int nodeCount = 0;
};

QVector<vtkIdType> toVtkKeys(const QVector<MeshIdType>& ids)
{
    QVector<vtkIdType> out;
    out.reserve(ids.size());
    for (MeshIdType id : ids)
        out.push_back(static_cast<vtkIdType>(id));
    return out;
}

MeshTagKey comboTagKey(QComboBox* combo, int row)
{
    if (!combo || row < 0)
        return {};
    return combo->itemData(row).value<MeshTagKey>();
}

QString boundaryDisplayText(MeshModuleControllerService* meshSvc, const MeshTagSet& set)
{
    QString text = meshSvc ? meshSvc->displayTextOfTag(set.key) : set.name.trimmed();
    if (text.trimmed().isEmpty())
        text = QStringLiteral("边界%1").arg(set.key.id);
    return text;
}

MeshTagSet findBoundarySet(MeshModuleControllerService* meshSvc, const MeshTagKey& key)
{
    if (!meshSvc || !key.isValid())
        return {};
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(key.dim);
    for (const MeshTagSet& set : sets)
        if (set.key == key)
            return set;
    return {};
}

bool readForceComponents(const Ui::GeoStressOuterForcePage* ui, double& fx, double& fy,
                         double& fz)
{
    if (!ui || !ui->fxEdit || !ui->fyEdit || !ui->fzEdit)
        return false;

    bool okX = false;
    bool okY = false;
    bool okZ = false;
    fx = QLocale::c().toDouble(ui->fxEdit->text().trimmed(), &okX);
    fy = QLocale::c().toDouble(ui->fyEdit->text().trimmed(), &okY);
    fz = QLocale::c().toDouble(ui->fzEdit->text().trimmed(), &okZ);
    return okX && okY && okZ;
}

bool hasNonZeroForce(double fx, double fy, double fz)
{
    constexpr double kForceTolerance = 1.0e-20;
    return std::abs(fx) > kForceTolerance || std::abs(fy) > kForceTolerance
           || std::abs(fz) > kForceTolerance;
}

QString formatScientific(double value)
{
    return QString::asprintf("%.6E", value);
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
        const vtkIdType cellId =
            (mode == PickMode::Face) ? pm.unpackFaceCellId(key) : pm.unpackEdgeCellId(key);
        const int localId =
            (mode == PickMode::Face) ? pm.unpackFaceLocalId(key) : pm.unpackEdgeLocalId(key);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        auto appendEdgeRow = [&](vtkIdType a, vtkIdType b)
        {
            EdgeRow r;
            r.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
            r.jd1 = int(nodeIdArr->GetTuple1(a));
            r.jd2 = int(nodeIdArr->GetTuple1(b));
            r.nodeCount = 2;
            rows.push_back(r);
        };

        auto appendFaceRow = [&](vtkIdList* ids)
        {
            EdgeRow r;
            r.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
            r.nodeCount = std::min<int>(ids ? ids->GetNumberOfIds() : 0, 4);
            if (r.nodeCount >= 1)
                r.jd1 = int(nodeIdArr->GetTuple1(ids->GetId(0)));
            if (r.nodeCount >= 2)
                r.jd2 = int(nodeIdArr->GetTuple1(ids->GetId(1)));
            if (r.nodeCount >= 3)
                r.jd3 = int(nodeIdArr->GetTuple1(ids->GetId(2)));
            if (r.nodeCount >= 4)
                r.jd4 = int(nodeIdArr->GetTuple1(ids->GetId(3)));
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
            if (!ids || ids->GetNumberOfIds() < 3)
                continue;
            appendFaceRow(ids);
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
            appendEdgeRow(a, b);
        }
    }
    return rows;
}
} // namespace

GeoStressOuterForcePage::GeoStressOuterForcePage(QWidget* parent)
    : BasePage("GeoStressRegressionOuterForce"), ui(new Ui::GeoStressOuterForcePage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    connect(ui->clearButton, &QPushButton::clicked, this, &GeoStressOuterForcePage::onClearClicked);
    connect(ui->batchAddButton, &QPushButton::clicked, this,
            &GeoStressOuterForcePage::onBatchAddClicked);
    connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
            });
    connect(ui->groupCombo, &CustomComboBox::itemHovered, this,
            &GeoStressOuterForcePage::onGroupHovered);
    connect(ui->groupCombo, &CustomComboBox::dropdownClosed, this,
            [this]()
            {
                PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                              currentPickMode());
            });
}

GeoStressOuterForcePage::~GeoStressOuterForcePage()
{
    delete ui;
}

void GeoStressOuterForcePage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
    if (ui->pickEnableCheckBox)
        ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    refreshGroupCombo();
}

void GeoStressOuterForcePage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QJsonObject GeoStressOuterForcePage::saveJson()
{
    QJsonObject obj;
    obj["pick_mode"] = currentPickMode() == PickMode::Face ? "face" : "edge";
    obj["fx"] = ui->fxEdit->text().trimmed();
    obj["fy"] = ui->fyEdit->text().trimmed();
    obj["fz"] = ui->fzEdit->text().trimmed();

    auto toJson = [](const QVector<vtkIdType>& ids)
    {
        QJsonArray arr;
        for (vtkIdType id : ids)
            arr.append(qint64(id));
        return arr;
    };
    obj["picked_edges"] = toJson(
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Edge, GridKind::Solid));
    obj["picked_faces"] = toJson(
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid));

    return obj;
}

void GeoStressOuterForcePage::loadJson(const QJsonObject& obj)
{
    ui->fxEdit->setText(obj.value("fx").toString(ui->fxEdit->text()));
    ui->fyEdit->setText(obj.value("fy").toString(ui->fyEdit->text()));
    ui->fzEdit->setText(obj.value("fz").toString(ui->fzEdit->text()));

    auto fromJson = [](const QJsonArray& arr)
    {
        QVector<vtkIdType> ids;
        ids.reserve(arr.size());
        for (const auto& v : arr)
            ids.push_back(v.toVariant().toLongLong());
        return ids;
    };

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("picked_edges").toArray()),
                                                true);
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("picked_faces").toArray()),
                                                true);
}

QString GeoStressOuterForcePage::forceFile() const
{
    return kForceFileName;
}

int GeoStressOuterForcePage::forceEnabled() const
{
    double fx = 0.0;
    double fy = 0.0;
    double fz = 0.0;
    if (!readForceComponents(ui, fx, fy, fz) || !hasNonZeroForce(fx, fy, fz))
        return 0;

    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return 0;

    const QVector<EdgeRow> rows =
        collectRows(grid, QStringLiteral("GeoStressRegressionOuterForce"), m_uuid,
                    currentPickMode());
    return rows.isEmpty() ? 0 : 1;
}

bool GeoStressOuterForcePage::exportDatFiles() const
{
    double fx = 0.0;
    double fy = 0.0;
    double fz = 0.0;
    if (!readForceComponents(ui, fx, fy, fz))
        return false;

    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return false;

    const QVector<EdgeRow> rows =
        collectRows(grid, QStringLiteral("GeoStressRegressionOuterForce"), m_uuid,
                    currentPickMode());

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile roofFile(inputDir.filePath(kRoofFileName));
    if (!roofFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream roofOs(&roofFile);
    const bool isFaceMode = (currentPickMode() == PickMode::Face);
    if (isFaceMode)
    {
        roofOs << QString("%1        6     NJX   MAKOD\n").arg(rows.size());
        roofOs << "    KJX     KE1     KE2     JD1     JD2     JD3     JD4  JAT\n";
    }
    else
    {
        roofOs << QString("%1        4     NJX   MAKOD\n").arg(rows.size());
        roofOs << "    KJX     KE1     KE2     JD1     JD2  JAT\n";
    }

    for (int i = 0; i < rows.size(); ++i)
    {
        const EdgeRow& row = rows[i];
        if (isFaceMode)
        {
            roofOs << QString("%1 %2 %3 %4 %5 %6 %7 %8\n")
                          .arg(i + 1, 9)
                          .arg(row.ke1, 9)
                          .arg(-1, 9)
                          .arg(row.jd1, 9)
                          .arg(row.jd2, 9)
                          .arg(row.jd3, 9)
                          .arg(row.jd4, 9)
                          .arg(3, 9);
        }
        else
        {
            roofOs << QString("%1 %2 %3 %4 %5 %6\n")
                          .arg(i + 1, 9)
                          .arg(row.ke1, 9)
                          .arg(-1, 9)
                          .arg(row.jd1, 9)
                          .arg(row.jd2, 9)
                          .arg(3, 9);
        }
    }

    const QString forceName = forceFile();
    QFile forceDat(inputDir.filePath(forceName));
    if (!forceDat.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream forceOs(&forceDat);
    forceOs << QString("%1       0.000       0.000   0.000000E+00  %2\n")
                   .arg(rows.size())
                   .arg(forceName);

    const QString fxText = formatScientific(fx);
    const QString fyText = formatScientific(fy);
    const QString fzText = formatScientific(fz);
    const QString totalText = formatScientific(std::sqrt(fx * fx + fy * fy + fz * fz));

    for (int faceIndex = 0; faceIndex < rows.size(); ++faceIndex)
    {
        const EdgeRow& row = rows.at(faceIndex);
        const int nfaJd = isFaceMode ? row.nodeCount : 2;
        const int nft = faceIndex + 1;

        // READ_IIWL 实际读取前三个整数：KE、NFT+NSHI、NFAJD。
        // 第四个 NFT 与旧求解器自身输出格式保持一致，读取时会被忽略。
        forceOs << QString("%1 %2 %3 %4  KE  NFT+NSHI  NFAJD  NFT\n")
                       .arg(row.ke1, 9)
                       .arg(nft, 9)
                       .arg(nfaJd, 9)
                       .arg(nft, 9);

        const int nodeIds[] = {row.jd1, row.jd2, row.jd3, row.jd4};
        for (int nodeIndex = 0; nodeIndex < nfaJd; ++nodeIndex)
        {
            forceOs << QString("%1  %2  %3  %4  P=%5\n")
                           .arg(nodeIds[nodeIndex], 9)
                           .arg(fxText, 14)
                           .arg(fyText, 14)
                           .arg(fzText, 14)
                           .arg(totalText, 14);
        }
    }

    return true;
}

void GeoStressOuterForcePage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
}

void GeoStressOuterForcePage::onBatchAddClicked()
{
    if (!ui->groupCombo || ui->groupCombo->currentIndex() < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MeshTagSet set =
        findBoundarySet(meshSvc, comboTagKey(ui->groupCombo, ui->groupCombo->currentIndex()));
    if (currentPickMode() == PickMode::Face)
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    toVtkKeys(set.faceKeys), false);
    else
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    toVtkKeys(set.edgeKeys), false);
}

void GeoStressOuterForcePage::onGroupHovered(int row)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || row < 0)
        return;
    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->groupCombo, row));
    if (currentPickMode() == PickMode::Face)
        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                   toVtkKeys(set.faceKeys));
    else
        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                   toVtkKeys(set.edgeKeys));
}

PickMode GeoStressOuterForcePage::currentPickMode() const
{
    const int ndim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return ndim == 2 ? PickMode::Edge : PickMode::Face;
}

void GeoStressOuterForcePage::refreshGroupCombo()
{
    const bool selectFaces = currentPickMode() == PickMode::Face;
    ui->selectionGroup->setTitle(selectFaces ? QStringLiteral("网格面选择")
                                              : QStringLiteral("网格边选择"));
    ui->pickHintLabel->setText(
        selectFaces
            ? QStringLiteral("选择对象：三维网格面。Shift+拖拽框选，Ctrl+拖拽取消选择。")
            : QStringLiteral("选择对象：二维网格边。Shift+拖拽框选，Ctrl+拖拽取消选择。"));
    ui->pickEnableCheckBox->setText(selectFaces ? QStringLiteral("开启面点选")
                                                 : QStringLiteral("开启边点选"));

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || !ui->groupCombo)
        return;

    const int wantDim = currentPickMode() == PickMode::Face ? 2 : 1;
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(wantDim);
    ui->groupCombo->blockSignals(true);
    ui->groupCombo->clear();
    for (const MeshTagSet& set : sets)
    {
        if (wantDim == 1 && set.edgeKeys.isEmpty())
            continue;
        if (wantDim == 2 && set.faceKeys.isEmpty())
            continue;
        ui->groupCombo->addItem(boundaryDisplayText(meshSvc, set), QVariant::fromValue(set.key));
    }
    ui->groupCombo->blockSignals(false);
}
