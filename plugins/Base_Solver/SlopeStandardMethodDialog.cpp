#include <CompactDoubleSpinBox.h>
#include "SlopeStandardMethodDialog.h"
#include "SlopeSectionDisplay.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <ProjectNode.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLineF>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkContourTriangulator.h>
#include <vtkAppendPolyData.h>
#include <vtkCutter.h>
#include <vtkDataArray.h>
#include <vtkFeatureEdges.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkPlane.h>
#include <vtkProp.h>
#include <vtkPropCollection.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace
{
struct PlotPoint
{
    double x = 0.0;
    double y = 0.0;
};

QVector<PlotPoint> jsonPoints(const QJsonValue& value)
{
    QVector<PlotPoint> points;
    const QJsonArray array = value.toArray();
    points.reserve(array.size());
    for (const QJsonValue& item : array)
    {
        const QJsonObject point = item.toObject();
        const double x = point.value(QStringLiteral("x")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        const double y = point.value(QStringLiteral("y")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(x) && std::isfinite(y))
            points.push_back({x, y});
    }
    return points;
}

QString methodDisplayName(const QString& method)
{
    if (method == QStringLiteral("swedish"))
        return QStringLiteral("瑞典圆弧法");
    if (method == QStringLiteral("bishop"))
        return QStringLiteral("毕肖普法");
    if (method == QStringLiteral("sarma"))
        return QStringLiteral("Sarma 法");
    return method;
}

QString modeDisplayName(const QString& mode)
{
    if (mode == QStringLiteral("fixed"))
        return QStringLiteral("指定滑弧");
    if (mode == QStringLiteral("search"))
        return QStringLiteral("自动搜索");
    return mode;
}

QString displayNumber(double value)
{
    return QString::number(value, 'g', 12);
}

QString sanitizedUuid(QString uuid)
{
    uuid.remove(QLatin1Char('{'));
    uuid.remove(QLatin1Char('}'));
    for (QChar& ch : uuid)
    {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    }
    return uuid.isEmpty() ? QStringLiteral("slope") : uuid;
}

QString axisName(int axis)
{
    static const QStringList names{QStringLiteral("X"), QStringLiteral("Y"),
                                   QStringLiteral("Z")};
    return names.value(axis, QStringLiteral("?"));
}

struct SectionPoint
{
    double x = 0.0;
    double y = 0.0;
};

struct SectionCellUse
{
    int material = 0;
    SectionPoint centroid;
};

struct SectionEdge
{
    int a = -1;
    int b = -1;
    int material = 0;
    bool surface = false;
};

struct SectionChain
{
    QVector<int> nodes;
    QVector<int> materials;
};

struct SectionExtractionResult
{
    SlopeStandardDatModel model;
    QStringList materialNames;
    QVector<int> sourceMaterialIds;
    int disconnectedSurfaceCount{0};
    QVector<double> surfaceCandidateSpans;
    int selectedSurfaceCandidate{0};
    bool horizontalAxisReversed{false};
    double horizontalMirrorMinimum{0.0};
    double horizontalMirrorMaximum{0.0};
    bool toleranceRaised{false};
    double finalTolerance{0.0};
};

double pointSegmentDistance(const SectionPoint& point,
                            const SectionPoint& a,
                            const SectionPoint& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    if (length2 <= std::numeric_limits<double>::epsilon())
        return std::hypot(point.x - a.x, point.y - a.y);
    const double t = std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length2,
                                0.0, 1.0);
    return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

void markRdpPoints(const QVector<int>& nodes,
                   const QVector<SectionPoint>& points,
                   int first,
                   int last,
                   double tolerance,
                   QVector<bool>* keep)
{
    if (!keep || last <= first + 1)
        return;
    double maximumDistance = -1.0;
    int split = -1;
    const SectionPoint& a = points.at(nodes.at(first));
    const SectionPoint& b = points.at(nodes.at(last));
    for (int i = first + 1; i < last; ++i)
    {
        const double distance = pointSegmentDistance(points.at(nodes.at(i)), a, b);
        if (distance > maximumDistance)
        {
            maximumDistance = distance;
            split = i;
        }
    }
    if (split >= 0 && maximumDistance > tolerance)
    {
        (*keep)[split] = true;
        markRdpPoints(nodes, points, first, split, tolerance, keep);
        markRdpPoints(nodes, points, split, last, tolerance, keep);
    }
}

QVector<SectionChain> traceSectionChains(const QVector<SectionEdge>& edges)
{
    QVector<SectionChain> chains;
    if (edges.isEmpty())
        return chains;

    QMap<int, QVector<int>> adjacency;
    for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
    {
        adjacency[edges.at(edgeIndex).a].append(edgeIndex);
        adjacency[edges.at(edgeIndex).b].append(edgeIndex);
    }
    QVector<bool> used(edges.size(), false);

    const auto trace = [&](int startNode, int firstEdge, QVector<bool>* edgeUsed)
    {
        SectionChain chain;
        int node = startNode;
        int edgeIndex = firstEdge;
        chain.nodes.append(node);
        while (edgeIndex >= 0 && !edgeUsed->at(edgeIndex))
        {
            (*edgeUsed)[edgeIndex] = true;
            const SectionEdge& edge = edges.at(edgeIndex);
            const int nextNode = edge.a == node ? edge.b : edge.a;
            chain.materials.append(edge.material);
            chain.nodes.append(nextNode);
            node = nextNode;
            edgeIndex = -1;
            for (int candidate : adjacency.value(node))
            {
                if (!edgeUsed->at(candidate))
                {
                    edgeIndex = candidate;
                    break;
                }
            }
            if (node == startNode)
                break;
        }
        return chain;
    };

    for (auto it = adjacency.constBegin(); it != adjacency.constEnd(); ++it)
    {
        if (it.value().size() == 2)
            continue;
        for (int edgeIndex : it.value())
            if (!used.at(edgeIndex))
                chains.append(trace(it.key(), edgeIndex, &used));
    }
    for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
        if (!used.at(edgeIndex))
            chains.append(trace(edges.at(edgeIndex).a, edgeIndex, &used));
    return chains;
}

QVector<SectionEdge> simplifySectionChain(const SectionChain& chain,
                                          const QVector<SectionPoint>& points,
                                          double tolerance,
                                          bool surface)
{
    QVector<SectionEdge> result;
    if (chain.nodes.size() < 2 || chain.materials.size() + 1 != chain.nodes.size())
        return result;

    int firstEdge = 0;
    while (firstEdge < chain.materials.size())
    {
        int lastEdge = firstEdge;
        while (lastEdge + 1 < chain.materials.size()
               && chain.materials.at(lastEdge + 1) == chain.materials.at(firstEdge))
            ++lastEdge;
        const int firstNode = firstEdge;
        const int lastNode = lastEdge + 1;
        QVector<bool> keep(chain.nodes.size(), false);
        keep[firstNode] = true;
        keep[lastNode] = true;
        markRdpPoints(chain.nodes, points, firstNode, lastNode,
                      std::max(0.0, tolerance), &keep);
        int previous = firstNode;
        for (int i = firstNode + 1; i <= lastNode; ++i)
        {
            if (!keep.at(i))
                continue;
            result.append({chain.nodes.at(previous), chain.nodes.at(i),
                           chain.materials.at(firstEdge), surface});
            previous = i;
        }
        firstEdge = lastEdge + 1;
    }
    return result;
}

double chainHorizontalSpan(const SectionChain& chain,
                           const QVector<SectionPoint>& points)
{
    if (chain.nodes.isEmpty())
        return 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (int node : chain.nodes)
    {
        minimum = std::min(minimum, points.at(node).x);
        maximum = std::max(maximum, points.at(node).x);
    }
    return maximum - minimum;
}

bool finalizeSection(const QVector<SectionPoint>& sourcePoints,
                     const QVector<SectionEdge>& rawSurfaceEdges,
                     const QVector<SectionEdge>& rawInterfaceEdges,
                     const QMap<int, QString>& materialLabels,
                     double requestedTolerance,
                     double span,
                     int preferredSurfaceCandidate,
                     SectionExtractionResult* output,
                     QString* error)
{
    if (!output)
        return false;
    Q_UNUSED(materialLabels);
    QVector<SectionPoint> points = sourcePoints;
    QVector<SectionChain> surfaceChains = traceSectionChains(rawSurfaceEdges);
    if (surfaceChains.isEmpty())
    {
        if (error)
            *error = QStringLiteral("未识别到有效坡面，请调整断面位置或方向。");
        return false;
    }

    QVector<int> surfaceOrder;
    surfaceOrder.reserve(surfaceChains.size());
    const double significantSpan = std::max(span * 1.0e-5, 1.0e-7);
    for (int i = 0; i < surfaceChains.size(); ++i)
    {
        // 数值碎边或近竖直端壁不应该被暴露成“坡面 2/3/4…”。
        // 只保留具有实际水平跨度的地表链；真正不连通的两个边坡仍会分别保留。
        if (chainHorizontalSpan(surfaceChains.at(i), points) > significantSpan)
            surfaceOrder.append(i);
    }
    if (surfaceOrder.isEmpty())
    {
        if (error)
            *error = QStringLiteral("未识别到完整坡面，请调整断面位置或方向。");
        return false;
    }
    std::sort(surfaceOrder.begin(), surfaceOrder.end(), [&](int lhs, int rhs)
    {
        return chainHorizontalSpan(surfaceChains.at(lhs), points)
               > chainHorizontalSpan(surfaceChains.at(rhs), points);
    });
    output->surfaceCandidateSpans.clear();
    for (int index : surfaceOrder)
        output->surfaceCandidateSpans.append(chainHorizontalSpan(surfaceChains.at(index), points));
    const int selectedRank = std::clamp(preferredSurfaceCandidate, 0, static_cast<int>(surfaceOrder.size()) - 1);
    const int selectedChain = surfaceOrder.at(selectedRank);
    output->selectedSurfaceCandidate = selectedRank;
    SectionChain surfaceChain = surfaceChains.at(selectedChain);

    // 断面左右两端的竖直边只是用于封闭二维截面，并不是条分法意义下的
    // “外坡面”。VTK 切割后这类端壁有时会因为浮点误差带一个极小 dx，
    // 因而漏过前面的竖直边过滤。这里在进入求解器模型前再次按水平跨度
    // 修剪链首/链尾的近竖直端壁；坡面中间的竖直台阶仍然保留并在后续
    // 校验中明确报错，避免偷偷改变真实坡形。
    const double terminalVerticalTolerance = std::max(span * 1.0e-6, 1.0e-8);
    const auto trimTerminalVerticalEdges = [&]()
    {
        while (surfaceChain.nodes.size() >= 2 && !surfaceChain.materials.isEmpty())
        {
            const SectionPoint& a = points.at(surfaceChain.nodes.at(0));
            const SectionPoint& b = points.at(surfaceChain.nodes.at(1));
            if (std::abs(b.x - a.x) > terminalVerticalTolerance)
                break;
            surfaceChain.nodes.removeAt(0);
            surfaceChain.materials.removeAt(0);
        }
        while (surfaceChain.nodes.size() >= 2 && !surfaceChain.materials.isEmpty())
        {
            const int last = surfaceChain.nodes.size() - 1;
            const SectionPoint& a = points.at(surfaceChain.nodes.at(last - 1));
            const SectionPoint& b = points.at(surfaceChain.nodes.at(last));
            if (std::abs(b.x - a.x) > terminalVerticalTolerance)
                break;
            surfaceChain.nodes.removeLast();
            surfaceChain.materials.removeLast();
        }
    };
    trimTerminalVerticalEdges();
    if (surfaceChain.nodes.size() < 2 || surfaceChain.materials.isEmpty())
    {
        if (error)
            *error = QStringLiteral("当前断面无法形成有效计算坡面，请调整方向或位置。");
        return false;
    }

    if (points.at(surfaceChain.nodes.first()).x > points.at(surfaceChain.nodes.last()).x)
    {
        std::reverse(surfaceChain.nodes.begin(), surfaceChain.nodes.end());
        std::reverse(surfaceChain.materials.begin(), surfaceChain.materials.end());
    }

    const double mergeTolerance = std::max(span * 1.0e-7, 1.0e-8);
    bool reversed = false;
    double sectionMinX = 0.0;
    double sectionMaxX = 0.0;
    if (!surfaceChain.nodes.isEmpty())
    {
        sectionMinX = points.at(surfaceChain.nodes.first()).x;
        sectionMaxX = sectionMinX;
        for (const int node : surfaceChain.nodes)
        {
            sectionMinX = std::min(sectionMinX, points.at(node).x);
            sectionMaxX = std::max(sectionMaxX, points.at(node).x);
        }
    }
    if (surfaceChain.nodes.size() >= 2)
    {
        const SectionPoint& leftEnd = points.at(surfaceChain.nodes.first());
        const SectionPoint& rightEnd = points.at(surfaceChain.nodes.last());
        if (leftEnd.y > rightEnd.y + mergeTolerance)
        {
            for (SectionPoint& point : points)
                point.x = sectionMinX + sectionMaxX - point.x;
            std::reverse(surfaceChain.nodes.begin(), surfaceChain.nodes.end());
            std::reverse(surfaceChain.materials.begin(), surfaceChain.materials.end());
            reversed = true;
        }
    }

    double simplificationTolerance = std::max(0.0, requestedTolerance);
    QVector<SectionEdge> surfaceEdges =
        simplifySectionChain(surfaceChain, points, simplificationTolerance, true);
    bool toleranceRaised = false;
    for (int attempt = 0; surfaceEdges.size() > 50 && attempt < 20; ++attempt)
    {
        simplificationTolerance = std::max(simplificationTolerance * 1.8,
                                           std::max(span * 1.0e-8, 1.0e-8));
        surfaceEdges = simplifySectionChain(surfaceChain, points,
                                            simplificationTolerance, true);
        toleranceRaised = true;
    }
    if (surfaceEdges.isEmpty() || surfaceEdges.size() > 50)
    {
        if (error)
            *error = QStringLiteral("计算坡面分段过多，请提高折线简化容差。");
        return false;
    }

    QVector<SectionEdge> interfaceEdges;
    QMap<int, QVector<SectionEdge>> interfacesByMaterial;
    for (const SectionEdge& edge : rawInterfaceEdges)
        interfacesByMaterial[edge.material].append(edge);
    for (auto it = interfacesByMaterial.constBegin(); it != interfacesByMaterial.constEnd(); ++it)
        for (const SectionChain& chain : traceSectionChains(it.value()))
            interfaceEdges += simplifySectionChain(chain, points,
                                                   simplificationTolerance, false);

    QVector<SectionEdge> allEdges = surfaceEdges;
    allEdges += interfaceEdges;
    QSet<int> sourceMaterials;
    for (const SectionEdge& edge : allEdges)
        sourceMaterials.insert(edge.material);
    QList<int> orderedMaterials = sourceMaterials.values();
    std::sort(orderedMaterials.begin(), orderedMaterials.end());
    if (orderedMaterials.isEmpty())
    {
        if (error)
            *error = QStringLiteral("断面没有得到可用材料区域。");
        return false;
    }

    QMap<int, int> materialToSoil;
    for (int i = 0; i < orderedMaterials.size(); ++i)
        materialToSoil.insert(orderedMaterials.at(i), i + 1);

    SlopeStandardDatModel model;
    model.projectTitle = QStringLiteral("边坡极限平衡工况");
    model.method = 2;
    model.mode = 1;
    model.searchStep = 0.2;
    model.sliceCount = 50;
    model.waterUnitWeight = 9.81;
    model.outsideWaterLevel = 9999.0;
    model.earthquakeEnabled = 0;

    for (int i = 0; i < orderedMaterials.size(); ++i)
    {
        SlopeStandardSoil soil;
        soil.id = i + 1;
        soil.poreMethod = -1;
        // Deliberately leave engineering parameters at zero. The UI will show
        // blank cells and force the user to provide real material data.
        model.soils.append(soil);
        const int sourceMaterial = orderedMaterials.at(i);
        output->sourceMaterialIds.append(sourceMaterial);
        // 分区编号只用于几何对应，不从工程材料模块继承名称或参数。
        output->materialNames.append(QString());
    }

    QMap<int, int> nodeIds;
    const auto ensureNode = [&](int sourceNode)
    {
        if (nodeIds.contains(sourceNode))
            return nodeIds.value(sourceNode);
        const int id = nodeIds.size() + 1;
        const SectionPoint& point = points.at(sourceNode);
        model.nodes.append({id, point.x, point.y});
        nodeIds.insert(sourceNode, id);
        return id;
    };
    for (const SectionEdge& edge : allEdges)
        model.segments.append({ensureNode(edge.a), ensureNode(edge.b),
                               materialToSoil.value(edge.material)});
    for (int i = 0; i < surfaceEdges.size(); ++i)
        model.surfaceLoads.append({i + 1, 0.0});

    if (!model.nodes.isEmpty())
    {
        double minY = model.nodes.first().y;
        double maxY = minY;
        for (const auto& node : model.nodes)
        {
            minY = std::min(minY, node.y);
            maxY = std::max(maxY, node.y);
        }
        model.slopeHeight = std::max(0.0, maxY - minY);
    }

    output->model = model;
    output->disconnectedSurfaceCount = surfaceOrder.size();
    output->horizontalAxisReversed = reversed;
    output->horizontalMirrorMinimum = sectionMinX;
    output->horizontalMirrorMaximum = sectionMaxX;
    output->toleranceRaised = toleranceRaised;
    output->finalTolerance = simplificationTolerance;
    return true;
}

bool fillMaterialArray(MeshModuleControllerService* meshService,
                       vtkUnstructuredGrid* grid,
                       const QString& source,
                       vtkIntArray* materialArray,
                       QMap<int, QString>* labels,
                       QString* error)
{
    if (!meshService || !grid || !materialArray || !labels)
        return false;
    materialArray->SetName("__SlopeSectionMaterial");
    materialArray->SetNumberOfTuples(grid->GetNumberOfCells());
    materialArray->FillValue(0);

    if (source == QStringLiteral("uniform"))
    {
        materialArray->FillValue(1);
        // 这里只识别断面区域，不继承任何工程材料名称或参数。
        labels->insert(1, QString());
    }
    else if (source.startsWith(QStringLiteral("tag:")))
    {
        const QString category = source.mid(4);
        for (const MeshTagSet& set : meshService->domainTagSets())
        {
            if (set.key.category.compare(category, Qt::CaseInsensitive) != 0
                || set.key.id == 0)
                continue;
            for (MeshIdType cellId : meshService->cellIdsOfTag(set.key))
                if (cellId >= 0 && cellId < grid->GetNumberOfCells())
                    materialArray->SetValue(static_cast<vtkIdType>(cellId), set.key.id);
            // Tag 只作为断面分区编号使用。材料参数由用户手动填写。
            labels->insert(set.key.id, QString());
        }
    }
    else if (source.startsWith(QStringLiteral("array:")))
    {
        const QString arrayName = source.mid(6);
        vtkDataArray* sourceArray = vtkDataArray::SafeDownCast(
            grid->GetCellData()->GetArray(arrayName.toUtf8().constData()));
        if (sourceArray)
        {
            const vtkIdType count = std::min(grid->GetNumberOfCells(),
                                             sourceArray->GetNumberOfTuples());
            for (vtkIdType cellId = 0; cellId < count; ++cellId)
            {
                const int material = qRound(sourceArray->GetComponent(cellId, 0));
                materialArray->SetValue(cellId, material);
                if (material > 0 && !labels->contains(material))
                    labels->insert(material, QString());
            }
        }
    }

    // 材料管理模块当前不参与极限平衡封装。这里仅保留区域编号，
    // φ、c、γ、γsat、Ru 由用户在第二步手动输入。

    bool hasMaterial = false;
    for (vtkIdType cellId = 0; cellId < materialArray->GetNumberOfTuples(); ++cellId)
        if (materialArray->GetValue(cellId) > 0)
        {
            hasMaterial = true;
            break;
        }
    if (!hasMaterial && error)
        *error = QStringLiteral("当前分区方式无法识别区域，请更换分区方式。");
    return hasMaterial;
}

QJsonArray stringListToJson(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QStringList jsonToStringList(const QJsonArray& array)
{
    QStringList values;
    for (const QJsonValue& value : array)
        values.append(value.toString());
    return values;
}

QJsonArray intVectorToJson(const QVector<int>& values)
{
    QJsonArray array;
    for (int value : values)
        array.append(value);
    return array;
}

QVector<int> jsonToIntVector(const QJsonArray& array)
{
    QVector<int> values;
    values.reserve(array.size());
    for (const QJsonValue& value : array)
        values.append(value.toInt());
    return values;
}
}

class SlopeStandardInputPreviewWidget final : public QWidget
{
  public:
    explicit SlopeStandardInputPreviewWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(420, 360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setModel(const SlopeStandardDatModel& model)
    {
        m_model = model;
        m_error.clear();
        update();
    }

    void setError(const QString& error)
    {
        m_error = error;
        update();
    }

    void setSurfaceToggleHandler(std::function<void(int)> handler)
    {
        m_toggleHandler = std::move(handler);
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (!m_toggleHandler || event->button() != Qt::LeftButton)
            return QWidget::mousePressEvent(event);
        double best = 12.0;
        int bestSegment = -1;
        for (int i = 0; i < m_segmentLines.size(); ++i)
        {
            const QLineF& line = m_segmentLines.at(i);
            const QPointF p = event->position();
            const double dx = line.dx();
            const double dy = line.dy();
            const double len2 = dx * dx + dy * dy;
            double distance = 1.0e9;
            if (len2 > 1.0e-12)
            {
                const double t = std::clamp(((p.x() - line.x1()) * dx
                                             + (p.y() - line.y1()) * dy) / len2,
                                            0.0, 1.0);
                distance = std::hypot(p.x() - (line.x1() + t * dx),
                                      p.y() - (line.y1() + t * dy));
            }
            if (distance < best)
            {
                best = distance;
                bestSegment = i + 1;
            }
        }
        if (bestSegment > 0)
            m_toggleHandler(bestSegment);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        m_segmentLines.clear();

        QFont titleFont = painter.font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(rect()).adjusted(14, 10, -14, -10),
                         Qt::AlignTop | Qt::AlignLeft,
                         QStringLiteral("二维边坡断面"));
        painter.setFont(font());

        if (!m_error.isEmpty() || m_model.nodes.isEmpty())
        {
            painter.setPen(QColor(170, 75, 55));
            painter.drawText(QRectF(rect()).adjusted(28, 58, -28, -28),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             m_error.isEmpty()
                                 ? QStringLiteral("请选择几何、三维网格、二维网格或导入已有断面。")
                                 : m_error);
            return;
        }

        double minX = m_model.nodes.first().x;
        double maxX = minX;
        double minY = m_model.nodes.first().y;
        double maxY = minY;
        for (const auto& node : m_model.nodes)
        {
            minX = std::min(minX, node.x);
            maxX = std::max(maxX, node.x);
            minY = std::min(minY, node.y);
            maxY = std::max(maxY, node.y);
        }
        if (std::abs(maxX - minX) < 1.0e-12) { minX -= 1.0; maxX += 1.0; }
        if (std::abs(maxY - minY) < 1.0e-12) { minY -= 1.0; maxY += 1.0; }

        const QRectF frame = QRectF(rect()).adjusted(48.0, 46.0, -22.0, -54.0);
        painter.setPen(QPen(palette().mid().color(), 1.0));
        painter.drawRect(frame);
        painter.setPen(QPen(palette().mid().color(), 0.8, Qt::DotLine));
        for (int i = 1; i < 5; ++i)
        {
            const double x = frame.left() + frame.width() * i / 5.0;
            const double y = frame.top() + frame.height() * i / 5.0;
            painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        }

        const double scale = std::min(frame.width() / (maxX - minX),
                                      frame.height() / (maxY - minY));
        const double usedWidth = (maxX - minX) * scale;
        const double usedHeight = (maxY - minY) * scale;
        const double left = frame.left() + (frame.width() - usedWidth) * 0.5;
        const double top = frame.top() + (frame.height() - usedHeight) * 0.5;
        const auto mapNode = [&](int nodeId)
        {
            const auto& node = m_model.nodes.at(nodeId - 1);
            return QPointF(left + (node.x - minX) * scale,
                           top + (maxY - node.y) * scale);
        };
        const auto soilColor = [](int soilId)
        {
            static const QVector<QColor> colors = {
                QColor(200, 120, 55), QColor(105, 150, 70), QColor(145, 95, 175),
                QColor(180, 165, 60), QColor(80, 145, 165), QColor(185, 95, 105)};
            return colors.at(std::abs(soilId - 1) % colors.size());
        };

        QSet<int> surfaceSegmentIds;
        for (const auto& surface : m_model.surfaceLoads)
            surfaceSegmentIds.insert(surface.segmentId);

        for (int i = 0; i < m_model.segments.size(); ++i)
        {
            const auto& segment = m_model.segments.at(i);
            if (segment.startNode < 1 || segment.startNode > m_model.nodes.size()
                || segment.endNode < 1 || segment.endNode > m_model.nodes.size())
            {
                m_segmentLines.append(QLineF());
                continue;
            }
            const QPointF a = mapNode(segment.startNode);
            const QPointF b = mapNode(segment.endNode);
            m_segmentLines.append(QLineF(a, b));
            const bool selected = surfaceSegmentIds.contains(i + 1);
            painter.setPen(selected ? QPen(QColor(35, 100, 205), 4.0)
                                    : QPen(soilColor(segment.soilId), 2.2));
            painter.drawLine(a, b);
        }

        if (std::abs(m_model.outsideWaterLevel - 9999.0) > 1.0e-8
            && m_model.outsideWaterLevel >= minY && m_model.outsideWaterLevel <= maxY)
        {
            const double y = top + (maxY - m_model.outsideWaterLevel) * scale;
            painter.setPen(QPen(QColor(30, 145, 190), 2.0, Qt::DashLine));
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        }

        painter.setPen(QPen(QColor(30, 175, 190), 3.0, Qt::DashLine));
        for (const auto& segment : m_model.phreaticSegments)
            if (segment.startNode >= 1 && segment.startNode <= m_model.nodes.size()
                && segment.endNode >= 1 && segment.endNode <= m_model.nodes.size())
                painter.drawLine(mapNode(segment.startNode), mapNode(segment.endNode));

        painter.setPen(palette().text().color());
        painter.drawText(QRectF(frame.left(), frame.bottom() + 8.0, frame.width(), 22.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("蓝色粗线为参与计算的外坡面；可直接点击线段增删。"));
        painter.drawText(QRectF(frame.left(), frame.bottom() + 28.0, frame.width(), 22.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("横轴：水平距离　纵轴：高程"));
    }

  private:
    SlopeStandardDatModel m_model;
    QString m_error;
    QVector<QLineF> m_segmentLines;
    std::function<void(int)> m_toggleHandler;
};

class SlopeStandardPlotWidget final : public QWidget
{
  public:
    explicit SlopeStandardPlotWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setPlot(const QJsonObject& plot)
    {
        m_outline = jsonPoints(plot.value(QStringLiteral("outline")));
        m_extendedOutline = jsonPoints(plot.value(QStringLiteral("extended_outline")));
        m_slipArc = jsonPoints(plot.value(QStringLiteral("slip_arc")));
        m_factor = plot.value(QStringLiteral("factor")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        update();
    }

    void clearPlot()
    {
        m_outline.clear();
        m_extendedOutline.clear();
        m_slipArc.clear();
        m_factor = std::numeric_limits<double>::quiet_NaN();
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        const QRectF frame = QRectF(rect()).adjusted(48.0, 24.0, -24.0, -42.0);
        painter.setPen(QPen(palette().mid().color(), 1.0));
        painter.drawRect(frame);

        QVector<PlotPoint> all = m_extendedOutline;
        all += m_outline;
        all += m_slipArc;
        if (all.size() < 2)
        {
            painter.setPen(palette().text().color());
            painter.drawText(frame, Qt::AlignCenter,
                             QStringLiteral("计算完成后在此显示临界滑弧"));
            return;
        }
        double minX = all.first().x, maxX = all.first().x;
        double minY = all.first().y, maxY = all.first().y;
        for (const PlotPoint& point : std::as_const(all))
        {
            minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
        }
        if (std::abs(maxX - minX) < 1.0e-12) { minX -= 1.0; maxX += 1.0; }
        if (std::abs(maxY - minY) < 1.0e-12) { minY -= 1.0; maxY += 1.0; }
        const double scale = std::min(frame.width() / (maxX - minX),
                                      frame.height() / (maxY - minY));
        const double usedWidth = (maxX - minX) * scale;
        const double usedHeight = (maxY - minY) * scale;
        const double left = frame.left() + (frame.width() - usedWidth) * 0.5;
        const double top = frame.top() + (frame.height() - usedHeight) * 0.5;
        const auto mapPoint = [&](const PlotPoint& point)
        {
            return QPointF(left + (point.x - minX) * scale,
                           top + (maxY - point.y) * scale);
        };
        painter.setPen(QPen(palette().mid().color(), 1.0, Qt::DotLine));
        for (int i = 1; i < 5; ++i)
        {
            const double x = frame.left() + frame.width() * i / 5.0;
            const double y = frame.top() + frame.height() * i / 5.0;
            painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        }
        const auto drawPolyline = [&](const QVector<PlotPoint>& points, const QPen& pen)
        {
            if (points.size() < 2) return;
            QPainterPath path(mapPoint(points.first()));
            for (int i = 1; i < points.size(); ++i) path.lineTo(mapPoint(points.at(i)));
            painter.setPen(pen); painter.drawPath(path);
        };
        drawPolyline(m_extendedOutline, QPen(QColor(125, 125, 125), 1.2, Qt::DashLine));
        drawPolyline(m_outline, QPen(QColor(40, 100, 190), 2.2));
        drawPolyline(m_slipArc, QPen(QColor(210, 60, 55), 2.5));
        painter.setPen(palette().text().color());
        if (std::isfinite(m_factor))
            painter.drawText(frame.adjusted(8.0, 6.0, -8.0, -6.0),
                             Qt::AlignTop | Qt::AlignRight,
                             QStringLiteral("Fs = %1").arg(displayNumber(m_factor)));
    }

  private:
    QVector<PlotPoint> m_outline;
    QVector<PlotPoint> m_extendedOutline;
    QVector<PlotPoint> m_slipArc;
    double m_factor{std::numeric_limits<double>::quiet_NaN()};
};

SlopeStandardAnalysisWidget::SlopeStandardAnalysisWidget(
    const QString& analysisUuid,
    const QJsonArray& savedCases,
    const QString& currentCaseId,
    const QString& legacyDatText,
    const QString& legacyLastJobDirectory,
    QWidget* parent,
    bool treeCaseMode,
    PageRole pageRole,
    SharedState sharedState)
    : BasePage(pageRole == PageRole::SectionEditor
                   ? "SlopeLimitEquilibriumSection"
                   : "SlopeLimitEquilibriumAnalysis"),
      m_analysisUuid(sanitizedUuid(analysisUuid)),
      m_treeCaseMode(treeCaseMode),
      m_pageRole(pageRole),
      m_sharedState(sharedState ? std::move(sharedState) : std::make_shared<QJsonObject>()),
      m_process(new QProcess(this))
{
    Q_UNUSED(parent);
    buildUi();
    loadCases(savedCases, currentCaseId, legacyDatText, legacyLastJobDirectory);
    if (m_sharedState && !m_sharedState->isEmpty())
        syncFromSharedState();
    else if (m_sharedState)
        *m_sharedState = saveJson();

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SlopeStandardAnalysisWidget::consumeProcessOutput);
    connect(m_process, &QProcess::errorOccurred,
            this, &SlopeStandardAnalysisWidget::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SlopeStandardAnalysisWidget::handleProcessFinished);
}

SlopeStandardAnalysisWidget::~SlopeStandardAnalysisWidget()
{
    setSectionAntialiasing(false);
    uninstallSectionInteractor();
    restoreOriginalModelVisibility();
    clearVisualization(false);
}

QString SlopeStandardAnalysisWidget::defaultDatText()
{
    // Kept only as an explicit advanced example. It is never auto-filled into a
    // new engineering case because example strength parameters can be mistaken
    // for project data.
    return QStringLiteral(
        "TWO_LAYER_SLOPE\n"
        "1 0.2\n"
        "2\n"
        "12.8 5 12\n"
        "9.81\n"
        "50\n"
        "4\n"
        "1 2 2\n"
        "2 3 1\n"
        "3 4 1\n"
        "2 5 2\n"
        "9999.0\n"
        "2\n"
        "1 23.0 5.3 19.5 19.5 -1 0.0\n"
        "2 20.0 7.2 19.5 19.5 -1 0.0\n"
        "5\n"
        "1 0.0 0.0\n"
        "2 5.2 4.0\n"
        "3 10.4 8.0\n"
        "4 20.0 8.0\n"
        "5 20.0 4.0\n"
        "3\n"
        "1 0.0\n"
        "2 0.0\n"
        "3 0.0\n"
        "8.0 0 0 0 0 0\n"
        "0\n");
}

QString SlopeStandardAnalysisWidget::datText() const
{
    SlopeStandardDatModel model;
    if (collectFormModel(&model, nullptr))
        return model.serialize();
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
        return m_cases.at(m_currentCaseIndex).dat;
    return {};
}

QJsonArray SlopeStandardAnalysisWidget::casesJson() const
{
    const_cast<SlopeStandardAnalysisWidget*>(this)->saveCurrentCase();
    QJsonArray array;
    for (const CaseRecord& record : m_cases)
        array.append(caseToJson(record));
    return array;
}

QString SlopeStandardAnalysisWidget::currentCaseId() const
{
    return m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
               ? m_cases.at(m_currentCaseIndex).id
               : QString();
}

QString SlopeStandardAnalysisWidget::lastJobDirectory() const
{
    return m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
               ? m_cases.at(m_currentCaseIndex).lastJobDirectory
               : QString();
}

bool SlopeStandardAnalysisWidget::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}


void SlopeStandardAnalysisWidget::setSectionAntialiasing(bool enabled)
{
    if (enabled == m_sectionAntialiasingActive)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = visual ? visual->renderer() : nullptr;
    if (renderer)
    {
        if (enabled)
            m_savedUseFXAA = renderer->GetUseFXAA();
        // Transparency disables MSAA for depth peeling. FXAA also smooths
        // section outlines in that mode, without changing the section geometry.
        renderer->SetUseFXAA(enabled ? true : m_savedUseFXAA);
    }
    m_sectionAntialiasingActive = enabled && renderer;
}

void SlopeStandardAnalysisWidget::beforeLoad()
{
    syncFromSharedState();
    m_pageActive = true;
    setSectionAntialiasing(true);
    applyPageRole();
    if (m_geometrySourceButton && m_pageRole != PageRole::AnalysisSettings)
    {
        const bool geometryAvailable = hasAvailableGeometrySection();
        m_geometrySourceButton->setEnabled(geometryAvailable);
        m_geometrySourceButton->setText(geometryAvailable
                                            ? QStringLiteral("当前几何模型")
                                            : QStringLiteral("当前几何模型（无可用几何）"));
        m_geometrySourceButton->setToolTip(geometryAvailable
            ? QStringLiteral("从当前几何模型提取断面。")
            : QStringLiteral("当前没有可用于剖切的可见几何实体。请先显示目标几何，或改用三维/二维网格。"));
    }
    installSectionInteractor();

    // A new analysis should not open as an empty wizard when a usable mesh is
    // already present. Pick the natural source and prepare the first section so
    // the user starts by inspecting/adjusting a section, not by hunting for a button.
    const bool hasSection = m_nodeTable && m_nodeTable->rowCount() >= 2
                            && m_segmentTable && m_segmentTable->rowCount() > 0;
    const bool sourceAlreadyChosen = m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                                     && !m_cases.at(m_currentCaseIndex).sourceType.isEmpty();
    if (m_pageRole != PageRole::AnalysisSettings && !hasSection && !sourceAlreadyChosen)
    {
        auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkSmartPointer<vtkUnstructuredGrid> grid = meshService
            ? meshService->getVtkUnstructuredGridSolid() : nullptr;
        if ((!grid || grid->GetNumberOfCells() <= 0) && meshService)
            grid = meshService->getVtkUnstructuredGrid();
        if (meshService && grid && grid->GetNumberOfCells() > 0)
        {
            int meshDimension = 0;
            const vtkIdType inspectCount = std::min<vtkIdType>(grid->GetNumberOfCells(), 200);
            for (vtkIdType cellId = 0; cellId < inspectCount; ++cellId)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (cell) meshDimension = std::max(meshDimension, cell->GetCellDimension());
            }
            if (meshDimension > 0 && meshDimension < 3)
            {
                if (m_mesh2dSourceButton) m_mesh2dSourceButton->setChecked(true);
                extractSectionFromCurrent2DMesh();
            }
            else
            {
                if (m_mesh3dSourceButton) m_mesh3dSourceButton->setChecked(true);
                if (m_meshSectionControls) m_meshSectionControls->show();
                initializeMeshSectionControls();
                extractSectionFromCurrentMesh(true);
                if (m_stateLabel)
                    m_stateLabel->setText(QStringLiteral("已生成断面，可在“断面”节点调整。"));
            }
        }
    }

    updateStepButtons();
    // “断面”子节点可由用户决定是否叠加原模型；“极限平衡”主节点固定使用
    // 二维断面视图，避免在参数设置页继续显示完整三维网格。
    const bool analysisSectionOnly = (m_pageRole == PageRole::AnalysisSettings);
    if (analysisSectionOnly)
    {
        saveSectionOnlyCameraState();
        setOriginalModelVisible(false);
    }
    else if (m_pageRole == PageRole::SectionEditor && m_showOriginalModelCheck)
    {
        const bool showOriginal = m_showOriginalModelCheck->isChecked();
        setOriginalModelVisible(showOriginal);
    }
    else
        restoreOriginalModelVisibility();

    const auto reapplySectionVisibility = [this]()
    {
        if (!m_pageActive)
            return;
        if (m_pageRole == PageRole::AnalysisSettings)
        {
            setOriginalModelVisible(false);
            applySectionOnlyCamera();
            return;
        }
        if (m_pageRole == PageRole::SectionEditor && m_showOriginalModelCheck
            && !m_showOriginalModelCheck->isChecked())
            setOriginalModelVisible(false);
    };

    if (m_pageRole == PageRole::CombinedLegacy && !m_lastPlot.isEmpty())
        updateResultVisualization(m_lastPlot);
    else
        updateVisualization();

    if (analysisSectionOnly)
        applySectionOnlyCamera();

    // 页面切换时，宿主模块可能还有一次延迟的 actor 刷新；再次应用当前页面显示规则。
    if (analysisSectionOnly
        || (m_pageRole == PageRole::SectionEditor && m_showOriginalModelCheck
            && !m_showOriginalModelCheck->isChecked()))
    {
        QTimer::singleShot(0, this, reapplySectionVisibility);
        QTimer::singleShot(80, this, reapplySectionVisibility);
    }
}

void SlopeStandardAnalysisWidget::beforeUnload()
{
    setSectionAntialiasing(false);
    syncToSharedState();
    setSectionInteractionMode(SectionInteractionMode::None);
    uninstallSectionInteractor();
    restoreOriginalModelVisibility();
    restoreSectionOnlyCameraState();
    m_pageActive = false;
    clearVisualization(true);
}

QJsonObject SlopeStandardAnalysisWidget::saveJson()
{
    // 分离式页面共享同一份分析状态。非当前页面不要用自己的旧控件值
    // 覆盖另一页面刚刚保存的断面/工况数据。
    if (m_pageRole != PageRole::CombinedLegacy && !m_pageActive
        && m_sharedState && !m_sharedState->isEmpty() && !m_syncingSharedState)
        return *m_sharedState;

    saveCurrentCase();
    QJsonObject object;
    object[QStringLiteral("cases")] = casesJson();
    object[QStringLiteral("current_case")] = currentCaseId();
    object[QStringLiteral("legacy_dat")] = datText();
    object[QStringLiteral("last_job")] = lastJobDirectory();
    object[QStringLiteral("schema")] = QStringLiteral("SlopeLimitEquilibriumAnalysis.v4");
    if (m_sharedState && !m_syncingSharedState)
        *m_sharedState = object;
    return object;
}

void SlopeStandardAnalysisWidget::loadJson(const QJsonObject& object)
{
    if (object.isEmpty())
        return;
    if (m_sharedState && !m_syncingSharedState)
        *m_sharedState = object;
    clearVisualization(false);
    loadCases(object.value(QStringLiteral("cases")).toArray(),
              object.value(QStringLiteral("current_case")).toString(),
              object.value(QStringLiteral("legacy_dat")).toString(),
              object.value(QStringLiteral("last_job")).toString());
    applyPageRole();
    if (m_pageActive)
        updateVisualization();
}

void SlopeStandardAnalysisWidget::buildUi()
{
    // 设置区统一使用标准输入背景，避免该分析页继承宿主面板的灰色底。
    QPalette widgetPalette = palette();
    widgetPalette.setColor(QPalette::Window, widgetPalette.color(QPalette::Base));
    setPalette(widgetPalette);
    setAutoFillBackground(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    const QString pageTitle = m_pageRole == PageRole::SectionEditor
                                  ? QStringLiteral("断面")
                                  : QStringLiteral("极限平衡");
    auto* title = new QLabel(pageTitle, this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    auto* caseRow = new QHBoxLayout();
    caseRow->addWidget(new QLabel(QStringLiteral("工况"), this));
    m_caseCombo = new QComboBox(this);
    m_caseCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    caseRow->addWidget(m_caseCombo, 1);
    root->addLayout(caseRow);
    auto* caseActions = new QHBoxLayout();
    m_newCaseButton = new QPushButton(QStringLiteral("新建工况"), this);
    m_copyCaseButton = new QPushButton(QStringLiteral("复制"), this);
    m_deleteCaseButton = new QPushButton(QStringLiteral("删除"), this);
    caseActions->addWidget(m_newCaseButton);
    caseActions->addWidget(m_copyCaseButton);
    caseActions->addWidget(m_deleteCaseButton);
    caseActions->addStretch();
    root->addLayout(caseActions);
    if (m_treeCaseMode)
    {
        // 一个极限平衡分析节点就是一个独立工况；页面内不再维护第二套工况选择器。
        m_caseCombo->setVisible(false);
        m_newCaseButton->setVisible(false);
        m_copyCaseButton->setVisible(false);
        m_deleteCaseButton->setVisible(false);
        caseRow->itemAt(0)->widget()->setVisible(false);
    }

    auto* stepFrame = new QFrame(this);
    stepFrame->setFrameShape(QFrame::StyledPanel);
    auto* stepLayout = new QHBoxLayout(stepFrame);
    stepLayout->setContentsMargins(8, 6, 8, 6);
    const QStringList stepTexts{
        QStringLiteral("① 断面"),
        QStringLiteral("② 材料 / 工况"),
        QStringLiteral("③ 结果")};
    for (int i = 0; i < stepTexts.size(); ++i)
    {
        auto* button = new QPushButton(stepTexts.at(i), stepFrame);
        button->setCheckable(true);
        button->setMinimumHeight(34);
        m_stepButtons.append(button);
        stepLayout->addWidget(button, 1);
        connect(button, &QPushButton::clicked, this, [this, i]() { setCurrentStep(i); });
    }
    root->addWidget(stepFrame);
    stepFrame->setVisible(m_pageRole == PageRole::CombinedLegacy);

    auto* viewControls = new QWidget(this);
    auto* viewRow = new QHBoxLayout(viewControls);
    viewRow->setContentsMargins(2, 0, 2, 0);
    auto* viewLabel = new QLabel(QStringLiteral("视图"), viewControls);
    viewRow->addWidget(viewLabel);
    m_showOriginalModelCheck = new QCheckBox(QStringLiteral("原模型"), viewControls);
    m_showOriginalModelCheck->setChecked(true);
    m_showOriginalModelCheck->setToolTip(QStringLiteral("显示或隐藏原模型。"));
    m_showSectionFillCheck = new QCheckBox(QStringLiteral("断面区域"), viewControls);
    m_showSectionFillCheck->setChecked(true);
    m_showSurfaceCheck = new QCheckBox(QStringLiteral("计算坡面"), viewControls);
    m_showSurfaceCheck->setChecked(true);
    viewRow->addWidget(m_showOriginalModelCheck);
    viewRow->addWidget(m_showSectionFillCheck);
    viewRow->addWidget(m_showSurfaceCheck);
    viewRow->addStretch();
    root->addWidget(viewControls);
    // 极限平衡主节点固定显示断面和计算坡面，不再提供额外视图开关。
    viewControls->setVisible(m_pageRole != PageRole::AnalysisSettings);

    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages, 1);
    buildSectionPage();
    buildCasePage();
    buildResultPage();
    applyPageRole();

    connect(m_newCaseButton, &QPushButton::clicked, this, [this]() { createCase(false); });
    connect(m_copyCaseButton, &QPushButton::clicked, this, [this]() { createCase(true); });
    connect(m_deleteCaseButton, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::removeCurrentCase);
    connect(m_caseCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_loadingCase || index < 0 || index == m_currentCaseIndex)
            return;
        saveCurrentCase();
        loadCase(index);
        emit currentCaseChanged(currentCaseId());
    });
    connect(m_showOriginalModelCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_pageActive || m_loadingCase)
            return;
        if (m_pageRole == PageRole::SectionEditor)
            setOriginalModelVisible(checked);
        else
            restoreOriginalModelVisibility();
        // 显示原模型时断面保持真实三维位置；隐藏原模型时切换成二维正断面。
        if (m_pages && m_pages->currentIndex() == 2 && !m_lastPlot.isEmpty())
            updateResultVisualization(m_lastPlot);
        else
            updateVisualization();
    });
    const auto refreshView = [this]() {
        if (!m_pageActive) return;
        if (m_pages && m_pages->currentIndex() == 2 && !m_lastPlot.isEmpty())
            updateResultVisualization(m_lastPlot);
        else
            updateVisualization();
    };
    connect(m_showSectionFillCheck, &QCheckBox::toggled, this, refreshView);
    connect(m_showSurfaceCheck, &QCheckBox::toggled, this, refreshView);
}

void SlopeStandardAnalysisWidget::applyPageRole()
{
    if (!m_pages)
        return;

    if (m_pageRole == PageRole::SectionEditor)
    {
        m_pages->setCurrentIndex(0);
        if (m_sectionNextButton)
        {
            m_sectionNextButton->show();
            m_sectionNextButton->setText(QStringLiteral("完成并返回 →"));
        }
        if (m_showOriginalModelCheck)
            m_showOriginalModelCheck->show();
    }
    else if (m_pageRole == PageRole::AnalysisSettings)
    {
        m_pages->setCurrentIndex(1);
        if (m_sectionNextButton)
            m_sectionNextButton->hide();
        // 极限平衡主节点只展示二维计算断面。原三维模型仅在“断面”子节点中用于
        // 定位和调整剖切位置，避免材料/工况设置时同时叠加三维网格造成视觉干扰。
        if (m_showOriginalModelCheck)
            m_showOriginalModelCheck->hide();
        if (m_showSectionFillCheck)
        {
            const QSignalBlocker blocker(m_showSectionFillCheck);
            m_showSectionFillCheck->setChecked(true);
        }
        if (m_showSurfaceCheck)
        {
            const QSignalBlocker blocker(m_showSurfaceCheck);
            m_showSurfaceCheck->setChecked(true);
        }
        if (m_pageActive)
            setOriginalModelVisible(false);
    }
    else
    {
        const int current = m_pages->currentIndex();
        if (current < 0 || current >= m_pages->count())
            m_pages->setCurrentIndex(0);
    }
    updateSectionStatusSummary();
    updateStepButtons();
}

void SlopeStandardAnalysisWidget::updateSectionStatusSummary()
{
    const int nodeCount = m_nodeTable ? m_nodeTable->rowCount() : 0;
    const int segmentCount = m_segmentTable ? m_segmentTable->rowCount() : 0;
    const int surfaceCount = m_surfaceTable ? m_surfaceTable->rowCount() : 0;
    const int regionCount = m_soilTable ? m_soilTable->rowCount() : 0;
    const bool ready = nodeCount >= 2 && segmentCount > 0 && surfaceCount > 0;

    if (m_sectionSummaryLabel)
    {
        if (ready)
            m_sectionSummaryLabel->setText(
                QStringLiteral("断面：%1 个区域 · %2 段坡面")
                    .arg(regionCount).arg(surfaceCount));
        else
            m_sectionSummaryLabel->setText(
                QStringLiteral("断面：未设置"));
    }
    if (m_openSectionButton)
        m_openSectionButton->setText(ready ? QStringLiteral("编辑断面")
                                           : QStringLiteral("设置断面"));
}

void SlopeStandardAnalysisWidget::setMaterialTableValue(int row, int column, double value)
{
    if (!m_soilTable || row < 0 || row >= m_soilTable->rowCount()
        || column < 0 || column >= m_soilTable->columnCount())
        return;
    QSignalBlocker blocker(m_soilTable);
    QTableWidgetItem* item = m_soilTable->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem();
        m_soilTable->setItem(row, column, item);
    }
    item->setText(displayNumber(value));
}

void SlopeStandardAnalysisWidget::loadMaterialRegionIntoEditor(int row)
{
    if (!m_soilTable || !m_materialRegionCombo || !m_frictionAngleSpin
        || !m_cohesionSpin || !m_naturalUnitWeightSpin
        || !m_saturatedUnitWeightSpin || !m_ruSpin)
        return;

    const bool valid = row >= 0 && row < m_soilTable->rowCount();
    for (QWidget* widget : {static_cast<QWidget*>(m_frictionAngleSpin),
                            static_cast<QWidget*>(m_cohesionSpin),
                            static_cast<QWidget*>(m_naturalUnitWeightSpin),
                            static_cast<QWidget*>(m_saturatedUnitWeightSpin),
                            static_cast<QWidget*>(m_ruSpin)})
        widget->setEnabled(valid);
    if (!valid)
    {
        updateMaterialRegionHighlight(-1);
        return;
    }

    const auto valueAt = [this, row](int column, double fallback)
    {
        bool ok = false;
        const QTableWidgetItem* item = m_soilTable->item(row, column);
        const double value = item ? item->text().trimmed().toDouble(&ok) : fallback;
        return ok && std::isfinite(value) ? value : fallback;
    };

    m_updatingMaterialEditor = true;
    const QSignalBlocker b1(m_frictionAngleSpin);
    const QSignalBlocker b2(m_cohesionSpin);
    const QSignalBlocker b3(m_naturalUnitWeightSpin);
    const QSignalBlocker b4(m_saturatedUnitWeightSpin);
    const QSignalBlocker b5(m_ruSpin);
    const double gamma = valueAt(4, 19.0);
    const double gammaSat = std::max(gamma, valueAt(5, 20.0));
    m_frictionAngleSpin->setValue(valueAt(2, 20.0));
    m_cohesionSpin->setValue(valueAt(3, 5.0));
    m_naturalUnitWeightSpin->setValue(gamma);
    m_saturatedUnitWeightSpin->setMinimum(gamma);
    m_saturatedUnitWeightSpin->setValue(gammaSat);
    m_ruSpin->setValue(valueAt(6, 0.0));
    m_updatingMaterialEditor = false;
    updateMaterialRegionHighlight(row);
}

void SlopeStandardAnalysisWidget::rebuildMaterialRegionEditor()
{
    if (!m_soilTable || !m_materialRegionCombo)
        return;

    const int rowCount = m_soilTable->rowCount();
    int current = m_materialRegionCombo->currentIndex();
    if (current < 0 && rowCount > 0)
        current = 0;
    current = std::clamp(current, 0, std::max(0, rowCount - 1));

    // New regions get engineering-friendly initial values.  They are explicit
    // starting values only; the user can overwrite them immediately.
    {
        QSignalBlocker blocker(m_soilTable);
        for (int row = 0; row < rowCount; ++row)
        {
            const std::pair<int, double> defaults[] = {
                {2, 20.0}, {3, 5.0}, {4, 19.0}, {5, 20.0}, {6, 0.0}};
            for (const auto& entry : defaults)
            {
                QTableWidgetItem* item = m_soilTable->item(row, entry.first);
                if (!item)
                {
                    item = new QTableWidgetItem();
                    m_soilTable->setItem(row, entry.first, item);
                }
                if (item->text().trimmed().isEmpty())
                    item->setText(displayNumber(entry.second));
            }
        }
    }

    {
        const QSignalBlocker blocker(m_materialRegionCombo);
        m_materialRegionCombo->clear();
        for (int row = 0; row < rowCount; ++row)
        {
            const QTableWidgetItem* regionItem = m_soilTable->item(row, 0);
            const QString technical = regionItem ? regionItem->text().trimmed() : QString();
            m_materialRegionCombo->addItem(QStringLiteral("区域 %1").arg(row + 1), row);
            if (!technical.isEmpty())
                m_materialRegionCombo->setItemData(row, technical, Qt::ToolTipRole);
        }
        if (rowCount > 0)
            m_materialRegionCombo->setCurrentIndex(current);
    }
    loadMaterialRegionIntoEditor(rowCount > 0 ? current : -1);
}

void SlopeStandardAnalysisWidget::syncFromSharedState()
{
    if (!m_sharedState || m_sharedState->isEmpty() || m_syncingSharedState)
        return;
    m_syncingSharedState = true;
    const QJsonObject object = *m_sharedState;
    clearVisualization(false);
    loadCases(object.value(QStringLiteral("cases")).toArray(),
              object.value(QStringLiteral("current_case")).toString(),
              object.value(QStringLiteral("legacy_dat")).toString(),
              object.value(QStringLiteral("last_job")).toString());
    m_syncingSharedState = false;
    applyPageRole();
}

void SlopeStandardAnalysisWidget::syncToSharedState()
{
    if (!m_sharedState || m_syncingSharedState)
        return;
    const QJsonObject object = saveJson();
    *m_sharedState = object;
}

void SlopeStandardAnalysisWidget::buildSectionPage()
{
    auto* page = new QWidget(m_pages);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* hint = new QLabel(
        QStringLiteral("选择断面来源，并确认计算坡面。"),
        page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* sourceGroup = new QGroupBox(QStringLiteral("断面来源"), page);
    auto* sourceLayout = new QGridLayout(sourceGroup);
    sourceLayout->setHorizontalSpacing(8);
    sourceLayout->setVerticalSpacing(8);
    m_geometrySourceButton = new QPushButton(QStringLiteral("当前几何模型"), sourceGroup);
    m_mesh3dSourceButton = new QPushButton(QStringLiteral("当前三维网格"), sourceGroup);
    m_mesh2dSourceButton = new QPushButton(QStringLiteral("当前二维网格"), sourceGroup);
    m_importSourceButton = new QPushButton(QStringLiteral("导入已有断面"), sourceGroup);
    m_sourceButtonGroup = new QButtonGroup(sourceGroup);
    m_sourceButtonGroup->setExclusive(true);
    const QVector<QPushButton*> sourceButtons{m_geometrySourceButton, m_mesh3dSourceButton,
                                               m_mesh2dSourceButton, m_importSourceButton};
    for (int i = 0; i < sourceButtons.size(); ++i)
    {
        auto* button = sourceButtons.at(i);
        button->setCheckable(true);
        button->setMinimumHeight(40);
        m_sourceButtonGroup->addButton(button);
        sourceLayout->addWidget(button, i / 2, i % 2);
    }
    m_geometrySourceButton->setToolTip(QStringLiteral("直接从当前 CAD/几何模型取得二维剖面。几何模块未接入时不会执行。"));
    m_mesh3dSourceButton->setToolTip(QStringLiteral("从当前三维网格提取断面。"));
    m_mesh2dSourceButton->setToolTip(QStringLiteral("当前工程本身为二维网格时直接使用。"));
    m_importSourceButton->setToolTip(QStringLiteral("导入已有边坡断面文件。"));
    layout->addWidget(sourceGroup);

    m_meshSectionControls = new QWidget(page);
    auto* meshForm = new QFormLayout(m_meshSectionControls);
    meshForm->setContentsMargins(8, 4, 8, 4);
    m_verticalCombo = new QComboBox(m_meshSectionControls);
    m_planeCombo = new QComboBox(m_meshSectionControls);
    auto* positionWidget = new QWidget(m_meshSectionControls);
    auto* positionLayout = new QHBoxLayout(positionWidget);
    positionLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionPositionSlider = new QSlider(Qt::Horizontal, positionWidget);
    m_sectionPositionSlider->setRange(0, 1000);
    m_sectionPositionSpin = new CompactDoubleSpinBox(positionWidget);
    m_sectionPositionSpin->setDecimals(6);
    m_sectionPositionSpin->setKeyboardTracking(false);
    positionLayout->addWidget(m_sectionPositionSlider, 1);
    positionLayout->addWidget(m_sectionPositionSpin);
    m_materialFieldCombo = new QComboBox(m_meshSectionControls);
    m_surfaceCandidateCombo = new QComboBox(m_meshSectionControls);
    m_surfaceCandidateCombo->setVisible(false);
    m_surfaceCandidateCombo->setToolTip(QStringLiteral("存在多个坡面时，在这里选择计算对象。"));
    m_simplificationToleranceSpin = new CompactDoubleSpinBox(m_meshSectionControls);
    m_simplificationToleranceSpin->setDecimals(8);
    m_simplificationToleranceSpin->setRange(0.0, 1.0e12);
    m_simplificationToleranceSpin->setKeyboardTracking(false);
    meshForm->addRow(QStringLiteral("高程方向"), m_verticalCombo);
    meshForm->addRow(QStringLiteral("剖面方向"), m_planeCombo);
    meshForm->addRow(QStringLiteral("剖面位置"), positionWidget);
    meshForm->addRow(QStringLiteral("目标边坡"), m_surfaceCandidateCombo);
    m_meshSectionControls->hide();
    layout->addWidget(m_meshSectionControls);

    m_editSurfaceButton = new QPushButton(QStringLiteral("在右侧图上编辑计算坡面"), page);
    m_editSurfaceButton->setCheckable(true);
    m_editSurfaceButton->setEnabled(false);
    m_editSurfaceButton->setToolTip(QStringLiteral("开启后，直接点击右侧断面边界即可加入/移出计算坡面。"));
    layout->addWidget(m_editSurfaceButton, 0, Qt::AlignLeft);

    m_sourceStatusLabel = new QLabel(QStringLiteral("尚未获取断面"), page);
    m_sourceStatusLabel->setWordWrap(true);
    m_sourceStatusLabel->setFrameShape(QFrame::StyledPanel);
    m_sourceStatusLabel->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(m_sourceStatusLabel);

    layout->addStretch();

    m_advancedGeometryToggle = new QToolButton(page);
    m_advancedGeometryToggle->setText(QStringLiteral("高级几何编辑 ▸"));
    m_advancedGeometryToggle->setCheckable(true);
    m_advancedGeometryToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    layout->addWidget(m_advancedGeometryToggle);

    m_advancedGeometryPanel = new QWidget(page);
    auto* advancedLayout = new QVBoxLayout(m_advancedGeometryPanel);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    auto* advancedHint = new QLabel(
        QStringLiteral("仅在自动识别不合适时调整。"),
        m_advancedGeometryPanel);
    advancedHint->setWordWrap(true);
    advancedLayout->addWidget(advancedHint);
    auto* simplifyForm = new QFormLayout();
    simplifyForm->addRow(QStringLiteral("分区识别字段"), m_materialFieldCombo);
    simplifyForm->addRow(QStringLiteral("折线简化容差"), m_simplificationToleranceSpin);
    advancedLayout->addLayout(simplifyForm);
    auto* tables = new QStackedWidget(m_advancedGeometryPanel);
    auto* tableSwitcher = new QComboBox(m_advancedGeometryPanel);
    tableSwitcher->addItems({QStringLiteral("节点"), QStringLiteral("边界线"),
                             QStringLiteral("外坡面"), QStringLiteral("浸润线")});
    advancedLayout->addWidget(tableSwitcher);
    advancedLayout->addWidget(tables);

    const auto makeTable = [tables](const QStringList& headers)
    {
        auto* table = new QTableWidget(tables);
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        tables->addWidget(table);
        return table;
    };
    m_nodeTable = makeTable({QStringLiteral("ID"), QStringLiteral("水平坐标"), QStringLiteral("高程")});
    m_segmentTable = makeTable({QStringLiteral("起点"), QStringLiteral("终点"), QStringLiteral("土层")});
    m_surfaceTable = makeTable({QStringLiteral("边界线"), QStringLiteral("坡面荷载")});
    m_phreaticTable = makeTable({QStringLiteral("起点"), QStringLiteral("终点"), QStringLiteral("土层")});
    connect(tableSwitcher, &QComboBox::currentIndexChanged, tables, &QStackedWidget::setCurrentIndex);
    auto* geometryTableActions = new QHBoxLayout();
    auto* addGeometryRow = new QPushButton(QStringLiteral("添加行"), m_advancedGeometryPanel);
    auto* removeGeometryRow = new QPushButton(QStringLiteral("删除选中行"), m_advancedGeometryPanel);
    geometryTableActions->addWidget(addGeometryRow);
    geometryTableActions->addWidget(removeGeometryRow);
    geometryTableActions->addStretch();
    advancedLayout->addLayout(geometryTableActions);
    const auto currentGeometryTable = [tables]() -> QTableWidget*
    {
        return qobject_cast<QTableWidget*>(tables->currentWidget());
    };
    connect(addGeometryRow, &QPushButton::clicked, this, [this, currentGeometryTable]()
    {
        addTableRow(currentGeometryTable());
        markCaseDirty();
        updateInputPreview();
    });
    connect(removeGeometryRow, &QPushButton::clicked, this, [this, currentGeometryTable]()
    {
        removeSelectedRows(currentGeometryTable());
        markCaseDirty();
        updateInputPreview();
    });
    m_advancedGeometryPanel->hide();
    layout->addWidget(m_advancedGeometryPanel);

    auto* nextRow = new QHBoxLayout();
    nextRow->addStretch();
    m_sectionNextButton = new QPushButton(
        m_pageRole == PageRole::SectionEditor
            ? QStringLiteral("完成并返回 →")
            : QStringLiteral("下一步 →"), page);
    m_sectionNextButton->setEnabled(false);
    nextRow->addWidget(m_sectionNextButton);
    layout->addLayout(nextRow);

    connect(m_advancedGeometryToggle, &QToolButton::toggled, this, [this](bool checked) {
        m_advancedGeometryPanel->setVisible(checked);
        m_advancedGeometryToggle->setText(checked ? QStringLiteral("高级几何编辑 ▾")
                                                  : QStringLiteral("高级几何编辑 ▸"));
    });
    connect(m_geometrySourceButton, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::requestGeometrySection);
    connect(m_mesh3dSourceButton, &QPushButton::clicked, this, [this]() {
        m_meshSectionControls->show();
        initializeMeshSectionControls();
        extractSectionFromCurrentMesh(false);
    });
    connect(m_mesh2dSourceButton, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::extractSectionFromCurrent2DMesh);
    connect(m_importSourceButton, &QPushButton::clicked, this, &SlopeStandardAnalysisWidget::importDat);
    connect(m_editSurfaceButton, &QPushButton::toggled, this, [this](bool on) {
        setSectionInteractionMode(on ? SectionInteractionMode::SurfaceEdit : SectionInteractionMode::None);
    });
    connect(m_sectionNextButton, &QPushButton::clicked, this, [this]() {
        if (m_pageRole == PageRole::SectionEditor)
        {
            syncToSharedState();
            emit openAnalysisRequested();
        }
        else
        {
            setCurrentStep(1);
        }
    });

    connect(m_verticalCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool geometrySource = m_geometrySourceButton && m_geometrySourceButton->isChecked();
        if (geometrySource)
        {
            const QSignalBlocker blocker(m_planeCombo);
            m_planeCombo->clear();
            QTimer::singleShot(0, this, [this]() { extractSectionFromCurrentGeometry(true); });
        }
        else
        {
            rebuildPlaneChoices();
            scheduleMeshSectionPreview();
        }
    });
    connect(m_planeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool geometrySource = m_geometrySourceButton && m_geometrySourceButton->isChecked();
        if (geometrySource)
            scheduleMeshSectionPreview();
        else
        {
            updatePlaneRange(true);
            scheduleMeshSectionPreview();
        }
    });
    connect(m_materialFieldCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { scheduleMeshSectionPreview(); });
    connect(m_surfaceCandidateCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { scheduleMeshSectionPreview(); });
    connect(m_simplificationToleranceSpin, &QDoubleSpinBox::editingFinished,
            this, &SlopeStandardAnalysisWidget::scheduleMeshSectionPreview);
    connect(m_sectionPositionSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_sectionPositionSpin->maximum() <= m_sectionPositionSpin->minimum())
            return;
        const double ratio = value / 1000.0;
        const double position = m_sectionPositionSpin->minimum()
                                + ratio * (m_sectionPositionSpin->maximum()
                                           - m_sectionPositionSpin->minimum());
        const QSignalBlocker blocker(m_sectionPositionSpin);
        m_sectionPositionSpin->setValue(position);
        scheduleMeshSectionPreview();
    });
    connect(m_sectionPositionSpin, &QDoubleSpinBox::editingFinished, this, [this]() {
        const double span = m_sectionPositionSpin->maximum() - m_sectionPositionSpin->minimum();
        if (span > 0.0)
        {
            const int value = qRound(1000.0 * (m_sectionPositionSpin->value()
                                               - m_sectionPositionSpin->minimum()) / span);
            const QSignalBlocker blocker(m_sectionPositionSlider);
            m_sectionPositionSlider->setValue(std::clamp(value, 0, 1000));
        }
        scheduleMeshSectionPreview();
    });

    for (QTableWidget* table : {m_nodeTable, m_segmentTable, m_surfaceTable, m_phreaticTable})
        connect(table, &QTableWidget::itemChanged, this, [this]() {
            if (m_loadingCase) return;
            markCaseDirty(); updateInputPreview();
        });

    m_pages->addWidget(page);
}

void SlopeStandardAnalysisWidget::buildCasePage()
{
    auto* page = new QWidget(m_pages);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);

    // 与其他设置页保持一致：分析节点使用标准输入区域背景，不继承宿主灰色面板。
    const QColor pageBackground = page->palette().color(QPalette::Base);
    QPalette pagePalette = page->palette();
    pagePalette.setColor(QPalette::Window, pageBackground);
    page->setPalette(pagePalette);
    page->setAutoFillBackground(true);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QPalette scrollPalette = scroll->palette();
    scrollPalette.setColor(QPalette::Base, pageBackground);
    scrollPalette.setColor(QPalette::Window, pageBackground);
    scroll->setPalette(scrollPalette);
    scroll->viewport()->setPalette(scrollPalette);
    scroll->viewport()->setAutoFillBackground(true);

    auto* content = new QWidget(scroll);
    content->setPalette(pagePalette);
    content->setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(content);

    if (m_pageRole == PageRole::AnalysisSettings)
    {
        auto* sectionCard = new QFrame(content);
        sectionCard->setFrameShape(QFrame::StyledPanel);
        auto* sectionCardLayout = new QHBoxLayout(sectionCard);
        sectionCardLayout->setContentsMargins(10, 8, 10, 8);
        m_sectionSummaryLabel = new QLabel(sectionCard);
        m_sectionSummaryLabel->setWordWrap(true);
        m_openSectionButton = new QPushButton(QStringLiteral("设置断面"), sectionCard);
        sectionCardLayout->addWidget(m_sectionSummaryLabel, 1);
        sectionCardLayout->addWidget(m_openSectionButton);
        layout->addWidget(sectionCard);
        connect(m_openSectionButton, &QPushButton::clicked, this, [this]() {
            syncToSharedState();
            emit openSectionRequested();
        });

    }

    auto* materialGroup = new QGroupBox(QStringLiteral("材料参数"), content);
    auto* materialLayout = new QVBoxLayout(materialGroup);
    // Keep the table only as an internal compatibility store for the existing DAT/state
    // serializer.  The normal UI edits one material region at a time instead of exposing
    // a spreadsheet-like editor.
    m_soilTable = new QTableWidget(materialGroup);
    m_soilTable->setColumnCount(7);
    m_soilTable->setHorizontalHeaderLabels(
        {QStringLiteral("材料区域"), QStringLiteral("内部区域名"),
         QStringLiteral("内摩擦角 φ (°)"), QStringLiteral("黏聚力 c (kPa)"),
         QStringLiteral("天然重度 γ (kN/m³)"), QStringLiteral("饱和重度 γsat (kN/m³)"),
         QStringLiteral("孔隙水压力比 Ru")});
    m_soilTable->hide();
    connect(m_soilTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_loadingCase || m_updatingMaterialEditor || !m_soilTable || !m_materialRegionCombo)
            return;
        const auto items = m_soilTable->selectedItems();
        if (!items.isEmpty())
        {
            const int row = items.first()->row();
            if (row >= 0 && row < m_materialRegionCombo->count() && row != m_materialRegionCombo->currentIndex())
                m_materialRegionCombo->setCurrentIndex(row);
        }
    });

    auto* materialForm = new QFormLayout();
    m_materialRegionCombo = new QComboBox(materialGroup);
    m_materialRegionCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const auto makeMaterialSpin = [materialGroup](double minimum, double maximum,
                                                   int decimals, double value)
    {
        auto* spin = new CompactDoubleSpinBox(materialGroup);
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setValue(value);
        spin->setKeyboardTracking(false);
        spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return spin;
    };
    m_frictionAngleSpin = makeMaterialSpin(0.0, 89.999, 3, 20.0);
    m_cohesionSpin = makeMaterialSpin(0.0, 1.0e9, 3, 5.0);
    m_naturalUnitWeightSpin = makeMaterialSpin(0.001, 1.0e6, 3, 19.0);
    m_saturatedUnitWeightSpin = makeMaterialSpin(0.001, 1.0e6, 3, 20.0);
    m_ruSpin = makeMaterialSpin(0.0, 1.0e6, 4, 0.0);

    materialForm->addRow(QStringLiteral("材料区域"), m_materialRegionCombo);
    materialForm->addRow(QStringLiteral("内摩擦角 φ (°)"), m_frictionAngleSpin);
    materialForm->addRow(QStringLiteral("黏聚力 c (kPa)"), m_cohesionSpin);
    materialForm->addRow(QStringLiteral("天然重度 γ (kN/m³)"), m_naturalUnitWeightSpin);
    materialForm->addRow(QStringLiteral("饱和重度 γsat (kN/m³)"), m_saturatedUnitWeightSpin);
    m_ruEditorLabel = new QLabel(QStringLiteral("孔隙水压力比 Ru"), materialGroup);
    materialForm->addRow(m_ruEditorLabel, m_ruSpin);
    materialLayout->addLayout(materialForm);
    layout->addWidget(materialGroup);

    auto* normalRow = new QVBoxLayout();
    auto* calculationGroup = new QGroupBox(QStringLiteral("计算设置"), content);
    auto* calculationForm = new QFormLayout(calculationGroup);
    m_titleEdit = new QLineEdit(calculationGroup);
    m_titleEdit->setPlaceholderText(QStringLiteral("例如：正常蓄水"));
    m_methodCombo = new QComboBox(calculationGroup);
    m_methodCombo->addItem(QStringLiteral("瑞典圆弧法"), 1);
    m_methodCombo->addItem(QStringLiteral("毕肖普法（推荐）"), 2);
    m_methodCombo->addItem(QStringLiteral("Sarma 法"), 3);
    m_methodCombo->setCurrentIndex(1);
    m_modeCombo = new QComboBox(calculationGroup);
    m_modeCombo->addItem(QStringLiteral("自动搜索（推荐）"), 1);
    m_modeCombo->addItem(QStringLiteral("指定滑弧"), 0);
    m_searchPrecisionCombo = new QComboBox(calculationGroup);
    m_searchPrecisionCombo->addItem(QStringLiteral("快速"), 0.5);
    m_searchPrecisionCombo->addItem(QStringLiteral("标准"), 0.2);
    m_searchPrecisionCombo->addItem(QStringLiteral("精细"), 0.1);
    m_searchPrecisionCombo->setCurrentIndex(1);
    m_computePrecisionCombo = new QComboBox(calculationGroup);
    m_computePrecisionCombo->addItem(QStringLiteral("标准"), 50);
    m_computePrecisionCombo->addItem(QStringLiteral("高精度"), 100);
    m_requiredFactorSpin = new CompactDoubleSpinBox(calculationGroup);
    m_requiredFactorSpin->setRange(0.001, 10.0);
    m_requiredFactorSpin->setDecimals(3);
    m_requiredFactorSpin->setValue(1.30);
    calculationForm->addRow(QStringLiteral("算例名称"), m_titleEdit);
    calculationForm->addRow(QStringLiteral("计算方法"), m_methodCombo);
    calculationForm->addRow(QStringLiteral("滑动面"), m_modeCombo);
    calculationForm->addRow(QStringLiteral("搜索精度"), m_searchPrecisionCombo);
    calculationForm->addRow(QStringLiteral("计算精度"), m_computePrecisionCombo);
    // 安全系数要求值仅保留为旧工程兼容字段，不参与前台结果判定。
    m_requiredFactorSpin->hide();
    if (m_pageRole == PageRole::AnalysisSettings)
    {
        if (QWidget* label = calculationForm->labelForField(m_titleEdit))
            label->hide();
        m_titleEdit->hide();
    }
    normalRow->addWidget(calculationGroup);

    auto* conditionGroup = new QGroupBox(QStringLiteral("水位与地震工况"), content);
    auto* conditionForm = new QFormLayout(conditionGroup);
    m_poreMethodCombo = new QComboBox(conditionGroup);
    m_poreMethodCombo->addItem(QStringLiteral("无地下水"), -1);
    m_poreMethodCombo->addItem(QStringLiteral("浸润线"), 0);
    m_poreMethodCombo->addItem(QStringLiteral("Ru 法"), 2);
    auto* phreaticButtons = new QWidget(conditionGroup);
    auto* phreaticButtonsLayout = new QHBoxLayout(phreaticButtons);
    phreaticButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_drawPhreaticButton = new QPushButton(QStringLiteral("在断面图上绘制"), phreaticButtons);
    m_drawPhreaticButton->setCheckable(true);
    m_clearPhreaticButton = new QPushButton(QStringLiteral("清除"), phreaticButtons);
    phreaticButtonsLayout->addWidget(m_drawPhreaticButton);
    phreaticButtonsLayout->addWidget(m_clearPhreaticButton);
    phreaticButtonsLayout->addStretch();
    m_outsideWaterCheck = new QCheckBox(QStringLiteral("考虑坡外水位"), conditionGroup);
    m_outsideWaterSpin = new CompactDoubleSpinBox(conditionGroup);
    m_outsideWaterSpin->setRange(-1.0e9, 1.0e9);
    m_outsideWaterSpin->setDecimals(4);
    m_outsideWaterSpin->setEnabled(false);
    m_earthquakeCheck = new QCheckBox(QStringLiteral("考虑地震作用"), conditionGroup);
    m_earthquakeIntensitySpin = new QSpinBox(conditionGroup);
    // SOLVER EARTHQUICK only handles intensity <= 9 (LD > 9 zeroes seismic load).
    m_earthquakeIntensitySpin->setRange(0, 9);
    m_earthquakeIntensitySpin->setEnabled(false);
    conditionForm->addRow(QStringLiteral("地下水"), m_poreMethodCombo);
    conditionForm->addRow(QStringLiteral("浸润线"), phreaticButtons);
    conditionForm->addRow(m_outsideWaterCheck);
    conditionForm->addRow(QStringLiteral("坡外水面高程"), m_outsideWaterSpin);
    conditionForm->addRow(m_earthquakeCheck);
    conditionForm->addRow(QStringLiteral("地震烈度"), m_earthquakeIntensitySpin);
    normalRow->addWidget(conditionGroup);
    layout->addLayout(normalRow);

    m_advancedSettingsToggle = new QToolButton(content);
    m_advancedSettingsToggle->setText(QStringLiteral("高级设置 ▸"));
    m_advancedSettingsToggle->setCheckable(true);
    layout->addWidget(m_advancedSettingsToggle);
    m_advancedSettingsPanel = new QWidget(content);
    auto* advanced = new QVBoxLayout(m_advancedSettingsPanel);
    advanced->setContentsMargins(0, 0, 0, 0);

    auto* advancedGroup = new QGroupBox(QStringLiteral("高级参数"), m_advancedSettingsPanel);
    auto* advancedForm = new QFormLayout(advancedGroup);
    m_searchStepSpin = new CompactDoubleSpinBox(advancedGroup);
    m_searchStepSpin->setRange(0.000001, 1.0e6); m_searchStepSpin->setDecimals(6); m_searchStepSpin->setValue(0.2);
    m_sliceCountSpin = new QSpinBox(advancedGroup);
    m_sliceCountSpin->setRange(1, 200); m_sliceCountSpin->setValue(50);
    m_radiusSpin = new CompactDoubleSpinBox(advancedGroup);
    m_radiusSpin->setRange(0.000001, 1.0e9); m_radiusSpin->setDecimals(6); m_radiusSpin->setValue(12.8);
    m_circleXSpin = new CompactDoubleSpinBox(advancedGroup);
    m_circleXSpin->setRange(-1.0e9, 1.0e9); m_circleXSpin->setDecimals(6); m_circleXSpin->setValue(5.0);
    m_circleYSpin = new CompactDoubleSpinBox(advancedGroup);
    m_circleYSpin->setRange(-1.0e9, 1.0e9); m_circleYSpin->setDecimals(6); m_circleYSpin->setValue(12.0);
    m_waterWeightSpin = new CompactDoubleSpinBox(advancedGroup);
    m_waterWeightSpin->setRange(0.000001, 1.0e6); m_waterWeightSpin->setDecimals(6); m_waterWeightSpin->setValue(9.81);
    m_slopeHeightSpin = new CompactDoubleSpinBox(advancedGroup);
    m_slopeHeightSpin->setRange(0.0, 1.0e9); m_slopeHeightSpin->setDecimals(6);
    m_earthquakeModeCombo = new QComboBox(advancedGroup);
    m_earthquakeModeCombo->addItem(QStringLiteral("仅水平向"), 0);
    m_earthquakeModeCombo->addItem(QStringLiteral("仅竖向"), 1);
    m_earthquakeModeCombo->addItem(QStringLiteral("水平 + 竖向"), 2);
    const auto fillDirection = [](QComboBox* combo) {
        combo->addItem(QStringLiteral("负方向"), -1);
        combo->addItem(QStringLiteral("不考虑"), 0);
        combo->addItem(QStringLiteral("正方向"), 1);
        combo->setCurrentIndex(1);
    };
    m_earthquakeDirectionHCombo = new QComboBox(advancedGroup);
    m_earthquakeDirectionVCombo = new QComboBox(advancedGroup);
    fillDirection(m_earthquakeDirectionHCombo);
    fillDirection(m_earthquakeDirectionVCombo);
    advancedForm->addRow(QStringLiteral("搜索步长 LTH"), m_searchStepSpin);
    advancedForm->addRow(QStringLiteral("竖向条分数"), m_sliceCountSpin);
    advancedForm->addRow(QStringLiteral("固定圆弧半径 R"), m_radiusSpin);
    advancedForm->addRow(QStringLiteral("圆心 X"), m_circleXSpin);
    advancedForm->addRow(QStringLiteral("圆心 Y"), m_circleYSpin);
    advancedForm->addRow(QStringLiteral("水的重度"), m_waterWeightSpin);
    advancedForm->addRow(QStringLiteral("实际边坡高度"), m_slopeHeightSpin);
    advancedForm->addRow(QStringLiteral("地震组合"), m_earthquakeModeCombo);
    advancedForm->addRow(QStringLiteral("水平地震方向"), m_earthquakeDirectionHCombo);
    advancedForm->addRow(QStringLiteral("竖向地震方向"), m_earthquakeDirectionVCombo);
    advanced->addWidget(advancedGroup);

    // DAT 仅作为与 SlopeEngine 通信的内部缓存，不向普通或高级用户展示。
    m_datEditor = new QPlainTextEdit(m_advancedSettingsPanel);
    m_datEditor->hide();
    m_advancedSettingsPanel->hide();
    layout->addWidget(m_advancedSettingsPanel);
    layout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* actions = new QHBoxLayout();
    auto* back = new QPushButton(QStringLiteral("← 上一步"), page);
    m_stateLabel = new QLabel(QString(), page);
    m_stateLabel->setWordWrap(true);
    m_progressBar = new QProgressBar(page);
    m_progressBar->setRange(0, 0);
    m_progressBar->setMaximumWidth(120);
    m_progressBar->hide();
    m_stopButton = new QPushButton(QStringLiteral("停止计算"), page);
    m_stopButton->hide();
    m_calculateButton = new QPushButton(QStringLiteral("计算"), page);
    m_calculateButton->setMinimumHeight(36);
    actions->addWidget(back);
    if (m_pageRole != PageRole::CombinedLegacy)
        back->hide();
    actions->addWidget(m_stateLabel, 1);
    actions->addWidget(m_progressBar);
    actions->addWidget(m_stopButton);
    actions->addWidget(m_calculateButton);
    root->addLayout(actions);

    connect(back, &QPushButton::clicked, this, [this]() { setCurrentStep(0); });
    connect(m_calculateButton, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::startValidateAndCalculate);
    connect(m_stopButton, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::stopProcess);
    connect(m_drawPhreaticButton, &QPushButton::toggled, this, [this](bool on) {
        setSectionInteractionMode(on ? SectionInteractionMode::PhreaticDraw : SectionInteractionMode::None);
    });
    connect(m_clearPhreaticButton, &QPushButton::clicked, this, &SlopeStandardAnalysisWidget::clearPhreaticLine);

    connect(m_advancedSettingsToggle, &QToolButton::toggled, this, [this](bool checked) {
        m_advancedSettingsPanel->setVisible(checked);
        m_advancedSettingsToggle->setText(checked ? QStringLiteral("高级设置 ▾")
                                                  : QStringLiteral("高级设置 ▸"));
    });
    connect(m_searchPrecisionCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_loadingCase) return;
        m_searchStepSpin->setValue(m_searchPrecisionCombo->currentData().toDouble());
        markCaseDirty();
    });
    connect(m_computePrecisionCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_loadingCase) return;
        m_sliceCountSpin->setValue(m_computePrecisionCombo->currentData().toInt());
        markCaseDirty();
    });

    const QList<QObject*> valueObjects = {
        m_titleEdit, m_methodCombo, m_modeCombo, m_requiredFactorSpin,
        m_searchStepSpin, m_sliceCountSpin, m_radiusSpin, m_circleXSpin, m_circleYSpin,
        m_waterWeightSpin, m_outsideWaterCheck, m_outsideWaterSpin, m_poreMethodCombo,
        m_earthquakeCheck, m_slopeHeightSpin, m_earthquakeIntensitySpin,
        m_earthquakeModeCombo, m_earthquakeDirectionHCombo, m_earthquakeDirectionVCombo};
    for (QObject* object : valueObjects)
    {
        if (auto* edit = qobject_cast<QLineEdit*>(object))
            connect(edit, &QLineEdit::textChanged, this, &SlopeStandardAnalysisWidget::markCaseDirty);
        else if (auto* combo = qobject_cast<QComboBox*>(object))
            connect(combo, &QComboBox::currentIndexChanged, this, [this](int) { markCaseDirty(); updateControlAvailability(); });
        else if (auto* check = qobject_cast<QCheckBox*>(object))
            connect(check, &QCheckBox::toggled, this, [this](bool) { markCaseDirty(); updateControlAvailability(); updateInputPreview(); });
        else if (auto* spin = qobject_cast<QDoubleSpinBox*>(object))
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { markCaseDirty(); updateControlAvailability(); updateInputPreview(); });
        else if (auto* spinI = qobject_cast<QSpinBox*>(object))
            connect(spinI, &QSpinBox::valueChanged, this, [this](int) { markCaseDirty(); updateControlAvailability(); });
    }
    connect(m_materialRegionCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        loadMaterialRegionIntoEditor(index);
    });

    const auto materialValueChanged = [this]() {
        if (m_loadingCase || m_updatingMaterialEditor || !m_materialRegionCombo)
            return;
        const int row = m_materialRegionCombo->currentIndex();
        if (row < 0 || !m_soilTable || row >= m_soilTable->rowCount())
            return;
        // Keep γsat physically consistent as γ is edited.
        if (m_saturatedUnitWeightSpin->value() < m_naturalUnitWeightSpin->value())
        {
            const QSignalBlocker blocker(m_saturatedUnitWeightSpin);
            m_saturatedUnitWeightSpin->setValue(m_naturalUnitWeightSpin->value());
        }
        setMaterialTableValue(row, 2, m_frictionAngleSpin->value());
        setMaterialTableValue(row, 3, m_cohesionSpin->value());
        setMaterialTableValue(row, 4, m_naturalUnitWeightSpin->value());
        setMaterialTableValue(row, 5, m_saturatedUnitWeightSpin->value());
        setMaterialTableValue(row, 6, m_ruSpin->value());
        markCaseDirty();
        updateInputPreview();
    };
    connect(m_frictionAngleSpin, &QDoubleSpinBox::valueChanged, this, [materialValueChanged](double) { materialValueChanged(); });
    connect(m_cohesionSpin, &QDoubleSpinBox::valueChanged, this, [materialValueChanged](double) { materialValueChanged(); });
    connect(m_naturalUnitWeightSpin, &QDoubleSpinBox::valueChanged, this, [this, materialValueChanged](double value) {
        m_saturatedUnitWeightSpin->setMinimum(value);
        materialValueChanged();
    });
    connect(m_saturatedUnitWeightSpin, &QDoubleSpinBox::valueChanged, this, [materialValueChanged](double) { materialValueChanged(); });
    connect(m_ruSpin, &QDoubleSpinBox::valueChanged, this, [materialValueChanged](double) { materialValueChanged(); });

    updateControlAvailability();
    m_pages->addWidget(page);
}

void SlopeStandardAnalysisWidget::buildResultPage()
{
    auto* page = new QWidget(m_pages);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* conclusion = new QFrame(page);
    conclusion->setFrameShape(QFrame::StyledPanel);
    auto* conclusionLayout = new QVBoxLayout(conclusion);
    m_factorLabel = new QLabel(QStringLiteral("安全系数：—"), conclusion);
    m_requiredLabel = new QLabel(conclusion);
    m_requiredLabel->hide();
    m_conclusionLabel = new QLabel(conclusion);
    m_conclusionLabel->hide();
    QFont factorFont = m_factorLabel->font();
    factorFont.setBold(true);
    m_factorLabel->setFont(factorFont);
    conclusionLayout->addWidget(m_factorLabel, 1);
    layout->addWidget(conclusion);

    auto* detailPane = new QWidget(page);
    auto* detailLayout = new QVBoxLayout(detailPane);
    m_summaryLabel = new QLabel(QString(), detailPane);
    m_summaryLabel->setWordWrap(true);
    detailLayout->addWidget(m_summaryLabel);
    // 原始 result.dat 仅内部解析，不在结果页展示。
    m_resultDetailToggle = new QToolButton(detailPane);
    m_resultDetailToggle->hide();
    m_resultViewer = new QPlainTextEdit(detailPane);
    m_resultViewer->setReadOnly(true);
    m_resultViewer->hide();
    layout->addWidget(detailPane, 1);

    auto* actions = new QHBoxLayout();
    auto* back = new QPushButton(QStringLiteral("← 修改材料和工况"), page);
    auto* recalc = new QPushButton(QStringLiteral("重新计算"), page);
    m_openDirectoryButton = new QPushButton(QStringLiteral("打开任务目录"), page);
    m_openDirectoryButton->setEnabled(false);
    actions->addWidget(back);
    actions->addWidget(recalc);
    actions->addStretch();
    actions->addWidget(m_openDirectoryButton);
    layout->addLayout(actions);
    connect(back, &QPushButton::clicked, this, [this]() { setCurrentStep(1); });
    connect(recalc, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::startValidateAndCalculate);
    connect(m_openDirectoryButton, &QPushButton::clicked,
            this, &SlopeStandardAnalysisWidget::openOutputDirectory);
    m_pages->addWidget(page);
}

void SlopeStandardAnalysisWidget::setCurrentStep(int step)
{
    if (m_pageRole == PageRole::SectionEditor)
        step = 0;
    else if (m_pageRole == PageRole::AnalysisSettings)
        step = 1;
    step = std::clamp(step, 0, m_pages->count() - 1);
    const bool hasSection = m_nodeTable && m_nodeTable->rowCount() >= 2
                            && m_segmentTable && m_segmentTable->rowCount() > 0;
    const bool hasResult = !m_lastPlot.isEmpty()
                           || (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                               && !m_cases.at(m_currentCaseIndex).lastJobDirectory.isEmpty());
    if (step >= 1 && !hasSection)
        step = 0;
    if (step >= 2 && !hasResult)
        step = hasSection ? 1 : 0;

    m_pages->setCurrentIndex(step);
    updateStepButtons();
    if (m_pageActive)
    {
        if (step == 2 && !m_lastPlot.isEmpty())
            updateResultVisualization(m_lastPlot);
        else
            updateVisualization();
    }
}

void SlopeStandardAnalysisWidget::updateStepButtons()
{
    const int current = m_pages ? m_pages->currentIndex() : 0;
    const bool hasSection = m_nodeTable && m_nodeTable->rowCount() >= 2
                            && m_segmentTable && m_segmentTable->rowCount() > 0;
    const bool hasResult = !m_lastPlot.isEmpty()
                           || (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                               && !m_cases.at(m_currentCaseIndex).lastJobDirectory.isEmpty());
    const QStringList labels{hasSection ? QStringLiteral("✓ 断面") : QStringLiteral("① 断面"),
                             QStringLiteral("② 材料 / 工况"),
                             hasResult ? QStringLiteral("✓ 结果") : QStringLiteral("③ 结果")};
    for (int i = 0; i < m_stepButtons.size(); ++i)
    {
        auto* button = m_stepButtons.at(i);
        button->setText(labels.value(i));
        button->setChecked(i == current);
        button->setEnabled(i == 0 || (i == 1 && hasSection) || (i == 2 && hasResult));
    }
    if (m_sectionNextButton)
        m_sectionNextButton->setEnabled(hasSection);
    if (m_advancedGeometryToggle)
        m_advancedGeometryToggle->setVisible(hasSection);
    if (m_advancedGeometryPanel && !hasSection)
        m_advancedGeometryPanel->hide();
}

QString SlopeStandardAnalysisWidget::newCaseId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject SlopeStandardAnalysisWidget::caseToJson(const CaseRecord& record)
{
    QJsonObject object;
    object[QStringLiteral("id")] = record.id;
    object[QStringLiteral("name")] = record.name;
    object[QStringLiteral("dat")] = record.dat;
    object[QStringLiteral("last_job")] = record.lastJobDirectory;
    object[QStringLiteral("source_type")] = record.sourceType;
    object[QStringLiteral("source_summary")] = record.sourceSummary;
    object[QStringLiteral("required_factor")] = record.requiredFactor;
    object[QStringLiteral("material_names")] = stringListToJson(record.materialNames);
    object[QStringLiteral("source_material_ids")] = intVectorToJson(record.sourceMaterialIds);
    object[QStringLiteral("state")] = record.state;
    return object;
}

SlopeStandardAnalysisWidget::CaseRecord
SlopeStandardAnalysisWidget::caseFromJson(const QJsonObject& object)
{
    CaseRecord record;
    record.id = object.value(QStringLiteral("id")).toString();
    if (record.id.isEmpty())
        record.id = newCaseId();
    record.name = object.value(QStringLiteral("name")).toString(QStringLiteral("工况"));
    record.dat = object.value(QStringLiteral("dat")).toString();
    record.lastJobDirectory = object.value(QStringLiteral("last_job")).toString();
    record.sourceType = object.value(QStringLiteral("source_type")).toString();
    record.sourceSummary = object.value(QStringLiteral("source_summary")).toString();
    record.requiredFactor = object.value(QStringLiteral("required_factor")).toDouble(1.30);
    record.materialNames = jsonToStringList(object.value(QStringLiteral("material_names")).toArray());
    record.sourceMaterialIds = jsonToIntVector(object.value(QStringLiteral("source_material_ids")).toArray());
    record.state = object.value(QStringLiteral("state")).toObject();
    return record;
}

void SlopeStandardAnalysisWidget::loadCases(const QJsonArray& savedCases,
                                            const QString& currentCaseId,
                                            const QString& legacyDatText,
                                            const QString& legacyLastJobDirectory)
{
    m_cases.clear();
    for (const QJsonValue& value : savedCases)
    {
        if (value.isObject())
        {
            m_cases.append(caseFromJson(value.toObject()));
            if (m_treeCaseMode)
                break; // 工程树工况节点一页只保存一个工况
        }
    }
    if (m_cases.isEmpty())
    {
        CaseRecord record;
        record.id = newCaseId();
        record.name = QStringLiteral("正常工况");
        record.dat = legacyDatText;
        record.lastJobDirectory = legacyLastJobDirectory;
        record.requiredFactor = 1.30;
        m_cases.append(record);
    }

    rebuildCaseCombo();
    int index = 0;
    if (!currentCaseId.isEmpty())
    {
        for (int i = 0; i < m_cases.size(); ++i)
            if (m_cases.at(i).id == currentCaseId)
            {
                index = i;
                break;
            }
    }
    loadCase(index);
}

void SlopeStandardAnalysisWidget::rebuildCaseCombo()
{
    const QSignalBlocker blocker(m_caseCombo);
    m_caseCombo->clear();
    for (const CaseRecord& record : m_cases)
        m_caseCombo->addItem(record.name, record.id);
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_caseCombo->count())
        m_caseCombo->setCurrentIndex(m_currentCaseIndex);
    m_deleteCaseButton->setEnabled(m_cases.size() > 1);
}

void SlopeStandardAnalysisWidget::saveCurrentCase()
{
    if (m_loadingCase || m_currentCaseIndex < 0 || m_currentCaseIndex >= m_cases.size())
        return;
    CaseRecord& record = m_cases[m_currentCaseIndex];
    record.state = serializeCurrentState();
    record.requiredFactor = m_requiredFactorSpin ? m_requiredFactorSpin->value() : 1.30;
    const QString title = m_titleEdit ? m_titleEdit->text().trimmed() : QString();
    if (!title.isEmpty())
        record.name = title;
    SlopeStandardDatModel model;
    if (collectFormModel(&model, nullptr))
        record.dat = model.serialize();
    record.lastJobDirectory = m_jobDirectory.isEmpty() ? record.lastJobDirectory : m_jobDirectory;
    m_caseDirty = false;
    rebuildCaseCombo();
}

void SlopeStandardAnalysisWidget::loadCase(int index)
{
    if (index < 0 || index >= m_cases.size())
        return;
    setSectionInteractionMode(SectionInteractionMode::None);
    m_pendingPhreaticNodeId = 0;
    m_pendingPhreaticSoilId = 1;
    m_loadingCase = true;
    m_currentCaseIndex = index;
    const CaseRecord& record = m_cases.at(index);
    {
        const QSignalBlocker blocker(m_caseCombo);
        m_caseCombo->setCurrentIndex(index);
    }

    resetEmptyForm();
    if (!record.state.isEmpty())
        applyCurrentState(record.state);
    else if (!record.dat.trimmed().isEmpty())
    {
        QString error;
        if (!loadFormFromDat(record.dat, &error))
        {
            resetEmptyForm();
            m_stateLabel->setText(QStringLiteral("旧工况数据无法载入：%1").arg(error));
        }
    }
    if (m_titleEdit->text().trimmed().isEmpty())
        m_titleEdit->setText(record.name);
    m_requiredFactorSpin->setValue(record.requiredFactor > 0.0 ? record.requiredFactor : 1.30);
    m_jobDirectory = record.lastJobDirectory;
    m_sourceStatusLabel->setText(record.sourceSummary.isEmpty()
                                     ? QStringLiteral("尚未获取断面")
                                     : record.sourceSummary);
    if (m_geometrySourceButton) m_geometrySourceButton->setChecked(record.sourceType == QStringLiteral("geometry"));
    if (m_mesh3dSourceButton) m_mesh3dSourceButton->setChecked(record.sourceType == QStringLiteral("mesh3d"));
    if (m_mesh2dSourceButton) m_mesh2dSourceButton->setChecked(record.sourceType == QStringLiteral("mesh2d"));
    if (m_importSourceButton) m_importSourceButton->setChecked(record.sourceType == QStringLiteral("dat"));
    if (m_meshSectionControls)
        m_meshSectionControls->setVisible(record.sourceType == QStringLiteral("mesh3d")
                                          || record.sourceType == QStringLiteral("geometry"));
    m_loadingCase = false;
    m_caseDirty = false;
    updateControlAvailability();
    updateInputPreview();
    updateRequiredFactorConclusion();
    m_lastPlot = QJsonObject();
    updateVisualization();
    m_resultViewer->clear();
    m_summaryLabel->clear();
    if (!record.lastJobDirectory.isEmpty())
        loadExistingResult(record.lastJobDirectory);
    else
    {
        m_openDirectoryButton->setEnabled(false);
        m_factorLabel->setText(QStringLiteral("安全系数：—"));
        m_conclusionLabel->setText(QStringLiteral("结论：尚未计算"));
    }
    const bool hasSection = m_nodeTable && m_nodeTable->rowCount() >= 2
                            && m_segmentTable && m_segmentTable->rowCount() > 0;
    const bool hasResult = !record.lastJobDirectory.isEmpty();
    setCurrentStep(hasResult ? 2 : (hasSection ? 1 : 0));
}

void SlopeStandardAnalysisWidget::createCase(bool copyCurrent)
{
    saveCurrentCase();
    CaseRecord record;
    if (copyCurrent && m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        record = m_cases.at(m_currentCaseIndex);
        record.id = newCaseId();
        record.name = record.name + QStringLiteral(" - 副本");
        record.lastJobDirectory.clear();
        if (!record.state.isEmpty())
            record.state[QStringLiteral("title")] = record.name;
    }
    else
    {
        record.id = newCaseId();
        record.name = QStringLiteral("新工况 %1").arg(m_cases.size() + 1);
        record.requiredFactor = 1.30;
    }
    m_cases.append(record);
    rebuildCaseCombo();
    loadCase(static_cast<int>(m_cases.size()) - 1);
    emit casesChanged();
}

void SlopeStandardAnalysisWidget::removeCurrentCase()
{
    if (m_cases.size() <= 1 || m_currentCaseIndex < 0)
        return;
    const QString name = m_cases.at(m_currentCaseIndex).name;
    if (QMessageBox::question(this, QStringLiteral("删除工况"),
                              QStringLiteral("确定删除工况“%1”？").arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    const int next = std::max(0, m_currentCaseIndex - 1);
    m_cases.removeAt(m_currentCaseIndex);
    m_currentCaseIndex = -1;
    rebuildCaseCombo();
    const int lastIndex = static_cast<int>(m_cases.size()) - 1;
    loadCase(std::min(next, lastIndex));
    emit casesChanged();
}

QJsonObject SlopeStandardAnalysisWidget::serializeCurrentState() const
{
    QJsonObject state;
    state[QStringLiteral("title")] = m_titleEdit->text();
    state[QStringLiteral("method")] = m_methodCombo->currentData().toInt();
    state[QStringLiteral("mode")] = m_modeCombo->currentData().toInt();
    state[QStringLiteral("search_step")] = m_searchStepSpin->value();
    state[QStringLiteral("slice_count")] = m_sliceCountSpin->value();
    state[QStringLiteral("radius")] = m_radiusSpin->value();
    state[QStringLiteral("circle_x")] = m_circleXSpin->value();
    state[QStringLiteral("circle_y")] = m_circleYSpin->value();
    state[QStringLiteral("water_weight")] = m_waterWeightSpin->value();
    state[QStringLiteral("outside_water_enabled")] = m_outsideWaterCheck->isChecked();
    state[QStringLiteral("outside_water_level")] = m_outsideWaterSpin->value();
    state[QStringLiteral("pore_method")] = m_poreMethodCombo->currentData().toInt();
    state[QStringLiteral("earthquake_enabled")] = m_earthquakeCheck->isChecked();
    state[QStringLiteral("slope_height")] = m_slopeHeightSpin->value();
    state[QStringLiteral("earthquake_intensity")] = m_earthquakeIntensitySpin->value();
    state[QStringLiteral("earthquake_mode")] = m_earthquakeModeCombo->currentData().toInt();
    state[QStringLiteral("earthquake_h")] = m_earthquakeDirectionHCombo->currentData().toInt();
    state[QStringLiteral("earthquake_v")] = m_earthquakeDirectionVCombo->currentData().toInt();
    state[QStringLiteral("required_factor")] = m_requiredFactorSpin->value();
    state[QStringLiteral("view_show_original_model")] = !m_showOriginalModelCheck || m_showOriginalModelCheck->isChecked();
    state[QStringLiteral("view_show_section_fill")] = !m_showSectionFillCheck || m_showSectionFillCheck->isChecked();
    state[QStringLiteral("view_show_surface")] = !m_showSurfaceCheck || m_showSurfaceCheck->isChecked();
    if (m_verticalCombo && m_verticalCombo->count() > 0)
        state[QStringLiteral("section_vertical_axis")] = m_verticalCombo->currentData().toInt();
    if (m_planeCombo && m_planeCombo->count() > 0)
        state[QStringLiteral("section_normal_axis")] = m_planeCombo->currentData().toInt();
    if (m_sectionPositionSpin)
        state[QStringLiteral("section_position")] = m_sectionPositionSpin->value();
    if (m_materialFieldCombo && m_materialFieldCombo->count() > 0)
    {
        state[QStringLiteral("section_material_field")] = m_materialFieldCombo->currentData().toString();
        state[QStringLiteral("material_region_source")] = m_materialFieldCombo->currentData().toString();
    }
    state[QStringLiteral("material_input_mode")] = QStringLiteral("manual_v1");
    if (m_simplificationToleranceSpin)
        state[QStringLiteral("section_simplify_tolerance")] = m_simplificationToleranceSpin->value();
    state[QStringLiteral("section_horizontal_reversed")] = m_sectionHorizontalReversed;
    state[QStringLiteral("section_horizontal_minimum")] = m_sectionHorizontalMinimum;
    state[QStringLiteral("section_horizontal_maximum")] = m_sectionHorizontalMaximum;
    state[QStringLiteral("section_horizontal_axis")] = m_sectionHorizontalAxis;
    state[QStringLiteral("section_display_vertical_axis")] = m_sectionVerticalAxis;
    state[QStringLiteral("section_display_normal_axis")] = m_sectionNormalAxis;
    state[QStringLiteral("section_world_offset")] = m_sectionWorldOffset;

    const auto tableToJson = [](const QTableWidget* table)
    {
        QJsonArray rows;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            QJsonArray values;
            for (int column = 0; column < table->columnCount(); ++column)
                values.append(table->item(row, column) ? table->item(row, column)->text() : QString());
            rows.append(values);
        }
        return rows;
    };
    state[QStringLiteral("materials")] = tableToJson(m_soilTable);
    state[QStringLiteral("nodes")] = tableToJson(m_nodeTable);
    state[QStringLiteral("segments")] = tableToJson(m_segmentTable);
    state[QStringLiteral("surfaces")] = tableToJson(m_surfaceTable);
    state[QStringLiteral("phreatic")] = tableToJson(m_phreaticTable);
    return state;
}

void SlopeStandardAnalysisWidget::applyCurrentState(const QJsonObject& state)
{
    const auto setComboData = [](QComboBox* combo, int value)
    {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    m_titleEdit->setText(state.value(QStringLiteral("title")).toString());
    setComboData(m_methodCombo, state.value(QStringLiteral("method")).toInt(2));
    setComboData(m_modeCombo, state.value(QStringLiteral("mode")).toInt(1));
    m_searchStepSpin->setValue(state.value(QStringLiteral("search_step")).toDouble(0.2));
    m_sliceCountSpin->setValue(state.value(QStringLiteral("slice_count")).toInt(50));
    m_radiusSpin->setValue(state.value(QStringLiteral("radius")).toDouble(12.8));
    m_circleXSpin->setValue(state.value(QStringLiteral("circle_x")).toDouble(5.0));
    m_circleYSpin->setValue(state.value(QStringLiteral("circle_y")).toDouble(12.0));
    m_waterWeightSpin->setValue(state.value(QStringLiteral("water_weight")).toDouble(9.81));
    m_outsideWaterCheck->setChecked(state.value(QStringLiteral("outside_water_enabled")).toBool(false));
    m_outsideWaterSpin->setValue(state.value(QStringLiteral("outside_water_level")).toDouble(0.0));
    setComboData(m_poreMethodCombo, state.value(QStringLiteral("pore_method")).toInt(-1));
    m_earthquakeCheck->setChecked(state.value(QStringLiteral("earthquake_enabled")).toBool(false));
    m_slopeHeightSpin->setValue(state.value(QStringLiteral("slope_height")).toDouble(0.0));
    m_earthquakeIntensitySpin->setValue(state.value(QStringLiteral("earthquake_intensity")).toInt(0));
    setComboData(m_earthquakeModeCombo, state.value(QStringLiteral("earthquake_mode")).toInt(0));
    setComboData(m_earthquakeDirectionHCombo, state.value(QStringLiteral("earthquake_h")).toInt(0));
    setComboData(m_earthquakeDirectionVCombo, state.value(QStringLiteral("earthquake_v")).toInt(0));
    m_requiredFactorSpin->setValue(state.value(QStringLiteral("required_factor")).toDouble(1.30));
    if (m_showOriginalModelCheck)
        m_showOriginalModelCheck->setChecked(state.value(QStringLiteral("view_show_original_model")).toBool(true));
    if (m_showSectionFillCheck)
        m_showSectionFillCheck->setChecked(state.value(QStringLiteral("view_show_section_fill")).toBool(true));
    if (m_showSurfaceCheck)
        m_showSurfaceCheck->setChecked(state.value(QStringLiteral("view_show_surface")).toBool(true));

    if (state.contains(QStringLiteral("section_vertical_axis"))
        || state.contains(QStringLiteral("section_normal_axis")))
    {
        initializeMeshSectionControls();
        const int vertical = state.value(QStringLiteral("section_vertical_axis")).toInt(1);
        int index = m_verticalCombo->findData(vertical);
        if (index >= 0) m_verticalCombo->setCurrentIndex(index);
        rebuildPlaneChoices();
        const int normal = state.value(QStringLiteral("section_normal_axis")).toInt(2);
        index = m_planeCombo->findData(normal);
        if (index >= 0) m_planeCombo->setCurrentIndex(index);
        updatePlaneRange(false);
        if (state.contains(QStringLiteral("section_position")))
            m_sectionPositionSpin->setValue(state.value(QStringLiteral("section_position")).toDouble());
        const QString field = state.value(QStringLiteral("section_material_field")).toString();
        index = m_materialFieldCombo->findData(field);
        if (index >= 0) m_materialFieldCombo->setCurrentIndex(index);
        if (state.contains(QStringLiteral("section_simplify_tolerance")))
            m_simplificationToleranceSpin->setValue(
                state.value(QStringLiteral("section_simplify_tolerance")).toDouble());
    }

    m_sectionHorizontalReversed = state.value(QStringLiteral("section_horizontal_reversed")).toBool(false);
    m_sectionHorizontalMinimum = state.value(QStringLiteral("section_horizontal_minimum")).toDouble(0.0);
    m_sectionHorizontalMaximum = state.value(QStringLiteral("section_horizontal_maximum")).toDouble(0.0);
    m_sectionVerticalAxis = state.contains(QStringLiteral("section_display_vertical_axis"))
                                ? state.value(QStringLiteral("section_display_vertical_axis")).toInt(1)
                                : state.value(QStringLiteral("section_vertical_axis")).toInt(1);
    m_sectionNormalAxis = state.contains(QStringLiteral("section_display_normal_axis"))
                              ? state.value(QStringLiteral("section_display_normal_axis")).toInt(2)
                              : state.value(QStringLiteral("section_normal_axis")).toInt(2);
    m_sectionHorizontalAxis = state.contains(QStringLiteral("section_horizontal_axis"))
                                  ? state.value(QStringLiteral("section_horizontal_axis")).toInt(0)
                                  : 3 - m_sectionVerticalAxis - m_sectionNormalAxis;
    m_sectionWorldOffset = state.contains(QStringLiteral("section_world_offset"))
                               ? state.value(QStringLiteral("section_world_offset")).toDouble(0.0)
                               : state.value(QStringLiteral("section_position")).toDouble(0.0);

    const auto jsonToTable = [](const QJsonArray& rows, QTableWidget* table)
    {
        table->clearContents();
        table->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row)
        {
            const QJsonArray values = rows.at(row).toArray();
            for (int column = 0; column < table->columnCount(); ++column)
            {
                auto* item = new QTableWidgetItem(column < values.size() ? values.at(column).toString() : QString());
                if ((table->columnCount() == 3 && table->horizontalHeaderItem(0)->text() == QStringLiteral("ID") && column == 0)
                    || (table->columnCount() == 7 && column == 0))
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, column, item);
            }
        }
    };
    jsonToTable(state.value(QStringLiteral("materials")).toArray(), m_soilTable);
    jsonToTable(state.value(QStringLiteral("nodes")).toArray(), m_nodeTable);
    jsonToTable(state.value(QStringLiteral("segments")).toArray(), m_segmentTable);
    jsonToTable(state.value(QStringLiteral("surfaces")).toArray(), m_surfaceTable);
    jsonToTable(state.value(QStringLiteral("phreatic")).toArray(), m_phreaticTable);
    rebuildMaterialRegionEditor();
    m_datEditor->setPlainText(state.value(QStringLiteral("dat_debug")).toString());
    m_datEditor->document()->setModified(false);

    // Sync friendly presets with the restored raw values without overwriting them.
    int precisionIndex = 1;
    if (m_searchStepSpin->value() >= 0.35) precisionIndex = 0;
    else if (m_searchStepSpin->value() <= 0.15) precisionIndex = 2;
    m_searchPrecisionCombo->setCurrentIndex(precisionIndex);
    m_computePrecisionCombo->setCurrentIndex(m_sliceCountSpin->value() >= 80 ? 1 : 0);
}

void SlopeStandardAnalysisWidget::resetEmptyForm()
{
    m_titleEdit->clear();
    m_methodCombo->setCurrentIndex(m_methodCombo->findData(2));
    m_modeCombo->setCurrentIndex(m_modeCombo->findData(1));
    m_searchPrecisionCombo->setCurrentIndex(1);
    m_computePrecisionCombo->setCurrentIndex(0);
    m_searchStepSpin->setValue(0.2);
    m_sliceCountSpin->setValue(50);
    m_radiusSpin->setValue(12.8);
    m_circleXSpin->setValue(5.0);
    m_circleYSpin->setValue(12.0);
    m_waterWeightSpin->setValue(9.81);
    m_outsideWaterCheck->setChecked(false);
    m_outsideWaterSpin->setValue(0.0);
    m_poreMethodCombo->setCurrentIndex(m_poreMethodCombo->findData(-1));
    m_earthquakeCheck->setChecked(false);
    m_slopeHeightSpin->setValue(0.0);
    m_earthquakeIntensitySpin->setValue(0);
    m_earthquakeModeCombo->setCurrentIndex(0);
    m_earthquakeDirectionHCombo->setCurrentIndex(m_earthquakeDirectionHCombo->findData(0));
    m_earthquakeDirectionVCombo->setCurrentIndex(m_earthquakeDirectionVCombo->findData(0));
    m_requiredFactorSpin->setValue(1.30);
    for (QTableWidget* table : {m_soilTable, m_nodeTable, m_segmentTable,
                                m_surfaceTable, m_phreaticTable})
    {
        table->clearContents();
        table->setRowCount(0);
    }
    rebuildMaterialRegionEditor();
    m_datEditor->clear();
    m_datEditor->document()->setModified(false);
    m_sourceStatusLabel->setText(QStringLiteral("尚未获取断面"));
    if (m_meshSectionControls) m_meshSectionControls->hide();
    if (m_sourceButtonGroup) m_sourceButtonGroup->setExclusive(false);
    for (QPushButton* button : {m_geometrySourceButton, m_mesh3dSourceButton,
                                m_mesh2dSourceButton, m_importSourceButton})
    {
        if (button) button->setChecked(false);
    }
    if (m_sourceButtonGroup) m_sourceButtonGroup->setExclusive(true);
    if (m_advancedGeometryToggle)
    {
        m_advancedGeometryToggle->setChecked(false);
        m_advancedGeometryToggle->hide();
    }
    m_jobDirectory.clear();
    m_statusPath.clear(); m_plotPath.clear(); m_resultPath.clear();
    m_lastPlot = QJsonObject();
    m_sectionHorizontalReversed = false;
    m_sectionHorizontalMinimum = 0.0;
    m_sectionHorizontalMaximum = 0.0;
    m_sectionHorizontalAxis = 0;
    m_sectionVerticalAxis = 1;
    m_sectionNormalAxis = 2;
    m_sectionWorldOffset = 0.0;
    updateStepButtons();
    updateVisualization();
}

void SlopeStandardAnalysisWidget::markCaseDirty()
{
    if (m_loadingCase)
        return;
    m_caseDirty = true;
    emit casesChanged();
}

bool SlopeStandardAnalysisWidget::loadFormFromDat(const QString& text, QString* errorMessage)
{
    SlopeStandardDatModel model;
    if (!SlopeStandardDatModel::parse(text, &model, errorMessage))
        return false;

    const bool previousLoading = m_loadingCase;
    m_loadingCase = true;
    const auto selectData = [](QComboBox* combo, int value)
    {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    const auto setCell = [](QTableWidget* table, int row, int column,
                            const QString& value, bool editable = true)
    {
        auto* item = new QTableWidgetItem(value);
        if (!editable)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    };

    m_titleEdit->setText(model.projectTitle);
    selectData(m_methodCombo, model.method);
    selectData(m_modeCombo, model.mode);
    m_searchStepSpin->setValue(model.searchStep);
    m_sliceCountSpin->setValue(model.sliceCount);
    m_radiusSpin->setValue(model.radius);
    m_circleXSpin->setValue(model.circleX);
    m_circleYSpin->setValue(model.circleY);
    m_waterWeightSpin->setValue(model.waterUnitWeight);
    const bool hasOutsideWater = std::abs(model.outsideWaterLevel - 9999.0) > 1.0e-8;
    m_outsideWaterCheck->setChecked(hasOutsideWater);
    m_outsideWaterSpin->setValue(hasOutsideWater ? model.outsideWaterLevel : 0.0);
    selectData(m_poreMethodCombo, model.poreMethod());
    m_earthquakeCheck->setChecked(model.earthquakeEnabled != 0);
    m_slopeHeightSpin->setValue(model.slopeHeight);
    m_earthquakeIntensitySpin->setValue(model.earthquakeIntensity);
    selectData(m_earthquakeModeCombo, model.earthquakeMode);
    selectData(m_earthquakeDirectionHCombo, model.earthquakeDirectionH);
    selectData(m_earthquakeDirectionVCombo, model.earthquakeDirectionV);

    m_soilTable->clearContents();
    m_soilTable->setRowCount(model.soils.size());
    for (int row = 0; row < model.soils.size(); ++row)
    {
        const auto& item = model.soils.at(row);
        const QString regionName = QStringLiteral("材料区域 %1").arg(row + 1);
        setCell(m_soilTable, row, 0, regionName, false);
        setCell(m_soilTable, row, 1, regionName, false);
        setCell(m_soilTable, row, 2, displayNumber(item.frictionAngle));
        setCell(m_soilTable, row, 3, displayNumber(item.cohesion));
        setCell(m_soilTable, row, 4, displayNumber(item.naturalUnitWeight));
        setCell(m_soilTable, row, 5, displayNumber(item.saturatedUnitWeight));
        setCell(m_soilTable, row, 6, displayNumber(item.ru));
    }
    m_nodeTable->clearContents();
    m_nodeTable->setRowCount(model.nodes.size());
    for (int row = 0; row < model.nodes.size(); ++row)
    {
        const auto& item = model.nodes.at(row);
        setCell(m_nodeTable, row, 0, QString::number(row + 1), false);
        setCell(m_nodeTable, row, 1, displayNumber(item.x));
        setCell(m_nodeTable, row, 2, displayNumber(item.y));
    }
    m_segmentTable->clearContents();
    m_segmentTable->setRowCount(model.segments.size());
    for (int row = 0; row < model.segments.size(); ++row)
    {
        const auto& item = model.segments.at(row);
        setCell(m_segmentTable, row, 0, QString::number(item.startNode));
        setCell(m_segmentTable, row, 1, QString::number(item.endNode));
        setCell(m_segmentTable, row, 2, QString::number(item.soilId));
    }
    m_surfaceTable->clearContents();
    m_surfaceTable->setRowCount(model.surfaceLoads.size());
    for (int row = 0; row < model.surfaceLoads.size(); ++row)
    {
        const auto& item = model.surfaceLoads.at(row);
        setCell(m_surfaceTable, row, 0, QString::number(item.segmentId));
        setCell(m_surfaceTable, row, 1, displayNumber(item.force));
    }
    m_phreaticTable->clearContents();
    m_phreaticTable->setRowCount(model.phreaticSegments.size());
    for (int row = 0; row < model.phreaticSegments.size(); ++row)
    {
        const auto& item = model.phreaticSegments.at(row);
        setCell(m_phreaticTable, row, 0, QString::number(item.startNode));
        setCell(m_phreaticTable, row, 1, QString::number(item.endNode));
        setCell(m_phreaticTable, row, 2, QString::number(item.soilId));
    }
    m_datEditor->setPlainText(model.serialize());
    m_datEditor->document()->setModified(false);
    m_searchPrecisionCombo->setCurrentIndex(model.searchStep >= 0.35 ? 0 : (model.searchStep <= 0.15 ? 2 : 1));
    m_computePrecisionCombo->setCurrentIndex(model.sliceCount >= 80 ? 1 : 0);
    m_loadingCase = previousLoading;
    updateControlAvailability();
    updateInputPreview();
    return true;
}

bool SlopeStandardAnalysisWidget::collectFormModel(SlopeStandardDatModel* model,
                                                   QString* errorMessage) const
{
    if (!model)
        return false;
    auto fail = [&](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    SlopeStandardDatModel value;
    value.projectTitle = m_titleEdit->text().trimmed();
    value.method = m_methodCombo->currentData().toInt();
    value.mode = m_modeCombo->currentData().toInt();
    value.searchStep = m_searchStepSpin->value();
    value.sliceCount = m_sliceCountSpin->value();
    value.radius = m_radiusSpin->value();
    value.circleX = m_circleXSpin->value();
    value.circleY = m_circleYSpin->value();
    value.waterUnitWeight = m_waterWeightSpin->value();
    value.outsideWaterLevel = m_outsideWaterCheck->isChecked() ? m_outsideWaterSpin->value() : 9999.0;
    value.slopeHeight = m_slopeHeightSpin->value();
    value.earthquakeEnabled = m_earthquakeCheck->isChecked() ? 1 : 0;
    value.earthquakeIntensity = m_earthquakeIntensitySpin->value();
    value.earthquakeMode = m_earthquakeModeCombo->currentData().toInt();
    value.earthquakeDirectionH = m_earthquakeDirectionHCombo->currentData().toInt();
    value.earthquakeDirectionV = m_earthquakeDirectionVCombo->currentData().toInt();
    const int poreMethod = m_poreMethodCombo->currentData().toInt();

    if (value.projectTitle.isEmpty())
        return fail(QStringLiteral("请填写算例名称。"));
    if (m_soilTable->rowCount() <= 0)
        return fail(QStringLiteral("还没有材料区域，请先设置断面。"));

    const auto cellText = [](const QTableWidget* table, int row, int column)
    {
        const QTableWidgetItem* item = table->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    const auto readInt = [&](const QTableWidget* table, int row, int column,
                             const QString& field, int* result)
    {
        bool ok = false;
        const int parsed = cellText(table, row, column).toInt(&ok);
        if (!ok)
            return fail(QStringLiteral("高级几何编辑中第 %1 行“%2”必须填写整数。")
                            .arg(row + 1).arg(field));
        *result = parsed;
        return true;
    };
    const auto readReal = [&](const QTableWidget* table, int row, int column,
                              const QString& field, double* result,
                              const QString& materialName = QString())
    {
        QString text = cellText(table, row, column);
        if (text.isEmpty())
            return fail(materialName.isEmpty()
                            ? QStringLiteral("第 %1 行“%2”不能为空。").arg(row + 1).arg(field)
                            : QStringLiteral("%1 的%2不能为空。").arg(materialName, field));
        text.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);
        bool ok = false;
        const double parsed = text.toDouble(&ok);
        if (!ok || !std::isfinite(parsed))
            return fail(materialName.isEmpty()
                            ? QStringLiteral("第 %1 行“%2”必须填写有效数值。").arg(row + 1).arg(field)
                            : QStringLiteral("%1 的%2必须填写有效数值。").arg(materialName, field));
        *result = parsed;
        return true;
    };

    value.soils.reserve(m_soilTable->rowCount());
    for (int row = 0; row < m_soilTable->rowCount(); ++row)
    {
        const QString gridMaterial = cellText(m_soilTable, row, 0);
        const QString materialName = gridMaterial.isEmpty()
                                         ? QStringLiteral("材料区域 %1").arg(row + 1)
                                         : gridMaterial;
        SlopeStandardSoil item;
        item.id = row + 1;
        item.poreMethod = poreMethod;
        if (!readReal(m_soilTable, row, 2, QStringLiteral("内摩擦角 φ"), &item.frictionAngle, materialName)
            || !readReal(m_soilTable, row, 3, QStringLiteral("黏聚力 c"), &item.cohesion, materialName)
            || !readReal(m_soilTable, row, 4, QStringLiteral("天然重度 γ"), &item.naturalUnitWeight, materialName)
            || !readReal(m_soilTable, row, 5, QStringLiteral("饱和重度 γsat"), &item.saturatedUnitWeight, materialName))
            return false;
        if (poreMethod == 2)
        {
            if (!readReal(m_soilTable, row, 6, QStringLiteral("孔压系数 Ru"), &item.ru, materialName))
                return false;
        }
        else
            item.ru = 0.0;
        if (item.saturatedUnitWeight < item.naturalUnitWeight)
            return fail(QStringLiteral("%1 的饱和重度 γsat 不能小于天然重度 γ。")
                            .arg(materialName));
        value.soils.append(item);
    }

    value.nodes.reserve(m_nodeTable->rowCount());
    for (int row = 0; row < m_nodeTable->rowCount(); ++row)
    {
        SlopeStandardNode item;
        item.id = row + 1;
        if (!readReal(m_nodeTable, row, 1, QStringLiteral("水平坐标"), &item.x)
            || !readReal(m_nodeTable, row, 2, QStringLiteral("高程"), &item.y))
            return false;
        value.nodes.append(item);
    }
    value.segments.reserve(m_segmentTable->rowCount());
    for (int row = 0; row < m_segmentTable->rowCount(); ++row)
    {
        SlopeStandardSegment item;
        if (!readInt(m_segmentTable, row, 0, QStringLiteral("起点"), &item.startNode)
            || !readInt(m_segmentTable, row, 1, QStringLiteral("终点"), &item.endNode)
            || !readInt(m_segmentTable, row, 2, QStringLiteral("土层"), &item.soilId))
            return false;
        value.segments.append(item);
    }
    value.surfaceLoads.reserve(m_surfaceTable->rowCount());
    for (int row = 0; row < m_surfaceTable->rowCount(); ++row)
    {
        SlopeStandardSurfaceLoad item;
        if (!readInt(m_surfaceTable, row, 0, QStringLiteral("边界线"), &item.segmentId)
            || !readReal(m_surfaceTable, row, 1, QStringLiteral("坡面荷载"), &item.force))
            return false;
        value.surfaceLoads.append(item);
    }
    value.phreaticSegments.reserve(m_phreaticTable->rowCount());
    for (int row = 0; row < m_phreaticTable->rowCount(); ++row)
    {
        SlopeStandardSegment item;
        if (!readInt(m_phreaticTable, row, 0, QStringLiteral("起点"), &item.startNode)
            || !readInt(m_phreaticTable, row, 1, QStringLiteral("终点"), &item.endNode)
            || !readInt(m_phreaticTable, row, 2, QStringLiteral("土层"), &item.soilId))
            return false;
        value.phreaticSegments.append(item);
    }

    if (!value.validate(errorMessage))
        return false;

    // The wrapped legacy engine evaluates the exposed slope as y(x).  Do not
    // silently delete vertical pieces (the old UI did that), and do not send an
    // incompatible profile to the engine either: keep the geometry visible and
    // stop here with an engineering-facing message.
    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    for (const auto& node : value.nodes)
    {
        xMin = std::min(xMin, node.x);
        xMax = std::max(xMax, node.x);
    }
    const double xTolerance = std::max(1.0e-10, std::abs(xMax - xMin) * 1.0e-10);
    for (const auto& surface : value.surfaceLoads)
    {
        const int segmentIndex = surface.segmentId - 1;
        if (segmentIndex < 0 || segmentIndex >= value.segments.size())
            continue;
        const auto& segment = value.segments.at(segmentIndex);
        const int a = segment.startNode - 1;
        const int b = segment.endNode - 1;
        if (a < 0 || b < 0 || a >= value.nodes.size() || b >= value.nodes.size())
            continue;
        if (std::abs(value.nodes.at(a).x - value.nodes.at(b).x) <= xTolerance)
        {
            return fail(QStringLiteral(
                "计算坡面中包含竖直段（边界线 %1），当前计算不支持。请调整断面方向或坡面。")
                            .arg(surface.segmentId));
        }
    }

    value.ensureSearchCircleNearSection();
    *model = value;
    return true;
}

SlopeStandardDatModel SlopeStandardAnalysisWidget::collectPreviewModel() const
{
    SlopeStandardDatModel value;
    value.projectTitle = m_titleEdit->text().trimmed();
    value.method = m_methodCombo->currentData().toInt();
    value.mode = m_modeCombo->currentData().toInt();
    value.searchStep = m_searchStepSpin->value();
    value.sliceCount = m_sliceCountSpin->value();
    value.radius = m_radiusSpin->value();
    value.circleX = m_circleXSpin->value();
    value.circleY = m_circleYSpin->value();
    value.waterUnitWeight = m_waterWeightSpin->value();
    value.outsideWaterLevel = m_outsideWaterCheck->isChecked() ? m_outsideWaterSpin->value() : 9999.0;
    value.slopeHeight = m_slopeHeightSpin->value();
    value.earthquakeEnabled = m_earthquakeCheck->isChecked() ? 1 : 0;
    value.earthquakeIntensity = m_earthquakeIntensitySpin->value();
    value.earthquakeMode = m_earthquakeModeCombo->currentData().toInt();
    value.earthquakeDirectionH = m_earthquakeDirectionHCombo->currentData().toInt();
    value.earthquakeDirectionV = m_earthquakeDirectionVCombo->currentData().toInt();
    const int poreMethod = m_poreMethodCombo->currentData().toInt();

    for (int row = 0; row < m_soilTable->rowCount(); ++row)
    {
        SlopeStandardSoil soil;
        soil.id = row + 1;
        soil.poreMethod = poreMethod;
        const auto read = [&](int column, double fallback = 0.0) {
            bool ok = false;
            const double number = m_soilTable->item(row, column)
                                      ? m_soilTable->item(row, column)->text().toDouble(&ok) : 0.0;
            return ok ? number : fallback;
        };
        soil.frictionAngle = read(2);
        soil.cohesion = read(3);
        soil.naturalUnitWeight = read(4);
        soil.saturatedUnitWeight = read(5);
        soil.ru = read(6);
        value.soils.append(soil);
    }
    for (int row = 0; row < m_nodeTable->rowCount(); ++row)
    {
        bool xOk = false, yOk = false;
        const double x = m_nodeTable->item(row, 1) ? m_nodeTable->item(row, 1)->text().toDouble(&xOk) : 0.0;
        const double y = m_nodeTable->item(row, 2) ? m_nodeTable->item(row, 2)->text().toDouble(&yOk) : 0.0;
        if (xOk && yOk)
            value.nodes.append({row + 1, x, y});
    }
    for (int row = 0; row < m_segmentTable->rowCount(); ++row)
    {
        bool aOk = false, bOk = false, sOk = false;
        const int a = m_segmentTable->item(row, 0) ? m_segmentTable->item(row, 0)->text().toInt(&aOk) : 0;
        const int b = m_segmentTable->item(row, 1) ? m_segmentTable->item(row, 1)->text().toInt(&bOk) : 0;
        const int s = m_segmentTable->item(row, 2) ? m_segmentTable->item(row, 2)->text().toInt(&sOk) : 0;
        if (aOk && bOk && sOk)
            value.segments.append({a, b, s});
    }
    for (int row = 0; row < m_surfaceTable->rowCount(); ++row)
    {
        bool idOk = false, fOk = false;
        const int id = m_surfaceTable->item(row, 0) ? m_surfaceTable->item(row, 0)->text().toInt(&idOk) : 0;
        const double force = m_surfaceTable->item(row, 1) ? m_surfaceTable->item(row, 1)->text().toDouble(&fOk) : 0.0;
        if (idOk)
            value.surfaceLoads.append({id, fOk ? force : 0.0});
    }
    for (int row = 0; row < m_phreaticTable->rowCount(); ++row)
    {
        bool aOk = false, bOk = false, sOk = false;
        const int a = m_phreaticTable->item(row, 0) ? m_phreaticTable->item(row, 0)->text().toInt(&aOk) : 0;
        const int b = m_phreaticTable->item(row, 1) ? m_phreaticTable->item(row, 1)->text().toInt(&bOk) : 0;
        const int s = m_phreaticTable->item(row, 2) ? m_phreaticTable->item(row, 2)->text().toInt(&sOk) : 0;
        if (aOk && bOk && sOk)
            value.phreaticSegments.append({a, b, s});
    }
    return value;
}

bool SlopeStandardAnalysisWidget::syncDatFromForm(QString* errorMessage)
{
    SlopeStandardDatModel model;
    if (!collectFormModel(&model, errorMessage))
        return false;
    const QString serialized = model.serialize();
    // Keep saved state and the next process input consistent with the seed.
    const QSignalBlocker radiusBlocker(m_radiusSpin);
    const QSignalBlocker xBlocker(m_circleXSpin);
    const QSignalBlocker yBlocker(m_circleYSpin);
    m_radiusSpin->setValue(model.radius);
    m_circleXSpin->setValue(model.circleX);
    m_circleYSpin->setValue(model.circleY);
    const QSignalBlocker blocker(m_datEditor);
    m_datEditor->setPlainText(serialized);
    m_datEditor->document()->setModified(false);
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
        m_cases[m_currentCaseIndex].dat = serialized;
    return true;
}

void SlopeStandardAnalysisWidget::updateControlAvailability()
{
    if (!m_modeCombo)
        return;
    const bool searching = m_modeCombo->currentData().toInt() == 1;
    m_searchPrecisionCombo->setEnabled(searching);
    m_searchStepSpin->setEnabled(searching);
    m_radiusSpin->setEnabled(!searching);
    m_circleXSpin->setEnabled(!searching);
    m_circleYSpin->setEnabled(!searching);
    m_outsideWaterSpin->setEnabled(m_outsideWaterCheck->isChecked());
    const bool earthquake = m_earthquakeCheck->isChecked();
    m_earthquakeIntensitySpin->setEnabled(earthquake);
    m_earthquakeModeCombo->setEnabled(earthquake);
    m_earthquakeDirectionHCombo->setEnabled(earthquake);
    m_earthquakeDirectionVCombo->setEnabled(earthquake);
    const int poreMethod = m_poreMethodCombo->currentData().toInt();
    rebuildMaterialRegionEditor();
    if (m_ruEditorLabel) m_ruEditorLabel->setVisible(poreMethod == 2);
    if (m_ruSpin) m_ruSpin->setVisible(poreMethod == 2);
    m_phreaticTable->setEnabled(poreMethod == 0);
    const bool hasSection = m_nodeTable && m_nodeTable->rowCount() >= 2
                            && m_segmentTable && m_segmentTable->rowCount() > 0
                            && m_surfaceTable && m_surfaceTable->rowCount() > 0;
    if (m_editSurfaceButton) m_editSurfaceButton->setEnabled(hasSection);
    if (m_sectionNextButton) m_sectionNextButton->setEnabled(hasSection);
    if (m_calculateButton && m_pageRole == PageRole::AnalysisSettings)
        m_calculateButton->setEnabled(hasSection && !isRunning());
    updateSectionStatusSummary();
    if (m_drawPhreaticButton)
    {
        m_drawPhreaticButton->setVisible(poreMethod == 0);
        m_drawPhreaticButton->setEnabled(poreMethod == 0 && hasSection);
        if (poreMethod != 0 && m_drawPhreaticButton->isChecked())
            m_drawPhreaticButton->setChecked(false);
    }
    if (m_clearPhreaticButton)
    {
        m_clearPhreaticButton->setVisible(poreMethod == 0);
        m_clearPhreaticButton->setEnabled(poreMethod == 0 && m_phreaticTable->rowCount() > 0);
    }
    updateRequiredFactorConclusion();
}

void SlopeStandardAnalysisWidget::updateInputPreview()
{
    updateVisualization();
}


void SlopeStandardAnalysisWidget::saveSectionOnlyCameraState()
{
    if (m_sectionOnlyCameraSaved)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = visual ? visual->renderer() : nullptr;
    vtkCamera* camera = renderer ? renderer->GetActiveCamera() : nullptr;
    if (!camera)
        return;

    camera->GetPosition(m_savedCameraPosition);
    camera->GetFocalPoint(m_savedCameraFocalPoint);
    camera->GetViewUp(m_savedCameraViewUp);
    m_savedCameraParallelScale = camera->GetParallelScale();
    m_savedCameraViewAngle = camera->GetViewAngle();
    m_savedCameraParallelProjection = camera->GetParallelProjection();
    m_sectionOnlyCameraSaved = true;
}

void SlopeStandardAnalysisWidget::applySectionOnlyCamera()
{
    if (!m_pageActive || m_pageRole != PageRole::AnalysisSettings)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = visual ? visual->renderer() : nullptr;
    vtkCamera* camera = renderer ? renderer->GetActiveCamera() : nullptr;
    if (!camera)
        return;

    // 参数页中的断面统一铺到 XY 平面，因此固定从 +Z 正视并使用正交投影。
    camera->SetPosition(0.0, 0.0, 1.0);
    camera->SetFocalPoint(0.0, 0.0, 0.0);
    camera->SetViewUp(0.0, 1.0, 0.0);
    camera->ParallelProjectionOn();
    renderer->ResetCamera();
    renderer->ResetCameraClippingRange();
    visual->render();
}

void SlopeStandardAnalysisWidget::restoreSectionOnlyCameraState()
{
    if (!m_sectionOnlyCameraSaved)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = visual ? visual->renderer() : nullptr;
    vtkCamera* camera = renderer ? renderer->GetActiveCamera() : nullptr;
    if (camera)
    {
        camera->SetPosition(m_savedCameraPosition);
        camera->SetFocalPoint(m_savedCameraFocalPoint);
        camera->SetViewUp(m_savedCameraViewUp);
        camera->SetParallelProjection(m_savedCameraParallelProjection);
        camera->SetParallelScale(m_savedCameraParallelScale);
        camera->SetViewAngle(m_savedCameraViewAngle);
        renderer->ResetCameraClippingRange();
    }
    m_sectionOnlyCameraSaved = false;
}

void SlopeStandardAnalysisWidget::setOriginalModelVisible(bool visible)
{
    if (visible)
    {
        restoreOriginalModelVisibility();
        return;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    const QString limitPrefix = QStringLiteral("slope.limit.%1.").arg(m_analysisUuid);
    const QList<QString> allIds = visual->allIds();
    QSet<vtkProp*> ourProps;
    if (m_sectionPickActor)
        ourProps.insert(m_sectionPickActor.GetPointer());
    if (m_sectionFillPickActor)
        ourProps.insert(m_sectionFillPickActor.GetPointer());

    for (const QString& id : allIds)
    {
        if (id.startsWith(limitPrefix) || id.startsWith(m_analysisUuid))
        {
            if (auto* p = visual->getProp(id))
                ourProps.insert(p);
            continue;
        }
        vtkProp* prop = visual->getProp(id);
        if (prop && prop->GetVisibility() != 0)
        {
            if (!m_savedPropVisibilities.contains(id))
                m_savedPropVisibilities.insert(id, true);
            visual->setVisible(id, false, false);
        }
    }

    if (auto* ren = visual->renderer())
    {
        vtkPropCollection* props = ren->GetViewProps();
        if (props)
        {
            props->InitTraversal();
            while (vtkProp* prop = props->GetNextProp())
            {
                if (!prop || prop->GetVisibility() == 0 || ourProps.contains(prop))
                    continue;
                if (!m_savedRendererProps.contains(prop))
                {
                    m_savedRendererProps.append(prop);
                    prop->SetVisibility(0);
                }
            }
        }
    }

    m_originalModelHiddenByPage = true;
}

void SlopeStandardAnalysisWidget::restoreOriginalModelVisibility()
{
    if (!m_originalModelHiddenByPage)
        return;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (visual)
    {
        for (auto it = m_savedPropVisibilities.constBegin(); it != m_savedPropVisibilities.constEnd(); ++it)
        {
            visual->setVisible(it.key(), it.value(), false);
        }
        for (vtkProp* prop : m_savedRendererProps)
        {
            if (prop)
                prop->SetVisibility(1);
        }
        visual->render();
    }
    m_savedPropVisibilities.clear();
    m_savedRendererProps.clear();
    m_originalModelHiddenByPage = false;
}

void SlopeStandardAnalysisWidget::setSectionInteractionMode(SectionInteractionMode mode)
{
    m_sectionInteractionMode = mode;
    if (m_editSurfaceButton && (mode != SectionInteractionMode::SurfaceEdit) && m_editSurfaceButton->isChecked())
    {
        const QSignalBlocker blocker(m_editSurfaceButton); m_editSurfaceButton->setChecked(false);
    }
    if (m_drawPhreaticButton && (mode != SectionInteractionMode::PhreaticDraw) && m_drawPhreaticButton->isChecked())
    {
        const QSignalBlocker blocker(m_drawPhreaticButton); m_drawPhreaticButton->setChecked(false);
    }
    if (mode == SectionInteractionMode::PhreaticDraw && m_pendingPhreaticNodeId <= 0)
        m_pendingPhreaticSoilId = 1;

    if (m_stateLabel)
    {
        if (mode == SectionInteractionMode::SurfaceEdit)
            m_stateLabel->setText(QStringLiteral("图上编辑：点击右侧断面边线，可加入或移出计算坡面；再次点击按钮结束。"));
        else if (mode == SectionInteractionMode::PhreaticDraw)
            m_stateLabel->setText(QStringLiteral("绘制浸润线：按水位线走向依次点击断面，至少点击 2 个点；再次点击按钮结束。"));
    }
}

void SlopeStandardAnalysisWidget::installSectionInteractor()
{
    if (m_interactorObserverTag != 0)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderWindowInteractor* interactor = visual ? visual->interactor() : nullptr;
    if (!interactor)
        return;
    m_interactorCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_interactorCallback->SetClientData(this);
    m_interactorCallback->SetCallback([](vtkObject* caller, unsigned long, void* clientData, void*) {
        auto* self = static_cast<SlopeStandardAnalysisWidget*>(clientData);
        auto* iren = vtkRenderWindowInteractor::SafeDownCast(caller);
        if (!self || !iren)
            return;
        int* pos = iren->GetEventPosition();
        if (!pos) return;
        self->handleSectionViewClick(pos[0], pos[1]);
        if (self->m_sectionInteractionMode != SectionInteractionMode::None && self->m_interactorCallback)
            self->m_interactorCallback->SetAbortFlag(1);
    });
    m_interactorObserverTag = interactor->AddObserver(vtkCommand::LeftButtonPressEvent,
                                                       m_interactorCallback, 1.0f);
}

void SlopeStandardAnalysisWidget::uninstallSectionInteractor()
{
    if (m_interactorObserverTag == 0)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (visual && visual->interactor())
        visual->interactor()->RemoveObserver(m_interactorObserverTag);
    m_interactorObserverTag = 0;
    m_interactorCallback = nullptr;
}

void SlopeStandardAnalysisWidget::handleSectionViewClick(int displayX, int displayY)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = visual ? visual->renderer() : nullptr;
    if (!renderer)
        return;
    auto picker = vtkSmartPointer<vtkCellPicker>::New();
    picker->SetTolerance(0.01);

    if (m_sectionInteractionMode == SectionInteractionMode::None)
    {
        if (!m_sectionFillPickActor || !m_materialRegionCombo)
            return;
        picker->AddPickList(m_sectionFillPickActor);
        picker->PickFromListOn();
        if (!picker->Pick(displayX, displayY, 0.0, renderer))
            return;

        vtkActor* pickedActor = vtkActor::SafeDownCast(picker->GetActor());
        const vtkIdType cellId = picker->GetCellId();
        if (pickedActor == m_sectionFillPickActor.GetPointer() && cellId >= 0)
        {
            auto* mapper = vtkPolyDataMapper::SafeDownCast(pickedActor->GetMapper());
            vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
            vtkDataArray* scalars = nullptr;
            if (poly && poly->GetCellData())
            {
                scalars = vtkDataArray::SafeDownCast(poly->GetCellData()->GetArray("__SlopeSectionMaterial"));
                if (!scalars)
                    scalars = vtkDataArray::SafeDownCast(poly->GetCellData()->GetArray("__SlopeSectionPreviewMaterial"));
            }
            if (scalars && cellId < scalars->GetNumberOfTuples())
            {
                const int sourceId = qRound(scalars->GetComponent(cellId, 0));
                int targetRow = -1;
                if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                    && !m_cases.at(m_currentCaseIndex).sourceMaterialIds.isEmpty())
                {
                    targetRow = m_cases.at(m_currentCaseIndex).sourceMaterialIds.indexOf(sourceId);
                }
                else
                {
                    targetRow = sourceId - 1;
                }
                if (targetRow >= 0 && targetRow < m_materialRegionCombo->count())
                    m_materialRegionCombo->setCurrentIndex(targetRow);
            }
        }
        return;
    }

    if (!picker->Pick(displayX, displayY, 0.0, renderer))
        return;

    vtkActor* pickedActor = vtkActor::SafeDownCast(picker->GetActor());
    const vtkIdType cellId = picker->GetCellId();

    if (m_sectionInteractionMode == SectionInteractionMode::SurfaceEdit)
    {
        if (pickedActor == m_sectionPickActor.GetPointer() && cellId >= 0)
            toggleSurfaceSegment(static_cast<int>(cellId) + 1);
        return;
    }
    if (m_sectionInteractionMode != SectionInteractionMode::PhreaticDraw)
        return;

    // 点击断面填充区时尽量取得所在材料区域，并转换为求解器 soil 编号。
    int soilId = 1;
    if (pickedActor && cellId >= 0)
    {
        auto* mapper = vtkPolyDataMapper::SafeDownCast(pickedActor->GetMapper());
        vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
        vtkDataArray* scalars = nullptr;
        if (poly && poly->GetCellData())
        {
            scalars = vtkDataArray::SafeDownCast(poly->GetCellData()->GetArray("__SlopeSectionMaterial"));
            if (!scalars)
                scalars = vtkDataArray::SafeDownCast(poly->GetCellData()->GetArray("__SlopeSectionPreviewMaterial"));
        }
        if (scalars && cellId < scalars->GetNumberOfTuples())
        {
            const int sourceId = qRound(scalars->GetComponent(cellId, 0));
            if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
            {
                const int index = m_cases.at(m_currentCaseIndex).sourceMaterialIds.indexOf(sourceId);
                if (index >= 0) soilId = index + 1;
            }
        }
    }
    m_pendingPhreaticSoilId = std::clamp(soilId, 1, std::max(1, m_soilTable->rowCount()));

    double world[3]{}; picker->GetPickPosition(world);
    const QString sourceType = m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                                   ? m_cases.at(m_currentCaseIndex).sourceType : QString();
    const bool mapped = sourceType == QStringLiteral("mesh3d")
                        || sourceType == QStringLiteral("mesh2d")
                        || sourceType == QStringLiteral("geometry");
    const bool showOriginalModel = m_pageRole == PageRole::AnalysisSettings
                                       ? false
                                       : (m_pageRole != PageRole::SectionEditor
                                          || !m_showOriginalModelCheck
                                          || m_showOriginalModelCheck->isChecked());
    const bool sectionOnly = mapped && !showOriginalModel;
    double x = world[0], y = world[1];
    if (mapped)
    {
        const double sourceX = sectionOnly ? world[0] : world[m_sectionHorizontalAxis];
        y = sectionOnly ? world[1] : world[m_sectionVerticalAxis];
        x = m_sectionHorizontalReversed
                ? m_sectionHorizontalMinimum + m_sectionHorizontalMaximum - sourceX
                : sourceX;
    }
    appendPhreaticPoint(x, y);
}

void SlopeStandardAnalysisWidget::toggleSurfaceSegment(int segmentId)
{
    if (segmentId < 1 || segmentId > m_segmentTable->rowCount())
        return;
    QVector<int> ids;
    for (int row = 0; row < m_surfaceTable->rowCount(); ++row)
    {
        bool ok=false; const int id=m_surfaceTable->item(row,0)?m_surfaceTable->item(row,0)->text().toInt(&ok):0;
        if (ok && id>0) ids.append(id);
    }
    const int found=ids.indexOf(segmentId);
    if(found>=0) ids.removeAt(found); else ids.append(segmentId);
    std::sort(ids.begin(),ids.end());
    setSelectedSurfaceSegments(ids);
    markCaseDirty(); updateInputPreview(); updateStepButtons();
}

void SlopeStandardAnalysisWidget::appendPhreaticPoint(double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    const int newId=m_nodeTable->rowCount()+1;
    const bool oldLoading=m_loadingCase; m_loadingCase=true;
    m_nodeTable->insertRow(m_nodeTable->rowCount());
    auto set=[&](int c,const QString&t,bool editable=true){auto* item=new QTableWidgetItem(t);if(!editable)item->setFlags(item->flags()&~Qt::ItemIsEditable);m_nodeTable->setItem(m_nodeTable->rowCount()-1,c,item);};
    set(0,QString::number(newId),false);set(1,displayNumber(x));set(2,displayNumber(y));
    if(m_pendingPhreaticNodeId>0)
    {
        const int row=m_phreaticTable->rowCount();m_phreaticTable->insertRow(row);
        m_phreaticTable->setItem(row,0,new QTableWidgetItem(QString::number(m_pendingPhreaticNodeId)));
        m_phreaticTable->setItem(row,1,new QTableWidgetItem(QString::number(newId)));
        m_phreaticTable->setItem(row,2,new QTableWidgetItem(QString::number(m_pendingPhreaticSoilId)));
    }
    m_pendingPhreaticNodeId=newId;m_loadingCase=oldLoading;
    markCaseDirty();updateControlAvailability();updateInputPreview();
}

void SlopeStandardAnalysisWidget::clearPhreaticLine()
{
    const bool oldLoading=m_loadingCase;m_loadingCase=true;
    m_phreaticTable->clearContents();m_phreaticTable->setRowCount(0);
    int maxGeometryNode=0;
    for(int row=0;row<m_segmentTable->rowCount();++row)
        for(int col=0;col<2;++col){bool ok=false;int id=m_segmentTable->item(row,col)?m_segmentTable->item(row,col)->text().toInt(&ok):0;if(ok)maxGeometryNode=std::max(maxGeometryNode,id);}
    while(m_nodeTable->rowCount()>maxGeometryNode)m_nodeTable->removeRow(m_nodeTable->rowCount()-1);
    m_pendingPhreaticNodeId=0;m_pendingPhreaticSoilId=1;m_loadingCase=oldLoading;
    if(m_drawPhreaticButton && m_drawPhreaticButton->isChecked())m_drawPhreaticButton->setChecked(false);
    markCaseDirty();updateControlAvailability();updateInputPreview();
}

void SlopeStandardAnalysisWidget::clearVisualization(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    const QString prefix = QStringLiteral("slope.limit.%1.").arg(m_analysisUuid);
    visual->removeProp(prefix + QStringLiteral("section_fill"), false);
    visual->removeProp(prefix + QStringLiteral("section_pick"), false);
    visual->removeProp(prefix + QStringLiteral("section"), false);
    visual->removeProp(prefix + QStringLiteral("surface"), false);
    visual->removeProp(prefix + QStringLiteral("phreatic"), false);
    visual->removeProp(prefix + QStringLiteral("outside_water"), false);
    visual->removeProp(prefix + QStringLiteral("result_outline"), false);
    visual->removeProp(prefix + QStringLiteral("slip_arc"), false);
    visual->removeProp(prefix + QStringLiteral("material_highlight_fill"), false);
    visual->removeProp(prefix + QStringLiteral("material_highlight_edge"), false);
    visual->removeProp(prefix + QStringLiteral("material_highlight_segments"), false);
    m_sectionPickActor = nullptr;
    m_sectionFillPickActor = nullptr;
    if (renderNow)
        visual->render();
}

void SlopeStandardAnalysisWidget::updateVisualization()
{
    if (!m_pageActive)
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    if (m_pageRole == PageRole::SectionEditor
        && m_showOriginalModelCheck && !m_showOriginalModelCheck->isChecked()
        && !m_originalModelHiddenByPage)
        setOriginalModelVisible(false);

    clearVisualization(false);
    const SlopeStandardDatModel model = collectPreviewModel();
    if (model.nodes.isEmpty() || model.segments.isEmpty())
    {
        visual->render();
        return;
    }

    const QString prefix = QStringLiteral("slope.limit.%1.").arg(m_analysisUuid);
    constexpr const char* groupName = "极限平衡";

    // Material zoning is the primary section preview. The former implementation
    // only drew a blue polyline on top of the 3-D mesh, which made the user guess
    // whether the actual section and material interfaces were correct.
    const auto materialRgb = [](int materialId, unsigned char rgb[3])
    {
        static const unsigned char palette[][3] = {
            {205, 177, 132}, {151, 184, 139}, {169, 153, 190}, {205, 194, 137},
            {132, 178, 188}, {196, 151, 151}, {160, 177, 205}, {184, 166, 145},
            {142, 196, 168}, {214, 164, 132}, {152, 162, 198}, {188, 198, 142},
            {198, 162, 184}, {140, 186, 206}, {212, 188, 156}, {164, 182, 162}};
        const int count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        const int index = std::abs(materialId > 0 ? materialId - 1 : 0) % count;
        rgb[0] = palette[index][0]; rgb[1] = palette[index][1]; rgb[2] = palette[index][2];
    };
    const auto makeFilledActor = [&](vtkPolyData* sourcePoly, vtkDataArray* materials)
        -> vtkSmartPointer<vtkActor>
    {
        if (!sourcePoly || !materials || sourcePoly->GetNumberOfCells() <= 0)
            return nullptr;
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->DeepCopy(sourcePoly);
        auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
        colors->SetName("__SlopeSectionPreviewColor");
        colors->SetNumberOfComponents(3);
        colors->SetNumberOfTuples(poly->GetNumberOfCells());
        for (vtkIdType cellId = 0; cellId < poly->GetNumberOfCells(); ++cellId)
        {
            const int material = cellId < materials->GetNumberOfTuples()
                                     ? qRound(materials->GetComponent(cellId, 0)) : 0;
            unsigned char rgb[3]{};
            materialRgb(material, rgb);
            colors->SetTypedTuple(cellId, rgb);
        }
        poly->GetCellData()->SetScalars(colors);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->SetScalarModeToUseCellData();
        mapper->SetColorModeToDirectScalars();
        mapper->ScalarVisibilityOn();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetOpacity(0.78);
        actor->GetProperty()->EdgeVisibilityOff();
        actor->GetProperty()->SetLineWidth(1.0);
        return actor;
    };

    QSet<int> selectedSurfaceIds;
    for (const auto& surface : model.surfaceLoads)
        selectedSurfaceIds.insert(surface.segmentId);

    const QString sourceType = m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                                   ? m_cases.at(m_currentCaseIndex).sourceType : QString();
    const bool mesh3dSource = sourceType == QStringLiteral("mesh3d");
    const bool mesh2dSource = sourceType == QStringLiteral("mesh2d");
    const bool geometrySource = sourceType == QStringLiteral("geometry");
    const bool mappedSectionSource = mesh3dSource || mesh2dSource || geometrySource;
    const bool showOriginalModel = m_pageRole == PageRole::AnalysisSettings
                                       ? false
                                       : (m_pageRole != PageRole::SectionEditor
                                          || !m_showOriginalModelCheck
                                          || m_showOriginalModelCheck->isChecked());
    // 原模型关闭时，不再把断面留在三维空间的真实剖切平面上。
    // 直接展开到全局 XY 平面，避免用户从侧面看到一条细线。
    const bool sectionOnlyView = mappedSectionSource && !showOriginalModel;
    const int horizontalAxis = m_sectionHorizontalAxis;
    const int verticalAxis = m_sectionVerticalAxis;
    const int normalAxis = m_sectionNormalAxis;
    const double planePosition = (mesh3dSource || geometrySource) && m_sectionPositionSpin
                                     && m_planeCombo && m_planeCombo->count() > 0
                                     ? m_sectionPositionSpin->value() : m_sectionWorldOffset;
    const auto sourceHorizontal = [&](double x)
    {
        return m_sectionHorizontalReversed
                   ? m_sectionHorizontalMinimum + m_sectionHorizontalMaximum - x
                   : x;
    };
    const auto insertSectionPoint = [&](vtkPoints* points, double x, double y, double offset)
    {
        if (sectionOnlyView)
        {
            // 求解器内部可能为了“坡脚在左、坡顶在右”镜像二维坐标，但用户查看断面时
            // 必须保持与原模型一致的左右方向。这里只恢复显示方向，不改变送给求解器的数据。
            points->InsertNextPoint(sourceHorizontal(x), y, offset);
            return;
        }
        double world[3]{x, y, offset};
        if (mappedSectionSource && verticalAxis >= 0 && verticalAxis < 3
            && normalAxis >= 0 && normalAxis < 3 && horizontalAxis >= 0 && horizontalAxis < 3)
        {
            world[0] = world[1] = world[2] = 0.0;
            world[horizontalAxis] = sourceHorizontal(x);
            world[verticalAxis] = y;
            world[normalAxis] = planePosition + offset;
        }
        points->InsertNextPoint(world);
    };

    // Draw the actual cut cells as filled material blocks in the shared
    // visualization window. This keeps the 3-D context while making the 2-D
    // section readable as an engineering section instead of a floating line.
    if (geometrySource && (!m_showSectionFillCheck || m_showSectionFillCheck->isChecked()))
    {
        QString geometryError;
        const auto regions = currentGeometrySectionRegions(&geometryError);
        if (!regions.isEmpty())
        {
            auto combined = vtkSmartPointer<vtkAppendPolyData>::New();
            int regionId = 0;
            for (const auto& sourcePoly : regions)
            {
                ++regionId;
                double b[6]{}; sourcePoly->GetBounds(b);
                double origin[3]{0.5*(b[0]+b[1]),0.5*(b[2]+b[3]),0.5*(b[4]+b[5])};
                double normal[3]{0.0,0.0,0.0}; origin[normalAxis]=planePosition; normal[normalAxis]=1.0;
                auto plane=vtkSmartPointer<vtkPlane>::New(); plane->SetOrigin(origin); plane->SetNormal(normal);
                auto cutter=vtkSmartPointer<vtkCutter>::New(); cutter->SetOutputPointsPrecision(vtkAlgorithm::DOUBLE_PRECISION); cutter->SetCutFunction(plane); cutter->SetInputData(sourcePoly); cutter->Update();
                auto triangulator=vtkSmartPointer<vtkContourTriangulator>::New(); triangulator->SetInputConnection(cutter->GetOutputPort()); triangulator->Update();
                vtkPolyData* filled=triangulator->GetOutput();
                if(!filled || filled->GetNumberOfCells()<=0) continue;
                auto regionPoly=vtkSmartPointer<vtkPolyData>::New(); regionPoly->DeepCopy(filled);
                auto ids=vtkSmartPointer<vtkIntArray>::New(); ids->SetName("__SlopeSectionMaterial");ids->SetNumberOfComponents(1);ids->SetNumberOfTuples(regionPoly->GetNumberOfCells());ids->FillComponent(0,regionId);regionPoly->GetCellData()->SetScalars(ids);
                if(sectionOnlyView && regionPoly->GetPoints())
                {
                    auto pts=vtkSmartPointer<vtkPoints>::New(); pts->SetDataTypeToDouble();pts->SetNumberOfPoints(regionPoly->GetNumberOfPoints());
                    for(vtkIdType pi=0;pi<regionPoly->GetNumberOfPoints();++pi){double pt[3]{};regionPoly->GetPoint(pi,pt);pts->SetPoint(pi,pt[horizontalAxis],pt[verticalAxis],0.0);}regionPoly->SetPoints(pts);
                }
                combined->AddInputData(regionPoly);
            }
            combined->Update();
            vtkPolyData* display=combined->GetOutput();
            vtkDataArray* mats=display && display->GetCellData()?display->GetCellData()->GetScalars():nullptr;
            auto actor=makeFilledActor(display,mats);
            if(actor){actor->PickableOn();m_sectionFillPickActor=actor;visual->addProp(prefix+QStringLiteral("section_fill"),actor,groupName,QStringLiteral("几何断面区域"),false,false);}
        }
    }
    else if (mesh3dSource && (!m_showSectionFillCheck || m_showSectionFillCheck->isChecked()))
    {
        auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkSmartPointer<vtkUnstructuredGrid> holder = meshService
            ? meshService->getVtkUnstructuredGridSolid() : nullptr;
        if ((!holder || holder->GetNumberOfCells() <= 0) && meshService)
            holder = meshService->getVtkUnstructuredGrid();
        vtkUnstructuredGrid* grid = holder;
        QString materialField = (m_materialFieldCombo && m_materialFieldCombo->count() > 0)
                                    ? m_materialFieldCombo->currentData().toString()
                                    : QString();
        if (materialField.isEmpty() && m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
        {
            const QJsonObject& st = m_cases.at(m_currentCaseIndex).state;
            materialField = st.value(QStringLiteral("section_material_field")).toString();
            if (materialField.isEmpty())
                materialField = st.value(QStringLiteral("material_region_source")).toString();
        }
        if (meshService && grid && grid->GetNumberOfCells() > 0 && !materialField.isEmpty())
        {
            QMap<int, QString> labels;
            QString materialError;
            auto materialArray = vtkSmartPointer<vtkIntArray>::New();
            if (fillMaterialArray(meshService, grid, materialField,
                                  materialArray, &labels, &materialError))
            {
                auto workingGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
                workingGrid->ShallowCopy(grid);
                workingGrid->GetCellData()->AddArray(materialArray);
                double bounds[6]{}; grid->GetBounds(bounds);
                double origin[3]{(bounds[0] + bounds[1]) * 0.5,
                                 (bounds[2] + bounds[3]) * 0.5,
                                 (bounds[4] + bounds[5]) * 0.5};
                double normal[3]{0.0, 0.0, 0.0};
                origin[normalAxis] = planePosition;
                normal[normalAxis] = 1.0;
                auto plane = vtkSmartPointer<vtkPlane>::New();
                plane->SetOrigin(origin); plane->SetNormal(normal);
                auto cutter = vtkSmartPointer<vtkCutter>::New();
                cutter->SetOutputPointsPrecision(vtkAlgorithm::DOUBLE_PRECISION); cutter->SetCutFunction(plane); cutter->SetInputData(workingGrid); cutter->Update();
                vtkPolyData* cut = cutter->GetOutput();
                vtkDataArray* cutMaterials = cut && cut->GetCellData()
                    ? vtkDataArray::SafeDownCast(cut->GetCellData()->GetArray("__SlopeSectionMaterial"))
                    : nullptr;

                vtkSmartPointer<vtkPolyData> flattenedCut;
                vtkPolyData* displayCut = cut;
                if (sectionOnlyView && cut && cut->GetPoints())
                {
                    flattenedCut = vtkSmartPointer<vtkPolyData>::New();
                    flattenedCut->DeepCopy(cut);
                    auto flatPoints = vtkSmartPointer<vtkPoints>::New(); flatPoints->SetDataTypeToDouble();
                    flatPoints->SetNumberOfPoints(cut->GetNumberOfPoints());
                    for (vtkIdType pointId = 0; pointId < cut->GetNumberOfPoints(); ++pointId)
                    {
                        double point[3]{};
                        cut->GetPoint(pointId, point);
                        // cut 本身就是原模型坐标，二维独立查看时不要再做求解器镜像。
                        flatPoints->SetPoint(pointId, point[horizontalAxis],
                                             point[verticalAxis], 0.0);
                    }
                    flattenedCut->SetPoints(flatPoints);
                    displayCut = flattenedCut;
                }

                auto actor = makeFilledActor(displayCut, cutMaterials);
                if (actor)
                {
                    actor->PickableOn();
                    m_sectionFillPickActor = actor;
                    if (!sectionOnlyView)
                    {
                        const double normalSpan = std::abs(bounds[2 * normalAxis + 1] - bounds[2 * normalAxis]);
                        const double edgeTolerance = std::max(normalSpan * 1.0e-7, 1.0e-9);
                        const double previewOffset = std::max(normalSpan * 2.0e-4, 1.0e-7);
                        double shift[3]{0.0, 0.0, 0.0};
                        if (std::abs(planePosition - bounds[2 * normalAxis]) <= edgeTolerance)
                            shift[normalAxis] = -previewOffset;
                        else if (std::abs(planePosition - bounds[2 * normalAxis + 1]) <= edgeTolerance)
                            shift[normalAxis] = previewOffset;
                        actor->SetPosition(shift[0], shift[1], shift[2]);
                    }
                    visual->addProp(prefix + QStringLiteral("section_fill"), actor, groupName,
                                    QStringLiteral("断面区域"), false, false);
                }
            }
        }
    }
    else if (mesh2dSource && (!m_showSectionFillCheck || m_showSectionFillCheck->isChecked()))
    {
        auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkSmartPointer<vtkUnstructuredGrid> holder = meshService
            ? meshService->getVtkUnstructuredGrid() : nullptr;
        vtkUnstructuredGrid* grid = holder;
        if (meshService && grid && grid->GetNumberOfCells() > 0)
        {
            QString source = m_materialFieldCombo && m_materialFieldCombo->count() > 0
                                 ? m_materialFieldCombo->currentData().toString()
                                 : QStringLiteral("uniform");
            QMap<int, QString> labels;
            QString materialError;
            auto materialArray = vtkSmartPointer<vtkIntArray>::New();
            if (fillMaterialArray(meshService, grid, source, materialArray, &labels, &materialError))
            {
                auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
                if (sectionOnlyView && grid->GetPoints())
                {
                    points->SetNumberOfPoints(grid->GetNumberOfPoints());
                    for (vtkIdType pointId = 0; pointId < grid->GetNumberOfPoints(); ++pointId)
                    {
                        double point[3]{};
                        grid->GetPoint(pointId, point);
                        points->SetPoint(pointId, point[horizontalAxis],
                                         point[verticalAxis], 0.0);
                    }
                }
                else
                {
                    points->DeepCopy(grid->GetPoints());
                }
                auto polys = vtkSmartPointer<vtkCellArray>::New();
                auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
                colors->SetName("__SlopeSectionPreviewColor");
                colors->SetNumberOfComponents(3);
                auto previewMaterials = vtkSmartPointer<vtkIntArray>::New();
                previewMaterials->SetName("__SlopeSectionPreviewMaterial");
                previewMaterials->SetNumberOfComponents(1);
                for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
                {
                    vtkCell* cell = grid->GetCell(cellId);
                    if (!cell || cell->GetCellDimension() != 2 || cell->GetNumberOfPoints() < 3)
                        continue;
                    polys->InsertNextCell(cell->GetNumberOfPoints());
                    for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
                        polys->InsertCellPoint(cell->GetPointId(i));
                    unsigned char rgb[3]{};
                    const int materialId = materialArray->GetValue(cellId);
                    materialRgb(materialId, rgb);
                    colors->InsertNextTypedTuple(rgb);
                    previewMaterials->InsertNextValue(materialId);
                }
                auto poly = vtkSmartPointer<vtkPolyData>::New();
                poly->SetPoints(points); poly->SetPolys(polys);
                poly->GetCellData()->AddArray(previewMaterials);
                poly->GetCellData()->SetScalars(colors);
                auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                mapper->SetInputData(poly);
                mapper->SetScalarModeToUseCellData();
                mapper->SetColorModeToDirectScalars();
                mapper->ScalarVisibilityOn();
                auto actor = vtkSmartPointer<vtkActor>::New();
                actor->SetMapper(mapper); actor->PickableOn();
                m_sectionFillPickActor = actor;
                actor->GetProperty()->LightingOff();
                actor->GetProperty()->SetOpacity(0.82);
                actor->GetProperty()->EdgeVisibilityOff();
                visual->addProp(prefix + QStringLiteral("section_fill"), actor, groupName,
                                QStringLiteral("断面区域"), false, false);
            }
        }
    }

    const auto makeSegmentActor = [&](const QVector<int>& segmentIndexes,
                                      double r, double g, double b,
                                      double width) -> vtkSmartPointer<vtkActor>
    {
        auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
        for (const auto& node : model.nodes)
            insertSectionPoint(points, node.x, node.y, 0.0);
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        for (int segmentIndex : segmentIndexes)
        {
            if (segmentIndex < 0 || segmentIndex >= model.segments.size())
                continue;
            const auto& segment = model.segments.at(segmentIndex);
            if (segment.startNode < 1 || segment.startNode > model.nodes.size()
                || segment.endNode < 1 || segment.endNode > model.nodes.size())
                continue;
            lines->InsertNextCell(2);
            lines->InsertCellPoint(segment.startNode - 1);
            lines->InsertCellPoint(segment.endNode - 1);
        }
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(r, g, b);
        actor->GetProperty()->SetLineWidth(width);
        return actor;
    };

    // 透明宽线用于图上拾取；cell 顺序与 model.segments 一一对应。
    {
        auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
        for (const auto& node : model.nodes) insertSectionPoint(points, node.x, node.y, 0.02);
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        for (const auto& segment : model.segments)
        {
            if (segment.startNode < 1 || segment.startNode > model.nodes.size()
                || segment.endNode < 1 || segment.endNode > model.nodes.size()) continue;
            lines->InsertNextCell(2); lines->InsertCellPoint(segment.startNode - 1); lines->InsertCellPoint(segment.endNode - 1);
        }
        auto poly = vtkSmartPointer<vtkPolyData>::New(); poly->SetPoints(points); poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New(); mapper->SetInputData(poly);
        m_sectionPickActor = vtkSmartPointer<vtkActor>::New(); m_sectionPickActor->SetMapper(mapper); m_sectionPickActor->PickableOn();
        m_sectionPickActor->GetProperty()->LightingOff(); m_sectionPickActor->GetProperty()->SetOpacity(0.01); m_sectionPickActor->GetProperty()->SetLineWidth(12.0);
        visual->addProp(prefix + QStringLiteral("section_pick"), m_sectionPickActor, groupName,
                        QStringLiteral("断面交互拾取"), false, false);
    }

    QVector<int> ordinarySegments;
    QVector<int> surfaceSegments;
    for (int i = 0; i < model.segments.size(); ++i)
    {
        if (selectedSurfaceIds.contains(i + 1))
            surfaceSegments.append(i);
        else
            ordinarySegments.append(i);
    }
    if (!ordinarySegments.isEmpty())
        visual->addProp(prefix + QStringLiteral("section"),
                        makeSegmentActor(ordinarySegments, 0.25, 0.28, 0.32, 2.0),
                        groupName, QStringLiteral("断面"), false, false);
    if (!surfaceSegments.isEmpty()
        && (!m_showSurfaceCheck || m_showSurfaceCheck->isChecked()))
        visual->addProp(prefix + QStringLiteral("surface"),
                        makeSegmentActor(surfaceSegments, 0.12, 0.38, 0.80, 4.0),
                        groupName, QStringLiteral("计算外坡面"), false, false);

    if (!model.phreaticSegments.isEmpty())
    {
        // Phreatic segments use the same node table but are stored separately.
        auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
        for (const auto& node : model.nodes)
            insertSectionPoint(points, node.x, node.y, 0.01);
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        for (const auto& segment : model.phreaticSegments)
        {
            if (segment.startNode < 1 || segment.startNode > model.nodes.size()
                || segment.endNode < 1 || segment.endNode > model.nodes.size())
                continue;
            lines->InsertNextCell(2);
            lines->InsertCellPoint(segment.startNode - 1);
            lines->InsertCellPoint(segment.endNode - 1);
        }
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(0.10, 0.62, 0.72);
        actor->GetProperty()->SetLineWidth(3.0);
        visual->addProp(prefix + QStringLiteral("phreatic"), actor,
                        groupName, QStringLiteral("浸润线"), false, false);
    }

    if (std::abs(model.outsideWaterLevel - 9999.0) > 1.0e-8 && !model.nodes.isEmpty())
    {
        double minX = model.nodes.first().x;
        double maxX = minX;
        for (const auto& node : model.nodes)
        {
            minX = std::min(minX, node.x);
            maxX = std::max(maxX, node.x);
        }
        auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
        insertSectionPoint(points, minX, model.outsideWaterLevel, 0.015);
        insertSectionPoint(points, maxX, model.outsideWaterLevel, 0.015);
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        lines->InsertNextCell(2);
        lines->InsertCellPoint(0);
        lines->InsertCellPoint(1);
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(0.15, 0.55, 0.78);
        actor->GetProperty()->SetLineWidth(2.0);
        visual->addProp(prefix + QStringLiteral("outside_water"), actor,
                        groupName, QStringLiteral("坡外水位"), false, false);
    }
    updateMaterialRegionHighlight(m_materialRegionCombo ? m_materialRegionCombo->currentIndex() : -1);
    visual->render();
}

void SlopeStandardAnalysisWidget::updateResultVisualization(const QJsonObject& plot)
{
    if (!m_pageActive)
        return;
    updateVisualization();
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual || plot.isEmpty())
        return;

    const QString prefix = QStringLiteral("slope.limit.%1.").arg(m_analysisUuid);
    constexpr const char* groupName = "极限平衡";
    const QString sourceType = m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                                   ? m_cases.at(m_currentCaseIndex).sourceType : QString();
    const bool mesh3dSource = sourceType == QStringLiteral("mesh3d");
    const bool mesh2dSource = sourceType == QStringLiteral("mesh2d");
    const bool geometrySource = sourceType == QStringLiteral("geometry");
    const bool mappedSectionSource = mesh3dSource || mesh2dSource || geometrySource;
    const bool showOriginalModel = m_pageRole == PageRole::AnalysisSettings
                                       ? false
                                       : (m_pageRole != PageRole::SectionEditor
                                          || !m_showOriginalModelCheck
                                          || m_showOriginalModelCheck->isChecked());
    const bool sectionOnlyView = mappedSectionSource && !showOriginalModel;
    const int horizontalAxis = m_sectionHorizontalAxis;
    const int verticalAxis = m_sectionVerticalAxis;
    const int normalAxis = m_sectionNormalAxis;
    const double planePosition = (mesh3dSource || geometrySource) && m_sectionPositionSpin
                                     && m_planeCombo && m_planeCombo->count() > 0
                                     ? m_sectionPositionSpin->value() : m_sectionWorldOffset;
    const auto sourceHorizontal = [&](double x)
    {
        return m_sectionHorizontalReversed
                   ? m_sectionHorizontalMinimum + m_sectionHorizontalMaximum - x
                   : x;
    };
    const auto insertPoint = [&](vtkPoints* points, double x, double y, double offset)
    {
        if (sectionOnlyView)
        {
            // 结果文件使用的是求解器二维方向；独立二维查看时恢复成原模型左右方向。
            points->InsertNextPoint(sourceHorizontal(x), y, offset);
            return;
        }
        double world[3]{x, y, offset};
        if (mappedSectionSource && verticalAxis >= 0 && verticalAxis < 3
            && normalAxis >= 0 && normalAxis < 3 && horizontalAxis >= 0 && horizontalAxis < 3)
        {
            world[0] = world[1] = world[2] = 0.0;
            world[horizontalAxis] = sourceHorizontal(x);
            world[verticalAxis] = y;
            world[normalAxis] = planePosition + offset;
        }
        points->InsertNextPoint(world);
    };
    const auto addPolyline = [&](const QString& id, const QString& name,
                                 const QVector<PlotPoint>& values,
                                 double r, double g, double b, double width)
    {
        if (values.size() < 2)
            return;
        auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        lines->InsertNextCell(values.size());
        for (int i = 0; i < values.size(); ++i)
        {
            insertPoint(points, values.at(i).x, values.at(i).y, 0.02);
            lines->InsertCellPoint(i);
        }
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(r, g, b);
        actor->GetProperty()->SetLineWidth(width);
        visual->addProp(prefix + id, actor, groupName, name, false, false);
    };
    if (!m_showSurfaceCheck || m_showSurfaceCheck->isChecked())
        addPolyline(QStringLiteral("result_outline"), QStringLiteral("计算坡面"),
                    jsonPoints(plot.value(QStringLiteral("outline"))),
                    0.16, 0.36, 0.68, 2.5);
    addPolyline(QStringLiteral("slip_arc"), QStringLiteral("临界滑弧"),
                jsonPoints(plot.value(QStringLiteral("slip_arc"))),
                0.86, 0.22, 0.18, 4.0);
    visual->render();
}

void SlopeStandardAnalysisWidget::updateRequiredFactorConclusion(double factor)
{
    Q_UNUSED(factor);
    // 要求值与结果判定不在前台展示；保留函数仅用于兼容现有调用。
}

void SlopeStandardAnalysisWidget::focusErrorField(const QString& error)
{
    if (error.contains(QStringLiteral("材料")) || error.contains(QStringLiteral("容重"))
        || error.contains(QStringLiteral("摩擦角")) || error.contains(QStringLiteral("黏聚力")))
    {
        setCurrentStep(1);
        if (m_materialRegionCombo) m_materialRegionCombo->setFocus();
    }
    else if (error.contains(QStringLiteral("节点")) || error.contains(QStringLiteral("边界"))
             || error.contains(QStringLiteral("坡面")) || error.contains(QStringLiteral("浸润")))
    {
        setCurrentStep(0);
        m_advancedGeometryToggle->setChecked(true);
    }
    else
        setCurrentStep(1);
}

void SlopeStandardAnalysisWidget::addTableRow(QTableWidget* table)
{
    if (!table) return;
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < table->columnCount(); ++column)
        table->setItem(row, column, new QTableWidgetItem());
    renumberIdentityColumn(table);
}

void SlopeStandardAnalysisWidget::removeSelectedRows(QTableWidget* table)
{
    if (!table) return;
    QSet<int> rows;
    for (const QModelIndex& index : table->selectionModel()->selectedRows())
        rows.insert(index.row());
    QList<int> ordered = rows.values();
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    for (int row : ordered)
        table->removeRow(row);
    renumberIdentityColumn(table);
}

void SlopeStandardAnalysisWidget::renumberIdentityColumn(QTableWidget* table)
{
    if (!table || table != m_nodeTable)
        return;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        QTableWidgetItem* item = table->item(row, 0);
        if (!item)
        {
            item = new QTableWidgetItem();
            table->setItem(row, 0, item);
        }
        item->setText(QString::number(row + 1));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
}

void SlopeStandardAnalysisWidget::setSelectedSurfaceSegments(const QVector<int>& segmentIds)
{
    const QSignalBlocker blocker(m_surfaceTable);
    m_surfaceTable->clearContents();
    m_surfaceTable->setRowCount(segmentIds.size());
    for (int row = 0; row < segmentIds.size(); ++row)
    {
        m_surfaceTable->setItem(row, 0, new QTableWidgetItem(QString::number(segmentIds.at(row))));
        m_surfaceTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0")));
    }
}

namespace
{
void classifySectionEdges(const QVector<SectionPoint>& points,
                          const std::map<std::pair<int, int>, QVector<SectionCellUse>>& edgeUses,
                          double mergeTolerance,
                          QVector<SectionEdge>* rawSurfaceEdges,
                          QVector<SectionEdge>* rawInterfaceEdges)
{
    if (!rawSurfaceEdges || !rawInterfaceEdges)
        return;

    // 对规范法来说“外坡面”不是所有外边界，而是从上方能看到的那条完整地表轮廓。
    // 旧逻辑通过单元质心判断边界朝向，会把侧壁、局部碎边也当成坡面，导致一个普通
    // 边坡被拆成十几个“坡面候选”。这里先收集所有真实外边界，再取 y(x) 的上包络。
    struct BoundaryEdge
    {
        int a{-1};
        int b{-1};
        int material{0};
    };
    QVector<BoundaryEdge> boundaryEdges;

    for (const auto& entry : edgeUses)
    {
        const int a = entry.first.first;
        const int b = entry.first.second;
        const SectionPoint& p1 = points.at(a);
        const SectionPoint& p2 = points.at(b);
        const double dx = p2.x - p1.x;
        const double dy = p2.y - p1.y;
        const double length = std::hypot(dx, dy);
        if (length <= mergeTolerance)
            continue;

        QMap<int, SectionCellUse> materialUses;
        for (const SectionCellUse& use : entry.second)
            if (!materialUses.contains(use.material))
                materialUses.insert(use.material, use);

        if (entry.second.size() == 1)
        {
            // 竖直端面不是 y(x) 意义下的计算坡面；左右端壁只负责封闭断面。
            if (std::abs(dx) > mergeTolerance)
                boundaryEdges.append({a, b, entry.second.first().material});
            continue;
        }

        if (materialUses.size() <= 1)
            continue; // 同一材料内部网格边。

        // 材料界面仍按“界面下方材料”编号，保持 DAT 的既有约定。
        double nx = -dy / length;
        double ny = dx / length;
        if (ny < 0.0)
        {
            nx = -nx;
            ny = -ny;
        }
        const SectionPoint midpoint{(p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5};
        int lowerMaterial = 0;
        double bestSide = std::numeric_limits<double>::max();
        double bestHeight = std::numeric_limits<double>::max();
        for (auto it = materialUses.constBegin(); it != materialUses.constEnd(); ++it)
        {
            const double side = (it.value().centroid.x - midpoint.x) * nx
                                + (it.value().centroid.y - midpoint.y) * ny;
            if (side < bestSide - mergeTolerance
                || (std::abs(side - bestSide) <= mergeTolerance
                    && it.value().centroid.y < bestHeight))
            {
                bestSide = side;
                bestHeight = it.value().centroid.y;
                lowerMaterial = it.key();
            }
        }
        if (lowerMaterial > 0)
            rawInterfaceEdges->append({a, b, lowerMaterial, false});
    }

    if (boundaryEdges.isEmpty())
        return;

    // 取所有外边界的“上包络”。对于每条候选边，在其中点 x 位置比较其它外边界的
    // 插值高度；只有处于最高位置的边才属于真正地表。这样底边、侧边、内部碎边不会
    // 再进入“目标边坡”列表，同时台阶/折坡的水平与斜坡段会自动连成完整轮廓。
    const double envelopeTolerance = std::max(mergeTolerance * 8.0, 1.0e-8);
    for (const BoundaryEdge& candidate : boundaryEdges)
    {
        const SectionPoint& c1 = points.at(candidate.a);
        const SectionPoint& c2 = points.at(candidate.b);
        const double candidateDx = c2.x - c1.x;
        if (std::abs(candidateDx) <= mergeTolerance)
            continue;
        const double x = (c1.x + c2.x) * 0.5;
        const double y = (c1.y + c2.y) * 0.5;
        double topY = std::numeric_limits<double>::lowest();

        for (const BoundaryEdge& edge : boundaryEdges)
        {
            const SectionPoint& p1 = points.at(edge.a);
            const SectionPoint& p2 = points.at(edge.b);
            const double dx = p2.x - p1.x;
            if (std::abs(dx) <= mergeTolerance)
                continue;
            const double minX = std::min(p1.x, p2.x) - envelopeTolerance;
            const double maxX = std::max(p1.x, p2.x) + envelopeTolerance;
            if (x < minX || x > maxX)
                continue;
            const double t = (x - p1.x) / dx;
            if (t < -1.0e-8 || t > 1.0 + 1.0e-8)
                continue;
            topY = std::max(topY, p1.y + t * (p2.y - p1.y));
        }

        if (topY == std::numeric_limits<double>::lowest()
            || y >= topY - envelopeTolerance)
            rawSurfaceEdges->append({candidate.a, candidate.b, candidate.material, true});
    }
}
}

void SlopeStandardAnalysisWidget::initializeMeshSectionControls()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        m_sourceStatusLabel->setText(QStringLiteral("当前工程没有可用网格服务。"));
        return;
    }
    vtkSmartPointer<vtkUnstructuredGrid> holder = meshService->getVtkUnstructuredGridSolid();
    if (!holder || holder->GetNumberOfCells() <= 0)
        holder = meshService->getVtkUnstructuredGrid();
    vtkUnstructuredGrid* grid = holder;
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        m_sourceStatusLabel->setText(QStringLiteral("尚未生成或导入网格。"));
        return;
    }

    const QSignalBlocker b1(m_verticalCombo);
    const QSignalBlocker b2(m_planeCombo);
    const QSignalBlocker b3(m_materialFieldCombo);
    m_verticalCombo->clear();
    int reportedVerticalAxis = meshService->verticalAxis() - 1;
    if (reportedVerticalAxis < 0 || reportedVerticalAxis > 2)
        reportedVerticalAxis = 1;
    for (int axis = 0; axis < 3; ++axis)
        m_verticalCombo->addItem(axis == reportedVerticalAxis
                                     ? QStringLiteral("%1（工程高程轴，推荐）").arg(axisName(axis))
                                     : axisName(axis), axis);
    m_verticalCombo->setCurrentIndex(m_verticalCombo->findData(reportedVerticalAxis));

    m_materialFieldCombo->clear();
    QString materialCategory = meshService->domainCategoryForUse(MeshDomainUse::MaterialRegion);
    const QVector<MeshTagSet> domainSets = meshService->domainTagSets();
    const auto categoryHasDomains = [&](const QString& category)
    {
        for (const MeshTagSet& set : domainSets)
            if (set.key.category.compare(category, Qt::CaseInsensitive) == 0
                && set.key.id != 0 && !set.cellIds.isEmpty())
                return true;
        return false;
    };
    QStringList categories;
    if (!materialCategory.isEmpty() && categoryHasDomains(materialCategory))
        categories.append(materialCategory);
    for (const QString& candidate : {QStringLiteral("MATYPE"), QStringLiteral("REGION"),
                                     QStringLiteral("Physical")})
        if (categoryHasDomains(candidate) && !categories.contains(candidate, Qt::CaseInsensitive))
            categories.append(candidate);
    for (const QString& category : categories)
        m_materialFieldCombo->addItem(
            QStringLiteral("%1（仅用于分区）").arg(category),
            QStringLiteral("tag:%1").arg(category));

    QSet<QString> addedArrays;
    vtkCellData* cellData = grid->GetCellData();
    if (cellData)
    {
        for (int i = 0; i < cellData->GetNumberOfArrays(); ++i)
        {
            vtkDataArray* array = vtkDataArray::SafeDownCast(cellData->GetArray(i));
            const QString name = array && array->GetName()
                                     ? QString::fromUtf8(array->GetName()).trimmed() : QString();
            if (!array || array->GetNumberOfComponents() != 1 || name.isEmpty())
                continue;
            const QString lower = name.toLower();
            if (addedArrays.contains(lower))
                continue;
            if (name.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("REGION"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("Physical"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("RegionId"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("src_part"), Qt::CaseInsensitive) == 0)
            {
                addedArrays.insert(lower);
                m_materialFieldCombo->addItem(QStringLiteral("单元字段：%1").arg(name),
                                              QStringLiteral("array:%1").arg(name));
            }
        }
    }
    m_materialFieldCombo->addItem(QStringLiteral("不分区（统一区域）"), QStringLiteral("uniform"));
    rebuildPlaneChoices();
}

void SlopeStandardAnalysisWidget::rebuildPlaneChoices()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService || m_verticalCombo->count() == 0)
        return;
    vtkSmartPointer<vtkUnstructuredGrid> holder = meshService->getVtkUnstructuredGridSolid();
    if (!holder || holder->GetNumberOfCells() <= 0)
        holder = meshService->getVtkUnstructuredGrid();
    vtkUnstructuredGrid* grid = holder;
    if (!grid)
        return;

    double bounds[6]{};
    grid->GetBounds(bounds);
    const int verticalAxis = m_verticalCombo->currentData().toInt();

    struct Candidate
    {
        int normalAxis{-1};
        int horizontalAxis{-1};
        vtkIdType cutCellCount{0};
        double aspectScore{0.0};
        double topRelief{0.0};
        double topReliefRatio{0.0};
        double score{0.0};
    };
    QVector<Candidate> candidates;
    for (int normalAxis = 0; normalAxis < 3; ++normalAxis)
    {
        if (normalAxis == verticalAxis)
            continue;
        Candidate candidate;
        candidate.normalAxis = normalAxis;
        candidate.horizontalAxis = 3 - verticalAxis - normalAxis;
        const double normalSpan = std::abs(bounds[2 * normalAxis + 1] - bounds[2 * normalAxis]);
        const double horizontalSpan = std::abs(bounds[2 * candidate.horizontalAxis + 1]
                                               - bounds[2 * candidate.horizontalAxis]);
        candidate.aspectScore = horizontalSpan / std::max(normalSpan, 1.0e-12);

        {
            double origin[3]{(bounds[0] + bounds[1]) * 0.5,
                             (bounds[2] + bounds[3]) * 0.5,
                             (bounds[4] + bounds[5]) * 0.5};
            double normal[3]{0.0, 0.0, 0.0};
            normal[normalAxis] = 1.0;
            auto plane = vtkSmartPointer<vtkPlane>::New();
            plane->SetOrigin(origin);
            plane->SetNormal(normal);
            auto cutter = vtkSmartPointer<vtkCutter>::New();
            cutter->SetCutFunction(plane);
            cutter->SetInputData(grid);
            cutter->Update();
            vtkPolyData* cut = cutter->GetOutput();
            candidate.cutCellCount = cut ? cut->GetNumberOfCells() : 0;
            // 推荐方向首先看“能否看到真正的坡形”，而不是看材料区域数量。
            // 横切一个分层模型往往会一次看到很多 MATYPE，但得到的只是矩形截面；
            // 沿坡向切时，上边界高程会随水平位置明显变化，这才是规范法需要的断面。
            if (cut && cut->GetPoints() && cut->GetNumberOfPoints() > 1)
            {
                double minX = std::numeric_limits<double>::max();
                double maxX = std::numeric_limits<double>::lowest();
                double minY = std::numeric_limits<double>::max();
                double maxY = std::numeric_limits<double>::lowest();
                for (vtkIdType pointId = 0; pointId < cut->GetNumberOfPoints(); ++pointId)
                {
                    double point[3]{};
                    cut->GetPoint(pointId, point);
                    minX = std::min(minX, point[candidate.horizontalAxis]);
                    maxX = std::max(maxX, point[candidate.horizontalAxis]);
                    minY = std::min(minY, point[verticalAxis]);
                    maxY = std::max(maxY, point[verticalAxis]);
                }
                const double cutWidth = std::max(maxX - minX, 0.0);
                const double cutHeight = std::max(maxY - minY, 0.0);
                const double xTolerance = std::max(cutWidth * 1.0e-6, 1.0e-8);
                QMap<qint64, double> topByX;
                for (vtkIdType pointId = 0; pointId < cut->GetNumberOfPoints(); ++pointId)
                {
                    double point[3]{};
                    cut->GetPoint(pointId, point);
                    const qint64 key = qRound64((point[candidate.horizontalAxis] - minX) / xTolerance);
                    const double y = point[verticalAxis];
                    auto it = topByX.find(key);
                    if (it == topByX.end())
                        topByX.insert(key, y);
                    else if (y > it.value())
                        it.value() = y;
                }
                if (topByX.size() >= 2)
                {
                    double topMin = std::numeric_limits<double>::max();
                    double topMax = std::numeric_limits<double>::lowest();
                    for (auto it = topByX.constBegin(); it != topByX.constEnd(); ++it)
                    {
                        topMin = std::min(topMin, it.value());
                        topMax = std::max(topMax, it.value());
                    }
                    candidate.topRelief = std::max(0.0, topMax - topMin);
                    candidate.topReliefRatio = candidate.topRelief / std::max(cutHeight, 1.0e-12);
                }
            }
        }

        // 坡形起伏为主判据；模型薄厚比只用于同等坡形下的次级判断。
        // 材料数量完全不参与推荐，避免“5 个材料区的横截面”被误判为主剖面。
        candidate.score = candidate.topReliefRatio * 1000000.0
                          + std::log1p(std::max(candidate.aspectScore, 0.0)) * 10000.0
                          + std::min<double>(candidate.cutCellCount, 999.0);
        candidates.append(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    const QSignalBlocker blocker(m_planeCombo);
    m_planeCombo->clear();
    for (int i = 0; i < candidates.size(); ++i)
    {
        const Candidate& candidate = candidates.at(i);
        const QString text = i == 0 ? QStringLiteral("主剖面方向（推荐）")
                                    : QStringLiteral("备选剖面方向");
        m_planeCombo->addItem(text, candidate.normalAxis);
        m_planeCombo->setItemData(
            i,
            QStringLiteral("沿 %1 展开，%2 为剖切方向。")
                .arg(axisName(candidate.horizontalAxis), axisName(candidate.normalAxis),
                     displayNumber(candidate.topRelief)),
            Qt::ToolTipRole);
    }
    if (!candidates.isEmpty())
        m_planeCombo->setCurrentIndex(0);
    updatePlaneRange(true);
}

void SlopeStandardAnalysisWidget::updatePlaneRange(bool resetPosition)
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService || m_planeCombo->count() == 0) return;
    vtkSmartPointer<vtkUnstructuredGrid> holder = meshService->getVtkUnstructuredGridSolid();
    if (!holder || holder->GetNumberOfCells() <= 0) holder = meshService->getVtkUnstructuredGrid();
    if (!holder) return;
    double bounds[6]{}; holder->GetBounds(bounds);
    const int normalAxis = m_planeCombo->currentData().toInt();
    const int verticalAxis = m_verticalCombo->currentData().toInt();
    const int horizontalAxis = 3 - verticalAxis - normalAxis;
    const double minimum = bounds[2 * normalAxis];
    const double maximum = bounds[2 * normalAxis + 1];
    const QSignalBlocker blocker1(m_sectionPositionSpin);
    const QSignalBlocker blocker2(m_sectionPositionSlider);
    m_sectionPositionSpin->setRange(minimum, maximum);
    if (resetPosition || m_sectionPositionSpin->value() < minimum || m_sectionPositionSpin->value() > maximum)
    {
        m_sectionPositionSpin->setValue((minimum + maximum) * 0.5);
        m_sectionPositionSlider->setValue(500);
    }
    const double width = bounds[2 * horizontalAxis + 1] - bounds[2 * horizontalAxis];
    const double height = bounds[2 * verticalAxis + 1] - bounds[2 * verticalAxis];
    const double span = std::max(std::abs(width), std::abs(height));
    m_simplificationToleranceSpin->setRange(0.0, std::max(1.0, span));
    if (resetPosition || m_simplificationToleranceSpin->value() <= 0.0)
        m_simplificationToleranceSpin->setValue(std::max(span * 1.0e-4, 1.0e-8));
}

void SlopeStandardAnalysisWidget::scheduleMeshSectionPreview()
{
    if (!m_meshSectionControls || !m_meshSectionControls->isVisible())
        return;
    QTimer::singleShot(180, this, [this]() {
        if (!m_meshSectionControls->isVisible() || isRunning()) return;
        const bool geometrySource = (m_geometrySourceButton && m_geometrySourceButton->isChecked())
            || (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                && m_cases.at(m_currentCaseIndex).sourceType == QStringLiteral("geometry"));
        if (geometrySource) extractSectionFromCurrentGeometry(true);
        else extractSectionFromCurrentMesh(true);
    });
}

bool SlopeStandardAnalysisWidget::extractSectionFromCurrentMesh(bool silent)
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        if (!silent) QMessageBox::warning(this, QStringLiteral("无法提取断面"), QStringLiteral("当前工程没有可用网格服务。"));
        return false;
    }
    vtkSmartPointer<vtkUnstructuredGrid> gridHolder = meshService->getVtkUnstructuredGridSolid();
    if (!gridHolder || gridHolder->GetNumberOfCells() <= 0)
        gridHolder = meshService->getVtkUnstructuredGrid();
    vtkUnstructuredGrid* grid = gridHolder;
    if (!grid || grid->GetNumberOfCells() <= 0 || grid->GetNumberOfPoints() <= 0)
    {
        if (!silent) QMessageBox::information(this, QStringLiteral("尚无网格"), QStringLiteral("请先导入或生成三维实体网格。"));
        return false;
    }
    const MshPickMap pickMap = meshService->mshPickMapSolid();
    if (pickMap.meshDim > 0 && pickMap.meshDim < 3)
    {
        if (!silent) QMessageBox::information(this, QStringLiteral("当前为二维网格"), QStringLiteral("请使用“使用当前二维网格”。"));
        return false;
    }
    if (m_verticalCombo->count() == 0 || m_planeCombo->count() == 0 || m_materialFieldCombo->count() == 0)
        initializeMeshSectionControls();
    if (m_verticalCombo->count() == 0 || m_planeCombo->count() == 0)
        return false;

    double bounds[6]{}; grid->GetBounds(bounds);
    const int verticalAxis = m_verticalCombo->currentData().toInt();
    const int normalAxis = m_planeCombo->currentData().toInt();
    if (verticalAxis < 0 || verticalAxis > 2 || normalAxis < 0 || normalAxis > 2 || verticalAxis == normalAxis)
        return false;
    const int horizontalAxis = 3 - verticalAxis - normalAxis;
    const QString source = m_materialFieldCombo->currentData().toString();

    QMap<int, QString> materialLabels;
    auto materialArray = vtkSmartPointer<vtkIntArray>::New();
    QString error;
    if (!fillMaterialArray(meshService, grid, source, materialArray, &materialLabels, &error))
    {
        if (!silent) QMessageBox::warning(this, QStringLiteral("分区识别失败"), error);
        else m_sourceStatusLabel->setText(error);
        return false;
    }

    auto workingGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    workingGrid->ShallowCopy(grid);
    workingGrid->GetCellData()->AddArray(materialArray);
    auto plane = vtkSmartPointer<vtkPlane>::New();
    double origin[3]{(bounds[0] + bounds[1]) * 0.5,
                     (bounds[2] + bounds[3]) * 0.5,
                     (bounds[4] + bounds[5]) * 0.5};
    double normal[3]{0.0, 0.0, 0.0};
    origin[normalAxis] = m_sectionPositionSpin->value();
    normal[normalAxis] = 1.0;
    plane->SetOrigin(origin); plane->SetNormal(normal);
    auto cutter = vtkSmartPointer<vtkCutter>::New();
    cutter->SetCutFunction(plane); cutter->SetInputData(workingGrid); cutter->Update();
    vtkPolyData* cut = cutter->GetOutput();
    vtkDataArray* cutMaterials = cut && cut->GetCellData()
                                     ? vtkDataArray::SafeDownCast(cut->GetCellData()->GetArray("__SlopeSectionMaterial"))
                                     : nullptr;
    if (!cut || cut->GetNumberOfCells() <= 0 || !cutMaterials)
    {
        const QString message = QStringLiteral("当前位置没有得到有效实体截面，请移动剖面位置。" );
        if (!silent) QMessageBox::warning(this, QStringLiteral("未截到实体"), message);
        else m_sourceStatusLabel->setText(message);
        return false;
    }

    const double horizontalMinimum = bounds[2 * horizontalAxis];
    const double verticalMinimum = bounds[2 * verticalAxis];
    const double span = std::max(std::abs(bounds[2 * horizontalAxis + 1] - horizontalMinimum),
                                 std::abs(bounds[2 * verticalAxis + 1] - verticalMinimum));
    const double mergeTolerance = std::max(span * 1.0e-7, 1.0e-8);
    QVector<SectionPoint> points;
    std::map<std::pair<long long, long long>, int> pointLookup;
    const auto pointIndex = [&](const double coordinates[3])
    {
        const SectionPoint point{coordinates[horizontalAxis], coordinates[verticalAxis]};
        const auto key = std::make_pair(
            std::llround((point.x - horizontalMinimum) / mergeTolerance),
            std::llround((point.y - verticalMinimum) / mergeTolerance));
        const auto found = pointLookup.find(key);
        if (found != pointLookup.end()) return found->second;
        const int index = points.size(); points.append(point); pointLookup.emplace(key, index); return index;
    };

    std::map<std::pair<int, int>, QVector<SectionCellUse>> edgeUses;
    for (vtkIdType cellId = 0; cellId < cut->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = cut->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() < 3 || cellId >= cutMaterials->GetNumberOfTuples())
            continue;
        const int material = qRound(cutMaterials->GetComponent(cellId, 0));
        if (material <= 0) continue;
        QVector<int> polygon;
        SectionPoint centroid;
        for (vtkIdType local = 0; local < cell->GetNumberOfPoints(); ++local)
        {
            double coordinates[3]{}; cut->GetPoint(cell->GetPointId(local), coordinates);
            polygon.append(pointIndex(coordinates));
            centroid.x += coordinates[horizontalAxis];
            centroid.y += coordinates[verticalAxis];
        }
        centroid.x /= static_cast<double>(polygon.size());
        centroid.y /= static_cast<double>(polygon.size());
        for (int i = 0; i < polygon.size(); ++i)
        {
            const int a = polygon.at(i), b = polygon.at((i + 1) % polygon.size());
            if (a != b) edgeUses[std::minmax(a, b)].append({material, centroid});
        }
    }

    QVector<SectionEdge> rawSurfaceEdges, rawInterfaceEdges;
    classifySectionEdges(points, edgeUses, mergeTolerance, &rawSurfaceEdges, &rawInterfaceEdges);
    SectionExtractionResult result;
    if (!finalizeSection(points, rawSurfaceEdges, rawInterfaceEdges, materialLabels,
                         m_simplificationToleranceSpin->value(), span,
                         m_surfaceCandidateCombo ? m_surfaceCandidateCombo->currentData().toInt() : 0,
                         &result, &error))
    {
        if (!silent) QMessageBox::warning(this, QStringLiteral("断面识别失败"), error);
        else m_sourceStatusLabel->setText(error);
        return false;
    }

    m_sectionHorizontalReversed = result.horizontalAxisReversed;
    m_sectionHorizontalMinimum = result.horizontalMirrorMinimum;
    m_sectionHorizontalMaximum = result.horizontalMirrorMaximum;
    m_sectionHorizontalAxis = horizontalAxis;
    m_sectionVerticalAxis = verticalAxis;
    m_sectionNormalAxis = normalAxis;
    m_sectionWorldOffset = m_sectionPositionSpin->value();

    if (m_surfaceCandidateCombo)
    {
        const QSignalBlocker blocker(m_surfaceCandidateCombo);
        m_surfaceCandidateCombo->clear();
        for (int i = 0; i < result.surfaceCandidateSpans.size(); ++i)
            m_surfaceCandidateCombo->addItem(
                QStringLiteral("不连通边坡 %1（水平跨度 %2）")
                    .arg(i + 1).arg(displayNumber(result.surfaceCandidateSpans.at(i))), i);
        m_surfaceCandidateCombo->setCurrentIndex(
            std::clamp(result.selectedSurfaceCandidate, 0,
                       std::max(0, m_surfaceCandidateCombo->count() - 1)));
        m_surfaceCandidateCombo->setVisible(result.disconnectedSurfaceCount > 1);
    }

    const bool oldLoading = m_loadingCase;
    m_loadingCase = true;
    const auto setCell = [](QTableWidget* table, int row, int column, const QString& text, bool editable = true)
    {
        auto* item = new QTableWidgetItem(text);
        if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    };

    // 用户手动填写的材料参数不能因为拖动剖面位置而丢失。
    // 只要原分区编号仍存在，就按 sourceMaterialId 保留该行输入。
    QHash<int, QStringList> previousMaterialInput;
    const bool preserveManualMaterialInput =
        m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
        && m_cases.at(m_currentCaseIndex).state.value(QStringLiteral("material_input_mode")).toString()
               == QStringLiteral("manual_v1")
        && m_cases.at(m_currentCaseIndex).state.value(QStringLiteral("material_region_source")).toString() == source;
    if (preserveManualMaterialInput)
    {
        const QVector<int>& previousIds = m_cases.at(m_currentCaseIndex).sourceMaterialIds;
        const int keepRows = std::min(static_cast<int>(previousIds.size()), m_soilTable->rowCount());
        for (int row = 0; row < keepRows; ++row)
        {
            QStringList values;
            for (int column = 2; column <= 6; ++column)
                values.append(m_soilTable->item(row, column) ? m_soilTable->item(row, column)->text() : QString());
            previousMaterialInput.insert(previousIds.at(row), values);
        }
    }

    m_soilTable->clearContents();
    m_soilTable->setRowCount(result.sourceMaterialIds.size());
    QString fieldName = source;
    if (fieldName.startsWith(QStringLiteral("tag:"))) fieldName = fieldName.mid(4);
    else if (fieldName.startsWith(QStringLiteral("array:"))) fieldName = fieldName.mid(6);
    else fieldName.clear();
    for (int row = 0; row < result.sourceMaterialIds.size(); ++row)
    {
        const int sourceId = result.sourceMaterialIds.value(row);
        QString regionText = QStringLiteral("区域 %1").arg(row + 1);
        if (!fieldName.isEmpty() && source != QStringLiteral("uniform"))
            regionText += QStringLiteral("（%1 %2）").arg(fieldName).arg(sourceId);
        setCell(m_soilTable, row, 0, regionText, false);
        m_soilTable->item(row, 0)->setToolTip(QStringLiteral("该编号只用于断面几何分区，不代表已继承工程材料。"));

        const QStringList previous = previousMaterialInput.value(sourceId);
        setCell(m_soilTable, row, 1, regionText, false);
        for (int column = 2; column <= 6; ++column)
            setCell(m_soilTable, row, column, previous.value(column - 2));

        for (int column = 2; column <= 5; ++column)
        {
            if (m_soilTable->item(row, column)->text().trimmed().isEmpty())
                m_soilTable->item(row, column)->setBackground(QColor(255, 235, 235));
            m_soilTable->item(row, column)->setToolTip(QStringLiteral("请手动填写工程材料参数。"));
        }
        if (m_poreMethodCombo && m_poreMethodCombo->currentData().toInt() == 2
            && m_soilTable->item(row, 6)->text().trimmed().isEmpty())
            m_soilTable->item(row, 6)->setBackground(QColor(255, 235, 235));
    }

    m_nodeTable->clearContents(); m_nodeTable->setRowCount(result.model.nodes.size());
    for (int row = 0; row < result.model.nodes.size(); ++row)
    {
        const auto& n = result.model.nodes.at(row);
        setCell(m_nodeTable, row, 0, QString::number(n.id), false);
        setCell(m_nodeTable, row, 1, displayNumber(n.x));
        setCell(m_nodeTable, row, 2, displayNumber(n.y));
    }
    m_segmentTable->clearContents(); m_segmentTable->setRowCount(result.model.segments.size());
    for (int row = 0; row < result.model.segments.size(); ++row)
    {
        const auto& s = result.model.segments.at(row);
        setCell(m_segmentTable, row, 0, QString::number(s.startNode));
        setCell(m_segmentTable, row, 1, QString::number(s.endNode));
        setCell(m_segmentTable, row, 2, QString::number(s.soilId));
    }
    m_surfaceTable->clearContents(); m_surfaceTable->setRowCount(result.model.surfaceLoads.size());
    for (int row = 0; row < result.model.surfaceLoads.size(); ++row)
    {
        setCell(m_surfaceTable, row, 0, QString::number(result.model.surfaceLoads.at(row).segmentId));
        setCell(m_surfaceTable, row, 1, QStringLiteral("0"));
    }
    m_phreaticTable->clearContents(); m_phreaticTable->setRowCount(0);
    m_slopeHeightSpin->setValue(result.model.slopeHeight);
    if (m_titleEdit->text().trimmed().isEmpty())
        m_titleEdit->setText(QStringLiteral("%1=%2 剖面").arg(axisName(normalAxis), displayNumber(m_sectionPositionSpin->value())));
    m_datEditor->clear(); m_datEditor->document()->setModified(false);
    m_loadingCase = oldLoading;

    const QString summary = QStringLiteral("当前断面：%1 个区域，%2 段计算坡面。")
                                .arg(result.materialNames.size())
                                .arg(result.model.surfaceLoads.size());
    m_sourceStatusLabel->setText(
        result.disconnectedSurfaceCount > 1
            ? summary + QStringLiteral(" 检测到 %1 个候选坡面，请选择目标坡面。")
                            .arg(result.disconnectedSurfaceCount)
            : summary);
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        CaseRecord& record = m_cases[m_currentCaseIndex];
        record.sourceType = QStringLiteral("mesh3d");
        record.sourceSummary = m_sourceStatusLabel->text();
        record.materialNames = result.materialNames;
        record.sourceMaterialIds = result.sourceMaterialIds;
        record.state[QStringLiteral("material_input_mode")] = QStringLiteral("manual_v1");
        record.state[QStringLiteral("material_region_source")] = source;
        record.lastJobDirectory.clear();
    }
    m_jobDirectory.clear();
    markCaseDirty();
    updateControlAvailability();
    updateInputPreview();
    if (!silent)
        m_stateLabel->setText(QStringLiteral("断面已更新。"));
    updateStepButtons();
    return true;
}

bool SlopeStandardAnalysisWidget::extractSectionFromCurrent2DMesh()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkSmartPointer<vtkUnstructuredGrid> holder = meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
    vtkUnstructuredGrid* grid = holder;
    if (!meshService || !grid || grid->GetNumberOfCells() <= 0)
    {
        QMessageBox::information(this, QStringLiteral("尚无二维网格"), QStringLiteral("当前工程没有可用网格。"));
        return false;
    }
    const MshPickMap pickMap = meshService->mshPickMapSolid();
    if (pickMap.meshDim >= 3)
    {
        QMessageBox::information(this, QStringLiteral("当前为三维网格"), QStringLiteral("请使用“从当前三维网格提取”。"));
        return false;
    }
    double bounds[6]{}; grid->GetBounds(bounds);
    int verticalAxis = meshService->verticalAxis() - 1;
    if (verticalAxis < 0 || verticalAxis > 2) verticalAxis = 1;
    int horizontalAxis = -1;
    double largestSpan = -1.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis == verticalAxis) continue;
        const double axisSpan = std::abs(bounds[2 * axis + 1] - bounds[2 * axis]);
        if (axisSpan > largestSpan) { largestSpan = axisSpan; horizontalAxis = axis; }
    }
    if (horizontalAxis < 0)
    {
        QMessageBox::warning(this, QStringLiteral("二维网格方向无效"), QStringLiteral("无法识别断面的水平轴。"));
        return false;
    }

    // Reuse the same material discovery policy as the 3-D path.
    m_meshSectionControls->show();
    initializeMeshSectionControls();
    QString source = m_materialFieldCombo->currentData().toString();
    if (source.isEmpty()) source = QStringLiteral("uniform");
    QMap<int, QString> materialLabels;
    auto materialArray = vtkSmartPointer<vtkIntArray>::New();
    QString error;
    if (!fillMaterialArray(meshService, grid, source, materialArray, &materialLabels, &error))
    {
        QMessageBox::warning(this, QStringLiteral("分区识别失败"), error);
        return false;
    }

    const double horizontalMinimum = bounds[2 * horizontalAxis];
    const double verticalMinimum = bounds[2 * verticalAxis];
    const double span = std::max(std::abs(bounds[2 * horizontalAxis + 1] - horizontalMinimum),
                                 std::abs(bounds[2 * verticalAxis + 1] - verticalMinimum));
    const double mergeTolerance = std::max(span * 1.0e-7, 1.0e-8);
    QVector<SectionPoint> points;
    std::map<std::pair<long long, long long>, int> pointLookup;
    const auto pointIndex = [&](const double coordinates[3])
    {
        const SectionPoint point{coordinates[horizontalAxis], coordinates[verticalAxis]};
        const auto key = std::make_pair(std::llround((point.x - horizontalMinimum) / mergeTolerance),
                                        std::llround((point.y - verticalMinimum) / mergeTolerance));
        const auto found = pointLookup.find(key);
        if (found != pointLookup.end()) return found->second;
        const int index = points.size(); points.append(point); pointLookup.emplace(key, index); return index;
    };
    std::map<std::pair<int, int>, QVector<SectionCellUse>> edgeUses;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 2 || cell->GetNumberOfPoints() < 3)
            continue;
        const int material = materialArray->GetValue(cellId);
        if (material <= 0) continue;
        QVector<int> polygon;
        SectionPoint centroid;
        for (vtkIdType local = 0; local < cell->GetNumberOfPoints(); ++local)
        {
            double coordinates[3]{}; grid->GetPoint(cell->GetPointId(local), coordinates);
            polygon.append(pointIndex(coordinates));
            centroid.x += coordinates[horizontalAxis]; centroid.y += coordinates[verticalAxis];
        }
        centroid.x /= polygon.size(); centroid.y /= polygon.size();
        for (int i = 0; i < polygon.size(); ++i)
        {
            const int a = polygon.at(i), b = polygon.at((i + 1) % polygon.size());
            if (a != b) edgeUses[std::minmax(a, b)].append({material, centroid});
        }
    }
    QVector<SectionEdge> rawSurfaceEdges, rawInterfaceEdges;
    classifySectionEdges(points, edgeUses, mergeTolerance, &rawSurfaceEdges, &rawInterfaceEdges);
    SectionExtractionResult result;
    const double tolerance = std::max(span * 1.0e-4, 1.0e-8);
    if (!finalizeSection(points, rawSurfaceEdges, rawInterfaceEdges, materialLabels,
                         tolerance, span,
                         m_surfaceCandidateCombo ? m_surfaceCandidateCombo->currentData().toInt() : 0,
                         &result, &error))
    {
        QMessageBox::warning(this, QStringLiteral("二维网格识别失败"), error);
        return false;
    }

    m_sectionHorizontalReversed = result.horizontalAxisReversed;
    m_sectionHorizontalMinimum = result.horizontalMirrorMinimum;
    m_sectionHorizontalMaximum = result.horizontalMirrorMaximum;
    m_sectionHorizontalAxis = horizontalAxis;
    m_sectionVerticalAxis = verticalAxis;
    m_sectionNormalAxis = 3 - horizontalAxis - verticalAxis;
    m_sectionWorldOffset = 0.5 * (bounds[2 * m_sectionNormalAxis] + bounds[2 * m_sectionNormalAxis + 1]);

    const bool oldLoading = m_loadingCase; m_loadingCase = true;
    const auto setCell = [](QTableWidget* table, int row, int column, const QString& text, bool editable = true)
    {
        auto* item = new QTableWidgetItem(text); if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable); table->setItem(row, column, item);
    };
    QHash<int, QStringList> previousMaterialInput;
    const bool preserveManualMaterialInput =
        m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
        && m_cases.at(m_currentCaseIndex).state.value(QStringLiteral("material_input_mode")).toString()
               == QStringLiteral("manual_v1")
        && m_cases.at(m_currentCaseIndex).state.value(QStringLiteral("material_region_source")).toString() == source;
    if (preserveManualMaterialInput)
    {
        const QVector<int>& previousIds = m_cases.at(m_currentCaseIndex).sourceMaterialIds;
        const int keepRows = std::min(static_cast<int>(previousIds.size()), m_soilTable->rowCount());
        for (int row = 0; row < keepRows; ++row)
        {
            QStringList values;
            for (int column = 2; column <= 6; ++column)
                values.append(m_soilTable->item(row, column) ? m_soilTable->item(row, column)->text() : QString());
            previousMaterialInput.insert(previousIds.at(row), values);
        }
    }
    m_soilTable->clearContents(); m_soilTable->setRowCount(result.sourceMaterialIds.size());
    QString fieldName = source.startsWith(QStringLiteral("tag:")) ? source.mid(4)
                         : source.startsWith(QStringLiteral("array:")) ? source.mid(6) : QString();
    for (int row = 0; row < result.sourceMaterialIds.size(); ++row)
    {
        const int sourceId = result.sourceMaterialIds.value(row);
        QString regionText = QStringLiteral("区域 %1").arg(row + 1);
        if (!fieldName.isEmpty() && source != QStringLiteral("uniform"))
            regionText += QStringLiteral("（%1 %2）").arg(fieldName).arg(sourceId);
        setCell(m_soilTable, row, 0, regionText, false);
        m_soilTable->item(row, 0)->setToolTip(QStringLiteral("该编号只用于断面几何分区，不代表已继承工程材料。"));
        const QStringList previous = previousMaterialInput.value(sourceId);
        setCell(m_soilTable, row, 1, regionText, false);
        for (int column = 2; column <= 6; ++column)
            setCell(m_soilTable, row, column, previous.value(column - 2));
        for (int column = 2; column <= 5; ++column)
            if (m_soilTable->item(row, column)->text().trimmed().isEmpty())
                m_soilTable->item(row, column)->setBackground(QColor(255,235,235));
    }
    m_nodeTable->clearContents(); m_nodeTable->setRowCount(result.model.nodes.size());
    for (int row = 0; row < result.model.nodes.size(); ++row)
    {
        const auto& n = result.model.nodes.at(row); setCell(m_nodeTable,row,0,QString::number(n.id),false); setCell(m_nodeTable,row,1,displayNumber(n.x)); setCell(m_nodeTable,row,2,displayNumber(n.y));
    }
    m_segmentTable->clearContents(); m_segmentTable->setRowCount(result.model.segments.size());
    for (int row = 0; row < result.model.segments.size(); ++row)
    {
        const auto& s = result.model.segments.at(row); setCell(m_segmentTable,row,0,QString::number(s.startNode)); setCell(m_segmentTable,row,1,QString::number(s.endNode)); setCell(m_segmentTable,row,2,QString::number(s.soilId));
    }
    m_surfaceTable->clearContents(); m_surfaceTable->setRowCount(result.model.surfaceLoads.size());
    for (int row = 0; row < result.model.surfaceLoads.size(); ++row) { setCell(m_surfaceTable,row,0,QString::number(result.model.surfaceLoads.at(row).segmentId)); setCell(m_surfaceTable,row,1,QStringLiteral("0")); }
    m_phreaticTable->clearContents(); m_phreaticTable->setRowCount(0);
    m_slopeHeightSpin->setValue(result.model.slopeHeight);
    if (m_titleEdit->text().trimmed().isEmpty()) m_titleEdit->setText(QStringLiteral("二维边坡工况"));
    m_datEditor->clear(); m_datEditor->document()->setModified(false);
    m_loadingCase = oldLoading;

    m_sourceStatusLabel->setText(QStringLiteral("当前断面：%1 个区域。")
                                     .arg(result.materialNames.size()));
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        CaseRecord& record = m_cases[m_currentCaseIndex]; record.sourceType = QStringLiteral("mesh2d"); record.sourceSummary = m_sourceStatusLabel->text(); record.materialNames = result.materialNames; record.sourceMaterialIds = result.sourceMaterialIds; record.state[QStringLiteral("material_input_mode")] = QStringLiteral("manual_v1"); record.state[QStringLiteral("material_region_source")] = source; record.lastJobDirectory.clear();
    }
    m_jobDirectory.clear(); markCaseDirty(); updateControlAvailability(); updateInputPreview();
    if (m_meshSectionControls) m_meshSectionControls->hide();
    if (m_mesh2dSourceButton) m_mesh2dSourceButton->setChecked(true);
    m_stateLabel->setText(QStringLiteral("断面已读取，请检查材料参数"));
    updateStepButtons();
    setCurrentStep(0);
    return true;
}

bool SlopeStandardAnalysisWidget::hasAvailableGeometrySection() const
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return false;
    const QList<QString> ids = visual->allIds();
    for (const QString& id : ids)
    {
        if (!id.endsWith(QStringLiteral("_face")))
            continue;
        auto* actor = vtkActor::SafeDownCast(visual->getProp(id));
        if (!actor || actor->GetVisibility() == 0)
            continue;
        auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
        vtkPolyData* input = mapper ? mapper->GetInput() : nullptr;
        if (input && input->GetNumberOfPoints() >= 3 && input->GetNumberOfCells() > 0)
            return true;
    }
    return false;
}

void SlopeStandardAnalysisWidget::updateMaterialRegionHighlight(int row)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    const QString prefix = QStringLiteral("slope.limit.%1.").arg(m_analysisUuid);
    constexpr const char* groupName = "极限平衡";

    visual->removeProp(prefix + QStringLiteral("material_highlight_fill"), false);
    visual->removeProp(prefix + QStringLiteral("material_highlight_edge"), false);
    visual->removeProp(prefix + QStringLiteral("material_highlight_segments"), false);

    if (row < 0 || !m_pageActive)
    {
        visual->render();
        return;
    }

    const int targetSoilId = row + 1;
    int targetSourceId = targetSoilId;
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        const auto& rec = m_cases.at(m_currentCaseIndex);
        if (!rec.sourceMaterialIds.isEmpty())
        {
            targetSourceId = (row < rec.sourceMaterialIds.size())
                                 ? rec.sourceMaterialIds.at(row)
                                 : -1;
        }
    }

    // 1. Highlight filled cells if available (strictly match source material ID, never solver soilId)
    bool filledHighlighted = false;
    if (m_sectionFillPickActor && targetSourceId >= 0)
    {
        auto* mapper = vtkPolyDataMapper::SafeDownCast(m_sectionFillPickActor->GetMapper());
        vtkPolyData* sourcePoly = mapper ? mapper->GetInput() : nullptr;
        if (sourcePoly && sourcePoly->GetNumberOfCells() > 0)
        {
            vtkDataArray* matArray = nullptr;
            if (sourcePoly->GetCellData())
            {
                matArray = sourcePoly->GetCellData()->GetArray("__SlopeSectionMaterial");
                if (!matArray)
                    matArray = sourcePoly->GetCellData()->GetArray("__SlopeSectionPreviewMaterial");
            }

            auto matchingCellIds = vtkSmartPointer<vtkIdList>::New();
            if (matArray)
            {
                for (vtkIdType cellId = 0; cellId < sourcePoly->GetNumberOfCells(); ++cellId)
                {
                    if (cellId < matArray->GetNumberOfTuples())
                    {
                        const int matVal = qRound(matArray->GetComponent(cellId, 0));
                        if (matVal == targetSourceId)
                            matchingCellIds->InsertNextId(cellId);
                    }
                }
            }

            if (matchingCellIds->GetNumberOfIds() > 0)
            {
                filledHighlighted = true;
                auto highlightPoly = vtkSmartPointer<vtkPolyData>::New();
                highlightPoly->SetPoints(sourcePoly->GetPoints());
                highlightPoly->Allocate(matchingCellIds->GetNumberOfIds());
                auto ptIds = vtkSmartPointer<vtkIdList>::New();
                for (vtkIdType i = 0; i < matchingCellIds->GetNumberOfIds(); ++i)
                {
                    vtkIdType cellId = matchingCellIds->GetId(i);
                    sourcePoly->GetCellPoints(cellId, ptIds);
                    highlightPoly->InsertNextCell(sourcePoly->GetCellType(cellId), ptIds);
                }

                const auto regionDisplay = makeSlopeRegionDisplay(highlightPoly);
                auto hMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                hMapper->SetInputData(regionDisplay.fill);
                hMapper->ScalarVisibilityOff();
                hMapper->SetResolveCoincidentTopologyToPolygonOffset();
                hMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, -4.0);

                auto hActor = vtkSmartPointer<vtkActor>::New();
                hActor->SetMapper(hMapper);
                hActor->PickableOff();
                hActor->GetProperty()->LightingOff();
                hActor->GetProperty()->SetColor(1.0, 0.82, 0.08);
                hActor->GetProperty()->SetOpacity(1.0);
                hActor->GetProperty()->EdgeVisibilityOff();

                double pos[3]{};
                m_sectionFillPickActor->GetPosition(pos);
                hActor->SetPosition(pos[0], pos[1], pos[2]);

                visual->addProp(prefix + QStringLiteral("material_highlight_fill"),
                                hActor, groupName, QStringLiteral("选中材料高亮"), false, false);

                if (regionDisplay.boundary->GetNumberOfCells() > 0)
                {
                    auto edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                    edgeMapper->SetInputData(regionDisplay.boundary);
                    edgeMapper->ScalarVisibilityOff();
                    edgeMapper->SetResolveCoincidentTopologyToPolygonOffset();
                    edgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, -6.0);

                    auto edgeActor = vtkSmartPointer<vtkActor>::New();
                    edgeActor->SetMapper(edgeMapper);
                    edgeActor->PickableOff();
                    edgeActor->GetProperty()->LightingOff();
                    edgeActor->GetProperty()->SetColor(0.95, 0.32, 0.0);
                    edgeActor->GetProperty()->SetLineWidth(3.5);
                    edgeActor->SetPosition(pos[0], pos[1], pos[2]);

                    visual->addProp(prefix + QStringLiteral("material_highlight_edge"),
                                    edgeActor, groupName, QStringLiteral("选中材料边界"), false, false);
                }
            }
        }
    }

    // 2. Fallback: only when no filled region is available (e.g. pure DAT import or fill hidden),
    // highlight matching boundary segments strictly by solver soilId (never sourceMaterialId).
    if (!filledHighlighted)
    {
        const SlopeStandardDatModel model = collectPreviewModel();
        QVector<int> matchingSegments;
        for (int i = 0; i < model.segments.size(); ++i)
        {
            if (model.segments.at(i).soilId == targetSoilId)
                matchingSegments.append(i);
        }
        if (!matchingSegments.isEmpty())
        {
            const QString sourceType = m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size()
                                           ? m_cases.at(m_currentCaseIndex).sourceType : QString();
            const bool mesh3dSource = sourceType == QStringLiteral("mesh3d");
            const bool mesh2dSource = sourceType == QStringLiteral("mesh2d");
            const bool geometrySource = sourceType == QStringLiteral("geometry");
            const bool mappedSectionSource = mesh3dSource || mesh2dSource || geometrySource;
            const bool showOriginalModel = m_pageRole == PageRole::AnalysisSettings
                                               ? false
                                               : (m_pageRole != PageRole::SectionEditor
                                                  || !m_showOriginalModelCheck
                                                  || m_showOriginalModelCheck->isChecked());
            const bool sectionOnlyView = mappedSectionSource && !showOriginalModel;
            const int horizontalAxis = m_sectionHorizontalAxis;
            const int verticalAxis = m_sectionVerticalAxis;
            const int normalAxis = m_sectionNormalAxis;
            const double planePosition = (mesh3dSource || geometrySource) && m_sectionPositionSpin
                                             && m_planeCombo && m_planeCombo->count() > 0
                                             ? m_sectionPositionSpin->value() : m_sectionWorldOffset;
            const auto sourceHorizontal = [&](double x)
            {
                return m_sectionHorizontalReversed
                           ? m_sectionHorizontalMinimum + m_sectionHorizontalMaximum - x
                           : x;
            };
            const auto insertPoint = [&](vtkPoints* points, double x, double y, double offset)
            {
                if (sectionOnlyView)
                {
                    points->InsertNextPoint(sourceHorizontal(x), y, offset);
                    return;
                }
                double world[3]{x, y, offset};
                if (mappedSectionSource && verticalAxis >= 0 && verticalAxis < 3
                    && normalAxis >= 0 && normalAxis < 3 && horizontalAxis >= 0 && horizontalAxis < 3)
                {
                    world[0] = world[1] = world[2] = 0.0;
                    world[horizontalAxis] = sourceHorizontal(x);
                    world[verticalAxis] = y;
                    world[normalAxis] = planePosition + offset;
                }
                points->InsertNextPoint(world);
            };

            auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
            for (const auto& node : model.nodes)
                insertPoint(points, node.x, node.y, 0.03);
            auto lines = vtkSmartPointer<vtkCellArray>::New();
            for (int segIdx : matchingSegments)
            {
                const auto& seg = model.segments.at(segIdx);
                if (seg.startNode >= 1 && seg.startNode <= model.nodes.size()
                    && seg.endNode >= 1 && seg.endNode <= model.nodes.size())
                {
                    lines->InsertNextCell(2);
                    lines->InsertCellPoint(seg.startNode - 1);
                    lines->InsertCellPoint(seg.endNode - 1);
                }
            }
            if (lines->GetNumberOfCells() > 0)
            {
                auto segPoly = vtkSmartPointer<vtkPolyData>::New();
                segPoly->SetPoints(points);
                segPoly->SetLines(lines);
                auto sMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                sMapper->SetInputData(segPoly);
                sMapper->SetResolveCoincidentTopologyToPolygonOffset();
                sMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, -8.0);
                auto sActor = vtkSmartPointer<vtkActor>::New();
                sActor->SetMapper(sMapper);
                sActor->PickableOff();
                sActor->GetProperty()->LightingOff();
                sActor->GetProperty()->SetColor(0.95, 0.32, 0.0);
                sActor->GetProperty()->SetLineWidth(4.0);
                visual->addProp(prefix + QStringLiteral("material_highlight_segments"),
                                sActor, groupName, QStringLiteral("选中材料分界线"), false, false);
            }
        }
    }

    visual->render();
}

QVector<vtkSmartPointer<vtkPolyData>> SlopeStandardAnalysisWidget::currentGeometrySectionRegions(
    QString* errorMessage)
{
    QVector<vtkSmartPointer<vtkPolyData>> regions;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        if (errorMessage) *errorMessage = QStringLiteral("可视化服务不可用，无法读取当前几何。");
        return regions;
    }

    // Base_Geometry 会把每个可见 CAD 实体的面 actor 以 <uuid>_face 注册到共享
    // 可视化服务。这里仅消费其 VTK 表示，不依赖 OCC/Geometry 的私有实现。
    const QList<QString> ids = visual->allIds();
    for (const QString& id : ids)
    {
        if (!id.endsWith(QStringLiteral("_face")))
            continue;
        auto* actor = vtkActor::SafeDownCast(visual->getProp(id));
        if (!actor || actor->GetVisibility() == 0)
            continue;
        auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
        vtkPolyData* input = mapper ? mapper->GetInput() : nullptr;
        if (!input || input->GetNumberOfPoints() < 3 || input->GetNumberOfCells() <= 0)
            continue;
        auto copy = vtkSmartPointer<vtkPolyData>::New();
        copy->DeepCopy(input);
        regions.append(copy);
    }
    if (regions.isEmpty() && errorMessage)
        *errorMessage = QStringLiteral("当前没有可用于剖切的可见几何实体。请先显示目标几何，或改用三维/二维网格。 ");
    return regions;
}

bool SlopeStandardAnalysisWidget::extractSectionFromCurrentGeometry(bool silent)
{
    QString error;
    const auto regions = currentGeometrySectionRegions(&error);
    if (regions.isEmpty())
    {
        if (!silent) QMessageBox::information(this, QStringLiteral("当前几何不可用"), error);
        else if (m_sourceStatusLabel) m_sourceStatusLabel->setText(error);
        return false;
    }

    double bounds[6]{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
                     std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
                     std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
    for (const auto& poly : regions)
    {
        double b[6]{}; poly->GetBounds(b);
        for (int axis = 0; axis < 3; ++axis)
        {
            bounds[2 * axis] = std::min(bounds[2 * axis], b[2 * axis]);
            bounds[2 * axis + 1] = std::max(bounds[2 * axis + 1], b[2 * axis + 1]);
        }
    }

    // 首次进入几何来源时初始化工程高程轴、候选剖面方向和位置。
    if (m_verticalCombo->count() == 0 || m_materialFieldCombo->currentData().toString() != QStringLiteral("geometry"))
    {
        const QSignalBlocker b1(m_verticalCombo), b2(m_planeCombo), b3(m_materialFieldCombo);
        m_verticalCombo->clear();
        int verticalAxis = 1;
        if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
        {
            const int reported = meshService->verticalAxis() - 1;
            if (reported >= 0 && reported < 3) verticalAxis = reported;
        }
        for (int axis = 0; axis < 3; ++axis)
            m_verticalCombo->addItem(axis == verticalAxis
                                         ? QStringLiteral("%1（工程高程轴，推荐）").arg(axisName(axis))
                                         : axisName(axis), axis);
        m_verticalCombo->setCurrentIndex(m_verticalCombo->findData(verticalAxis));
        m_materialFieldCombo->clear();
        m_materialFieldCombo->addItem(QStringLiteral("几何实体（仅用于分区）"), QStringLiteral("geometry"));
        m_planeCombo->clear();
    }

    const int verticalAxis = m_verticalCombo->currentData().toInt();
    struct Candidate { int normal=-1; int horizontal=-1; double relief=0.0; double score=0.0; };
    QVector<Candidate> candidates;
    const auto evaluateCandidate = [&](int normalAxis)
    {
        Candidate c; c.normal = normalAxis; c.horizontal = 3 - verticalAxis - normalAxis;
        double topMin = std::numeric_limits<double>::max();
        double topMax = std::numeric_limits<double>::lowest();
        vtkIdType cutCount = 0;
        const double planePosition = 0.5 * (bounds[2 * normalAxis] + bounds[2 * normalAxis + 1]);
        for (const auto& poly : regions)
        {
            double origin[3]{0.5*(bounds[0]+bounds[1]),0.5*(bounds[2]+bounds[3]),0.5*(bounds[4]+bounds[5])};
            double normal[3]{0.0,0.0,0.0}; origin[normalAxis]=planePosition; normal[normalAxis]=1.0;
            auto plane=vtkSmartPointer<vtkPlane>::New(); plane->SetOrigin(origin); plane->SetNormal(normal);
            auto cutter=vtkSmartPointer<vtkCutter>::New(); cutter->SetCutFunction(plane); cutter->SetInputData(poly); cutter->Update();
            vtkPolyData* cut=cutter->GetOutput();
            if (!cut) continue;
            cutCount += cut->GetNumberOfCells();
            for (vtkIdType i=0;i<cut->GetNumberOfPoints();++i)
            {
                double pt[3]{}; cut->GetPoint(i,pt);
                topMin=std::min(topMin,pt[verticalAxis]); topMax=std::max(topMax,pt[verticalAxis]);
            }
        }
        c.relief = (topMax >= topMin) ? topMax-topMin : 0.0;
        const double normalSpan=std::abs(bounds[2*normalAxis+1]-bounds[2*normalAxis]);
        const double horizontalSpan=std::abs(bounds[2*c.horizontal+1]-bounds[2*c.horizontal]);
        c.score = c.relief * 1.0e6 + std::log1p(horizontalSpan/std::max(normalSpan,1.0e-12))*1.0e4 + std::min<double>(cutCount,999.0);
        return c;
    };
    for (int axis=0;axis<3;++axis) if (axis!=verticalAxis) candidates.append(evaluateCandidate(axis));
    std::sort(candidates.begin(),candidates.end(),[](const Candidate&a,const Candidate&b){return a.score>b.score;});

    if (m_planeCombo->count() == 0)
    {
        const QSignalBlocker blocker(m_planeCombo);
        for (int i=0;i<candidates.size();++i)
        {
            const auto& c=candidates.at(i);
            m_planeCombo->addItem(i==0 ? QStringLiteral("主剖面方向（推荐）") : QStringLiteral("备选剖面方向"), c.normal);
            m_planeCombo->setItemData(i, QStringLiteral("沿 %1 展开，剖面 %2=常数；几何地表高程起伏约 %3。")
                                      .arg(axisName(c.horizontal),axisName(c.normal),displayNumber(c.relief)), Qt::ToolTipRole);
        }
        if (!candidates.isEmpty()) m_planeCombo->setCurrentIndex(0);
    }
    if (m_planeCombo->count() <= 0) return false;
    const int normalAxis=m_planeCombo->currentData().toInt();
    const int horizontalAxis=3-verticalAxis-normalAxis;
    const double minimum=bounds[2*normalAxis], maximum=bounds[2*normalAxis+1];
    {
        const QSignalBlocker b1(m_sectionPositionSpin), b2(m_sectionPositionSlider);
        m_sectionPositionSpin->setRange(minimum,maximum);
        if (m_sectionPositionSpin->value()<minimum || m_sectionPositionSpin->value()>maximum
            || std::abs(m_sectionPositionSpin->maximum()-m_sectionPositionSpin->minimum())<1.0e-15)
        {
            m_sectionPositionSpin->setValue(0.5*(minimum+maximum));
            m_sectionPositionSlider->setValue(500);
        }
    }
    const double planePosition=m_sectionPositionSpin->value();
    const double horizontalMinimum=bounds[2*horizontalAxis];
    const double verticalMinimum=bounds[2*verticalAxis];
    const double span=std::max(std::abs(bounds[2*horizontalAxis+1]-horizontalMinimum),
                               std::abs(bounds[2*verticalAxis+1]-verticalMinimum));
    const double mergeTolerance=std::max(span*1.0e-7,1.0e-8);
    if (m_simplificationToleranceSpin->value()<=0.0)
        m_simplificationToleranceSpin->setValue(std::max(span*1.0e-4,1.0e-8));

    QVector<SectionPoint> points;
    std::map<std::pair<long long,long long>,int> pointLookup;
    const auto pointIndex=[&](const double coordinates[3])
    {
        const SectionPoint point{coordinates[horizontalAxis],coordinates[verticalAxis]};
        const auto key=std::make_pair(std::llround((point.x-horizontalMinimum)/mergeTolerance),
                                      std::llround((point.y-verticalMinimum)/mergeTolerance));
        auto it=pointLookup.find(key); if(it!=pointLookup.end()) return it->second;
        const int index=points.size(); points.append(point); pointLookup.emplace(key,index); return index;
    };
    std::map<std::pair<int,int>,QVector<SectionCellUse>> edgeUses;
    QMap<int,QString> materialLabels;
    int regionId=0;
    for (const auto& poly : regions)
    {
        ++regionId; materialLabels.insert(regionId,QStringLiteral("几何分区 %1").arg(regionId));
        double origin[3]{0.5*(bounds[0]+bounds[1]),0.5*(bounds[2]+bounds[3]),0.5*(bounds[4]+bounds[5])};
        double normal[3]{0.0,0.0,0.0}; origin[normalAxis]=planePosition; normal[normalAxis]=1.0;
        auto plane=vtkSmartPointer<vtkPlane>::New(); plane->SetOrigin(origin); plane->SetNormal(normal);
        auto cutter=vtkSmartPointer<vtkCutter>::New(); cutter->SetCutFunction(plane); cutter->SetInputData(poly); cutter->Update();
        auto triangulator=vtkSmartPointer<vtkContourTriangulator>::New(); triangulator->SetInputConnection(cutter->GetOutputPort()); triangulator->Update();
        vtkPolyData* filled=triangulator->GetOutput();
        if (!filled || filled->GetNumberOfCells()<=0) continue;
        for (vtkIdType cellId=0;cellId<filled->GetNumberOfCells();++cellId)
        {
            vtkCell* cell=filled->GetCell(cellId);
            if(!cell || cell->GetCellDimension()!=2 || cell->GetNumberOfPoints()<3) continue;
            QVector<int> polygon; SectionPoint centroid;
            for(vtkIdType local=0;local<cell->GetNumberOfPoints();++local)
            {
                double coord[3]{}; filled->GetPoint(cell->GetPointId(local),coord);
                polygon.append(pointIndex(coord)); centroid.x+=coord[horizontalAxis]; centroid.y+=coord[verticalAxis];
            }
            centroid.x/=polygon.size(); centroid.y/=polygon.size();
            for(int i=0;i<polygon.size();++i){int a=polygon.at(i),b=polygon.at((i+1)%polygon.size()); if(a!=b) edgeUses[std::minmax(a,b)].append({regionId,centroid});}
        }
    }
    if (edgeUses.empty())
    {
        error=QStringLiteral("当前剖切位置没有形成有效几何截面，请移动剖面位置。");
        if(!silent) QMessageBox::warning(this,QStringLiteral("几何断面识别失败"),error); else m_sourceStatusLabel->setText(error);
        return false;
    }
    QVector<SectionEdge> rawSurfaceEdges,rawInterfaceEdges;
    classifySectionEdges(points,edgeUses,mergeTolerance,&rawSurfaceEdges,&rawInterfaceEdges);
    SectionExtractionResult result;
    if(!finalizeSection(points,rawSurfaceEdges,rawInterfaceEdges,materialLabels,
                        m_simplificationToleranceSpin->value(),span,
                        m_surfaceCandidateCombo ? m_surfaceCandidateCombo->currentData().toInt():0,
                        &result,&error))
    {
        if(!silent) QMessageBox::warning(this,QStringLiteral("几何断面识别失败"),error); else m_sourceStatusLabel->setText(error);
        return false;
    }

    m_sectionHorizontalReversed=result.horizontalAxisReversed;
    m_sectionHorizontalMinimum=result.horizontalMirrorMinimum; m_sectionHorizontalMaximum=result.horizontalMirrorMaximum;
    m_sectionHorizontalAxis=horizontalAxis; m_sectionVerticalAxis=verticalAxis; m_sectionNormalAxis=normalAxis; m_sectionWorldOffset=planePosition;

    const bool oldLoading=m_loadingCase; m_loadingCase=true;
    const auto setCell=[](QTableWidget* table,int row,int column,const QString& text,bool editable=true){auto* item=new QTableWidgetItem(text);if(!editable)item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(row,column,item);};
    QHash<int,QStringList> previousMaterialInput;
    if(m_currentCaseIndex>=0 && m_currentCaseIndex<m_cases.size())
    {
        const QVector<int>& previousIds=m_cases.at(m_currentCaseIndex).sourceMaterialIds;
        const int keepRows=std::min(static_cast<int>(previousIds.size()),m_soilTable->rowCount());
        for(int row=0;row<keepRows;++row){QStringList values;for(int col=2;col<=6;++col)values.append(m_soilTable->item(row,col)?m_soilTable->item(row,col)->text():QString());previousMaterialInput.insert(previousIds.at(row),values);}
    }
    m_soilTable->clearContents(); m_soilTable->setRowCount(result.sourceMaterialIds.size());
    for(int row=0;row<result.sourceMaterialIds.size();++row)
    {
        const int sourceId=result.sourceMaterialIds.value(row); const QString regionText=QStringLiteral("材料区域 %1").arg(row+1);
        setCell(m_soilTable,row,0,regionText,false); setCell(m_soilTable,row,1,regionText,false);
        const QStringList previous=previousMaterialInput.value(sourceId); for(int col=2;col<=6;++col)setCell(m_soilTable,row,col,previous.value(col-2));
        for(int col=2;col<=5;++col)if(m_soilTable->item(row,col)->text().trimmed().isEmpty())m_soilTable->item(row,col)->setBackground(QColor(255,235,235));
    }
    m_nodeTable->clearContents();m_nodeTable->setRowCount(result.model.nodes.size());
    for(int row=0;row<result.model.nodes.size();++row){const auto& n=result.model.nodes.at(row);setCell(m_nodeTable,row,0,QString::number(n.id),false);setCell(m_nodeTable,row,1,displayNumber(n.x));setCell(m_nodeTable,row,2,displayNumber(n.y));}
    m_segmentTable->clearContents();m_segmentTable->setRowCount(result.model.segments.size());
    for(int row=0;row<result.model.segments.size();++row){const auto& seg=result.model.segments.at(row);setCell(m_segmentTable,row,0,QString::number(seg.startNode));setCell(m_segmentTable,row,1,QString::number(seg.endNode));setCell(m_segmentTable,row,2,QString::number(seg.soilId));}
    m_surfaceTable->clearContents();m_surfaceTable->setRowCount(result.model.surfaceLoads.size());
    for(int row=0;row<result.model.surfaceLoads.size();++row){setCell(m_surfaceTable,row,0,QString::number(result.model.surfaceLoads.at(row).segmentId));setCell(m_surfaceTable,row,1,QStringLiteral("0"));}
    m_phreaticTable->clearContents();m_phreaticTable->setRowCount(0);m_slopeHeightSpin->setValue(result.model.slopeHeight);m_loadingCase=oldLoading;
    m_sourceStatusLabel->setText(QStringLiteral("当前断面：%1 个区域。")
                                 .arg(result.sourceMaterialIds.size()));
    if(m_currentCaseIndex>=0 && m_currentCaseIndex<m_cases.size())
    {
        CaseRecord& record=m_cases[m_currentCaseIndex];record.sourceType=QStringLiteral("geometry");record.sourceSummary=m_sourceStatusLabel->text();record.materialNames=result.materialNames;record.sourceMaterialIds=result.sourceMaterialIds;record.state[QStringLiteral("material_input_mode")]=QStringLiteral("manual_v1");record.state[QStringLiteral("material_region_source")]=QStringLiteral("geometry");record.lastJobDirectory.clear();
    }
    markCaseDirty();updateControlAvailability();updateInputPreview();updateStepButtons();
    if(!silent)m_stateLabel->setText(QStringLiteral("断面已更新。"));
    return true;
}

void SlopeStandardAnalysisWidget::requestGeometrySection()
{
    if (m_geometrySourceButton) m_geometrySourceButton->setChecked(true);
    if (m_meshSectionControls) m_meshSectionControls->show();
    // 清空 mesh 专属选择，extractSectionFromCurrentGeometry 会重建几何来源控件。
    {
        const QSignalBlocker b1(m_verticalCombo), b2(m_planeCombo), b3(m_materialFieldCombo);
        m_verticalCombo->clear(); m_planeCombo->clear(); m_materialFieldCombo->clear();
    }
    extractSectionFromCurrentGeometry(false);
}

void SlopeStandardAnalysisWidget::importDat()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入已有边坡断面"), QString(),
        QStringLiteral("边坡断面文件 (*.dat *.DAT);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, QStringLiteral("导入失败"), file.errorString());
        return;
    }
    const QString imported = QString::fromLocal8Bit(file.readAll());
    QString error;
    if (!loadFormFromDat(imported, &error))
    {
        QMessageBox::warning(this, QStringLiteral("断面导入失败"), error);
        return;
    }
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        CaseRecord& record = m_cases[m_currentCaseIndex];
        record.sourceType = QStringLiteral("dat");
        record.sourceSummary = QStringLiteral("已导入断面：%1").arg(QFileInfo(path).fileName());
        record.dat = imported;
        record.lastJobDirectory.clear();
        record.materialNames.clear();
        record.sourceMaterialIds.clear();
        for (int row = 0; row < m_soilTable->rowCount(); ++row)
        {
            record.materialNames.append(m_soilTable->item(row, 0)
                                            ? m_soilTable->item(row, 0)->text()
                                            : QStringLiteral("区域 %1").arg(row + 1));
            record.sourceMaterialIds.append(row + 1);
        }
        m_sourceStatusLabel->setText(record.sourceSummary);
    }
    m_jobDirectory.clear();
    markCaseDirty();
    if (m_meshSectionControls) m_meshSectionControls->hide();
    if (m_importSourceButton) m_importSourceButton->setChecked(true);
    updateInputPreview();
    updateStepButtons();
    m_stateLabel->setText(QStringLiteral("断面已导入；请核对材料参数与工况后计算"));
    setCurrentStep(0);
}

void SlopeStandardAnalysisWidget::exportDat()
{
    QString error;
    if (m_datEditor->document()->isModified())
    {
        if (!loadFormFromDat(m_datEditor->toPlainText(), &error))
        {
            QMessageBox::warning(this, QStringLiteral("无法导出 DAT"),
                                 QStringLiteral("工况数据无法转换：%1").arg(error));
            return;
        }
    }
    if (!syncDatFromForm(&error))
    {
        focusErrorField(error);
        QMessageBox::warning(this, QStringLiteral("无法导出 DAT"), error);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 SlopeEngine DAT"), QStringLiteral("case.dat"),
        QStringLiteral("DAT 文件 (*.dat);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    if (!writeInputDat(path, &error))
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
}

void SlopeStandardAnalysisWidget::loadExampleForAdvancedUse()
{
    if (QMessageBox::question(
            this, QStringLiteral("载入示例"),
            QStringLiteral("这会替换当前工况的断面和材料参数。示例仅用于测试，不代表工程参数。是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QString error;
    if (!loadFormFromDat(defaultDatText(), &error))
    {
        QMessageBox::warning(this, QStringLiteral("示例载入失败"), error);
        return;
    }
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        m_cases[m_currentCaseIndex].sourceType = QStringLiteral("example");
        m_cases[m_currentCaseIndex].sourceSummary = QStringLiteral("官方两层测试示例（非工程参数）");
        m_sourceStatusLabel->setText(m_cases[m_currentCaseIndex].sourceSummary);
    }
    markCaseDirty();
    updateInputPreview();
}

QString SlopeStandardAnalysisWidget::engineExecutable() const
{
    return QDir::toNativeSeparators(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("solver/SlopeEngine/SlopeEngine.exe")));
}

QString SlopeStandardAnalysisWidget::createJobDirectory(QString* errorMessage) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString().trimmed();
    if (projectDir.isEmpty() || !QDir(projectDir).exists())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("请先创建或保存工程，再进行边坡极限平衡计算。");
        return {};
    }

    // 一个“边坡极限平衡分析”节点就是一个独立计算工况。结果目录直接使用
    // 工程树节点名称，不再把 UUID、内部 caseId 和时间戳暴露给用户。
    QString analysisName = property("solverNodeTitle").toString().trimmed();
    if (analysisName.isEmpty())
        analysisName = QStringLiteral("极限平衡");

#ifdef Q_OS_WIN
    static const QRegularExpression invalidPathChars(QStringLiteral(R"([<>:"/\\|?*])"));
    if (analysisName.contains(invalidPathChars) || analysisName.endsWith(QLatin1Char('.'))
        || analysisName.endsWith(QLatin1Char(' ')))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral(
                "分析节点名称“%1”不能作为结果文件夹名称。请重命名节点，且不要使用这些字符：\\ / : * ? \" < > |；名称末尾也不能是空格或句点。")
                                .arg(analysisName);
        }
        return {};
    }
#endif

    const QString job = QDir(projectDir).filePath(
        QStringLiteral("边坡极限平衡/%1").arg(analysisName));
    if (!QDir().mkpath(job))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建计算结果目录：%1").arg(job);
        return {};
    }
    return QDir(job).absolutePath();
}

bool SlopeStandardAnalysisWidget::writeInputDat(const QString& path, QString* errorMessage) const
{
    SlopeStandardDatModel model;
    if (!collectFormModel(&model, errorMessage))
        return false;
    const QString input = model.serialize();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入输入文件：%1").arg(path);
        return false;
    }
    file.write(input.toLocal8Bit());
    file.write("\r\n");
    if (!file.commit())
    {
        if (errorMessage) *errorMessage = QStringLiteral("输入文件保存失败：%1").arg(path);
        return false;
    }
    return true;
}

void SlopeStandardAnalysisWidget::startValidateAndCalculate()
{
    if (isRunning()) return;
    QString error;
    if (m_datEditor->document()->isModified())
    {
        if (!loadFormFromDat(m_datEditor->toPlainText(), &error))
        {
            m_advancedSettingsToggle->setChecked(true);
            QMessageBox::warning(this, QStringLiteral("工况数据无法使用"), error);
            return;
        }
    }
    if (!syncDatFromForm(&error))
    {
        focusErrorField(error);
        m_stateLabel->clear();
        QMessageBox::warning(this, QStringLiteral("请完善边坡工况"), error);
        return;
    }
    saveCurrentCase();
    m_autoCalculateAfterValidate = false;
    startOperation(Operation::Calculate, false);
}

void SlopeStandardAnalysisWidget::startOperation(Operation operation, bool reuseCurrentJob)
{
    if (isRunning()) return;
    QString error;
    const QString executable = engineExecutable();
    if (!QFileInfo::exists(executable))
    {
        QMessageBox::warning(this, QStringLiteral("计算程序未安装"),
                             QStringLiteral("未找到计算程序：\n%1").arg(executable));
        m_autoCalculateAfterValidate = false;
        return;
    }

    if (!reuseCurrentJob)
    {
        m_jobDirectory = createJobDirectory(&error);
        if (m_jobDirectory.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("无法开始计算"), error);
            m_autoCalculateAfterValidate = false;
            return;
        }
        m_statusPath = QDir(m_jobDirectory).filePath(QStringLiteral("status.json"));
        m_plotPath = QDir(m_jobDirectory).filePath(QStringLiteral("plot.json"));
        m_resultPath = QDir(m_jobDirectory).filePath(QStringLiteral("result.dat"));
        const QString inputPath = QDir(m_jobDirectory).filePath(QStringLiteral("case.dat"));
        if (!writeInputDat(inputPath, &error))
        {
            QMessageBox::warning(this, QStringLiteral("无法开始计算"), error);
            m_autoCalculateAfterValidate = false;
            return;
        }
        for (const QString& path : {inputPath, m_statusPath, m_plotPath, m_resultPath, m_jobDirectory})
        {
            if (QDir::toNativeSeparators(path).size() >= 260)
            {
                QMessageBox::warning(this, QStringLiteral("任务路径过长"),
                                     QStringLiteral("计算目录路径过长，请将工程保存到更短路径。\n%1").arg(path));
                m_autoCalculateAfterValidate = false;
                return;
            }
        }
    }

    QStringList arguments;
    if (operation == Operation::Validate)
        arguments << QStringLiteral("validate") << QStringLiteral("--input") << QStringLiteral("case.dat")
                  << QStringLiteral("--status") << QStringLiteral("status.json")
                  << QStringLiteral("--job-dir") << QStringLiteral(".");
    else
        arguments << QStringLiteral("calculate") << QStringLiteral("--input") << QStringLiteral("case.dat")
                  << QStringLiteral("--result") << QStringLiteral("result.dat")
                  << QStringLiteral("--status") << QStringLiteral("status.json")
                  << QStringLiteral("--plot") << QStringLiteral("plot.json")
                  << QStringLiteral("--job-dir") << QStringLiteral(".");

    m_operation = operation;
    m_stopRequested = false;
    m_processOutput.clear();
    if (operation == Operation::Validate)
    {
        m_lastPlot = QJsonObject();
        updateVisualization();
        m_resultViewer->clear();
        m_factorLabel->setText(QStringLiteral("安全系数：—"));
        updateRequiredFactorConclusion();
    }
    m_process->setWorkingDirectory(m_jobDirectory);
    m_process->setProgram(executable);
    m_process->setArguments(arguments);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    setRunning(true);
    m_stateLabel->setText(operation == Operation::Calculate
                              ? QStringLiteral("计算中……")
                              : QString());
    m_process->start();
}

void SlopeStandardAnalysisWidget::setRunning(bool running)
{
    m_calculateButton->setEnabled(!running);
    m_stopButton->setVisible(running);
    m_stopButton->setEnabled(running);
    m_progressBar->setVisible(running);
    m_caseCombo->setEnabled(!running);
    m_newCaseButton->setEnabled(!running);
    m_copyCaseButton->setEnabled(!running);
    m_deleteCaseButton->setEnabled(!running && m_cases.size() > 1);
    emit runningChanged(running);
}

void SlopeStandardAnalysisWidget::consumeProcessOutput()
{
    m_processOutput += QString::fromLocal8Bit(m_process->readAllStandardOutput());
    constexpr qsizetype kMaximumCapturedCharacters = 1024 * 1024;
    if (m_processOutput.size() > kMaximumCapturedCharacters)
        m_processOutput = m_processOutput.right(kMaximumCapturedCharacters);
}

void SlopeStandardAnalysisWidget::handleProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart) return;
    setRunning(false);
    m_operation = Operation::None;
    m_autoCalculateAfterValidate = false;
    m_stateLabel->setText(QStringLiteral("计算程序启动失败"));
    QMessageBox::warning(this, QStringLiteral("计算程序未启动"), m_process->errorString());
}

QJsonObject SlopeStandardAnalysisWidget::readJsonObject(const QString& path,
                                                        QString* errorMessage) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage) *errorMessage = QStringLiteral("无法读取文件：%1").arg(path);
        return {};
    }
    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (!document.isObject())
    {
        QJsonParseError localParseError;
        const QJsonDocument localDocument = QJsonDocument::fromJson(QString::fromLocal8Bit(data).toUtf8(), &localParseError);
        if (localDocument.isObject()) document = localDocument;
    }
    if (!document.isObject())
    {
        if (errorMessage) *errorMessage = QStringLiteral("JSON 格式错误：%1（%2）").arg(parseError.errorString(), path);
        return {};
    }
    return document.object();
}

QString SlopeStandardAnalysisWidget::errorDescription(int exitCode,
                                                       const QJsonObject& status) const
{
    QString detail = status.value(QStringLiteral("message")).toString().trimmed();
    const QString field = status.value(QStringLiteral("field")).toString().trimmed();
    const QString key = status.value(QStringLiteral("message_key")).toString().trimmed();
    if (!field.isEmpty()) detail += QStringLiteral("\n字段：%1").arg(field);
    if (!key.isEmpty()) detail += QStringLiteral("\n错误标识：%1").arg(key);
    if (detail.trimmed().isEmpty())
    {
        QStringList usefulOutput;
        for (QString line : m_processOutput.split(QLatin1Char('\n')))
        {
            line = line.trimmed();
            if (!line.isEmpty() && !usefulOutput.contains(line)
                && line.compare(QStringLiteral("THE PROGRAM OF SLOPE IS RUNNING NOW, PLEASE WAIT!"), Qt::CaseInsensitive) != 0)
                usefulOutput.append(line);
        }
        detail = usefulOutput.join(QLatin1Char('\n'));
    }
    if (detail.isEmpty()) detail = QStringLiteral("未知错误");
    return QStringLiteral("计算失败（退出码 %1）。\n%2").arg(exitCode).arg(detail);
}

QString SlopeStandardAnalysisWidget::userFacingEngineError(const QJsonObject& status,
                                                           int exitCode) const
{
    const QString key = status.value(QStringLiteral("message_key")).toString();
    const QString field = status.value(QStringLiteral("field")).toString();
    if (key == QStringLiteral("SEARCH_RESULT_MISSING"))
    {
        QFile report(m_resultPath);
        if (report.open(QIODevice::ReadOnly)
            && report.readAll().contains("NO VALID INITIAL ARC FOUND"))
            return QStringLiteral("没有找到有效的初始滑弧，自动搜索未能开始。\n"
                                  "请检查计算坡面、材料分区及滑弧是否能够穿过断面；"
                                  "也可改用指定圆弧检查。\n结果目录：%1").arg(m_jobDirectory);
        return QStringLiteral("搜索结果不完整，无法读取临界滑弧和安全系数。\n"
                              "请查看详细计算报告。\n结果目录：%1").arg(m_jobDirectory);
    }
    static const QMap<QString, QString> keyMessages = {
        {QStringLiteral("PHREATIC_LINE_REQUIRED"), QStringLiteral("当前选择了浸润线法，但还没有绘制浸润线。")},
        {QStringLiteral("SURFACE_COUNT_INVALID"), QStringLiteral("请至少选择一条外坡面，且不能超过 50 条。")},
        {QStringLiteral("SEGMENTS_MISSING"), QStringLiteral("断面没有有效边界线，请重新设置断面。")},
        {QStringLiteral("SOILS_MISSING"), QStringLiteral("没有材料区域，请重新设置断面。")},
        {QStringLiteral("NODES_MISSING"), QStringLiteral("断面没有有效节点，请重新设置断面。")},
        {QStringLiteral("SEARCH_STEP_INVALID"), QStringLiteral("搜索精度参数无效，请检查高级设置中的 LTH。")},
        {QStringLiteral("RADIUS_INVALID"), QStringLiteral("指定滑弧的半径必须大于 0。")},
        {QStringLiteral("SLICE_COUNT_INVALID"), QStringLiteral("计算条分数必须在 1～200 之间。")},
        {QStringLiteral("SURFACE_SEGMENT_OUT_OF_RANGE"), QStringLiteral("外坡面选择已失效，请返回断面图重新选择坡面。")},
        {QStringLiteral("SEGMENT_NODE_OUT_OF_RANGE"), QStringLiteral("断面边界引用了无效节点，请在高级几何编辑中检查。")},
        {QStringLiteral("SEGMENT_SOIL_OUT_OF_RANGE"), QStringLiteral("断面边界引用了无效材料，请重新获取断面或检查材料映射。")},
        {QStringLiteral("SEGMENT_TOO_SHORT"), QStringLiteral("断面中存在过短边界线，请调整断面或提高折线简化容差。")}};
    if (keyMessages.contains(key))
        return keyMessages.value(key);
    if (field.startsWith(QStringLiteral("soils")))
        return QStringLiteral("材料参数有误，请检查各区域参数。\n%1")
            .arg(status.value(QStringLiteral("message")).toString());
    if (field.startsWith(QStringLiteral("outer_surface")))
        return QStringLiteral("计算坡面设置有误，请检查蓝色坡面。\n%1")
            .arg(status.value(QStringLiteral("message")).toString());
    return errorDescription(exitCode, status);
}

void SlopeStandardAnalysisWidget::handleProcessFinished(int exitCode,
                                                        QProcess::ExitStatus exitStatus)
{
    consumeProcessOutput();
    const Operation finishedOperation = m_operation;
    m_operation = Operation::None;
    setRunning(false);

    if (m_stopRequested)
    {
        m_stopRequested = false;
        m_autoCalculateAfterValidate = false;
        m_stateLabel->setText(QStringLiteral("计算已停止"));
        m_openDirectoryButton->setEnabled(QDir(m_jobDirectory).exists());
        return;
    }

    QString jsonError;
    const QJsonObject status = readJsonObject(m_statusPath, &jsonError);
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0
                         && status.value(QStringLiteral("success")).toBool();
    if (!success)
    {
        m_autoCalculateAfterValidate = false;
        m_stateLabel->setText(QStringLiteral("计算未完成"));
        m_openDirectoryButton->setEnabled(QDir(m_jobDirectory).exists());
        QString description;
        if (exitStatus == QProcess::CrashExit)
            description = QStringLiteral("计算程序异常退出。\n%1").arg(m_processOutput.trimmed());
        else if (exitCode == 0 && status.isEmpty())
            description = QStringLiteral("计算已结束，但结果状态读取失败。\n%1\n结果目录：%2")
                              .arg(jsonError, m_jobDirectory);
        else
            description = userFacingEngineError(status, exitCode);
        focusErrorField(description + status.value(QStringLiteral("field")).toString());
        QMessageBox::warning(this, QStringLiteral("边坡计算未完成"), description);
        return;
    }

    if (finishedOperation == Operation::Validate)
    {
        m_stateLabel->setText(QStringLiteral("计算中……"));
        if (m_autoCalculateAfterValidate)
        {
            QTimer::singleShot(0, this, [this]() { startOperation(Operation::Calculate, true); });
            return;
        }
        return;
    }

    m_autoCalculateAfterValidate = false;
    showStatus(status, m_jobDirectory, true);
    m_stateLabel->setText(QStringLiteral("计算完成"));
    m_openDirectoryButton->setEnabled(true);
    if (m_currentCaseIndex >= 0 && m_currentCaseIndex < m_cases.size())
    {
        m_cases[m_currentCaseIndex].lastJobDirectory = m_jobDirectory;
        m_cases[m_currentCaseIndex].dat = datText();
        m_cases[m_currentCaseIndex].state = serializeCurrentState();
    }
    emit casesChanged();
    syncToSharedState();
    emit resultReady(m_jobDirectory,
                     m_requiredFactorSpin ? m_requiredFactorSpin->value() : 1.30,
                     m_sectionHorizontalReversed, m_sectionHorizontalMinimum,
                     m_sectionHorizontalMaximum);
    if (m_pageRole == PageRole::CombinedLegacy)
        setCurrentStep(2);
    else if (m_pageRole == PageRole::AnalysisSettings)
    {
        setCurrentStep(1);
        m_stateLabel->setText(QStringLiteral("计算完成"));
    }
}

void SlopeStandardAnalysisWidget::showStatus(const QJsonObject& status,
                                             const QString& jobDirectory,
                                             bool calculation)
{
    if (!calculation)
        return;
    const QJsonObject result = status.value(QStringLiteral("result")).toObject();
    const QJsonObject circle = result.value(QStringLiteral("critical_circle")).toObject();
    const double factor = result.value(QStringLiteral("factor")).toDouble();
    m_factorLabel->setText(QStringLiteral("%1安全系数：%2")
                               .arg(methodDisplayName(status.value(QStringLiteral("method")).toString()),
                                    QString::number(factor, 'f', 3)));

    QStringList summary;
    summary << QStringLiteral("计算方法：%1")
                   .arg(methodDisplayName(status.value(QStringLiteral("method")).toString()));
    if (!circle.isEmpty())
    {
        summary << QStringLiteral("临界滑弧：圆心 (%1, %2)，半径 %3")
                       .arg(displayNumber(circle.value(QStringLiteral("cx")).toDouble()),
                            displayNumber(circle.value(QStringLiteral("cy")).toDouble()),
                            displayNumber(circle.value(QStringLiteral("radius")).toDouble()));
    }
    m_summaryLabel->setText(summary.join(QStringLiteral("\n")));

    QString plotError;
    const QJsonObject plot = readJsonObject(QDir(jobDirectory).filePath(QStringLiteral("plot.json")), &plotError);
    m_lastPlot = plot;
    if (m_pageRole == PageRole::CombinedLegacy && !plot.isEmpty())
        updateResultVisualization(plot);
    else
        updateVisualization();

    QFile resultFile(QDir(jobDirectory).filePath(QStringLiteral("result.dat")));
    if (resultFile.open(QIODevice::ReadOnly))
        m_resultViewer->setPlainText(QString::fromLocal8Bit(resultFile.readAll()));
}

void SlopeStandardAnalysisWidget::loadExistingResult(const QString& jobDirectory)
{
    const QDir job(jobDirectory);
    const QString statusPath = job.filePath(QStringLiteral("status.json"));
    if (!job.exists() || !QFileInfo::exists(statusPath))
        return;
    const QJsonObject status = readJsonObject(statusPath);
    if (!status.value(QStringLiteral("success")).toBool()
        || !status.contains(QStringLiteral("result")))
        return;
    m_jobDirectory = job.absolutePath();
    m_statusPath = statusPath;
    m_plotPath = job.filePath(QStringLiteral("plot.json"));
    m_resultPath = job.filePath(QStringLiteral("result.dat"));
    showStatus(status, m_jobDirectory, true);
    m_openDirectoryButton->setEnabled(true);
}

void SlopeStandardAnalysisWidget::stopProcess()
{
    if (!isRunning()) return;
    m_stopRequested = true;
    m_autoCalculateAfterValidate = false;
    m_stateLabel->setText(QStringLiteral("正在停止计算……"));
    m_process->terminate();
    QTimer::singleShot(1500, m_process, [process = m_process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void SlopeStandardAnalysisWidget::openOutputDirectory()
{
    if (!m_jobDirectory.isEmpty() && QDir(m_jobDirectory).exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_jobDirectory));
}
