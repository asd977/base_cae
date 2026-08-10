#pragma once

#include <services/VisualWidgetService.h>
#include <services/MeshModuleControllerService.h>
#include <EventManager.h>
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <vtkActor.h>
#include <vtkAlgorithmOutput.h>
#include <vtkContourFilter.h>
#include <vtkDataObject.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkLookupTable.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkProp.h>
#include <vtkRenderer.h>
#include <vtkStripper.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkStaticPointLocator.h>
#include <vtkCell.h>
#include <vtkTextProperty.h>
#include <vtkWeakPointer.h>

#include <QByteArray>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QVector>
#include <QStringList>
#include <QVariantMap>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <vtkPointData.h>
#include <algorithm>
#include <cmath>

struct PostScalarBarStyle
{
    bool visible{true};
    int titleFont{14};
    int labelFont{11};
    int precision{4};
    int tickCount{7};
    int widthPercent{8};
    int heightPercent{62};
    bool hasCustomPosition{false};
    double positionX{0.0};
    double positionY{0.0};
};

struct PostContourStyle
{
    bool enabled{false};
    int levels{10};
    bool labels{false};
    int labelFont{10};
};

struct PostFieldRangeStyle
{
    bool automatic{true};
    bool initialized{false};
    double minimum{0.0};
    double maximum{1.0};
};

struct PostSceneStyle
{
    bool visible{true};
    bool followMeshFilter{true};
};

struct PostDisplayFieldContext
{
    QString fieldName;
    double dataMinimum{0.0};
    double dataMaximum{1.0};
    bool valid{false};
    bool supportsScalarBar{true};
    bool supportsContours{false};
    bool supportsContourLabels{false};
};

struct PostMeshFilterReport
{
    bool requested{false};
    bool customFilter{false};
    bool applied{false};
    bool fullyResolved{true};
    vtkIdType inputCells{0};
    vtkIdType outputCells{0};
    QString message;
};

inline PostScalarBarStyle& postScalarBarStyle()
{
    static PostScalarBarStyle style;
    return style;
}

inline PostContourStyle& postContourStyle()
{
    static PostContourStyle style;
    return style;
}

inline PostSceneStyle& postSceneStyle()
{
    static PostSceneStyle style;
    return style;
}

inline PostMeshFilterReport& postMeshFilterReport()
{
    static PostMeshFilterReport report;
    return report;
}

inline PostDisplayFieldContext& postDisplayFieldContext()
{
    static PostDisplayFieldContext context;
    return context;
}

inline bool postScalarBarShouldBeVisible()
{
    const PostDisplayFieldContext& context = postDisplayFieldContext();
    return postSceneStyle().visible && postScalarBarStyle().visible &&
           context.valid && context.supportsScalarBar;
}

inline bool& postDisplaySettingsAuthoritative()
{
    static bool authoritative{true};
    return authoritative;
}

inline QString postDisplaySettingsChangedEvent()
{
    return QStringLiteral("post.display.settings.changed");
}

inline QString postDisplayContextChangedEvent()
{
    return QStringLiteral("post.display.context.changed");
}

inline QString meshDisplayFilterChangedEvent()
{
    return QStringLiteral("mesh.display.filter.changed");
}

inline QString postViewActiveKey()
{
    return QStringLiteral("post.display.view.active");
}

inline QString postViewActiveChangedEvent()
{
    return QStringLiteral("post.display.view.active.changed");
}

inline void setPostViewActive(bool active)
{
    const bool previous = PublicVariables::instance().get(postViewActiveKey()).toBool();
    PublicVariables::instance().set(postViewActiveKey(), active,
                                    QStringLiteral("post_processing_view"));
    if (previous != active)
    {
        QVariantMap data;
        data.insert(QStringLiteral("active"), active);
        EventManager::instance().sendEvent(postViewActiveChangedEvent(), data);
    }
}

inline void publishPostDisplayContext(const QString& fieldName, double dataMin, double dataMax,
                                      bool supportsContours, bool supportsContourLabels,
                                      bool supportsScalarBar = true)
{
    PostDisplayFieldContext& context = postDisplayFieldContext();
    context.fieldName = fieldName;
    context.dataMinimum = dataMin;
    context.dataMaximum = dataMax;
    context.valid = !fieldName.isEmpty() && std::isfinite(dataMin) && std::isfinite(dataMax) &&
                    dataMax >= dataMin;
    context.supportsScalarBar = supportsScalarBar;
    context.supportsContours = supportsContours;
    context.supportsContourLabels = supportsContourLabels;

    QVariantMap data;
    data.insert(QStringLiteral("fieldName"), fieldName);
    EventManager::instance().sendEvent(postDisplayContextChangedEvent(), data);
}

inline void clearPostDisplayContext()
{
    postDisplayFieldContext() = PostDisplayFieldContext{};
    EventManager::instance().sendEvent(postDisplayContextChangedEvent());
}

inline void notifyPostDisplaySettingsChanged(const QString& reason = QString())
{
    QVariantMap data;
    if (!reason.isEmpty())
        data.insert(QStringLiteral("reason"), reason);
    EventManager::instance().sendEvent(postDisplaySettingsChangedEvent(), data);
}

inline QHash<QString, PostFieldRangeStyle>& postFieldRangeStyles()
{
    static QHash<QString, PostFieldRangeStyle> styles;
    return styles;
}

inline PostFieldRangeStyle& postFieldRangeStyle(const QString& fieldName,
                                                 double dataMin = 0.0,
                                                 double dataMax = 1.0)
{
    PostFieldRangeStyle& style = postFieldRangeStyles()[fieldName];
    if (!style.initialized && dataMax > dataMin)
    {
        style.minimum = dataMin;
        style.maximum = dataMax;
        style.initialized = true;
    }
    return style;
}

inline void resolvePostDisplayRange(const QString& fieldName, double dataMin, double dataMax,
                                    double& minV, double& maxV)
{
    minV = dataMin;
    maxV = dataMax;
    if (fieldName.isEmpty() || !(dataMax > dataMin))
        return;
    PostFieldRangeStyle& style = postFieldRangeStyle(fieldName, dataMin, dataMax);
    if (!style.automatic && style.initialized && style.maximum > style.minimum)
    {
        minV = style.minimum;
        maxV = style.maximum;
    }
}

inline QJsonObject savePostDisplaySettings()
{
    const PostScalarBarStyle& bar = postScalarBarStyle();
    const PostContourStyle& contour = postContourStyle();
    const PostSceneStyle& scene = postSceneStyle();
    QJsonObject obj;
    obj[QStringLiteral("postVisible")] = scene.visible;
    obj[QStringLiteral("followMeshFilter")] = scene.followMeshFilter;
    obj[QStringLiteral("scalarBarVisible")] = bar.visible;
    obj[QStringLiteral("scalarBarTitleFont")] = bar.titleFont;
    obj[QStringLiteral("scalarBarLabelFont")] = bar.labelFont;
    obj[QStringLiteral("scalarBarPrecision")] = bar.precision;
    obj[QStringLiteral("scalarBarTickCount")] = bar.tickCount;
    obj[QStringLiteral("scalarBarWidth")] = bar.widthPercent;
    obj[QStringLiteral("scalarBarHeight")] = bar.heightPercent;
    obj[QStringLiteral("scalarBarHasPosition")] = bar.hasCustomPosition;
    obj[QStringLiteral("scalarBarPositionX")] = bar.positionX;
    obj[QStringLiteral("scalarBarPositionY")] = bar.positionY;
    obj[QStringLiteral("contourEnabled")] = contour.enabled;
    obj[QStringLiteral("contourLevels")] = contour.levels;
    obj[QStringLiteral("contourLabels")] = contour.labels;
    obj[QStringLiteral("contourLabelFont")] = contour.labelFont;
    QJsonObject ranges;
    const auto& rangeStyles = postFieldRangeStyles();
    for (auto it = rangeStyles.constBegin(); it != rangeStyles.constEnd(); ++it)
    {
        QJsonObject range;
        range[QStringLiteral("automatic")] = it.value().automatic;
        range[QStringLiteral("initialized")] = it.value().initialized;
        range[QStringLiteral("minimum")] = it.value().minimum;
        range[QStringLiteral("maximum")] = it.value().maximum;
        ranges[it.key()] = range;
    }
    obj[QStringLiteral("fieldRanges")] = ranges;
    return obj;
}

inline void loadPostDisplaySettings(const QJsonObject& obj)
{
    PostScalarBarStyle& bar = postScalarBarStyle();
    PostContourStyle& contour = postContourStyle();
    PostSceneStyle& scene = postSceneStyle();
    scene.visible = obj.value(QStringLiteral("postVisible")).toBool(scene.visible);
    scene.followMeshFilter = obj.value(QStringLiteral("followMeshFilter")).toBool(scene.followMeshFilter);
    bar.visible = obj.value(QStringLiteral("scalarBarVisible")).toBool(bar.visible);
    bar.titleFont = qBound(8, obj.value(QStringLiteral("scalarBarTitleFont")).toInt(bar.titleFont), 36);
    bar.labelFont = qBound(8, obj.value(QStringLiteral("scalarBarLabelFont")).toInt(bar.labelFont), 32);
    bar.precision = qBound(1, obj.value(QStringLiteral("scalarBarPrecision")).toInt(bar.precision), 10);
    bar.tickCount = qBound(2, obj.value(QStringLiteral("scalarBarTickCount")).toInt(bar.tickCount), 64);
    bar.widthPercent = qBound(5, obj.value(QStringLiteral("scalarBarWidth")).toInt(bar.widthPercent), 25);
    bar.heightPercent = qBound(25, obj.value(QStringLiteral("scalarBarHeight")).toInt(bar.heightPercent), 90);
    bar.hasCustomPosition = obj.value(QStringLiteral("scalarBarHasPosition")).toBool(bar.hasCustomPosition);
    bar.positionX = obj.value(QStringLiteral("scalarBarPositionX")).toDouble(bar.positionX);
    bar.positionY = obj.value(QStringLiteral("scalarBarPositionY")).toDouble(bar.positionY);
    contour.enabled = obj.value(QStringLiteral("contourEnabled")).toBool(contour.enabled);
    contour.levels = qBound(2, obj.value(QStringLiteral("contourLevels")).toInt(contour.levels), 30);
    contour.labels = obj.value(QStringLiteral("contourLabels")).toBool(contour.labels);
    contour.labelFont = qBound(8, obj.value(QStringLiteral("contourLabelFont")).toInt(contour.labelFont), 28);
    postFieldRangeStyles().clear();
    const QJsonObject ranges = obj.value(QStringLiteral("fieldRanges")).toObject();
    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it)
    {
        const QJsonObject range = it.value().toObject();
        PostFieldRangeStyle style;
        style.automatic = range.value(QStringLiteral("automatic")).toBool(true);
        style.initialized = range.value(QStringLiteral("initialized")).toBool(false);
        style.minimum = range.value(QStringLiteral("minimum")).toDouble(0.0);
        style.maximum = range.value(QStringLiteral("maximum")).toDouble(1.0);
        if (style.maximum <= style.minimum)
            style.automatic = true;
        postFieldRangeStyles().insert(it.key(), style);
    }
}

inline QSet<int> postIntSetFromString(const QString& text)
{
    QSet<int> values;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok)
            values.insert(value);
    }
    return values;
}

inline QSet<int> postMeshCheckedValues(const QString& category)
{
    QString key;
    if (category == QStringLiteral("KETYPE"))
        key = QStringLiteral("mesh.display.filter.ketype.checked");
    else if (category == QStringLiteral("MATYPE"))
        key = QStringLiteral("mesh.display.filter.matype.checked");
    else if (category == QStringLiteral("LAYER"))
        key = QStringLiteral("mesh.display.filter.layer.checked");
    return key.isEmpty() ? QSet<int>{}
                         : postIntSetFromString(PublicVariables::instance().get(key).toString());
}

inline bool postMeshFilterIsCustom()
{
    if (!PublicVariables::instance()
             .get(QStringLiteral("mesh.display.filter.custom"))
             .toBool())
        return false;

    const QString storedProject = PublicVariables::instance()
                                      .get(QStringLiteral("mesh.display.filter.project"))
                                      .toString()
                                      .trimmed();
    const QString currentProject = PublicVariables::instance()
                                       .get(PublicConstants::kProjectDirectoryKey)
                                       .toString()
                                       .trimmed();
    if (storedProject.isEmpty() || currentProject.isEmpty())
        return false;
    return storedProject == currentProject;
}

inline QSet<int> postUniqueCellValues(vtkDataArray* array)
{
    QSet<int> values;
    if (!array)
        return values;
    for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
        values.insert(static_cast<int>(array->GetTuple1(i)));
    return values;
}

inline vtkSmartPointer<vtkUnstructuredGrid> postShallowGrid(vtkUnstructuredGrid* source)
{
    auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (source)
        result->ShallowCopy(source);
    return result;
}

inline bool postCellTopologyCompatible(vtkUnstructuredGrid* resultGrid,
                                       vtkUnstructuredGrid* meshGrid)
{
    if (!resultGrid || !meshGrid ||
        resultGrid->GetNumberOfCells() != meshGrid->GetNumberOfCells())
        return false;

    const vtkIdType cellCount = resultGrid->GetNumberOfCells();
    if (cellCount <= 0)
        return true;

    // 快速路径：结果网格与原网格保持相同的单元顺序和连接关系。
    const vtkIdType sampleCount = std::min<vtkIdType>(cellCount, 48);
    auto resultIds = vtkSmartPointer<vtkIdList>::New();
    auto meshIds = vtkSmartPointer<vtkIdList>::New();
    for (vtkIdType i = 0; i < sampleCount; ++i)
    {
        const vtkIdType cellId = sampleCount == 1
                                     ? 0
                                     : (i * (cellCount - 1)) / (sampleCount - 1);
        if (resultGrid->GetCellType(cellId) != meshGrid->GetCellType(cellId))
            return false;

        resultIds->Reset();
        meshIds->Reset();
        resultGrid->GetCellPoints(cellId, resultIds);
        meshGrid->GetCellPoints(cellId, meshIds);
        if (resultIds->GetNumberOfIds() != meshIds->GetNumberOfIds())
            return false;
        for (vtkIdType p = 0; p < resultIds->GetNumberOfIds(); ++p)
            if (resultIds->GetId(p) != meshIds->GetId(p))
                return false;
    }
    return true;
}

struct PostResultMeshCellMap
{
    vtkWeakPointer<vtkUnstructuredGrid> resultGrid;
    vtkWeakPointer<vtkUnstructuredGrid> meshGrid;
    vtkIdType resultCells{0};
    vtkIdType meshCells{0};
    vtkMTimeType resultMTime{0};
    vtkMTimeType meshMTime{0};
    QVector<vtkIdType> resultToMesh;
    bool complete{false};
    quint64 accessSerial{0};
};

inline QVector<PostResultMeshCellMap>& postResultMeshCellMapCache()
{
    static QVector<PostResultMeshCellMap> cache;
    return cache;
}

inline quint64& postResultMeshCellMapSerial()
{
    static quint64 serial = 0;
    return serial;
}

inline double postGridDiagonal(vtkUnstructuredGrid* grid)
{
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return 0.0;
    double bounds[6]{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    grid->GetBounds(bounds);
    const double dx = bounds[1] - bounds[0];
    const double dy = bounds[3] - bounds[2];
    const double dz = bounds[5] - bounds[4];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline QByteArray postCellPointIdKey(vtkUnstructuredGrid* grid, vtkIdType cellId)
{
    QByteArray key;
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return key;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell || !cell->GetPointIds())
        return key;

    QVector<vtkIdType> ids;
    ids.reserve(static_cast<int>(cell->GetPointIds()->GetNumberOfIds()));
    for (vtkIdType i = 0; i < cell->GetPointIds()->GetNumberOfIds(); ++i)
        ids.push_back(cell->GetPointIds()->GetId(i));
    std::sort(ids.begin(), ids.end());

    key.reserve(24 + ids.size() * 12);
    key.append(QByteArray::number(grid->GetCellType(cellId)));
    key.append(':');
    for (vtkIdType id : ids)
    {
        key.append(QByteArray::number(static_cast<qlonglong>(id)));
        key.append(',');
    }
    return key;
}

inline QVector<vtkIdType> postBuildResultToMeshCellMap(vtkUnstructuredGrid* resultGrid,
                                                        vtkUnstructuredGrid* meshGrid,
                                                        bool& complete)
{
    complete = false;
    QVector<vtkIdType> mapping;
    if (!resultGrid || !meshGrid || resultGrid->GetNumberOfCells() <= 0 ||
        meshGrid->GetNumberOfCells() <= 0 || meshGrid->GetNumberOfPoints() <= 0)
        return mapping;

    const vtkIdType resultCellCount = resultGrid->GetNumberOfCells();
    mapping.fill(-1, static_cast<int>(resultCellCount));

    // 1) 最快路径：结果仍保持原网格的单元顺序和连接关系。
    if (postCellTopologyCompatible(resultGrid, meshGrid))
    {
        for (vtkIdType cid = 0; cid < resultCellCount; ++cid)
            mapping[static_cast<int>(cid)] = cid;
        complete = true;
        return mapping;
    }

    QHash<QByteArray, vtkIdType> meshCellByKey;
    meshCellByKey.reserve(static_cast<int>(meshGrid->GetNumberOfCells() * 1.25));
    for (vtkIdType cid = 0; cid < meshGrid->GetNumberOfCells(); ++cid)
    {
        const QByteArray key = postCellPointIdKey(meshGrid, cid);
        if (!key.isEmpty() && !meshCellByKey.contains(key))
            meshCellByKey.insert(key, cid);
    }

    vtkIdType mappedCount = 0;

    // 2) 稳定单元编号优先。内部生成的后处理缓存会携带 elem_id，
    // 即使显示坐标以后发生变形，也不需要再依赖空间位置。
    vtkDataArray* resultElemIds = resultGrid->GetCellData()
                                      ? resultGrid->GetCellData()->GetArray("elem_id")
                                      : nullptr;
    vtkDataArray* meshElemIds = meshGrid->GetCellData()
                                    ? meshGrid->GetCellData()->GetArray("elem_id")
                                    : nullptr;
    if (resultElemIds && meshElemIds &&
        resultElemIds->GetNumberOfTuples() >= resultCellCount)
    {
        QHash<qlonglong, vtkIdType> meshCellByElementId;
        meshCellByElementId.reserve(static_cast<int>(meshGrid->GetNumberOfCells() * 1.25));
        for (vtkIdType cid = 0; cid < meshGrid->GetNumberOfCells(); ++cid)
            meshCellByElementId.insert(
                static_cast<qlonglong>(std::llround(meshElemIds->GetTuple1(cid))), cid);

        for (vtkIdType cid = 0; cid < resultCellCount; ++cid)
        {
            const qlonglong elementId =
                static_cast<qlonglong>(std::llround(resultElemIds->GetTuple1(cid)));
            const auto it = meshCellByElementId.constFind(elementId);
            if (it == meshCellByElementId.constEnd())
                continue;
            mapping[static_cast<int>(cid)] = it.value();
            ++mappedCount;
        }
        if (mappedCount == resultCellCount)
        {
            complete = true;
            return mapping;
        }
    }

    // 3) 稳定节点编号。Tecplot 分区合并会重排 VTK 点号，但 node_id
    // 不变，因此可以用“单元类型 + 原始节点集合”建立关联。
    vtkDataArray* resultNodeIds = resultGrid->GetPointData()
                                      ? resultGrid->GetPointData()->GetArray("node_id")
                                      : nullptr;
    vtkDataArray* meshNodeIds = meshGrid->GetPointData()
                                    ? meshGrid->GetPointData()->GetArray("node_id")
                                    : nullptr;
    if (resultNodeIds && meshNodeIds &&
        resultNodeIds->GetNumberOfTuples() >= resultGrid->GetNumberOfPoints())
    {
        QHash<qlonglong, vtkIdType> meshPointByNodeId;
        meshPointByNodeId.reserve(static_cast<int>(meshGrid->GetNumberOfPoints() * 1.25));
        for (vtkIdType pid = 0; pid < meshGrid->GetNumberOfPoints(); ++pid)
            meshPointByNodeId.insert(
                static_cast<qlonglong>(std::llround(meshNodeIds->GetTuple1(pid))), pid);

        for (vtkIdType resultCellId = 0; resultCellId < resultCellCount; ++resultCellId)
        {
            if (mapping[static_cast<int>(resultCellId)] >= 0)
                continue;
            vtkCell* resultCell = resultGrid->GetCell(resultCellId);
            if (!resultCell || !resultCell->GetPointIds())
                continue;

            QVector<vtkIdType> meshPointIds;
            meshPointIds.reserve(static_cast<int>(resultCell->GetPointIds()->GetNumberOfIds()));
            bool resolved = true;
            for (vtkIdType p = 0; p < resultCell->GetPointIds()->GetNumberOfIds(); ++p)
            {
                const vtkIdType resultPointId = resultCell->GetPointIds()->GetId(p);
                const qlonglong nodeId = static_cast<qlonglong>(
                    std::llround(resultNodeIds->GetTuple1(resultPointId)));
                const auto it = meshPointByNodeId.constFind(nodeId);
                if (it == meshPointByNodeId.constEnd())
                {
                    resolved = false;
                    break;
                }
                meshPointIds.push_back(it.value());
            }
            if (!resolved)
                continue;

            std::sort(meshPointIds.begin(), meshPointIds.end());
            QByteArray key;
            key.reserve(24 + meshPointIds.size() * 12);
            key.append(QByteArray::number(resultGrid->GetCellType(resultCellId)));
            key.append(':');
            for (vtkIdType id : meshPointIds)
            {
                key.append(QByteArray::number(static_cast<qlonglong>(id)));
                key.append(',');
            }
            const auto cellIt = meshCellByKey.constFind(key);
            if (cellIt == meshCellByKey.constEnd())
                continue;
            mapping[static_cast<int>(resultCellId)] = cellIt.value();
            ++mappedCount;
        }
        if (mappedCount == resultCellCount)
        {
            complete = true;
            return mapping;
        }
    }

    // 4) 最后才做几何位置兜底，兼容外部导入、没有稳定 ID 的旧结果文件。
    // 这个分支只匹配尚未关联的单元。
    auto locator = vtkSmartPointer<vtkStaticPointLocator>::New();
    locator->SetDataSet(meshGrid);
    locator->BuildLocator();

    const double diagonal = std::max(postGridDiagonal(meshGrid), 1.0);
    const double tolerance = std::max(diagonal * 1.0e-6, 1.0e-8);
    const double tolerance2 = tolerance * tolerance;

    for (vtkIdType resultCellId = 0; resultCellId < resultCellCount; ++resultCellId)
    {
        if (mapping[static_cast<int>(resultCellId)] >= 0)
            continue;
        vtkCell* resultCell = resultGrid->GetCell(resultCellId);
        if (!resultCell || !resultCell->GetPointIds())
            continue;

        QVector<vtkIdType> meshPointIds;
        meshPointIds.reserve(static_cast<int>(resultCell->GetPointIds()->GetNumberOfIds()));
        bool cellMapped = true;
        for (vtkIdType p = 0; p < resultCell->GetPointIds()->GetNumberOfIds(); ++p)
        {
            const vtkIdType resultPointId = resultCell->GetPointIds()->GetId(p);
            double resultPoint[3]{0.0, 0.0, 0.0};
            resultGrid->GetPoint(resultPointId, resultPoint);

            const vtkIdType meshPointId = locator->FindClosestPoint(resultPoint);
            if (meshPointId < 0)
            {
                cellMapped = false;
                break;
            }

            double meshPoint[3]{0.0, 0.0, 0.0};
            meshGrid->GetPoint(meshPointId, meshPoint);
            const double dx = resultPoint[0] - meshPoint[0];
            const double dy = resultPoint[1] - meshPoint[1];
            const double dz = resultPoint[2] - meshPoint[2];
            if (dx * dx + dy * dy + dz * dz > tolerance2)
            {
                cellMapped = false;
                break;
            }
            meshPointIds.push_back(meshPointId);
        }

        if (!cellMapped)
            continue;

        std::sort(meshPointIds.begin(), meshPointIds.end());
        QByteArray key;
        key.reserve(24 + meshPointIds.size() * 12);
        key.append(QByteArray::number(resultGrid->GetCellType(resultCellId)));
        key.append(':');
        for (vtkIdType id : meshPointIds)
        {
            key.append(QByteArray::number(static_cast<qlonglong>(id)));
            key.append(',');
        }

        const auto it = meshCellByKey.constFind(key);
        if (it == meshCellByKey.constEnd())
            continue;

        mapping[static_cast<int>(resultCellId)] = it.value();
        ++mappedCount;
    }

    complete = mappedCount == resultCellCount;
    return mapping;
}

inline const PostResultMeshCellMap& postResultToMeshCellMap(vtkUnstructuredGrid* resultGrid,
                                                             vtkUnstructuredGrid* meshGrid)
{
    auto& cache = postResultMeshCellMapCache();

    // 清理已经释放的数据集，避免缓存长期保留悬空地址；同时用 MTime 判断
    // “对象地址没变但网格内容已经原地更新”的情况。
    for (int i = cache.size() - 1; i >= 0; --i)
    {
        if (!cache[i].resultGrid.GetPointer() || !cache[i].meshGrid.GetPointer())
            cache.removeAt(i);
    }

    const vtkIdType resultCells = resultGrid ? resultGrid->GetNumberOfCells() : 0;
    const vtkIdType meshCells = meshGrid ? meshGrid->GetNumberOfCells() : 0;
    const vtkMTimeType resultMTime = resultGrid ? resultGrid->GetMTime() : 0;
    const vtkMTimeType meshMTime = meshGrid ? meshGrid->GetMTime() : 0;

    for (PostResultMeshCellMap& entry : cache)
    {
        if (entry.resultGrid.GetPointer() == resultGrid &&
            entry.meshGrid.GetPointer() == meshGrid &&
            entry.resultCells == resultCells && entry.meshCells == meshCells &&
            entry.resultMTime == resultMTime && entry.meshMTime == meshMTime)
        {
            entry.accessSerial = ++postResultMeshCellMapSerial();
            return entry;
        }
    }

    PostResultMeshCellMap entry;
    entry.resultGrid = resultGrid;
    entry.meshGrid = meshGrid;
    entry.resultCells = resultCells;
    entry.meshCells = meshCells;
    entry.resultMTime = resultMTime;
    entry.meshMTime = meshMTime;
    entry.resultToMesh = postBuildResultToMeshCellMap(resultGrid, meshGrid, entry.complete);
    entry.accessSerial = ++postResultMeshCellMapSerial();
    cache.push_back(entry);

    constexpr int maxCacheEntries = 12;
    if (cache.size() > maxCacheEntries)
    {
        int oldestIndex = 0;
        for (int i = 1; i < cache.size(); ++i)
            if (cache[i].accessSerial < cache[oldestIndex].accessSerial)
                oldestIndex = i;
        cache.removeAt(oldestIndex);
    }

    for (PostResultMeshCellMap& cached : cache)
    {
        if (cached.resultGrid.GetPointer() == resultGrid &&
            cached.meshGrid.GetPointer() == meshGrid &&
            cached.resultMTime == resultMTime && cached.meshMTime == meshMTime)
            return cached;
    }

    static PostResultMeshCellMap empty;
    return empty;
}


inline QString postSortedIntSetText(const QSet<int>& values)
{
    QList<int> ordered = values.values();
    std::sort(ordered.begin(), ordered.end());
    QStringList parts;
    parts.reserve(ordered.size());
    for (int value : ordered)
        parts.push_back(QString::number(value));
    return parts.join(QLatin1Char(','));
}

inline QString postMeshFilterSignature()
{
    const QString project = PublicVariables::instance()
                                .get(PublicConstants::kProjectDirectoryKey)
                                .toString()
                                .trimmed();
    return QStringLiteral("%1|K=%2|M=%3|L=%4")
        .arg(project,
             postSortedIntSetText(postMeshCheckedValues(QStringLiteral("KETYPE"))),
             postSortedIntSetText(postMeshCheckedValues(QStringLiteral("MATYPE"))),
             postSortedIntSetText(postMeshCheckedValues(QStringLiteral("LAYER"))));
}

struct PostFilteredResultCacheEntry
{
    vtkWeakPointer<vtkUnstructuredGrid> sourceGrid;
    vtkWeakPointer<vtkUnstructuredGrid> meshGrid;
    vtkMTimeType sourceMTime{0};
    vtkMTimeType meshMTime{0};
    QString filterSignature;
    vtkSmartPointer<vtkUnstructuredGrid> output;
    PostMeshFilterReport report;
    quint64 accessSerial{0};
};

inline QVector<PostFilteredResultCacheEntry>& postFilteredResultCache()
{
    static QVector<PostFilteredResultCacheEntry> cache;
    return cache;
}

inline quint64& postFilteredResultCacheSerial()
{
    static quint64 serial = 0;
    return serial;
}

inline vtkSmartPointer<vtkUnstructuredGrid> postCachedFilteredResult(
    vtkUnstructuredGrid* source, vtkUnstructuredGrid* meshGrid,
    const QString& signature, PostMeshFilterReport& report)
{
    auto& cache = postFilteredResultCache();
    for (int i = cache.size() - 1; i >= 0; --i)
    {
        if (!cache[i].sourceGrid.GetPointer())
            cache.removeAt(i);
    }

    const vtkMTimeType sourceMTime = source ? source->GetMTime() : 0;
    const vtkMTimeType meshMTime = meshGrid ? meshGrid->GetMTime() : 0;
    for (PostFilteredResultCacheEntry& entry : cache)
    {
        if (entry.sourceGrid.GetPointer() == source &&
            entry.meshGrid.GetPointer() == meshGrid &&
            entry.sourceMTime == sourceMTime && entry.meshMTime == meshMTime &&
            entry.filterSignature == signature && entry.output)
        {
            entry.accessSerial = ++postFilteredResultCacheSerial();
            report = entry.report;
            return postShallowGrid(entry.output);
        }
    }
    return {};
}

inline void postStoreFilteredResult(vtkUnstructuredGrid* source,
                                    vtkUnstructuredGrid* meshGrid,
                                    const QString& signature,
                                    vtkUnstructuredGrid* output,
                                    const PostMeshFilterReport& report)
{
    if (!source || !output || signature.isEmpty())
        return;

    PostFilteredResultCacheEntry entry;
    entry.sourceGrid = source;
    entry.meshGrid = meshGrid;
    entry.sourceMTime = source->GetMTime();
    entry.meshMTime = meshGrid ? meshGrid->GetMTime() : 0;
    entry.filterSignature = signature;
    entry.output = vtkSmartPointer<vtkUnstructuredGrid>::New();
    entry.output->ShallowCopy(output);
    entry.report = report;
    entry.accessSerial = ++postFilteredResultCacheSerial();

    auto& cache = postFilteredResultCache();
    cache.push_back(entry);
    constexpr int maxCacheEntries = 6;
    while (cache.size() > maxCacheEntries)
    {
        int oldestIndex = 0;
        for (int i = 1; i < cache.size(); ++i)
            if (cache[i].accessSerial < cache[oldestIndex].accessSerial)
                oldestIndex = i;
        cache.removeAt(oldestIndex);
    }
}

inline vtkSmartPointer<vtkUnstructuredGrid> postFilteredResultGrid(vtkUnstructuredGrid* source)
{
    PostMeshFilterReport report;
    report.requested = postSceneStyle().followMeshFilter;
    report.customFilter = postMeshFilterIsCustom();
    report.inputCells = source ? source->GetNumberOfCells() : 0;
    report.outputCells = report.inputCells;

    if (!source || !report.requested || !report.customFilter)
    {
        postMeshFilterReport() = report;
        return postShallowGrid(source);
    }

    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkSmartPointer<vtkUnstructuredGrid> meshGrid =
        meshService ? meshService->getVtkUnstructuredGrid()
                    : vtkSmartPointer<vtkUnstructuredGrid>{};
    vtkSmartPointer<vtkUnstructuredGrid> mappingGrid = meshGrid;

    const QString filterSignature = postMeshFilterSignature();
    PostMeshFilterReport cachedReport;
    if (auto cached = postCachedFilteredResult(source, mappingGrid, filterSignature, cachedReport))
    {
        postMeshFilterReport() = cachedReport;
        return cached;
    }

    vtkDataArray* originalIds =
        source->GetCellData() ? source->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;

    struct Category
    {
        const char* name;
        QSet<int> checked;
        QSet<int> all;
        vtkDataArray* directArray{nullptr};
        vtkDataArray* meshArray{nullptr};
        bool restricted{false};
        bool canResolve{false};
    };

    Category categories[] = {
        {"KETYPE", postMeshCheckedValues(QStringLiteral("KETYPE"))},
        {"MATYPE", postMeshCheckedValues(QStringLiteral("MATYPE"))},
        {"LAYER", postMeshCheckedValues(QStringLiteral("LAYER"))},
    };

    bool anyRestricted = false;
    bool anyResolvableRestricted = false;
    bool allRestrictedResolved = true;
    bool requiresCellMap = false;

    // 先判断当前真正限制了哪些类别。结果文件自己已经携带分组字段时，
    // 不再提前构建结果-原网格映射，避免第一次筛选产生无意义的大开销。
    for (Category& category : categories)
    {
        category.directArray =
            source->GetCellData() ? source->GetCellData()->GetArray(category.name) : nullptr;
        category.meshArray =
            mappingGrid && mappingGrid->GetCellData()
                ? mappingGrid->GetCellData()->GetArray(category.name)
                : nullptr;

        vtkDataArray* fullArray =
            meshGrid && meshGrid->GetCellData()
                ? meshGrid->GetCellData()->GetArray(category.name)
                : nullptr;
        if (fullArray)
            category.all = postUniqueCellValues(fullArray);
        else if (category.meshArray)
            category.all = postUniqueCellValues(category.meshArray);
        else if (category.directArray)
            category.all = postUniqueCellValues(category.directArray);

        if (category.all.isEmpty())
            continue;

        category.restricted = category.checked != category.all;
        anyRestricted = anyRestricted || category.restricted;
    }

    const PostResultMeshCellMap* cellMap = nullptr;
    bool hasMappedCell = false;
    bool needsCellMap = false;
    for (const Category& category : categories)
    {
        if (category.restricted && !category.directArray && category.meshArray && !originalIds)
        {
            needsCellMap = true;
            break;
        }
    }

    if (needsCellMap)
    {
        cellMap = &postResultToMeshCellMap(source, mappingGrid);
        for (vtkIdType mappedId : cellMap->resultToMesh)
        {
            if (mappedId >= 0)
            {
                hasMappedCell = true;
                break;
            }
        }
    }

    for (Category& category : categories)
    {
        if (!category.restricted)
            continue;
        category.canResolve = category.directArray ||
                              (category.meshArray && (originalIds || hasMappedCell));
        if (!category.directArray && category.meshArray && !originalIds)
            requiresCellMap = true;
        anyResolvableRestricted = anyResolvableRestricted || category.canResolve;
        allRestrictedResolved = allRestrictedResolved && category.canResolve;
    }

    if (!anyRestricted)
    {
        report.applied = true;
        auto result = postShallowGrid(source);
        postStoreFilteredResult(source, mappingGrid, filterSignature, result, report);
        postMeshFilterReport() = report;
        return result;
    }

    if (!anyResolvableRestricted || !allRestrictedResolved)
    {
        // 所有被限制的类别都必须能可靠关联。不能只应用 MATYPE 却悄悄忽略
        // LAYER/KETYPE，否则画面与用户勾选条件并不一致。
        auto emptyResult = vtkSmartPointer<vtkUnstructuredGrid>::New();
        report.applied = false;
        report.fullyResolved = false;
        report.outputCells = 0;
        report.message = QStringLiteral("当前结果无法完整应用网格筛选");
        postStoreFilteredResult(source, mappingGrid, filterSignature, emptyResult, report);
        postMeshFilterReport() = report;
        return emptyResult;
    }

    auto ids = vtkSmartPointer<vtkIdList>::New();
    ids->Allocate(source->GetNumberOfCells());

    auto valueForCell = [&](const Category& category, vtkIdType cellId, bool& ok) -> int
    {
        ok = false;
        if (category.directArray && cellId < category.directArray->GetNumberOfTuples())
        {
            ok = true;
            return static_cast<int>(category.directArray->GetTuple1(cellId));
        }

        if (!category.meshArray)
            return 0;

        vtkIdType meshCellId = -1;
        if (originalIds && cellId < originalIds->GetNumberOfTuples())
            meshCellId = static_cast<vtkIdType>(originalIds->GetTuple1(cellId));
        else if (cellMap && cellId >= 0 && cellId < cellMap->resultToMesh.size())
            meshCellId = cellMap->resultToMesh.at(static_cast<int>(cellId));

        if (meshCellId < 0 || meshCellId >= category.meshArray->GetNumberOfTuples())
            return 0;

        ok = true;
        return static_cast<int>(category.meshArray->GetTuple1(meshCellId));
    };

    vtkIdType unresolvedCellCount = 0;
    for (vtkIdType cellId = 0; cellId < source->GetNumberOfCells(); ++cellId)
    {
        bool keep = true;
        bool unresolved = false;
        for (const Category& category : categories)
        {
            if (!category.restricted)
                continue;

            bool ok = false;
            const int value = valueForCell(category, cellId, ok);
            if (!ok)
            {
                // 未知归属的结果单元不继续显示，保证显示出来的每个单元都满足
                // 用户当前的全部筛选条件。
                unresolved = true;
                keep = false;
                break;
            }
            if (!category.checked.contains(value))
            {
                keep = false;
                break;
            }
        }
        if (unresolved)
            ++unresolvedCellCount;
        if (keep)
            ids->InsertNextId(cellId);
    }

    auto extractor = vtkSmartPointer<vtkExtractCells>::New();
    extractor->SetInputData(source);
    extractor->SetCellList(ids);
    extractor->Update();

    auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (extractor->GetOutput())
        result->ShallowCopy(extractor->GetOutput());

    report.applied = true;
    report.fullyResolved = allRestrictedResolved &&
                           (!requiresCellMap || (cellMap && cellMap->complete)) &&
                           unresolvedCellCount == 0;
    report.outputCells = result->GetNumberOfCells();
    if (report.outputCells <= 0)
    {
        report.message = unresolvedCellCount >= report.inputCells && report.inputCells > 0
                             ? QStringLiteral("当前结果无法可靠关联网格筛选")
                             : QStringLiteral("当前网格筛选下没有可显示的结果");
    }
    else if (!report.fullyResolved)
    {
        report.message = QStringLiteral("已应用网格筛选，%1 个无法关联的结果单元已隐藏")
                             .arg(unresolvedCellCount);
    }
    postStoreFilteredResult(source, mappingGrid, filterSignature, result, report);
    postMeshFilterReport() = report;
    return result;
}

inline void applyStandardPostLookupTable(vtkLookupTable* lut, double minV, double maxV)
{
    if (!lut)
        return;

    if (!std::isfinite(minV) || !std::isfinite(maxV))
    {
        minV = 0.0;
        maxV = 1.0;
    }
    if (maxV < minV)
        std::swap(minV, maxV);
    if (std::abs(maxV - minV) < 1.0e-14)
    {
        const double delta = std::max(std::abs(minV) * 1.0e-6, 1.0e-9);
        minV -= delta;
        maxV += delta;
    }

    struct ColorStop
    {
        double t;
        double r;
        double g;
        double b;
    };
    static constexpr ColorStop kStops[] = {
        {0.00, 0.10, 0.20, 0.72},
        {0.16, 0.05, 0.43, 0.92},
        {0.32, 0.00, 0.72, 0.88},
        {0.48, 0.13, 0.78, 0.55},
        {0.64, 0.72, 0.84, 0.22},
        {0.80, 0.98, 0.65, 0.12},
        {0.92, 0.94, 0.28, 0.10},
        {1.00, 0.72, 0.04, 0.08},
    };

    constexpr int tableSize = 512;
    constexpr int stopCount = int(sizeof(kStops) / sizeof(kStops[0]));
    lut->SetNumberOfTableValues(tableSize);
    lut->SetTableRange(minV, maxV);
    for (int i = 0; i < tableSize; ++i)
    {
        const double t = double(i) / double(tableSize - 1);
        int hi = 1;
        while (hi < stopCount && t > kStops[hi].t)
            ++hi;
        hi = std::min(hi, stopCount - 1);
        const int lo = std::max(0, hi - 1);
        const double span = std::max(kStops[hi].t - kStops[lo].t, 1.0e-12);
        const double u = std::clamp((t - kStops[lo].t) / span, 0.0, 1.0);
        lut->SetTableValue(
            i,
            kStops[lo].r + (kStops[hi].r - kStops[lo].r) * u,
            kStops[lo].g + (kStops[hi].g - kStops[lo].g) * u,
            kStops[lo].b + (kStops[hi].b - kStops[lo].b) * u,
            1.0);
    }
    lut->SetNanColor(0.55, 0.55, 0.55, 1.0);
    lut->Build();
}

inline void applyStandardPostScalarBar(vtkScalarBarActor* scalarBar)
{
    if (!scalarBar)
        return;

    const PostScalarBarStyle& style = postScalarBarStyle();
    const double width = style.widthPercent / 100.0;
    const double height = style.heightPercent / 100.0;
    const double defaultX = qBound(0.0, 0.965 - width, 1.0 - width);
    const double defaultY = qBound(0.0, 0.5 - height * 0.5, 1.0 - height);
    const double x = style.hasCustomPosition
                         ? qBound(0.0, style.positionX, 1.0 - width)
                         : defaultX;
    const double y = style.hasCustomPosition
                         ? qBound(0.0, style.positionY, 1.0 - height)
                         : defaultY;

    scalarBar->SetOrientationToVertical();
    scalarBar->UnconstrainedFontSizeOn();
    scalarBar->SetWidth(width);
    scalarBar->SetHeight(height);
    scalarBar->SetPosition(x, y);
    scalarBar->SetMaximumWidthInPixels(1000);
    scalarBar->SetMaximumHeightInPixels(2000);
    scalarBar->UseCustomLabelsOff();
    scalarBar->SetCustomLabels(nullptr);
    scalarBar->SetNumberOfLabels(style.tickCount);
    const QByteArray labelFormat = QStringLiteral("%.%1g").arg(style.precision).toLatin1();
    scalarBar->SetLabelFormat(labelFormat.constData());
    scalarBar->DrawBackgroundOff();
    scalarBar->DrawFrameOff();

    if (vtkTextProperty* title = scalarBar->GetTitleTextProperty())
    {
        title->SetFontSize(style.titleFont);
        title->SetColor(0.08, 0.08, 0.08);
        title->SetBold(1);
    }
    if (vtkTextProperty* label = scalarBar->GetLabelTextProperty())
    {
        label->SetFontSize(style.labelFont);
        label->SetColor(0.08, 0.08, 0.08);
        label->SetBold(0);
    }
    scalarBar->Modified();
}

inline void applyStandardPostScalarBar(VisualWidgetService* service, const QString& title)
{
    if (!service)
        return;
    vtkScalarBarActor* scalarBar = service->scalarBarActor();
    if (!scalarBar)
        return;
    applyStandardPostScalarBar(scalarBar);
    scalarBar->SetTitle(title.toUtf8().constData());
    scalarBar->Modified();
}

inline bool editPostDisplaySettings(QWidget* parent, VisualWidgetService* service,
                                    bool supportsContours, bool supportsContourLabels = false,
                                    const QString& fieldName = QString(),
                                    double dataMin = 0.0, double dataMax = 1.0)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("显示设置"));
    dialog.setModal(true);

    auto* rootLayout = new QVBoxLayout(&dialog);

    QCheckBox* automaticRange = nullptr;
    QDoubleSpinBox* rangeMinSpin = nullptr;
    QDoubleSpinBox* rangeMaxSpin = nullptr;
    if (!fieldName.isEmpty() && dataMax > dataMin)
    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(fieldName, dataMin, dataMax);
        auto* rangeGroup = new QGroupBox(QStringLiteral("显示范围"), &dialog);
        auto* rangeForm = new QFormLayout(rangeGroup);
        automaticRange = new QCheckBox(QStringLiteral("自动使用数据范围"), rangeGroup);
        automaticRange->setChecked(rangeStyle.automatic);
        rangeMinSpin = new QDoubleSpinBox(rangeGroup);
        rangeMaxSpin = new QDoubleSpinBox(rangeGroup);
        rangeMinSpin->setRange(-1.0e100, 1.0e100);
        rangeMaxSpin->setRange(-1.0e100, 1.0e100);
        rangeMinSpin->setDecimals(8);
        rangeMaxSpin->setDecimals(8);
        rangeMinSpin->setKeyboardTracking(false);
        rangeMaxSpin->setKeyboardTracking(false);
        rangeMinSpin->setValue(rangeStyle.initialized ? rangeStyle.minimum : dataMin);
        rangeMaxSpin->setValue(rangeStyle.initialized ? rangeStyle.maximum : dataMax);
        rangeMinSpin->setEnabled(!rangeStyle.automatic);
        rangeMaxSpin->setEnabled(!rangeStyle.automatic);
        QObject::connect(automaticRange, &QCheckBox::toggled, rangeMinSpin, &QWidget::setDisabled);
        QObject::connect(automaticRange, &QCheckBox::toggled, rangeMaxSpin, &QWidget::setDisabled);
        rangeForm->addRow(automaticRange);
        rangeForm->addRow(QStringLiteral("范围下限"), rangeMinSpin);
        rangeForm->addRow(QStringLiteral("范围上限"), rangeMaxSpin);
        rootLayout->addWidget(rangeGroup);
    }

    auto* scalarBarGroup = new QGroupBox(QStringLiteral("色标"), &dialog);
    auto* scalarBarForm = new QFormLayout(scalarBarGroup);
    PostScalarBarStyle& bar = postScalarBarStyle();

    auto makeSpin = [&dialog](int min, int max, int value) {
        auto* spin = new QSpinBox(&dialog);
        spin->setRange(min, max);
        spin->setValue(value);
        spin->setKeyboardTracking(false);
        return spin;
    };

    QSpinBox* titleFont = makeSpin(8, 36, bar.titleFont);
    QSpinBox* labelFont = makeSpin(8, 32, bar.labelFont);
    QSpinBox* precision = makeSpin(1, 10, bar.precision);
    QSpinBox* ticks = makeSpin(2, 64, bar.tickCount);
    QSpinBox* width = makeSpin(5, 25, bar.widthPercent);
    QSpinBox* height = makeSpin(25, 90, bar.heightPercent);
    width->setSuffix(QStringLiteral(" %"));
    height->setSuffix(QStringLiteral(" %"));

    scalarBarForm->addRow(QStringLiteral("标题字号"), titleFont);
    scalarBarForm->addRow(QStringLiteral("刻度字号"), labelFont);
    scalarBarForm->addRow(QStringLiteral("数值位数"), precision);
    scalarBarForm->addRow(QStringLiteral("刻度数量"), ticks);
    scalarBarForm->addRow(QStringLiteral("色标宽度"), width);
    scalarBarForm->addRow(QStringLiteral("色标高度"), height);
    rootLayout->addWidget(scalarBarGroup);

    QCheckBox* contourEnabled = nullptr;
    QSpinBox* contourLevels = nullptr;
    QCheckBox* contourLabels = nullptr;
    QSpinBox* contourLabelFont = nullptr;
    if (supportsContours)
    {
        auto* contourGroup = new QGroupBox(QStringLiteral("等值线"), &dialog);
        auto* contourForm = new QFormLayout(contourGroup);
        PostContourStyle& contour = postContourStyle();
        contourEnabled = new QCheckBox(QStringLiteral("显示等值线"), contourGroup);
        contourEnabled->setChecked(contour.enabled);
        contourLevels = makeSpin(2, 30, contour.levels);
        contourForm->addRow(contourEnabled);
        contourForm->addRow(QStringLiteral("等值线数量"), contourLevels);
        if (supportsContourLabels)
        {
            contourLabels = new QCheckBox(QStringLiteral("显示数值"), contourGroup);
            contourLabels->setChecked(contour.labels);
            contourLabelFont = makeSpin(8, 28, contour.labelFont);
            contourForm->addRow(contourLabels, contourLabelFont);
        }
        rootLayout->addWidget(contourGroup);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (automaticRange && rangeMinSpin && rangeMaxSpin)
    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(fieldName, dataMin, dataMax);
        rangeStyle.automatic = automaticRange->isChecked();
        rangeStyle.initialized = true;
        if (rangeMaxSpin->value() > rangeMinSpin->value())
        {
            rangeStyle.minimum = rangeMinSpin->value();
            rangeStyle.maximum = rangeMaxSpin->value();
        }
        else
        {
            rangeStyle.minimum = dataMin;
            rangeStyle.maximum = dataMax;
            rangeStyle.automatic = true;
        }
    }

    bar.titleFont = titleFont->value();
    bar.labelFont = labelFont->value();
    bar.precision = precision->value();
    bar.tickCount = ticks->value();
    bar.widthPercent = width->value();
    bar.heightPercent = height->value();

    if (supportsContours)
    {
        PostContourStyle& contour = postContourStyle();
        contour.enabled = contourEnabled && contourEnabled->isChecked();
        contour.levels = contourLevels ? contourLevels->value() : contour.levels;
        if (supportsContourLabels)
        {
            contour.labels = contourLabels && contourLabels->isChecked();
            contour.labelFont = contourLabelFont ? contourLabelFont->value() : contour.labelFont;
        }
    }

    if (service && service->scalarBarActor())
    {
        applyStandardPostScalarBar(service->scalarBarActor());
        service->showScalarBar(postScalarBarShouldBeVisible(), false);
        service->render();
    }
    return true;
}


inline vtkSmartPointer<vtkActor> createPostContourActor(vtkAlgorithmOutput* inputPort,
                                                        const QString& scalarName,
                                                        double minV, double maxV)
{
    if (!inputPort || scalarName.isEmpty() || !postContourStyle().enabled ||
        !(maxV > minV))
        return nullptr;

    const QByteArray name = scalarName.toUtf8();
    auto contour = vtkSmartPointer<vtkContourFilter>::New();
    contour->SetInputConnection(inputPort);
    contour->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    name.constData());
    const int levels = qMax(2, postContourStyle().levels);
    contour->SetNumberOfContours(levels);
    for (int i = 0; i < levels; ++i)
    {
        const double t = double(i + 1) / double(levels + 1);
        contour->SetValue(i, minV + (maxV - minV) * t);
    }

    auto stripper = vtkSmartPointer<vtkStripper>::New();
    stripper->SetInputConnection(contour->GetOutputPort());

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(stripper->GetOutputPort());
    mapper->ScalarVisibilityOff();
    mapper->SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -1.0);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetColor(0.10, 0.10, 0.10);
    actor->GetProperty()->SetOpacity(0.82);
    actor->GetProperty()->SetLineWidth(1.0);
    actor->GetProperty()->SetRenderLinesAsTubes(true);
    return actor;
}

inline QString postActorId(const QString& ownerUuid, const QString& actorRole)
{
    const QString owner = ownerUuid.trimmed();
    const QString role = actorRole.trimmed();
    if (owner.isEmpty() || role.isEmpty())
        return QString();
    return QString("post.%1.%2").arg(owner, role);
}

template <typename T>
inline bool addPostPropByService(VisualWidgetService* service, const QString& id,
                                 const vtkSmartPointer<T>& prop,
                                 const QString& displayName = QString(),
                                 bool renderNow = true)
{
    if (!service || id.isEmpty() || !prop)
        return false;

    vtkProp* existing = service->getProp(id);
    if (existing && existing != prop.GetPointer())
        service->removeProp(id, false);

    const bool ok = service->addProp(id, prop, QStringLiteral("post"), displayName,
                                     false, false);
    const bool attached = ok || service->getProp(id) == prop.GetPointer();
    if (attached && !postSceneStyle().visible)
        service->setVisible(id, false, false);
    if (renderNow)
        service->render();
    return attached;
}

template <typename T>
inline bool removePostPropByService(VisualWidgetService* service, const QString& id,
                                    const vtkSmartPointer<T>& prop,
                                    bool renderNow = true)
{
    if (!service || id.isEmpty())
        return false;
    vtkProp* existing = service->getProp(id);
    const bool ok = existing && (!prop.GetPointer() || existing == prop.GetPointer())
                        ? service->removeProp(id, false)
                        : false;
    if (renderNow)
        service->render();
    return ok;
}

inline bool addActorByService(VisualWidgetService* service, const QString& id,
                              const vtkSmartPointer<vtkActor>& actor, bool renderNow = true)
{
    return addPostPropByService(service, id, actor, QString(), renderNow);
}

inline bool addActorByService(VisualWidgetService* service, const QString& id,
                              const vtkSmartPointer<vtkActor>& actor,
                              const QString& displayName, bool renderNow = true)
{
    return addPostPropByService(service, id, actor, displayName, renderNow);
}

inline bool removeActorByService(VisualWidgetService* service, const QString& id,
                                 bool renderNow = true)
{
    if (!service || id.isEmpty())
        return false;
    const bool ok = service->removeProp(id, false);
    if (renderNow)
        service->render();
    return ok;
}

inline void showPostProcessingOnly(VisualWidgetService* service, bool renderNow = true)
{
    setPostViewActive(true);
    if (!service)
        return;
    service->setGroupVisible("post", postSceneStyle().visible, false);
    service->setGroupVisible("Geometry", false, false);
    service->setGroupVisible("网格", false, false);
    // 网格存在多条历史显示路径，进入后处理时显式压住常用 Actor，
    // 不依赖 ProjectTree 后续再做一次场景切换，避免瞬间叠加/闪烁。
    service->setVisible(QStringLiteral("mesh.global.surf"), false, false);
    service->setVisible(QStringLiteral("mesh.global.wire"), false, false);
    service->setVisible(QStringLiteral("mesh.imported.surface"), false, false);
    service->setVisible(QStringLiteral("mesh.imported.line"), false, false);
    if (auto* renderer = service->renderer())
        renderer->UseFXAAOn();
    if (renderNow)
        service->render();
}
