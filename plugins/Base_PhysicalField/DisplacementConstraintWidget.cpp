#include <CompactDoubleSpinBox.h>
#include "DisplacementConstraintWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <StyleManager.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhySicalFieldService.h>
#include <services/VisualWidgetService.h>

#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QSet>
#include <QSignalBlocker>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

#include "ui_DisplacementConstraintWidget.h"

// VTK
#include <PickManager.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkAssembly.h>
#include <vtkAxesActor.h>
#include <vtkCaptionActor2D.h>
#include <vtkCamera.h>
#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCell.h>
#include <vtkCellPicker.h>
#include <vtkCoordinate.h>
#include <vtkCubeSource.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>
#include <vtkVertexGlyphFilter.h>


namespace
{
static QString boundaryDimText(int dim)
{
    switch (dim)
    {
    case 0: return QStringLiteral("点边界");
    case 1: return QStringLiteral("线边界");
    case 2: return QStringLiteral("面边界");
    default: return QStringLiteral("边界");
    }
}

static QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& ids)
{
    QVector<vtkIdType> out;
    out.reserve(ids.size());
    for (MeshIdType id : ids)
        out.push_back(static_cast<vtkIdType>(id));
    return out;
}

static MeshTagKey comboTagKey(QComboBox* combo, int row)
{
    if (!combo || row < 0)
        return {};
    return combo->itemData(row).value<MeshTagKey>();
}

static QString boundaryDisplayText(MeshModuleControllerService* meshSvc, const MeshTagSet& set, bool showDimPrefix)
{
    QString text = meshSvc ? meshSvc->displayTextOfTag(set.key) : set.name.trimmed();
    if (text.trimmed().isEmpty())
        text = QStringLiteral("%1%2").arg(boundaryDimText(set.key.dim)).arg(set.key.id);
    return showDimPrefix ? QStringLiteral("%1：%2").arg(boundaryDimText(set.key.dim), text) : text;
}

static void initConstraintCombo(QComboBox* combo)
{
    combo->clear();
    combo->addItem(QStringLiteral("固定"), QStringLiteral("0"));
    combo->addItem(QStringLiteral("自由"), QStringLiteral("1"));
    combo->addItem(QStringLiteral("指定位移"), QStringLiteral("2"));
    combo->addItem(QStringLiteral("悬空杆件节点（专用）"), QStringLiteral("-10000"));
    combo->setCurrentIndex(0);
}

static void setConstraintValue(QComboBox* combo, const QString& value)
{
    const int index = combo->findData(value.trimmed());
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

struct CoordinateRangeFilter
{
    bool useX = false;
    bool useY = false;
    bool useZ = false;
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;
    double tolerance = 0.0;
};

static bool coordinateMatches(double value, double minimum, double maximum, double tolerance)
{
    return value >= minimum - tolerance && value <= maximum + tolerance;
}

static int maximumCellDimension(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return -1;

    int maxDimension = -1;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (cell)
            maxDimension = std::max(maxDimension, cell->GetCellDimension());
    }
    return maxDimension;
}

static QSet<vtkIdType> boundaryPointIds(vtkUnstructuredGrid* grid)
{
    QSet<vtkIdType> pointIds;
    if (!grid || grid->GetNumberOfPoints() <= 0 || grid->GetNumberOfCells() <= 0)
        return pointIds;

    const int maxDimension = maximumCellDimension(grid);
    if (maxDimension <= 1)
    {
        // 杆、梁等线网格没有体内部节点，坐标约束仍允许按位置选择其线节点。
        pointIds.reserve(static_cast<int>(grid->GetNumberOfPoints()));
        for (vtkIdType pointId = 0; pointId < grid->GetNumberOfPoints(); ++pointId)
            pointIds.insert(pointId);
        return pointIds;
    }

    // 对最高维单元的面/边进行拓扑计数：只出现一次的子单元属于真实网格边界。
    // 三维网格统计面，二维网格统计边；材料内部共享界面出现两次，不会被当成外边界。
    using FacetKey = std::vector<vtkIdType>;
    struct FacetKeyHash
    {
        std::size_t operator()(const FacetKey& key) const noexcept
        {
            std::size_t seed = key.size();
            for (vtkIdType id : key)
            {
                const std::size_t value = std::hash<vtkIdType>{}(id);
                seed ^= value + std::size_t(0x9e3779b9) + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    std::unordered_map<FacetKey, int, FacetKeyHash> facetUseCount;
    const bool useFaces = (maxDimension >= 3);

    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != maxDimension)
            continue;

        const int facetCount = useFaces ? cell->GetNumberOfFaces() : cell->GetNumberOfEdges();
        for (int facetIndex = 0; facetIndex < facetCount; ++facetIndex)
        {
            vtkCell* facet = useFaces ? cell->GetFace(facetIndex) : cell->GetEdge(facetIndex);
            if (!facet || facet->GetNumberOfPoints() <= 0)
                continue;

            FacetKey key;
            key.reserve(static_cast<std::size_t>(facet->GetNumberOfPoints()));
            for (vtkIdType i = 0; i < facet->GetNumberOfPoints(); ++i)
                key.push_back(facet->GetPointId(i));
            std::sort(key.begin(), key.end());
            ++facetUseCount[key];
        }
    }

    for (const auto& entry : facetUseCount)
    {
        if (entry.second != 1)
            continue;
        for (vtkIdType pointId : entry.first)
            pointIds.insert(pointId);
    }
    return pointIds;
}


// “点选整片”仅用于交互扩展：
// 2D 沿连续近似共线的外边界边扩展；3D 沿共享边的连续共面外表面扩展。
// 结果仍交给 PickManager::setSelectedPointIds()；位移约束允许共享节点，最终按自由度组合。
using SmartFacetKey = std::vector<vtkIdType>;
struct SmartFacetKeyHash
{
    std::size_t operator()(const SmartFacetKey& key) const noexcept
    {
        std::size_t seed = key.size();
        for (vtkIdType id : key)
        {
            const std::size_t value = std::hash<vtkIdType>{}(id);
            seed ^= value + std::size_t(0x9e3779b9) + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

struct SmartBoundaryFacet
{
    QVector<vtkIdType> points;
};

static QVector<SmartBoundaryFacet> smartBoundaryFacets(vtkUnstructuredGrid* grid)
{
    QVector<SmartBoundaryFacet> result;
    if (!grid || grid->GetNumberOfCells() <= 0)
        return result;

    const int maxDimension = maximumCellDimension(grid);
    if (maxDimension <= 1)
        return result;

    struct FacetRecord
    {
        int count = 0;
        QVector<vtkIdType> orderedPoints;
    };

    std::unordered_map<SmartFacetKey, FacetRecord, SmartFacetKeyHash> records;
    const bool useFaces = (maxDimension >= 3);

    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != maxDimension)
            continue;

        const int facetCount = useFaces ? cell->GetNumberOfFaces() : cell->GetNumberOfEdges();
        for (int facetIndex = 0; facetIndex < facetCount; ++facetIndex)
        {
            vtkCell* facet = useFaces ? cell->GetFace(facetIndex) : cell->GetEdge(facetIndex);
            if (!facet || facet->GetNumberOfPoints() <= 0)
                continue;

            SmartFacetKey key;
            QVector<vtkIdType> ordered;
            key.reserve(static_cast<std::size_t>(facet->GetNumberOfPoints()));
            ordered.reserve(static_cast<int>(facet->GetNumberOfPoints()));
            for (vtkIdType i = 0; i < facet->GetNumberOfPoints(); ++i)
            {
                const vtkIdType pointId = facet->GetPointId(i);
                key.push_back(pointId);
                ordered.push_back(pointId);
            }
            std::sort(key.begin(), key.end());

            FacetRecord& record = records[key];
            ++record.count;
            if (record.orderedPoints.isEmpty())
                record.orderedPoints = ordered;
        }
    }

    for (const auto& entry : records)
    {
        if (entry.second.count == 1)
            result.push_back({entry.second.orderedPoints});
    }
    return result;
}

static bool normalizeSmartVector(double vector[3])
{
    const double length = vtkMath::Norm(vector);
    if (length <= 1.0e-15)
        return false;
    vector[0] /= length;
    vector[1] /= length;
    vector[2] /= length;
    return true;
}

static bool smartFacetNormalAndCentroid(vtkUnstructuredGrid* grid,
                                        const SmartBoundaryFacet& facet,
                                        double normal[3], double centroid[3])
{
    normal[0] = normal[1] = normal[2] = 0.0;
    centroid[0] = centroid[1] = centroid[2] = 0.0;
    if (!grid || facet.points.size() < 3)
        return false;

    const int count = facet.points.size();
    for (int i = 0; i < count; ++i)
    {
        double a[3] = {0.0, 0.0, 0.0};
        double b[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(facet.points[i], a);
        grid->GetPoint(facet.points[(i + 1) % count], b);

        centroid[0] += a[0];
        centroid[1] += a[1];
        centroid[2] += a[2];

        // Newell 法，兼容三角形和四边形等平面外表面。
        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }

    const double invCount = 1.0 / static_cast<double>(count);
    centroid[0] *= invCount;
    centroid[1] *= invCount;
    centroid[2] *= invCount;
    return normalizeSmartVector(normal);
}

struct SmartEdgeKey
{
    vtkIdType a = -1;
    vtkIdType b = -1;
    bool operator==(const SmartEdgeKey& other) const noexcept
    {
        return a == other.a && b == other.b;
    }
};

struct SmartEdgeKeyHash
{
    std::size_t operator()(const SmartEdgeKey& edge) const noexcept
    {
        std::size_t seed = std::hash<vtkIdType>{}(edge.a);
        seed ^= std::hash<vtkIdType>{}(edge.b) + std::size_t(0x9e3779b9) + (seed << 6) + (seed >> 2);
        return seed;
    }
};

static SmartEdgeKey makeSmartEdgeKey(vtkIdType a, vtkIdType b)
{
    if (a > b)
        std::swap(a, b);
    return {a, b};
}

static QVector<vtkIdType> smartConnectedLinePatch(vtkUnstructuredGrid* grid,
                                                   vtkIdType seedPointId)
{
    if (!grid || seedPointId < 0 || seedPointId >= grid->GetNumberOfPoints())
        return {};

    struct Segment
    {
        vtkIdType a = -1;
        vtkIdType b = -1;
        double direction[3] = {0.0, 0.0, 0.0};
    };

    QVector<Segment> segments;
    QHash<vtkIdType, QVector<int>> pointSegments;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 1 || cell->GetNumberOfPoints() < 2)
            continue;

        Segment segment;
        segment.a = cell->GetPointId(0);
        segment.b = cell->GetPointId(cell->GetNumberOfPoints() - 1);
        double a[3] = {0.0, 0.0, 0.0};
        double b[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(segment.a, a);
        grid->GetPoint(segment.b, b);
        segment.direction[0] = b[0] - a[0];
        segment.direction[1] = b[1] - a[1];
        segment.direction[2] = b[2] - a[2];
        if (!normalizeSmartVector(segment.direction))
            continue;

        const int index = segments.size();
        segments.push_back(segment);
        pointSegments[segment.a].push_back(index);
        pointSegments[segment.b].push_back(index);
    }

    const QVector<int> seedSegments = pointSegments.value(seedPointId);
    if (seedSegments.isEmpty())
        return {seedPointId};

    const Segment& seed = segments[seedSegments.front()];
    const double reference[3] = {seed.direction[0], seed.direction[1], seed.direction[2]};
    const double cosTolerance = std::cos(5.0 * vtkMath::Pi() / 180.0);

    QSet<int> visited;
    QVector<int> queue{seedSegments.front()};
    QSet<vtkIdType> points;
    while (!queue.isEmpty())
    {
        const int index = queue.takeLast();
        if (visited.contains(index))
            continue;

        const Segment& segment = segments[index];
        if (std::abs(vtkMath::Dot(reference, segment.direction)) < cosTolerance)
            continue;

        visited.insert(index);
        points.insert(segment.a);
        points.insert(segment.b);
        for (vtkIdType pointId : {segment.a, segment.b})
        {
            const QVector<int> neighbours = pointSegments.value(pointId);
            for (int next : neighbours)
            {
                if (!visited.contains(next))
                    queue.push_back(next);
            }
        }
    }

    QVector<vtkIdType> result = points.values().toVector();
    std::sort(result.begin(), result.end());
    return result;
}

static QVector<vtkIdType> smartConnectedBoundaryPatch(vtkUnstructuredGrid* grid,
                                                       vtkIdType seedPointId,
                                                       vtkRenderer* renderer)
{
    if (!grid || seedPointId < 0 || seedPointId >= grid->GetNumberOfPoints())
        return {};

    const int maxDimension = maximumCellDimension(grid);
    if (maxDimension <= 1)
        return smartConnectedLinePatch(grid, seedPointId);

    const QVector<SmartBoundaryFacet> facets = smartBoundaryFacets(grid);
    if (facets.isEmpty())
        return {seedPointId};

    QVector<int> incidentFacets;
    for (int i = 0; i < facets.size(); ++i)
    {
        if (facets[i].points.contains(seedPointId))
            incidentFacets.push_back(i);
    }
    if (incidentFacets.isEmpty())
        return {seedPointId};

    const double cosTolerance = std::cos(5.0 * vtkMath::Pi() / 180.0);

    if (maxDimension == 2)
    {
        const int seedFacet = incidentFacets.front();
        if (facets[seedFacet].points.size() < 2)
            return {seedPointId};

        double a[3] = {0.0, 0.0, 0.0};
        double b[3] = {0.0, 0.0, 0.0};
        double reference[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(facets[seedFacet].points.front(), a);
        grid->GetPoint(facets[seedFacet].points.back(), b);
        reference[0] = b[0] - a[0];
        reference[1] = b[1] - a[1];
        reference[2] = b[2] - a[2];
        if (!normalizeSmartVector(reference))
            return {seedPointId};

        QHash<vtkIdType, QVector<int>> pointFacets;
        for (int i = 0; i < facets.size(); ++i)
        {
            for (vtkIdType pointId : facets[i].points)
                pointFacets[pointId].push_back(i);
        }

        QSet<int> visited;
        QVector<int> queue{seedFacet};
        QSet<vtkIdType> points;
        while (!queue.isEmpty())
        {
            const int index = queue.takeLast();
            if (visited.contains(index))
                continue;

            const SmartBoundaryFacet& edge = facets[index];
            if (edge.points.size() < 2)
                continue;

            double p0[3] = {0.0, 0.0, 0.0};
            double p1[3] = {0.0, 0.0, 0.0};
            double direction[3] = {0.0, 0.0, 0.0};
            grid->GetPoint(edge.points.front(), p0);
            grid->GetPoint(edge.points.back(), p1);
            direction[0] = p1[0] - p0[0];
            direction[1] = p1[1] - p0[1];
            direction[2] = p1[2] - p0[2];
            if (!normalizeSmartVector(direction) ||
                std::abs(vtkMath::Dot(reference, direction)) < cosTolerance)
                continue;

            visited.insert(index);
            for (vtkIdType pointId : edge.points)
            {
                points.insert(pointId);
                const QVector<int> neighbours = pointFacets.value(pointId);
                for (int next : neighbours)
                {
                    if (!visited.contains(next))
                        queue.push_back(next);
                }
            }
        }

        QVector<vtkIdType> result = points.values().toVector();
        std::sort(result.begin(), result.end());
        return result;
    }

    // 一个角点可能同时属于多个外表面。优先取当前视角最正对用户的那个面作为种子面。
    int seedFacet = incidentFacets.front();
    double bestFacing = -1.0;
    double cameraPosition[3] = {0.0, 0.0, 0.0};
    const bool hasCamera = renderer && renderer->GetActiveCamera();
    if (hasCamera)
        renderer->GetActiveCamera()->GetPosition(cameraPosition);

    for (int index : incidentFacets)
    {
        double normal[3] = {0.0, 0.0, 0.0};
        double centroid[3] = {0.0, 0.0, 0.0};
        if (!smartFacetNormalAndCentroid(grid, facets[index], normal, centroid))
            continue;

        double facing = 1.0;
        if (hasCamera)
        {
            double view[3] = {cameraPosition[0] - centroid[0],
                              cameraPosition[1] - centroid[1],
                              cameraPosition[2] - centroid[2]};
            if (normalizeSmartVector(view))
                facing = std::abs(vtkMath::Dot(normal, view));
        }

        if (facing > bestFacing)
        {
            bestFacing = facing;
            seedFacet = index;
        }
    }

    double referenceNormal[3] = {0.0, 0.0, 0.0};
    double referenceCentroid[3] = {0.0, 0.0, 0.0};
    if (!smartFacetNormalAndCentroid(grid, facets[seedFacet], referenceNormal, referenceCentroid))
        return {seedPointId};

    std::unordered_map<SmartEdgeKey, QVector<int>, SmartEdgeKeyHash> edgeOwners;
    for (int i = 0; i < facets.size(); ++i)
    {
        const QVector<vtkIdType>& points = facets[i].points;
        for (int j = 0; j < points.size(); ++j)
        {
            const vtkIdType aId = points[j];
            const vtkIdType bId = points[(j + 1) % points.size()];
            edgeOwners[makeSmartEdgeKey(aId, bId)].push_back(i);
        }
    }

    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    grid->GetBounds(bounds);
    const double span = std::max({bounds[1] - bounds[0], bounds[3] - bounds[2], bounds[5] - bounds[4]});
    const double planeTolerance = std::max(1.0e-9, span * 1.0e-7);

    QSet<int> visited;
    QVector<int> queue{seedFacet};
    QSet<vtkIdType> points;
    while (!queue.isEmpty())
    {
        const int index = queue.takeLast();
        if (visited.contains(index))
            continue;

        double normal[3] = {0.0, 0.0, 0.0};
        double centroid[3] = {0.0, 0.0, 0.0};
        if (!smartFacetNormalAndCentroid(grid, facets[index], normal, centroid))
            continue;
        if (std::abs(vtkMath::Dot(referenceNormal, normal)) < cosTolerance)
            continue;

        const double delta[3] = {centroid[0] - referenceCentroid[0],
                                 centroid[1] - referenceCentroid[1],
                                 centroid[2] - referenceCentroid[2]};
        if (std::abs(vtkMath::Dot(referenceNormal, delta)) > planeTolerance)
            continue;

        visited.insert(index);
        const QVector<vtkIdType>& facetPoints = facets[index].points;
        for (vtkIdType pointId : facetPoints)
            points.insert(pointId);

        for (int j = 0; j < facetPoints.size(); ++j)
        {
            const SmartEdgeKey edge = makeSmartEdgeKey(facetPoints[j],
                                                       facetPoints[(j + 1) % facetPoints.size()]);
            const QVector<int>& neighbours = edgeOwners[edge];
            for (int next : neighbours)
            {
                if (!visited.contains(next))
                    queue.push_back(next);
            }
        }
    }

    QVector<vtkIdType> result = points.values().toVector();
    std::sort(result.begin(), result.end());
    return result;
}

static QVector<vtkIdType> boundaryPointsInCoordinateRange(vtkUnstructuredGrid* grid,
                                                           const CoordinateRangeFilter& filter)
{
    QVector<vtkIdType> pointIds;
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return pointIds;

    const QSet<vtkIdType> boundaryIds = boundaryPointIds(grid);
    pointIds.reserve(boundaryIds.size());
    double point[3] = {0.0, 0.0, 0.0};
    for (vtkIdType pointId : boundaryIds)
    {
        if (pointId < 0 || pointId >= grid->GetNumberOfPoints())
            continue;

        grid->GetPoint(pointId, point);
        if (filter.useX && !coordinateMatches(point[0], filter.minX, filter.maxX, filter.tolerance))
            continue;
        if (filter.useY && !coordinateMatches(point[1], filter.minY, filter.maxY, filter.tolerance))
            continue;
        if (filter.useZ && !coordinateMatches(point[2], filter.minZ, filter.maxZ, filter.tolerance))
            continue;
        pointIds.push_back(pointId);
    }
    std::sort(pointIds.begin(), pointIds.end());
    return pointIds;
}

static bool appendGridBounds(vtkUnstructuredGrid* grid, double bounds[6], bool& initialized)
{
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return false;

    double current[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    grid->GetBounds(current);
    if (!initialized)
    {
        std::copy(current, current + 6, bounds);
        initialized = true;
        return true;
    }

    bounds[0] = std::min(bounds[0], current[0]);
    bounds[1] = std::max(bounds[1], current[1]);
    bounds[2] = std::min(bounds[2], current[2]);
    bounds[3] = std::max(bounds[3], current[3]);
    bounds[4] = std::min(bounds[4], current[4]);
    bounds[5] = std::max(bounds[5], current[5]);
    return true;
}

static void normalizeRange(QDoubleSpinBox* minimumSpin, QDoubleSpinBox* maximumSpin,
                           double& minimum, double& maximum)
{
    minimum = minimumSpin->value();
    maximum = maximumSpin->value();
    if (minimum <= maximum)
        return;

    std::swap(minimum, maximum);
    minimumSpin->setValue(minimum);
    maximumSpin->setValue(maximum);
}

}

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget* parent)
    : BasePage("DisplacementConstraintWidget"), ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);

    // 位移约束允许多个约束共享同一 node_id。每个约束仍保存自己的选区，
    // 最终自由度在显示和 IIID 写出时按方向合并；其他 PickManager domain 仍保持原互斥规则。
    PickManager::instance().setNodeSelectionAllowsOverlap(getPageType(), true);

    initConstraintCombo(ui->UXLineEdit);
    initConstraintCombo(ui->UYLineEdit);
    initConstraintCombo(ui->UZLineEdit);
    const std::array<QComboBox*,3> directionCombos={ui->UXLineEdit,ui->UYLineEdit,ui->UZLineEdit};
    for(int axis=0;axis<3;++axis)
    {
        auto* value=new CompactDoubleSpinBox(this);
        value->setObjectName(QStringLiteral("prescribedValue%1").arg(axis));
        value->setDecimals(10);value->setRange(-1.e6,1.e6);value->setSuffix(QStringLiteral(" m"));
        value->setToolTip(QStringLiteral("施加给当前选区的位移，正负号按全局坐标方向；自定义荷载静力分析使用。"));
        m_prescribed[axis]=value;ui->gridLayout->addWidget(value,axis,2);value->hide();
        connect(directionCombos[axis],qOverload<int>(&QComboBox::currentIndexChanged),this,
            [this,axis,directionCombos]{m_prescribed[axis]->setVisible(directionCombos[axis]->currentData().toString()==QStringLiteral("2"));});
    }

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);
    auto* rotationLabel=new QLabel(QStringLiteral("绕 Z 转动（平面梁）"),this);
    m_rotation=new QComboBox(this);initConstraintCombo(m_rotation);
    const int prescribedIndex=m_rotation->findData(QStringLiteral("2"));m_rotation->setItemText(prescribedIndex,QStringLiteral("指定转角"));
    m_rotation->removeItem(m_rotation->findData(QStringLiteral("-10000")));
    setConstraintValue(m_rotation,QStringLiteral("1"));
    m_rotationValue=new CompactDoubleSpinBox(this);m_rotationValue->setDecimals(10);m_rotationValue->setRange(-1.e6,1.e6);m_rotationValue->setSuffix(QStringLiteral(" rad"));
    ui->gridLayout->addWidget(rotationLabel,4,0);ui->gridLayout->addWidget(m_rotation,4,1);ui->gridLayout->addWidget(m_rotationValue,4,2);
    rotationLabel->setVisible(is2D);m_rotation->setVisible(is2D);m_rotationValue->hide();
    connect(m_rotation,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,is2D]{m_rotationValue->setVisible(is2D&&m_rotation->currentData().toString()==QStringLiteral("2"));});
    ui->UZLineEdit->setEnabled(!is2D);
    ui->UZLineEdit->setToolTip(is2D ? QStringLiteral("二维模型不使用 Z 方向") : QString());

    connect(ui->btnSetAllBarsSuspended, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onSetAllBarsSuspended);
    connect(ui->btnSelectByCoordinate, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onSelectByCoordinateRange);
    connect(ui->btnUseMeshBounds, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onUseMeshBounds);

    const auto coordinateStateChanged = [this](bool)
    {
        updateCoordinateControlState();
    };
    connect(ui->coordXCheckBox, &QCheckBox::toggled, this, coordinateStateChanged);
    connect(ui->coordYCheckBox, &QCheckBox::toggled, this, coordinateStateChanged);
    connect(ui->coordZCheckBox, &QCheckBox::toggled, this, coordinateStateChanged);

    const auto constraintDirectionChanged = [this](int)
    {
        registerConstraintFlags();
        refreshConstraintVisualization(false);
        refreshOverlapFeedback(true);
    };
    connect(ui->UXLineEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            constraintDirectionChanged);
    connect(ui->UYLineEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            constraintDirectionChanged);
    connect(ui->UZLineEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            constraintDirectionChanged);

    // “开启点选”和“点选整片”是两个独立开关：
    // 前者只控制鼠标单击是否参与选择；后者只决定一次单击是否扩展到连续边界整片。
    // 框选、分组和坐标范围选择不受“点选整片”影响。

    // 分组、点选控制和坐标范围都是常规选区入口，直接显示。
    ui->advancedPanel->setVisible(true);

    updateCoordinateControlState();
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
    clearOverlapVisualization(false);
    clearConstraintVisualization(false);
    delete ui;
}

void DisplacementConstraintWidget::beforeLoad()
{
    m_constraintPageActive = true;
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();

    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    pickManager.activate(getPageType(), m_uuid, PickMode::Node);
    // 页面可能被反复进入，先移除 PickManager 到本页面的旧连接，再建立当前连接。
    QObject::disconnect(&PickManager::instance(), nullptr, this, nullptr);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &DisplacementConstraintWidget::onNodePicked, Qt::UniqueConnection);

    // 当前约束的方向元数据用于计算共享节点的“最终约束”。
    registerConstraintFlags();

    // 无论点选、框选、分组还是坐标范围，只要节点集合变化就同步刷新共享节点提示。
    QObject::connect(&PickManager::instance(), &PickManager::selectionChanged, this,
                     [this](const QString& domain, const QString&, PickMode mode, GridKind)
                     {
                         if (domain == getPageType() && mode == PickMode::Node)
                             refreshOverlapFeedback();
                     });

    // ===== 1) 从某处拿到映射（示例：meshSvc 提供 pick map）=====
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("全部边界"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("点边界"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());

    // 防止页面来回切换后重复连接，造成批量选逻辑多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    // 维度变化时仅刷新候选组
    QObject::connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DisplacementConstraintWidget::onDimChanged, Qt::UniqueConnection);
    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            });

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    if (row < 0)
                        return;

                    PickManager& pickManager = PickManager::instance();
                    auto* meshSvc =
                        ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    pickManager.setPreviewPointIds(
                        getPageType(), m_uuid, GridKind::Solid,
                        toVtkIds(meshSvc->pointIdsOfTag(key)));
                    pickManager.setPreviewPointIds(getPageType(), m_uuid, GridKind::Line, {});
                });
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                });
    }

    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool enabled)
            {
                PickManager& pickManager = PickManager::instance();
                if (enabled)
                {
                    // 显示筛选可能刚刚重建 surface，重新同步后直接恢复点选，
                    // 不再要求用户切一次边/面或离开页面。
                    pickManager.updateStructure();
                    pickManager.activate(getPageType(), m_uuid, PickMode::Node);
                }
                pickManager.setClickPickEnabled(getPageType(), m_uuid, enabled);
            });

    // 不强制重置用户选择。新页面由 .ui 默认开启点选、开启整片；
    // 已存在页面在本次会话中切换回来时保留用户刚才的开关状态。
    pickManager.setClickPickEnabled(getPageType(), m_uuid, ui->pickEnableCheckBox->isChecked());

    if (!m_coordinateRangeLoaded)
        onUseMeshBounds();
    updateCoordinateControlState();
    refreshConstraintVisualization(false);
    refreshOverlapFeedback(true);
}


void DisplacementConstraintWidget::refreshGroupCombo(int dimFilter)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(dimFilter);
    const bool showDimPrefix = (dimFilter < 0);

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, showDimPrefix),
                                   QVariant::fromValue(set.key));

    const bool available = ui->comboBoxGroup->count() > 0;
    ui->comboBoxGroup->setEnabled(available);
    ui->btnBatchAdd->setEnabled(available);
    ui->comboBoxGroup->setToolTip(
        available ? QStringLiteral("选择边界后点击“添加”")
                  : QStringLiteral("当前维度没有可用边界组"));
    if (available)
        ui->comboBoxGroup->setCurrentIndex(0);

    ui->comboBoxGroup->blockSignals(false);
}

void DisplacementConstraintWidget::onDimChanged(int)
{
    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());
}


void DisplacementConstraintWidget::onGroupChanged(int index)
{
    if (index < 0)
        return;

    PickManager& pickManager = PickManager::instance();
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid,
                                    toVtkIds(meshSvc->pointIdsOfTag(key)), false);
}


void DisplacementConstraintWidget::updateCoordinateControlState()
{
    const int dimension = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (dimension == 2);

    ui->xMinSpinBox->setEnabled(ui->coordXCheckBox->isChecked());
    ui->xMaxSpinBox->setEnabled(ui->coordXCheckBox->isChecked());
    ui->yMinSpinBox->setEnabled(ui->coordYCheckBox->isChecked());
    ui->yMaxSpinBox->setEnabled(ui->coordYCheckBox->isChecked());

    if (is2D)
        ui->coordZCheckBox->setChecked(false);
    ui->coordZCheckBox->setEnabled(!is2D);
    ui->zMinSpinBox->setEnabled(!is2D && ui->coordZCheckBox->isChecked());
    ui->zMaxSpinBox->setEnabled(!is2D && ui->coordZCheckBox->isChecked());
}

void DisplacementConstraintWidget::onUseMeshBounds()
{
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();

    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool initialized = false;
    appendGridBounds(pickManager.solidGrid(), bounds, initialized);
    appendGridBounds(pickManager.lineGrid(), bounds, initialized);

    if (!initialized)
    {
        ui->coordinateResultLabel->setText(QStringLiteral("当前没有可用网格。"));
        return;
    }

    ui->xMinSpinBox->setValue(bounds[0]);
    ui->xMaxSpinBox->setValue(bounds[1]);
    ui->yMinSpinBox->setValue(bounds[2]);
    ui->yMaxSpinBox->setValue(bounds[3]);
    ui->zMinSpinBox->setValue(bounds[4]);
    ui->zMaxSpinBox->setValue(bounds[5]);

    if (!m_coordinateRangeLoaded)
    {
        const double maxSpan = std::max({bounds[1] - bounds[0], bounds[3] - bounds[2],
                                         bounds[5] - bounds[4]});
        ui->coordToleranceSpinBox->setValue(std::max(1.0e-9, maxSpan * 1.0e-8));
    }

    ui->coordinateResultLabel->setText(QStringLiteral("已读取模型坐标范围。"));
}

void DisplacementConstraintWidget::onSelectByCoordinateRange()
{
    const int dimension = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool useX = ui->coordXCheckBox->isChecked();
    const bool useY = ui->coordYCheckBox->isChecked();
    const bool useZ = dimension != 2 && ui->coordZCheckBox->isChecked();
    if (!useX && !useY && !useZ)
    {
        QMessageBox::information(this, QStringLiteral("坐标范围选择"),
                                 QStringLiteral("请至少勾选 X、Y、Z 中的一个坐标方向。"));
        return;
    }

    CoordinateRangeFilter filter;
    filter.useX = useX;
    filter.useY = useY;
    filter.useZ = useZ;
    filter.tolerance = ui->coordToleranceSpinBox->value();
    normalizeRange(ui->xMinSpinBox, ui->xMaxSpinBox, filter.minX, filter.maxX);
    normalizeRange(ui->yMinSpinBox, ui->yMaxSpinBox, filter.minY, filter.maxY);
    normalizeRange(ui->zMinSpinBox, ui->zMaxSpinBox, filter.minZ, filter.maxZ);

    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    pickManager.setClickPickEnabled(getPageType(), m_uuid,
                                    ui->pickEnableCheckBox->isChecked());
    const QVector<vtkIdType> solidPointIds =
        boundaryPointsInCoordinateRange(pickManager.solidGrid(), filter);
    const QVector<vtkIdType> linePointIds =
        boundaryPointsInCoordinateRange(pickManager.lineGrid(), filter);

    if (solidPointIds.isEmpty() && linePointIds.isEmpty())
    {
        ui->coordinateResultLabel->setText(QStringLiteral("未找到范围内的边界节点，当前选择保持不变。"));
        QMessageBox::information(this, QStringLiteral("坐标范围选择"),
                                 QStringLiteral("指定坐标范围内没有找到边界节点。请检查范围和容差。"));
        return;
    }

    const bool append = ui->coordAppendCheckBox->isChecked();
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, solidPointIds, !append);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, linePointIds, !append);

    ui->IDLabel->clear();
    ui->XLabel->clear();
    ui->YLabel->clear();
    ui->ZLabel->clear();
    ui->PrevUuidLabel->clear();
    ui->coordinateResultLabel->setText(
        QStringLiteral("已%1边界节点：实体网格 %2 个，杆网格 %3 个。")
            .arg(append ? QStringLiteral("追加选择") : QStringLiteral("替换选择"))
            .arg(solidPointIds.size())
            .arg(linePointIds.size()));

    qInfo().noquote()
        << QStringLiteral("__RAW__按坐标范围%1边界节点：实体网格 %2，杆网格 %3。")
               .arg(append ? QStringLiteral("追加") : QStringLiteral("选择"))
               .arg(solidPointIds.size())
               .arg(linePointIds.size());
}

QJsonObject DisplacementConstraintWidget::saveJson()
{
    registerConstraintFlags();
    QJsonObject obj;
    obj["ux"] = ui->UXLineEdit->currentData().toString();
    obj["uy"] = ui->UYLineEdit->currentData().toString();
    obj["uz"] = ui->UZLineEdit->currentData().toString();
    QJsonArray prescribed;for(auto* field:m_prescribed)prescribed.append(field->value());
    obj["prescribed_values"] = prescribed;
    obj["rz"]=m_rotation->currentData().toString();obj["prescribed_rz"]=m_rotationValue->value();
    obj["click_pick_enabled"] = ui->pickEnableCheckBox->isChecked();
    obj["whole_patch_on_click"] = ui->smartPatchCheckBox->isChecked();

    QJsonObject coordinateRange;
    coordinateRange["use_x"] = ui->coordXCheckBox->isChecked();
    coordinateRange["use_y"] = ui->coordYCheckBox->isChecked();
    coordinateRange["use_z"] = ui->coordZCheckBox->isChecked();
    coordinateRange["x_min"] = ui->xMinSpinBox->value();
    coordinateRange["x_max"] = ui->xMaxSpinBox->value();
    coordinateRange["y_min"] = ui->yMinSpinBox->value();
    coordinateRange["y_max"] = ui->yMaxSpinBox->value();
    coordinateRange["z_min"] = ui->zMinSpinBox->value();
    coordinateRange["z_max"] = ui->zMaxSpinBox->value();
    coordinateRange["tolerance"] = ui->coordToleranceSpinBox->value();
    coordinateRange["append"] = ui->coordAppendCheckBox->isChecked();
    obj["coordinate_range"] = coordinateRange;
    return obj;
}

void DisplacementConstraintWidget::onSetAllBarsSuspended()
{
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    vtkUnstructuredGrid* lineGrid = pickManager.lineGrid();
    if (!lineGrid || lineGrid->GetNumberOfCells() == 0)
    {
        QMessageBox::information(this, QStringLiteral("没有杆单元"),
                                 QStringLiteral("当前网格中没有可设置的杆单元。"));
        return;
    }

    QSet<vtkIdType> usedPointIds;
    for (vtkIdType cellId = 0; cellId < lineGrid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = lineGrid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 1)
            continue;
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
            usedPointIds.insert(cell->GetPointId(i));
    }

    if (usedPointIds.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("没有杆单元"),
                                 QStringLiteral("当前网格中没有可设置的杆单元。"));
        return;
    }

    QVector<vtkIdType> pointIds;
    pointIds.reserve(usedPointIds.size());
    for (vtkIdType pointId : usedPointIds)
        pointIds.push_back(pointId);
    std::sort(pointIds.begin(), pointIds.end());

    setConstraintValue(ui->UXLineEdit, QStringLiteral("-10000"));
    setConstraintValue(ui->UYLineEdit, QStringLiteral("-10000"));
    setConstraintValue(ui->UZLineEdit, QStringLiteral("-10000"));

    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, {}, true);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, pointIds, true);

    qInfo().noquote()
        << QStringLiteral("__RAW__已自动选择 %1 个杆件节点并设置为悬空。")
               .arg(pointIds.size());
}

void DisplacementConstraintWidget::loadJson(const QJsonObject& obj)
{
    const QSignalBlocker uxBlocker(ui->UXLineEdit);
    const QSignalBlocker uyBlocker(ui->UYLineEdit);
    const QSignalBlocker uzBlocker(ui->UZLineEdit);

    setConstraintValue(ui->UXLineEdit,
                       obj.value("ux").toString(ui->UXLineEdit->currentData().toString()));
    setConstraintValue(ui->UYLineEdit,
                       obj.value("uy").toString(ui->UYLineEdit->currentData().toString()));
    setConstraintValue(ui->UZLineEdit,
                       obj.value("uz").toString(ui->UZLineEdit->currentData().toString()));
    const auto prescribed=obj.value("prescribed_values").toArray();
    setConstraintValue(m_rotation,obj.value("rz").toString(QStringLiteral("1")));
    m_rotationValue->setValue(obj.value("prescribed_rz").toDouble());
    m_rotationValue->setVisible(PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt()==2&&m_rotation->currentData().toString()==QStringLiteral("2"));
    for(int axis=0;axis<3;++axis)
    {
        m_prescribed[axis]->setValue(prescribed.size()>axis?prescribed[axis].toDouble():0.0);
        m_prescribed[axis]->setVisible(axis<PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt()&&isPrescribed(axis));
    }

    ui->pickEnableCheckBox->setChecked(obj.value("click_pick_enabled").toBool(true));
    ui->smartPatchCheckBox->setChecked(obj.value("whole_patch_on_click").toBool(true));

    const QJsonObject coordinateRange = obj.value("coordinate_range").toObject();
    if (!coordinateRange.isEmpty())
    {
        m_coordinateRangeLoaded = true;
        ui->coordXCheckBox->setChecked(coordinateRange.value("use_x").toBool(false));
        ui->coordYCheckBox->setChecked(coordinateRange.value("use_y").toBool(false));
        ui->coordZCheckBox->setChecked(coordinateRange.value("use_z").toBool(false));
        ui->xMinSpinBox->setValue(coordinateRange.value("x_min").toDouble(0.0));
        ui->xMaxSpinBox->setValue(coordinateRange.value("x_max").toDouble(0.0));
        ui->yMinSpinBox->setValue(coordinateRange.value("y_min").toDouble(0.0));
        ui->yMaxSpinBox->setValue(coordinateRange.value("y_max").toDouble(0.0));
        ui->zMinSpinBox->setValue(coordinateRange.value("z_min").toDouble(0.0));
        ui->zMaxSpinBox->setValue(coordinateRange.value("z_max").toDouble(0.0));
        ui->coordToleranceSpinBox->setValue(
            std::max(0.0, coordinateRange.value("tolerance").toDouble(1.0e-6)));
        ui->coordAppendCheckBox->setChecked(coordinateRange.value("append").toBool(false));
    }
    updateCoordinateControlState();
    registerConstraintFlags();
}

void DisplacementConstraintWidget::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), nullptr, this, nullptr);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);

    clearOverlapVisualization(false);

    // 单节点查看模式：离开当前约束节点后，右下角全局约束方向指示器立即消失。
    // PickManager::deactivate() 同时会隐藏当前节点的全部选中点高亮。
    m_constraintPageActive = false;
    clearConstraintVisualization(false);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);

    if (!m_visual)
        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (m_visual)
        m_visual->render();
}

QString DisplacementConstraintWidget::constraintVisualizationPropId() const
{
    return QStringLiteral("physical.constraint.direction.%1").arg(m_uuid);
}


QString DisplacementConstraintWidget::overlapVisualizationPropId() const
{
    return QStringLiteral("physical.constraint.overlap.%1").arg(m_uuid);
}

void DisplacementConstraintWidget::registerConstraintFlags()
{
    if (m_uuid.isEmpty())
        return;
    PickManager::instance().setNodeConstraintFlags(getPageType(), m_uuid,
                                                   uxFlag(), uyFlag(), uzFlag());
}

QString DisplacementConstraintWidget::formatConstraintFlags(const QVector<int>& flags,
                                                             bool conflict) const
{
    if (conflict)
        return QStringLiteral("存在冲突");

    const bool is2D = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2;
    const QString axisNames[3] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};

    QStringList fixedAxes;
    QStringList suspendedAxes;
    const int axisCount = is2D ? 2 : 3;
    for (int axis = 0; axis < axisCount; ++axis)
    {
        const int value = flags.value(axis, 1);
        if (value == 0)
            fixedAxes.push_back(axisNames[axis]);
        else if (value == -10000)
            suspendedAxes.push_back(axisNames[axis]);
    }

    QStringList parts;
    if (!fixedAxes.isEmpty())
        parts.push_back(QStringLiteral("%1 固定").arg(fixedAxes.join(QStringLiteral("、"))));
    if (!suspendedAxes.isEmpty())
        parts.push_back(QStringLiteral("%1 悬空杆件").arg(suspendedAxes.join(QStringLiteral("、"))));
    if (parts.isEmpty())
        return QStringLiteral("自由");
    return parts.join(QStringLiteral("；"));
}

void DisplacementConstraintWidget::clearOverlapVisualization(bool renderNow)
{
    auto* visual = m_visual ? m_visual
                            : ServiceRegistry::instance().getService<VisualWidgetService>();
    if (visual && !m_uuid.isEmpty())
        visual->removeProp(overlapVisualizationPropId(), false);

    m_overlapActor = nullptr;
    m_overlapMapper = nullptr;
    m_overlapGlyph = nullptr;
    m_overlapPolyData = nullptr;
    m_overlapPoints = nullptr;

    if (renderNow && visual)
        visual->render();
}

void DisplacementConstraintWidget::refreshOverlapFeedback(bool renderNow)
{
    if (!ui)
        return;

    registerConstraintFlags();

    PickManager& pickManager = PickManager::instance();
    const QVector<int> selectedIds = pickManager.selectedNodeIds(getPageType(), m_uuid);
    const QVector<int> sharedIds = pickManager.sharedNodeIds(getPageType(), m_uuid);

    const QSet<int> conflictingIds =
        pickManager.conflictingSharedNodeIds(getPageType(), m_uuid);
    const int conflictCount = conflictingIds.size();

    if (conflictCount > 0)
    {
        ui->overlapSummaryLabel->setText(
            QStringLiteral("已选择 %1 个节点；%2 个节点存在约束冲突，请检查。")
                .arg(selectedIds.size())
                .arg(conflictCount));
        ui->overlapSummaryLabel->setStyleSheet(QStringLiteral("color:#c0392b;font-weight:600;"));
    }
    else if (!sharedIds.isEmpty())
    {
        ui->overlapSummaryLabel->setText(
            QStringLiteral("已选择 %1 个节点，其中 %2 个已有其他方向约束，最终约束已自动组合。")
                .arg(selectedIds.size())
                .arg(sharedIds.size()));
        ui->overlapSummaryLabel->setStyleSheet(QString());
    }
    else
    {
        ui->overlapSummaryLabel->setText(
            QStringLiteral("已选择 %1 个节点").arg(selectedIds.size()));
        ui->overlapSummaryLabel->setStyleSheet(QString());
    }

    if (!m_constraintPageActive)
    {
        clearOverlapVisualization(renderNow);
        return;
    }

    if (!m_visual)
        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visual)
        return;

    if (sharedIds.isEmpty())
    {
        clearOverlapVisualization(renderNow);
        return;
    }

    if (!m_overlapPoints)
    {
        m_overlapPoints = vtkSmartPointer<vtkPoints>::New();
        m_overlapPolyData = vtkSmartPointer<vtkPolyData>::New();
        m_overlapPolyData->SetPoints(m_overlapPoints);

        m_overlapGlyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
        m_overlapGlyph->SetInputData(m_overlapPolyData);

        m_overlapMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        m_overlapMapper->SetInputConnection(m_overlapGlyph->GetOutputPort());
        m_overlapMapper->SetColorModeToDirectScalars();
        m_overlapMapper->ScalarVisibilityOn();

        m_overlapActor = vtkSmartPointer<vtkActor>::New();
        m_overlapActor->SetMapper(m_overlapMapper);
        m_overlapActor->PickableOff();
        m_overlapActor->GetProperty()->SetPointSize(7.0);
        m_overlapActor->GetProperty()->SetRenderPointsAsSpheres(true);
        m_overlapActor->GetProperty()->LightingOff();

        m_visual->removeProp(overlapVisualizationPropId(), false);
        m_visual->addProp(overlapVisualizationPropId(), m_overlapActor,
                          QStringLiteral("{constraint} 共享节点"),
                          QStringLiteral("当前位移约束与其他位移约束共享的节点"), true, true);
    }

    m_overlapPoints->Reset();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("constraint_overlap_color");
    colors->SetNumberOfComponents(3);

    auto appendPoints = [&](vtkUnstructuredGrid* grid, const QVector<vtkIdType>& pointIds)
    {
        if (!grid || !grid->GetPointData())
            return;

        vtkIntArray* nodeIds = vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
        if (!nodeIds)
            nodeIds = vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("NodeId"));

        for (vtkIdType pointId : pointIds)
        {
            if (pointId < 0 || pointId >= grid->GetNumberOfPoints())
                continue;
            double p[3] = {0.0, 0.0, 0.0};
            grid->GetPoint(pointId, p);
            m_overlapPoints->InsertNextPoint(p);

            bool conflict = false;
            if (nodeIds && pointId < nodeIds->GetNumberOfTuples())
                conflict = conflictingIds.contains(nodeIds->GetValue(int(pointId)));

            const unsigned char orange[3] = {255, 140, 0};
            const unsigned char red[3] = {220, 45, 35};
            colors->InsertNextTypedTuple(conflict ? red : orange);
        }
    };

    appendPoints(pickManager.solidGrid(),
                 pickManager.sharedSelectedPointIds(getPageType(), m_uuid, GridKind::Solid));
    appendPoints(pickManager.lineGrid(),
                 pickManager.sharedSelectedPointIds(getPageType(), m_uuid, GridKind::Line));

    // 共享但兼容：橙色；真正不兼容：红色。每个点独立着色，不会因为一个冲突
    // 把整条共享边都误显示成错误。
    m_overlapPolyData->GetPointData()->SetScalars(colors);
    m_overlapActor->SetVisibility(m_overlapPoints->GetNumberOfPoints() > 0);

    colors->Modified();
    m_overlapPoints->Modified();
    m_overlapPolyData->Modified();
    m_overlapGlyph->Update();

    if (renderNow)
        m_visual->render();
}

void DisplacementConstraintWidget::clearConstraintVisualization(bool renderNow)
{
    // v10 曾把约束状态作为 vtkTextActor 注册到主渲染器。这里顺手清理旧 prop，
    // 便于开发时从旧版本热切换，也避免重复残留。
    auto* visual = m_visual ? m_visual
                            : ServiceRegistry::instance().getService<VisualWidgetService>();
    if (visual && !m_uuid.isEmpty())
        visual->removeProp(constraintVisualizationPropId(), false);

    if (m_constraintMarkerWidget)
    {
        m_constraintMarkerWidget->SetEnabled(0);
        m_constraintMarkerWidget->SetInteractor(nullptr);
    }
    m_constraintMarkerWidget = nullptr;
    m_constraintMarkerAssembly = nullptr;

    if (renderNow && visual)
        visual->render();
}

namespace
{
static vtkSmartPointer<vtkActor> makeConstraintStopPlate(int axis, bool darkTheme)
{
    // 固定方向不是“仍能向前的箭头 + 一个标记”，而应该明确表达“到这里被挡住”。
    // vtkAxesActor 的轴杆占总长度的 72%，箭头占后 28%。固定方向隐藏箭头，
    // 再把一块薄挡板放在轴杆末端，使视觉语义变成：原点 ---- [挡板]。
    constexpr double totalLength = 0.86;
    constexpr double normalizedShaftLength = 0.72;
    constexpr double shaftEnd = totalLength * normalizedShaftLength;
    constexpr double plateThickness = 0.055;
    constexpr double plateSize = 0.30;
    constexpr double halfThickness = plateThickness * 0.5;
    constexpr double halfSize = plateSize * 0.5;

    auto plate = vtkSmartPointer<vtkCubeSource>::New();
    if (axis == 0)
    {
        plate->SetBounds(shaftEnd - halfThickness, shaftEnd + halfThickness,
                         -halfSize, halfSize,
                         -halfSize, halfSize);
    }
    else if (axis == 1)
    {
        plate->SetBounds(-halfSize, halfSize,
                         shaftEnd - halfThickness, shaftEnd + halfThickness,
                         -halfSize, halfSize);
    }
    else
    {
        plate->SetBounds(-halfSize, halfSize,
                         -halfSize, halfSize,
                         shaftEnd - halfThickness, shaftEnd + halfThickness);
    }

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(plate->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();

    // 挡板主体使用中性灰，避免与 X/Y/Z 轴本身争抢颜色；边框仍跟随轴颜色，
    // 既能看出“这是一个实体挡板”，又能立即识别它属于哪个全局方向。
    static const double axisColors[3][3] = {
        {0.90, 0.24, 0.20},
        {0.20, 0.72, 0.32},
        {0.20, 0.42, 0.90}};
    const double face = darkTheme ? 0.62 : 0.72;

    vtkProperty* property = actor->GetProperty();
    property->SetColor(face, face, face);
    property->SetOpacity(0.96);
    property->EdgeVisibilityOn();
    property->SetEdgeColor(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2]);
    property->SetLineWidth(2.5);
    property->LightingOff();
    return actor;
}

static void setAxisVisualState(vtkAxesActor* axes, int axis, bool fixed, bool darkTheme)
{
    if (!axes)
        return;

    vtkProperty* shaft = nullptr;
    vtkProperty* tip = nullptr;
    vtkCaptionActor2D* caption = nullptr;
    if (axis == 0)
    {
        shaft = axes->GetXAxisShaftProperty();
        tip = axes->GetXAxisTipProperty();
        caption = axes->GetXAxisCaptionActor2D();
    }
    else if (axis == 1)
    {
        shaft = axes->GetYAxisShaftProperty();
        tip = axes->GetYAxisTipProperty();
        caption = axes->GetYAxisCaptionActor2D();
    }
    else
    {
        shaft = axes->GetZAxisShaftProperty();
        tip = axes->GetZAxisTipProperty();
        caption = axes->GetZAxisCaptionActor2D();
    }

    // 自由方向是完整箭头；固定方向必须“被挡住”：保留轴杆、隐藏箭头尖端，
    // 由轴杆末端的实体挡板终止该方向。这样不需要猜图标含义。
    if (shaft)
    {
        shaft->SetOpacity(1.0);
        shaft->SetLineWidth(fixed ? 3.2 : 2.2);
    }
    if (tip)
        tip->SetOpacity(fixed ? 0.0 : 1.0);

    if (caption && caption->GetCaptionTextProperty())
    {
        vtkTextProperty* text = caption->GetCaptionTextProperty();
        const double v = darkTheme ? 0.94 : 0.16;
        text->SetColor(v, v, v);
        text->SetOpacity(1.0);
        text->SetBold(fixed ? 1 : 0);
        text->SetShadow(0);
    }
}
}

void DisplacementConstraintWidget::refreshConstraintVisualization(bool renderNow)
{
    if (m_uuid.isEmpty())
        return;

    if (!m_visual)
        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visual)
        return;

    // 只在当前位移约束节点被打开时显示。约束作用范围仍由 PickManager 完整高亮；
    // 本指示器只负责表达“全局哪个方向被约束”，不在模型上寻找锚点、不铺采样符号。
    if (!m_constraintPageActive)
    {
        clearConstraintVisualization(renderNow);
        return;
    }

    vtkRenderWindowInteractor* interactor = m_visual->interactor();
    if (!interactor)
    {
        clearConstraintVisualization(renderNow);
        return;
    }

    // 每次方向状态变化时重建这个很小的 marker。组件数量固定且极少，
    // 比在模型空间维护大量 glyph 更简单，也不会随网格规模增长。
    clearConstraintVisualization(false);

    const bool is2D = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2;
    const bool darkTheme = StyleManager::currentThemeName() == QStringLiteral("dark");

    const int flags[3] = {uxFlag(), uyFlag(), uzFlag()};
    const bool activeAxis[3] = {true, true, !is2D};

    auto axes = vtkSmartPointer<vtkAxesActor>::New();
    axes->PickableOff();
    axes->SetShaftTypeToCylinder();
    axes->SetTipTypeToCone();
    axes->SetTotalLength(0.86, 0.86, is2D ? 0.0 : 0.86);
    axes->SetNormalizedShaftLength(0.72, 0.72, 0.72);
    axes->SetNormalizedTipLength(0.28, 0.28, 0.28);
    axes->SetCylinderRadius(0.042);
    axes->SetConeRadius(0.12);
    axes->SetAxisLabels(1);

    m_constraintMarkerAssembly = vtkSmartPointer<vtkAssembly>::New();
    m_constraintMarkerAssembly->PickableOff();
    m_constraintMarkerAssembly->AddPart(axes);

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!activeAxis[axis])
            continue;

        const bool fixed = (flags[axis] == 0);
        setAxisVisualState(axes, axis, fixed, darkTheme);
        if (fixed)
            m_constraintMarkerAssembly->AddPart(makeConstraintStopPlate(axis, darkTheme));
    }

    // 固定方向的箭头已经在挡板处终止；自由方向仍是完整箭头。

    // 二维模型不显示 Z 文本，避免产生“Z 也参与约束”的误解。
    if (is2D)
    {
        if (auto* zCaption = axes->GetZAxisCaptionActor2D())
            zCaption->SetVisibility(0);
    }

    m_constraintMarkerWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_constraintMarkerWidget->SetOrientationMarker(m_constraintMarkerAssembly);
    m_constraintMarkerWidget->SetInteractor(interactor);

    // 主视图已有全局坐标轴在左下角；约束方向指示器放右下角并适当放大，避免重叠且保证可读性。
    // vtkOrientationMarkerWidget 会自动使用主视图相机方向，因此模型旋转时它同步旋转，
    // 但屏幕位置和尺寸保持稳定。
    m_constraintMarkerWidget->SetViewport(0.78, 0.02, 0.98, 0.22);
    const double outline = darkTheme ? 0.78 : 0.28;
    m_constraintMarkerWidget->SetOutlineColor(outline, outline, outline);
    // vtkOrientationMarkerWidget 要求先 SetInteractor + SetEnabled，再修改 Interaction。
    // 否则 VTK 9.2 会持续输出：Set interactor and Enabled before changing interaction.
    m_constraintMarkerWidget->SetEnabled(1);
    m_constraintMarkerWidget->InteractiveOff();

    if (renderNow)
        m_visual->render();
}

int DisplacementConstraintWidget::uxFlag()
{
    if(isPrescribed(0))return 0;
    return ui->UXLineEdit->currentData().toInt();
}

int DisplacementConstraintWidget::uyFlag()
{
    if(isPrescribed(1))return 0;
    return ui->UYLineEdit->currentData().toInt();
}

int DisplacementConstraintWidget::uzFlag()
{
    if(isPrescribed(2))return 0;
    return ui->UZLineEdit->currentData().toInt();
}

bool DisplacementConstraintWidget::isPrescribed(int axis) const
{
    if(axis==2&&PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt()==2)
        return m_rotation&&m_rotation->currentData().toString()==QStringLiteral("2");
    const std::array<QComboBox*,3> combos={ui->UXLineEdit,ui->UYLineEdit,ui->UZLineEdit};
    return axis>=0&&axis<3&&combos[axis]->currentData().toString()==QStringLiteral("2");
}

bool DisplacementConstraintWidget::hasPrescribedDisplacement() const
{
    const int dimension=PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    for(int axis=0;axis<dimension;++axis)if(isPrescribed(axis))return true;
    if(dimension==2&&isPrescribed(2))return true;
    return false;
}

double DisplacementConstraintWidget::prescribedValue(int axis) const
{
    if(axis==2&&PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt()==2)return m_rotationValue->value();
    return axis>=0&&axis<3?m_prescribed[axis]->value():0.0;
}

int DisplacementConstraintWidget::rotationFlag() const
{return isPrescribed(2)?0:m_rotation->currentData().toInt();}

void DisplacementConstraintWidget::onNodePicked(const QString& domain, const QString& activeUuid,
                                                GridKind kind, vtkIdType pointId, int nodeId,
                                                double x, double y, double z,
                                                const QString& /*prevUuid*/)
{
    if (domain != getPageType())
        return;
    if (activeUuid != m_uuid)
        return;

    if (ui->pickEnableCheckBox->isChecked() && ui->smartPatchCheckBox->isChecked())
    {
        PickManager& pickManager = PickManager::instance();
        vtkUnstructuredGrid* grid =
            (kind == GridKind::Solid) ? pickManager.solidGrid() : pickManager.lineGrid();
        vtkRenderer* renderer = m_visual ? m_visual->renderer() : nullptr;
        const QVector<vtkIdType> patchIds = smartConnectedBoundaryPatch(grid, pointId, renderer);
        if (!patchIds.isEmpty())
        {
            // PickManager 已先完成种子节点的切换。以种子点切换后的状态决定整片是“选中”还是“取消”，
            // 这样再次点击已选整片不会出现“种子取消后又被整片重新加回”的反直觉行为。
            const QVector<vtkIdType> afterSeed =
                pickManager.getSelectedPointIds(getPageType(), m_uuid, kind);
            const bool selectWholePatch = std::find(afterSeed.begin(), afterSeed.end(), pointId) !=
                                          afterSeed.end();

            if (selectWholePatch)
            {
                pickManager.setSelectedPointIds(getPageType(), m_uuid, kind, patchIds, false);
            }
            else
            {
                QSet<vtkIdType> remaining;
                remaining.reserve(afterSeed.size());
                for (vtkIdType pid : afterSeed)
                    remaining.insert(pid);
                for (vtkIdType pid : patchIds)
                    remaining.remove(pid);

                QVector<vtkIdType> remainingIds;
                remainingIds.reserve(remaining.size());
                for (vtkIdType pid : remaining)
                    remainingIds.push_back(pid);
                std::sort(remainingIds.begin(), remainingIds.end());
                pickManager.setSelectedPointIds(getPageType(), m_uuid, kind, remainingIds, true);
            }

            qInfo().noquote()
                << QStringLiteral("__RAW__点选整片：已%1 %2 个连续边界节点。")
                       .arg(selectWholePatch ? QStringLiteral("选择") : QStringLiteral("取消"))
                       .arg(patchIds.size());
        }
    }

    // 基础节点信息仍按原界面显示。
    ui->IDLabel->setText(QString::number(nodeId));
    ui->XLabel->setText(QString::number(x, 'g', 12));
    ui->YLabel->setText(QString::number(y, 'g', 12));
    ui->ZLabel->setText(QString::number(z, 'g', 12));

    PickManager& pickManager = PickManager::instance();
    bool conflict = false;
    const QVector<int> effective =
        pickManager.effectiveNodeConstraintFlags(getPageType(), nodeId, &conflict);
    ui->PrevUuidLabel->setText(formatConstraintFlags(effective, conflict));

    const QStringList sourceUuids = pickManager.selectedNodeUuids(getPageType(), nodeId);
    QStringList sourceTexts;
    auto* pf = ServiceRegistry::instance().getService<PhysicalFieldService>();
    for (const QString& sourceUuid : sourceUuids)
    {
        QString sourceName = pf ? pf->pageTitleByUuid(sourceUuid) : QString();
        if (sourceName.trimmed().isEmpty())
            sourceName = sourceUuid;

        bool sourceConflict = false;
        const QVector<int> sourceFlags = pickManager.nodeConstraintFlags(getPageType(), sourceUuid);
        sourceTexts.push_back(QStringLiteral("%1（%2）")
                                  .arg(sourceName, formatConstraintFlags(sourceFlags, sourceConflict)));
    }
    ui->OverlapSourceLabel->setText(sourceTexts.join(QStringLiteral("；")));

    refreshOverlapFeedback();
}
