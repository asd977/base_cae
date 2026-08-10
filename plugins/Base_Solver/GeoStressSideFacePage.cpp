#include "GeoStressSideFacePage.h"

#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextStream>
#include <QTimer>

#include "ui_GeoStressSideFacePage.h"

#include "SolverUiLayoutHelper.h"
namespace
{
struct EdgeRow
{
    int ke1 = 0;
    int ke2 = 0;
    int jd1 = 0;
    int jd2 = 0;
    int jd3 = 0;
    int jd4 = 0;
};

using PreviewPoint = std::array<double, 3>;

PreviewPoint interpolateAtLevel(const PreviewPoint& from, const PreviewPoint& to,
                                int verticalAxis, double height);

struct BoundaryPreview
{
    QVector<PreviewPoint> points;
    PreviewPoint displayNormal{0.0, 0.0, 0.0};
};

void resetBounds(double bounds[6])
{
    bounds[0] = bounds[2] = bounds[4] = std::numeric_limits<double>::max();
    bounds[1] = bounds[3] = bounds[5] = std::numeric_limits<double>::lowest();
}

void expandBounds(double bounds[6], const PreviewPoint& point)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        bounds[2 * axis] = std::min(bounds[2 * axis], point[axis]);
        bounds[2 * axis + 1] = std::max(bounds[2 * axis + 1], point[axis]);
    }
}

bool validBounds(const double bounds[6])
{
    return bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5];
}

bool nearlySamePoint(const PreviewPoint& lhs, const PreviewPoint& rhs, double tolerance)
{
    for (int axis = 0; axis < 3; ++axis)
        if (std::abs(lhs[axis] - rhs[axis]) > tolerance)
            return false;
    return true;
}

bool normalizeVector(PreviewPoint& vector)
{
    const double length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1]
                                    + vector[2] * vector[2]);
    if (!(length > 1.0e-12))
        return false;
    for (double& component : vector)
        component /= length;
    return true;
}

PreviewPoint polygonCenter(const QVector<PreviewPoint>& points)
{
    PreviewPoint center{0.0, 0.0, 0.0};
    if (points.isEmpty())
        return center;
    for (const PreviewPoint& point : points)
        for (int axis = 0; axis < 3; ++axis)
            center[axis] += point[axis];
    for (double& component : center)
        component /= points.size();
    return center;
}

PreviewPoint outwardPolygonNormal(const QVector<PreviewPoint>& points,
                                  const PreviewPoint& gridCenter)
{
    PreviewPoint normal{0.0, 0.0, 0.0};
    if (points.size() < 3)
        return normal;

    const PreviewPoint firstEdge{points[1][0] - points[0][0],
                                 points[1][1] - points[0][1],
                                 points[1][2] - points[0][2]};
    const PreviewPoint secondEdge{points[2][0] - points[0][0],
                                  points[2][1] - points[0][1],
                                  points[2][2] - points[0][2]};
    normal = {firstEdge[1] * secondEdge[2] - firstEdge[2] * secondEdge[1],
              firstEdge[2] * secondEdge[0] - firstEdge[0] * secondEdge[2],
              firstEdge[0] * secondEdge[1] - firstEdge[1] * secondEdge[0]};
    if (!normalizeVector(normal))
        return {0.0, 0.0, 0.0};

    const PreviewPoint center = polygonCenter(points);
    const PreviewPoint outward{center[0] - gridCenter[0], center[1] - gridCenter[1],
                               center[2] - gridCenter[2]};
    const double orientation = normal[0] * outward[0] + normal[1] * outward[1]
                               + normal[2] * outward[2];
    if (orientation < 0.0)
        for (double& component : normal)
            component = -component;
    return normal;
}

PreviewPoint offsetPoint(const PreviewPoint& point, const PreviewPoint& normal, double distance)
{
    return {point[0] + normal[0] * distance, point[1] + normal[1] * distance,
            point[2] + normal[2] * distance};
}

QVector<PreviewPoint> polygonLevelIntersections(const QVector<PreviewPoint>& points,
                                                int verticalAxis, double height,
                                                double tolerance)
{
    QVector<PreviewPoint> intersections;
    if (points.size() < 2)
        return intersections;

    auto appendUnique = [&intersections, tolerance](const PreviewPoint& point)
    {
        for (const PreviewPoint& existing : std::as_const(intersections))
            if (nearlySamePoint(existing, point, tolerance))
                return;
        intersections.push_back(point);
    };

    for (int index = 0; index < points.size(); ++index)
    {
        const PreviewPoint& first = points[index];
        const PreviewPoint& second = points[(index + 1) % points.size()];
        const double firstOffset = first[verticalAxis] - height;
        const double secondOffset = second[verticalAxis] - height;
        const bool firstOnLevel = std::abs(firstOffset) <= tolerance;
        const bool secondOnLevel = std::abs(secondOffset) <= tolerance;

        if (firstOnLevel)
            appendUnique(first);
        if (secondOnLevel)
            appendUnique(second);
        if ((firstOffset < -tolerance && secondOffset > tolerance)
            || (firstOffset > tolerance && secondOffset < -tolerance))
        {
            appendUnique(interpolateAtLevel(first, second, verticalAxis, height));
        }
    }
    return intersections;
}

double pressureAt(const PreviewPoint& point, int verticalAxis, double height, double gama,
                  double pgrout)
{
    return std::max(height - point[verticalAxis], 0.0) * gama * 9.81 + pgrout;
}

QString numberText(double value)
{
    return QString::number(value, 'g', 15);
}

double jsonNumber(const QJsonValue& value, double fallback)
{
    if (value.isDouble())
        return value.toDouble(fallback);
    bool ok = false;
    const double parsed = value.toString().toDouble(&ok);
    return ok ? parsed : fallback;
}

PreviewPoint interpolateAtLevel(const PreviewPoint& from, const PreviewPoint& to,
                                int verticalAxis, double height)
{
    const double denominator = to[verticalAxis] - from[verticalAxis];
    double ratio = 0.0;
    if (std::abs(denominator) > 1.0e-12)
        ratio = (height - from[verticalAxis]) / denominator;
    ratio = std::clamp(ratio, 0.0, 1.0);

    PreviewPoint point{};
    for (int axis = 0; axis < 3; ++axis)
        point[axis] = from[axis] + ratio * (to[axis] - from[axis]);
    point[verticalAxis] = height;
    return point;
}

QVector<PreviewPoint> clipLineBelow(const QVector<PreviewPoint>& points, int verticalAxis,
                                    double height)
{
    if (points.size() < 2)
        return {};
    const bool firstInside = points[0][verticalAxis] <= height;
    const bool secondInside = points[1][verticalAxis] <= height;
    if (firstInside && secondInside)
        return {points[0], points[1]};
    if (!firstInside && !secondInside)
        return {};

    const PreviewPoint intersection =
        interpolateAtLevel(points[0], points[1], verticalAxis, height);
    return firstInside ? QVector<PreviewPoint>{points[0], intersection}
                       : QVector<PreviewPoint>{intersection, points[1]};
}

QVector<PreviewPoint> clipPolygonBelow(const QVector<PreviewPoint>& points, int verticalAxis,
                                       double height)
{
    QVector<PreviewPoint> clipped;
    if (points.size() < 3)
        return clipped;

    PreviewPoint previous = points.constLast();
    bool previousInside = previous[verticalAxis] <= height;
    for (const PreviewPoint& current : points)
    {
        const bool currentInside = current[verticalAxis] <= height;
        if (currentInside != previousInside)
            clipped.push_back(interpolateAtLevel(previous, current, verticalAxis, height));
        if (currentInside)
            clipped.push_back(current);
        previous = current;
        previousInside = currentInside;
    }
    return clipped;
}

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

EdgeRow makeEdgeRow(vtkDataArray* elemIdArr, vtkDataArray* nodeIdArr, vtkIdType cellId,
                    vtkIdType pidA, vtkIdType pidB)
{
    EdgeRow row;
    row.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
    row.ke2 = 0;
    row.jd1 = int(nodeIdArr->GetTuple1(pidA));
    row.jd2 = int(nodeIdArr->GetTuple1(pidB));
    return row;
}

EdgeRow makeFaceRow(vtkDataArray* elemIdArr, vtkDataArray* nodeIdArr, vtkIdType cellId,
                    vtkIdList* ids)
{
    EdgeRow row;
    row.ke1 = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
    row.ke2 = 0;

    const int cornerCount = std::min<int>(ids ? ids->GetNumberOfIds() : 0, 4);
    if (cornerCount >= 1)
        row.jd1 = int(nodeIdArr->GetTuple1(ids->GetId(0)));
    if (cornerCount >= 2)
        row.jd2 = int(nodeIdArr->GetTuple1(ids->GetId(1)));
    if (cornerCount >= 3)
        row.jd3 = int(nodeIdArr->GetTuple1(ids->GetId(2)));
    if (cornerCount >= 4)
        row.jd4 = int(nodeIdArr->GetTuple1(ids->GetId(3)));
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
        const vtkIdType cellId =
            (mode == PickMode::Face) ? pm.unpackFaceCellId(key) : pm.unpackEdgeCellId(key);
        const int localId =
            (mode == PickMode::Face) ? pm.unpackFaceLocalId(key) : pm.unpackEdgeLocalId(key);
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
            if (!ids || ids->GetNumberOfIds() < 3)
                continue;
            rows.push_back(makeFaceRow(elemIdArr, nodeIdArr, cellId, ids));
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
            rows.push_back(makeEdgeRow(elemIdArr, nodeIdArr, cellId, a, b));
        }
    }
    return rows;
}
} // namespace

GeoStressSideFacePage::GeoStressSideFacePage(QWidget* parent)
    : BasePage("GeoStressRegressionSideFace"), ui(new Ui::GeoStressSideFacePage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    ui->idirCombo->addItem("X 方向", "1");
    ui->idirCombo->addItem("Y 方向", "2");
    ui->idirCombo->addItem("Z 方向", "3");

    connect(ui->clearButton, &QPushButton::clicked, this, &GeoStressSideFacePage::onClearClicked);
    connect(ui->batchAddButton, &QPushButton::clicked, this, &GeoStressSideFacePage::onBatchAddClicked);
    connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                PickManager::instance().setClickPickEnabled(pickDomain(), m_uuid, on);
            });
    connect(ui->groupCombo, &CustomComboBox::itemHovered, this,
            &GeoStressSideFacePage::onGroupHovered);
    connect(ui->groupCombo, &CustomComboBox::dropdownClosed, this,
            [this]()
            {
                PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid,
                                                              currentPickMode());
            });

    m_pressurePreviewTimer = new QTimer(this);
    m_pressurePreviewTimer->setSingleShot(true);
    m_pressurePreviewTimer->setInterval(80);
    connect(m_pressurePreviewTimer, &QTimer::timeout, this,
            &GeoStressSideFacePage::updatePressurePreview);
    connect(ui->heightSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double)
            {
                syncHeightSliderFromSpinBox();
                schedulePressurePreview();
            });
    connect(ui->heightSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (m_updatingHeightSlider)
                    return;
                const double ratio = value / 1000.0;
                const double height = m_heightSliderMinimum
                                      + ratio * (m_heightSliderMaximum - m_heightSliderMinimum);
                m_updatingHeightSlider = true;
                ui->heightSpinBox->setValue(height);
                m_updatingHeightSlider = false;
            });
    connect(ui->gamaSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { schedulePressurePreview(); });
    connect(ui->pgroutSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { schedulePressurePreview(); });
    connect(ui->showPreviewCheckBox, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                if (!m_pressurePreviewActive)
                    return;
                if (checked)
                {
                    setPreviewStatus(QStringLiteral("正在更新侧压预览……"), false);
                    schedulePressurePreview();
                }
                else
                {
                    if (m_pressurePreviewTimer)
                        m_pressurePreviewTimer->stop();
                    clearPressurePreview(false);
                    restoreSelectedBoundaryHighlight();
                    setPreviewStatus(QStringLiteral("侧压预览已关闭；已恢复所选侧立面的高亮。"),
                                     false);
                    if (auto* visual =
                            ServiceRegistry::instance().getService<VisualWidgetService>())
                        visual->render();
                }
            });
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind kind)
            {
                if (kind == GridKind::Solid && domain == pickDomain() && uuid == m_uuid
                    && mode == currentPickMode())
                {
                    updateHeightSliderRange();
                    schedulePressurePreview();
                }
            });
}

GeoStressSideFacePage::~GeoStressSideFacePage()
{
    m_pressurePreviewActive = false;
    clearPressurePreview(false);
    delete ui;
}

QString GeoStressSideFacePage::pickDomain() const
{
    return QStringLiteral("GeoStressRegressionSideFace/%1").arg(m_uuid);
}


void GeoStressSideFacePage::beforeLoad()
{
    m_pressurePreviewActive = true;

    // updateStructure() 会恢复原始网格着色并清空面拾取索引。
    // 必须先更新网格引用，再 activate() 重建当前面拾取索引和选面高亮。
    PickManager& picker = PickManager::instance();
    picker.updateStructure();
    picker.activate(pickDomain(), m_uuid, currentPickMode());
    if (ui->pickEnableCheckBox)
        ui->pickEnableCheckBox->setChecked(false);
    picker.setClickPickEnabled(pickDomain(), m_uuid, false);
    refreshGroupCombo();
    updateHeightSliderRange();
    restoreSelectedBoundaryHighlight();
    schedulePressurePreview();
}

void GeoStressSideFacePage::beforeUnload()
{
    m_pressurePreviewActive = false;
    if (m_pressurePreviewTimer)
        m_pressurePreviewTimer->stop();
    clearPressurePreview();
    PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid, currentPickMode());
    PickManager::instance().setClickPickEnabled(pickDomain(), m_uuid, true);
    PickManager::instance().deactivate(pickDomain(), m_uuid, currentPickMode());
}

QJsonObject GeoStressSideFacePage::saveJson()
{
    QJsonObject obj;
    obj["face_index"] = m_faceIndex;
    obj["pick_mode"] = currentPickMode() == PickMode::Face ? "face" : "edge";
    obj["idir"] = ui->idirCombo->currentData().toString();
    obj["height"] = numberText(ui->heightSpinBox->value());
    obj["gama"] = numberText(ui->gamaSpinBox->value());
    obj["pgrout"] = numberText(ui->pgroutSpinBox->value());
    obj["show_preview"] = ui->showPreviewCheckBox->isChecked();

    auto toJson = [](const QVector<vtkIdType>& ids)
    {
        QJsonArray arr;
        for (vtkIdType id : ids)
            arr.append(qint64(id));
        return arr;
    };

    obj["picked_edges"] = toJson(
        PickManager::instance().getIdList(pickDomain(), m_uuid, PickMode::Edge, GridKind::Solid));
    obj["picked_faces"] = toJson(
        PickManager::instance().getIdList(pickDomain(), m_uuid, PickMode::Face, GridKind::Solid));
    return obj;
}

void GeoStressSideFacePage::loadJson(const QJsonObject& obj)
{
    setFaceIndex(obj.value("face_index").toInt(m_faceIndex));
    {
        const int index = ui->idirCombo->findData(obj.value("idir").toString("1"));
        ui->idirCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    ui->heightSpinBox->setValue(
        jsonNumber(obj.value("height"), ui->heightSpinBox->value()));
    ui->gamaSpinBox->setValue(jsonNumber(obj.value("gama"), ui->gamaSpinBox->value()));
    ui->pgroutSpinBox->setValue(
        jsonNumber(obj.value("pgrout"), ui->pgroutSpinBox->value()));
    ui->showPreviewCheckBox->setChecked(obj.value("show_preview").toBool(true));

    auto fromJson = [](const QJsonArray& arr)
    {
        QVector<vtkIdType> ids;
        ids.reserve(arr.size());
        for (const auto& v : arr)
            ids.push_back(v.toVariant().toLongLong());
        return ids;
    };

    PickManager::instance().setSelectedEdgeKeys(pickDomain(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("picked_edges").toArray()),
                                                true);
    PickManager::instance().setSelectedFaceKeys(pickDomain(), m_uuid, GridKind::Solid,
                                                fromJson(obj.value("picked_faces").toArray()),
                                                true);
    updateHeightSliderRange();
}

QString GeoStressSideFacePage::faceFile() const
{
    return QString("FA_SIDE%1.DAT").arg(m_faceIndex);
}

GeoStressSideFacePage::FaceAttr GeoStressSideFacePage::attr() const
{
    FaceAttr a;
    a.idir = ui->idirCombo->currentData().toString();
    a.height = numberText(ui->heightSpinBox->value());
    a.gama = numberText(ui->gamaSpinBox->value());
    a.pgrout = numberText(ui->pgroutSpinBox->value());
    a.ii = QString::number(m_faceIndex);
    return a;
}

void GeoStressSideFacePage::setFaceIndex(int index)
{
    if (index < 1)
        index = 1;
    if (index > 4)
        index = 4;
    m_faceIndex = index;
}

int GeoStressSideFacePage::selectionCount() const
{
    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return 0;
    return collectRows(grid, pickDomain(), m_uuid,
                       currentPickMode())
        .size();
}

bool GeoStressSideFacePage::validatePressure(int verticalAxis, QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    const double height = ui->heightSpinBox->value();
    const double gama = ui->gamaSpinBox->value();
    const double pgrout = ui->pgroutSpinBox->value();
    if (!std::isfinite(height) || !std::isfinite(gama) || !std::isfinite(pgrout))
    {
        return fail(QStringLiteral("侧立面%1的侧压参数不是有效数值。").arg(m_faceIndex));
    }
    if (verticalAxis < 1 || verticalAxis > 3)
        return fail(QStringLiteral("无法确定侧立面%1的竖向坐标轴。").arg(m_faceIndex));

    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return fail(QStringLiteral("无法读取侧立面%1的网格数据。").arg(m_faceIndex));

    const QVector<EdgeRow> rows = collectRows(
        grid, pickDomain(), m_uuid, currentPickMode());
    vtkDataArray* nodeIdArr =
        vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    if (rows.isEmpty() || !nodeIdArr)
        return fail(QStringLiteral("侧立面%1尚未选择有效网格边界。").arg(m_faceIndex));

    QHash<qint64, vtkIdType> pointByNodeId;
    pointByNodeId.reserve(int(grid->GetNumberOfPoints()));
    for (vtkIdType pointId = 0; pointId < grid->GetNumberOfPoints(); ++pointId)
        pointByNodeId.insert(qRound64(nodeIdArr->GetTuple1(pointId)), pointId);

    const int axis = verticalAxis - 1;
    double minimumCoordinate = std::numeric_limits<double>::max();
    double maximumPressure = 0.0;
    QSet<qint64> visitedNodeIds;
    for (const EdgeRow& row : rows)
    {
        const int nodeIds[] = {row.jd1, row.jd2, row.jd3, row.jd4};
        for (int nodeId : nodeIds)
        {
            if (nodeId <= 0 || visitedNodeIds.contains(nodeId))
                continue;
            visitedNodeIds.insert(nodeId);
            const auto pointIt = pointByNodeId.constFind(nodeId);
            if (pointIt == pointByNodeId.cend())
                continue;

            double xyz[3] = {0.0, 0.0, 0.0};
            grid->GetPoint(pointIt.value(), xyz);
            minimumCoordinate = std::min(minimumCoordinate, xyz[axis]);
            const double pressure =
                std::max(height - xyz[axis], 0.0) * gama * 9.81 + pgrout;
            maximumPressure = std::max(maximumPressure, std::abs(pressure));
        }
    }

    if (minimumCoordinate == std::numeric_limits<double>::max())
        return fail(QStringLiteral("无法读取侧立面%1的节点坐标。").arg(m_faceIndex));
    if (maximumPressure <= 1.0e-9)
    {
        return fail(
            QStringLiteral("侧立面%1不产生侧压力：侧压高程为 %2，所选面最低竖向坐标为 %3，"
                           "测压容重为 %4，附加矩形常压为 %5。请调整侧压参数。")
                .arg(m_faceIndex)
                .arg(height, 0, 'g', 10)
                .arg(minimumCoordinate, 0, 'g', 10)
                .arg(gama, 0, 'g', 10)
                .arg(pgrout, 0, 'g', 10));
    }
    return true;
}

bool GeoStressSideFacePage::exportDatFile() const
{
    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return false;

    const QString domain = pickDomain();
    const QVector<EdgeRow> rows = collectRows(grid, domain, m_uuid, currentPickMode());

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (rows.isEmpty())
    {
        QFile::remove(inputDir.filePath(faceFile()));
        return false;
    }
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(faceFile()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const bool isFaceMode = (currentPickMode() == PickMode::Face);
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
                      .arg(row.ke2, 9)
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
                      .arg(0, 9);
        }
    }
    return true;
}

void GeoStressSideFacePage::onClearClicked()
{
    PickManager::instance().clearSelection(pickDomain(), m_uuid, currentPickMode());
    PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid, currentPickMode());
}

void GeoStressSideFacePage::onBatchAddClicked()
{
    if (!ui->groupCombo || ui->groupCombo->currentIndex() < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MeshTagSet set =
        findBoundarySet(meshSvc, comboTagKey(ui->groupCombo, ui->groupCombo->currentIndex()));
    if (currentPickMode() == PickMode::Face)
        PickManager::instance().setSelectedFaceKeys(pickDomain(), m_uuid, GridKind::Solid,
                                                    toVtkKeys(set.faceKeys), false);
    else
        PickManager::instance().setSelectedEdgeKeys(pickDomain(), m_uuid, GridKind::Solid,
                                                    toVtkKeys(set.edgeKeys), false);
}

void GeoStressSideFacePage::onGroupHovered(int row)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || row < 0)
        return;
    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->groupCombo, row));
    if (currentPickMode() == PickMode::Face)
        PickManager::instance().setPreviewFaceKeys(pickDomain(), m_uuid, GridKind::Solid,
                                                   toVtkKeys(set.faceKeys));
    else
        PickManager::instance().setPreviewEdgeKeys(pickDomain(), m_uuid, GridKind::Solid,
                                                   toVtkKeys(set.edgeKeys));
}

PickMode GeoStressSideFacePage::currentPickMode() const
{
    const int ndim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return ndim == 2 ? PickMode::Edge : PickMode::Face;
}

void GeoStressSideFacePage::refreshGroupCombo()
{
    const bool selectFaces = currentPickMode() == PickMode::Face;
    ui->selectionGroup->setTitle(selectFaces ? QStringLiteral("网格面选择")
                                              : QStringLiteral("网格边选择"));
    ui->pickHintLabel->setText(
        selectFaces
            ? QStringLiteral("提示：Shift 框选网格面，Ctrl 取消选择。")
            : QStringLiteral("提示：Shift 框选网格边，Ctrl 取消选择。"));
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

void GeoStressSideFacePage::schedulePressurePreview()
{
    if (m_pressurePreviewActive && ui->showPreviewCheckBox->isChecked()
        && m_pressurePreviewTimer)
        m_pressurePreviewTimer->start();
}

void GeoStressSideFacePage::clearPressurePreview(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    const QString prefix = QStringLiteral("geostress_side_pressure_%1_").arg(m_uuid);
    visual->removeProp(prefix + QStringLiteral("level"), false);
    visual->removeProp(prefix + QStringLiteral("level_label"), false);
    visual->removeProp(prefix + QStringLiteral("loaded"), false);
    visual->removeProp(prefix + QStringLiteral("legend"), false);
    visual->removeProp(prefix + QStringLiteral("arrows"), false);
    if (renderNow)
        visual->render();
}

void GeoStressSideFacePage::restoreSelectedBoundaryHighlight()
{
    PickManager& picker = PickManager::instance();
    const PickMode mode = currentPickMode();
    const QVector<vtkIdType> selectedKeys =
        picker.getIdList(pickDomain(), m_uuid, mode, GridKind::Solid);
    if (selectedKeys.isEmpty())
        return;

    // 自定义预览 Actor 被移除后，VTK 的选择 Actor 有时不会自动重建。
    // 先置空再恢复同一组 key，强制 PickManager 重建所选面的正常高亮。
    if (mode == PickMode::Face)
    {
        picker.setSelectedFaceKeys(pickDomain(), m_uuid, GridKind::Solid, {}, true);
        picker.setSelectedFaceKeys(pickDomain(), m_uuid, GridKind::Solid, selectedKeys, true);
    }
    else
    {
        picker.setSelectedEdgeKeys(pickDomain(), m_uuid, GridKind::Solid, {}, true);
        picker.setSelectedEdgeKeys(pickDomain(), m_uuid, GridKind::Solid, selectedKeys, true);
    }
}

void GeoStressSideFacePage::updateHeightSliderRange()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    // 页面激活时已经同步过 PickManager。这里不能再次调用 updateStructure()：
    // updateStructure() 会调用 restoreFaceColoring()，从而把当前选面高亮清掉。
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!meshService || !grid || grid->GetNumberOfPoints() <= 0)
    {
        ui->heightSlider->setEnabled(false);
        return;
    }

    int verticalAxis = meshService->verticalAxis() - 1;
    if (verticalAxis < 0 || verticalAxis > 2)
        verticalAxis = 1;

    double gridBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    grid->GetBounds(gridBounds);
    double minimum = gridBounds[2 * verticalAxis];
    double maximum = gridBounds[2 * verticalAxis + 1];

    double selectedMinimum = std::numeric_limits<double>::max();
    double selectedMaximum = std::numeric_limits<double>::lowest();
    const bool faceMode = currentPickMode() == PickMode::Face;
    const auto selectedKeys = PickManager::instance().getIdList(
        pickDomain(), m_uuid, currentPickMode(), GridKind::Solid);
    for (vtkIdType key : selectedKeys)
    {
        const vtkIdType cellId = faceMode ? PickManager::instance().unpackFaceCellId(key)
                                          : PickManager::instance().unpackEdgeCellId(key);
        const int localId = faceMode ? PickManager::instance().unpackFaceLocalId(key)
                                     : PickManager::instance().unpackEdgeLocalId(key);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        vtkIdList* ids = nullptr;
        if (faceMode)
        {
            if (cell->GetCellDimension() == 2)
                ids = cell->GetPointIds();
            else if (localId >= 0 && localId < cell->GetNumberOfFaces())
            {
                vtkCell* face = cell->GetFace(localId);
                ids = face ? face->GetPointIds() : nullptr;
            }
        }
        else if (cell->GetCellDimension() == 1 || localId == 15)
        {
            ids = cell->GetPointIds();
        }
        else if (localId >= 0 && localId < cell->GetNumberOfEdges())
        {
            vtkCell* edge = cell->GetEdge(localId);
            ids = edge ? edge->GetPointIds() : nullptr;
        }
        if (!ids)
            continue;

        for (vtkIdType pointIndex = 0; pointIndex < ids->GetNumberOfIds(); ++pointIndex)
        {
            double xyz[3] = {0.0, 0.0, 0.0};
            grid->GetPoint(ids->GetId(pointIndex), xyz);
            selectedMinimum = std::min(selectedMinimum, xyz[verticalAxis]);
            selectedMaximum = std::max(selectedMaximum, xyz[verticalAxis]);
        }
    }

    if (selectedMinimum <= selectedMaximum)
    {
        minimum = selectedMinimum;
        maximum = selectedMaximum;
    }

    const double gridSpan = std::max(gridBounds[2 * verticalAxis + 1]
                                         - gridBounds[2 * verticalAxis],
                                     1.0e-6);
    const double selectedSpan = std::max(maximum - minimum, 0.0);
    const double padding = std::max(selectedSpan * 0.08, gridSpan * 0.015);
    m_heightSliderMinimum = minimum - padding;
    m_heightSliderMaximum = maximum + padding;
    if (!(m_heightSliderMaximum > m_heightSliderMinimum))
        m_heightSliderMaximum = m_heightSliderMinimum + 1.0;

    ui->heightSlider->setEnabled(true);
    syncHeightSliderFromSpinBox();
}

void GeoStressSideFacePage::syncHeightSliderFromSpinBox()
{
    if (!ui->heightSlider || m_updatingHeightSlider
        || !(m_heightSliderMaximum > m_heightSliderMinimum))
        return;

    const double ratio = std::clamp(
        (ui->heightSpinBox->value() - m_heightSliderMinimum)
            / (m_heightSliderMaximum - m_heightSliderMinimum),
        0.0, 1.0);
    m_updatingHeightSlider = true;
    const QSignalBlocker blocker(ui->heightSlider);
    ui->heightSlider->setValue(qRound(ratio * 1000.0));
    m_updatingHeightSlider = false;
}

void GeoStressSideFacePage::setPreviewStatus(const QString& text, bool warning)
{
    if (!ui->previewStatusLabel)
        return;
    ui->previewStatusLabel->setText(warning ? QStringLiteral("提示：%1").arg(text) : text);
    QFont font = ui->previewStatusLabel->font();
    font.setBold(warning);
    ui->previewStatusLabel->setFont(font);
}

void GeoStressSideFacePage::updatePressurePreview()
{
    if (!m_pressurePreviewActive || !ui->showPreviewCheckBox->isChecked())
        return;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    clearPressurePreview(false);

    const double height = ui->heightSpinBox->value();
    const double gama = ui->gamaSpinBox->value();
    const double pgrout = ui->pgroutSpinBox->value();
    if (!std::isfinite(height) || !std::isfinite(gama) || !std::isfinite(pgrout))
    {
        setPreviewStatus(QStringLiteral("侧压参数不是有效数值。"), true);
        visual->render();
        return;
    }

    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        setPreviewStatus(QStringLiteral("无法读取网格方向信息。"), true);
        visual->render();
        return;
    }
    // 不要在实时预览刷新中调用 updateStructure()。该函数会恢复原始面颜色，
    // 造成关闭预览后所选侧立面的高亮无法恢复。
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid || grid->GetNumberOfPoints() <= 0)
    {
        setPreviewStatus(QStringLiteral("当前没有可用于预览的网格。"), true);
        visual->render();
        return;
    }

    int verticalAxis = meshService->verticalAxis() - 1;
    if (verticalAxis < 0 || verticalAxis > 2)
        verticalAxis = 1;
    const bool faceMode = currentPickMode() == PickMode::Face;
    const auto selectedKeys = PickManager::instance().getIdList(
        pickDomain(), m_uuid, currentPickMode(), GridKind::Solid);
    if (selectedKeys.isEmpty())
    {
        setPreviewStatus(QStringLiteral("请先选择侧立面边界，再调节侧压高程。"), true);
        visual->render();
        return;
    }

    double gridBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    grid->GetBounds(gridBounds);
    const PreviewPoint gridCenter{(gridBounds[0] + gridBounds[1]) * 0.5,
                                  (gridBounds[2] + gridBounds[3]) * 0.5,
                                  (gridBounds[4] + gridBounds[5]) * 0.5};

    QVector<BoundaryPreview> originalBoundaries;
    QVector<BoundaryPreview> loadedBoundaries;
    originalBoundaries.reserve(selectedKeys.size());
    loadedBoundaries.reserve(selectedKeys.size());
    double selectedBounds[6];
    resetBounds(selectedBounds);
    double maximumPressure = 0.0;

    for (vtkIdType key : selectedKeys)
    {
        const vtkIdType cellId = faceMode ? PickManager::instance().unpackFaceCellId(key)
                                          : PickManager::instance().unpackEdgeCellId(key);
        const int localId = faceMode ? PickManager::instance().unpackFaceLocalId(key)
                                     : PickManager::instance().unpackEdgeLocalId(key);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        vtkIdList* ids = nullptr;
        if (faceMode)
        {
            if (cell->GetCellDimension() == 2)
                ids = cell->GetPointIds();
            else if (localId >= 0 && localId < cell->GetNumberOfFaces())
            {
                vtkCell* face = cell->GetFace(localId);
                ids = face ? face->GetPointIds() : nullptr;
            }
        }
        else if (cell->GetCellDimension() == 1 || localId == 15)
        {
            ids = cell->GetPointIds();
        }
        else if (localId >= 0 && localId < cell->GetNumberOfEdges())
        {
            vtkCell* edge = cell->GetEdge(localId);
            ids = edge ? edge->GetPointIds() : nullptr;
        }
        if (!ids)
            continue;

        QVector<PreviewPoint> originalPoints;
        const int pointCount = faceMode ? std::min<int>(ids->GetNumberOfIds(), 4) : 2;
        if (pointCount < (faceMode ? 3 : 2))
            continue;
        originalPoints.reserve(pointCount);
        for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
        {
            vtkIdType pointId = ids->GetId(pointIndex);
            if (!faceMode && pointIndex == 1 && ids->GetNumberOfIds() > 2)
                pointId = ids->GetId(ids->GetNumberOfIds() - 1);
            double xyz[3] = {0.0, 0.0, 0.0};
            grid->GetPoint(pointId, xyz);
            const PreviewPoint point{xyz[0], xyz[1], xyz[2]};
            originalPoints.push_back(point);
            expandBounds(selectedBounds, point);
        }
        BoundaryPreview originalBoundary;
        originalBoundary.points = originalPoints;
        if (faceMode)
            originalBoundary.displayNormal =
                outwardPolygonNormal(originalPoints, gridCenter);
        originalBoundaries.push_back(originalBoundary);

        BoundaryPreview boundary = originalBoundary;
        if (std::abs(pgrout) <= 1.0e-12)
        {
            boundary.points = faceMode
                                  ? clipPolygonBelow(boundary.points, verticalAxis, height)
                                  : clipLineBelow(boundary.points, verticalAxis, height);
        }
        if (boundary.points.size() < (faceMode ? 3 : 2))
            continue;

        for (const PreviewPoint& point : std::as_const(boundary.points))
        {
            maximumPressure = std::max(
                maximumPressure,
                std::abs(pressureAt(point, verticalAxis, height, gama, pgrout)));
        }
        loadedBoundaries.push_back(boundary);
    }

    if (!validBounds(selectedBounds))
    {
        setPreviewStatus(QStringLiteral("所选边界中没有可读取的有效网格点。"), true);
        visual->render();
        return;
    }

    const double selectedSpan[3] = {selectedBounds[1] - selectedBounds[0],
                                    selectedBounds[3] - selectedBounds[2],
                                    selectedBounds[5] - selectedBounds[4]};
    const double gridSpan[3] = {gridBounds[1] - gridBounds[0],
                                gridBounds[3] - gridBounds[2],
                                gridBounds[5] - gridBounds[4]};
    const double maxSpan = std::max({std::abs(selectedSpan[0]), std::abs(selectedSpan[1]),
                                     std::abs(selectedSpan[2]),
                                     std::abs(gridSpan[0]) * 0.05,
                                     std::abs(gridSpan[1]) * 0.05,
                                     std::abs(gridSpan[2]) * 0.05, 1.0e-6});
    const double tolerance = maxSpan * 1.0e-8;
    // 预览面与原网格面完全重合会产生深度冲突（转动视角时若隐若现）。
    // 将预览面沿模型外法向轻微抬离，同时使用不透明着色，彻底避免 Z-fighting。
    const double previewSurfaceOffset = faceMode ? std::max(maxSpan * 2.0e-4, 1.0e-7) : 0.0;
    const double verticalMinimum = selectedBounds[2 * verticalAxis];
    const double verticalMaximum = selectedBounds[2 * verticalAxis + 1];
    const bool levelInsideSelection = height >= verticalMinimum - tolerance
                                      && height <= verticalMaximum + tolerance;
    const QString prefix = QStringLiteral("geostress_side_pressure_%1_").arg(m_uuid);

    auto levelPoints = vtkSmartPointer<vtkPoints>::New();
    auto levelCells = vtkSmartPointer<vtkCellArray>::New();
    const int meshDimension =
        PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (levelInsideSelection && meshDimension == 2)
    {
        int horizontalAxis = -1;
        double bestGridSpan = -1.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis != verticalAxis && std::abs(gridSpan[axis]) > bestGridSpan)
            {
                bestGridSpan = std::abs(gridSpan[axis]);
                horizontalAxis = axis;
            }
        }
        if (horizontalAxis < 0)
            horizontalAxis = verticalAxis == 0 ? 1 : 0;
        const int normalAxis = 3 - verticalAxis - horizontalAxis;
        PreviewPoint first{(selectedBounds[0] + selectedBounds[1]) * 0.5,
                           (selectedBounds[2] + selectedBounds[3]) * 0.5,
                           (selectedBounds[4] + selectedBounds[5]) * 0.5};
        PreviewPoint second = first;
        const double selectedHorizontalSpan = std::abs(selectedSpan[horizontalAxis]);
        const double halfLength = selectedHorizontalSpan > tolerance
                                      ? selectedHorizontalSpan * 0.5 + maxSpan * 0.025
                                      : std::max(std::abs(gridSpan[horizontalAxis]) * 0.035,
                                                 maxSpan * 0.06);
        first[verticalAxis] = second[verticalAxis] = height;
        first[horizontalAxis] -= halfLength;
        second[horizontalAxis] += halfLength;
        first[normalAxis] = second[normalAxis] =
            (selectedBounds[2 * normalAxis] + selectedBounds[2 * normalAxis + 1]) * 0.5;
        vtkIdType ids[2] = {levelPoints->InsertNextPoint(first.data()),
                            levelPoints->InsertNextPoint(second.data())};
        levelCells->InsertNextCell(2, ids);
    }
    else if (levelInsideSelection)
    {
        for (const BoundaryPreview& polygon : std::as_const(originalBoundaries))
        {
            const QVector<PreviewPoint> intersections =
                polygonLevelIntersections(polygon.points, verticalAxis, height, tolerance);
            if (intersections.size() < 2)
                continue;

            int firstIndex = 0;
            int secondIndex = 1;
            double maximumDistanceSquared = -1.0;
            for (int firstCandidate = 0; firstCandidate < intersections.size(); ++firstCandidate)
            {
                for (int secondCandidate = firstCandidate + 1;
                     secondCandidate < intersections.size(); ++secondCandidate)
                {
                    double distanceSquared = 0.0;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const double delta = intersections[firstCandidate][axis]
                                             - intersections[secondCandidate][axis];
                        distanceSquared += delta * delta;
                    }
                    if (distanceSquared > maximumDistanceSquared)
                    {
                        maximumDistanceSquared = distanceSquared;
                        firstIndex = firstCandidate;
                        secondIndex = secondCandidate;
                    }
                }
            }
            if (maximumDistanceSquared <= tolerance * tolerance)
                continue;
            const PreviewPoint firstPoint =
                offsetPoint(intersections[firstIndex], polygon.displayNormal,
                            previewSurfaceOffset);
            const PreviewPoint secondPoint =
                offsetPoint(intersections[secondIndex], polygon.displayNormal,
                            previewSurfaceOffset);
            vtkIdType ids[2] = {levelPoints->InsertNextPoint(firstPoint.data()),
                                levelPoints->InsertNextPoint(secondPoint.data())};
            levelCells->InsertNextCell(2, ids);
        }
    }

    if (levelCells->GetNumberOfCells() > 0)
    {
        auto levelPolyData = vtkSmartPointer<vtkPolyData>::New();
        levelPolyData->SetPoints(levelPoints);
        levelPolyData->SetLines(levelCells);
        auto levelMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        levelMapper->SetInputData(levelPolyData);
        levelMapper->ScalarVisibilityOff();
        levelMapper->SetResolveCoincidentTopologyToPolygonOffset();
        auto levelActor = vtkSmartPointer<vtkActor>::New();
        levelActor->SetMapper(levelMapper);
        levelActor->PickableOff();
        levelActor->GetProperty()->LightingOff();
        levelActor->GetProperty()->SetColor(0.02, 0.58, 0.82);
        levelActor->GetProperty()->SetLineWidth(4.0);
        visual->addProp(prefix + QStringLiteral("level"), levelActor,
                        QStringLiteral("地应力回归侧压预览"), QStringLiteral("侧压高程"),
                        false, false);
    }

    QString pressureUnit = QStringLiteral("Pa");
    double pressureDisplayScale = 1.0;
    if (maximumPressure >= 1.0e6)
    {
        pressureUnit = QStringLiteral("MPa");
        pressureDisplayScale = 1.0e-6;
    }
    else if (maximumPressure >= 1.0e3)
    {
        pressureUnit = QStringLiteral("kPa");
        pressureDisplayScale = 1.0e-3;
    }
    const double maximumDisplayPressure = maximumPressure * pressureDisplayScale;

    if (maximumPressure > 1.0e-9 && !loadedBoundaries.isEmpty())
    {
        auto pressurePoints = vtkSmartPointer<vtkPoints>::New();
        auto pressureCells = vtkSmartPointer<vtkCellArray>::New();
        auto pressureValues = vtkSmartPointer<vtkDoubleArray>::New();
        pressureValues->SetName("pressure");
        for (const BoundaryPreview& boundary : std::as_const(loadedBoundaries))
        {
            QVector<vtkIdType> ids;
            ids.reserve(boundary.points.size());
            for (const PreviewPoint& point : boundary.points)
            {
                const PreviewPoint displayPoint =
                    offsetPoint(point, boundary.displayNormal, previewSurfaceOffset);
                ids.push_back(pressurePoints->InsertNextPoint(displayPoint.data()));
                pressureValues->InsertNextValue(
                    std::abs(pressureAt(point, verticalAxis, height, gama, pgrout))
                    * pressureDisplayScale);
            }
            pressureCells->InsertNextCell(ids.size(), ids.constData());
        }
        auto pressurePolyData = vtkSmartPointer<vtkPolyData>::New();
        pressurePolyData->SetPoints(pressurePoints);
        pressurePolyData->GetPointData()->SetScalars(pressureValues);
        if (faceMode)
            pressurePolyData->SetPolys(pressureCells);
        else
            pressurePolyData->SetLines(pressureCells);

        auto pressureLut = vtkSmartPointer<vtkLookupTable>::New();
        pressureLut->SetHueRange(0.13, 0.0);
        pressureLut->SetSaturationRange(0.88, 1.0);
        pressureLut->SetValueRange(1.0, 0.88);
        pressureLut->SetTableRange(0.0, maximumDisplayPressure);
        pressureLut->Build();
        auto pressureMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        pressureMapper->SetInputData(pressurePolyData);
        pressureMapper->SetLookupTable(pressureLut);
        pressureMapper->SetScalarRange(0.0, maximumDisplayPressure);
        pressureMapper->ScalarVisibilityOn();
        pressureMapper->SetResolveCoincidentTopologyToPolygonOffset();
        auto pressureActor = vtkSmartPointer<vtkActor>::New();
        pressureActor->SetMapper(pressureMapper);
        pressureActor->PickableOff();
        pressureActor->GetProperty()->LightingOff();
        pressureActor->GetProperty()->SetOpacity(faceMode ? 1.0 : 0.95);
        pressureActor->GetProperty()->SetLineWidth(faceMode ? 1.0 : 5.0);
        if (faceMode)
        {
            pressureActor->GetProperty()->EdgeVisibilityOn();
            pressureActor->GetProperty()->SetEdgeColor(0.16, 0.16, 0.16);
        }
        visual->addProp(prefix + QStringLiteral("loaded"), pressureActor,
                        QStringLiteral("地应力回归侧压预览"), QStringLiteral("受压区域"),
                        false, false);

        auto scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
        scalarBar->SetLookupTable(pressureLut);
        const QByteArray title =
            QStringLiteral("侧压力 (%1)").arg(pressureUnit).toUtf8();
        scalarBar->SetTitle(title.constData());
        scalarBar->SetNumberOfLabels(5);
        scalarBar->SetLabelFormat("%.3g");
        scalarBar->SetOrientationToVertical();
        scalarBar->SetPosition(0.86, 0.14);
        scalarBar->SetWidth(0.08);
        scalarBar->SetHeight(0.52);
        scalarBar->PickableOff();
        scalarBar->GetTitleTextProperty()->SetFontSize(14);
        scalarBar->GetTitleTextProperty()->SetBold(true);
        scalarBar->GetTitleTextProperty()->SetColor(0.15, 0.15, 0.15);
        scalarBar->GetLabelTextProperty()->SetFontSize(12);
        scalarBar->GetLabelTextProperty()->SetColor(0.15, 0.15, 0.15);
        visual->addProp(prefix + QStringLiteral("legend"), scalarBar,
                        QStringLiteral("地应力回归侧压预览"), QStringLiteral("侧压力色标"),
                        false, false);
    }

    const QString rangeText = QStringLiteral("%1 ～ %2")
                                  .arg(verticalMinimum, 0, 'g', 10)
                                  .arg(verticalMaximum, 0, 'g', 10);
    if (!levelInsideSelection)
    {
        setPreviewStatus(
            QStringLiteral("侧压高程 %1 位于所选边界高程范围 %2 之外；模型中不显示高程交线。")
                .arg(height, 0, 'g', 10)
                .arg(rangeText),
            true);
    }
    else if (maximumPressure <= 1.0e-9)
    {
        setPreviewStatus(
            QStringLiteral("蓝线为侧压高程；当前参数在所选边界上没有产生有效侧压。"), true);
    }
    else
    {
        setPreviewStatus(
            QStringLiteral("按 p=ρg·max(H-z,0)+p₀ 计算；蓝线为侧压高程，色标表示压力。"
                           "最大压力：%1 %2")
                .arg(maximumDisplayPressure, 0, 'g', 8)
                .arg(pressureUnit),
            false);
    }
    visual->render();
}
