#pragma once

#include <services/VisualWidgetService.h>
#include <EventManager.h>
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <vtkActor.h>
#include <vtkAlgorithmOutput.h>
#include <vtkContourFilter.h>
#include <vtkDataObject.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkCellData.h>
#include <vtkCellArray.h>
#include <vtkDataArray.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkProp.h>
#include <vtkRenderer.h>
#include <vtkStripper.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkTextProperty.h>
#include <vtkWeakPointer.h>

#include <QByteArray>
#include <QCheckBox>
#include <QDialog>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonObject>
#include <QMap>
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
#include <limits>

struct PostScalarBarStyle
{
    bool visible{true};
    QString customTitle;
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
    // 0=表面，1=表面+单元边线，2=线框。
    int representationMode{0};
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

inline QString postResultFilterChangedEvent()
{
    return QStringLiteral("post.result.filter.changed");
}

inline QString postResultFilterSourceChangedEvent()
{
    return QStringLiteral("post.result.filter.source.changed");
}

inline QString postViewActiveKey()
{
    return QStringLiteral("post.display.view.active");
}

inline QString postViewActiveChangedEvent()
{
    return QStringLiteral("post.display.view.active.changed");
}

inline bool isPostViewActive()
{
    return PublicVariables::instance().get(postViewActiveKey()).toBool();
}

struct PostCoincidentTopologyState
{
    bool applied{false};
    int previousMode{0};
    int previousOffsetFaces{1};
};

inline PostCoincidentTopologyState& postCoincidentTopologyState()
{
    static PostCoincidentTopologyState state;
    return state;
}

// ParaView 将“面与线/点共面”的处理作为 RenderView 级策略，而不是让每个
// Contour/Edge Actor 自己写 shader 或反复调局部 depth 参数。
// 进入后处理时统一启用“Offset faces relative to lines and points”；退出后处理时
// 恢复进入前的 VTK 全局状态，避免影响几何、网格等其他工作台。
inline void setPostCoincidentTopologyActive(bool active)
{
    PostCoincidentTopologyState& state = postCoincidentTopologyState();

    if (active)
    {
        if (state.applied)
            return;

        state.previousMode = vtkMapper::GetResolveCoincidentTopology();
        state.previousOffsetFaces =
            vtkMapper::GetResolveCoincidentTopologyPolygonOffsetFaces();

        vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
        vtkMapper::SetResolveCoincidentTopologyPolygonOffsetFaces(1);
        state.applied = true;
        return;
    }

    if (!state.applied)
        return;

    vtkMapper::SetResolveCoincidentTopology(state.previousMode);
    vtkMapper::SetResolveCoincidentTopologyPolygonOffsetFaces(
        state.previousOffsetFaces);
    state.applied = false;
}

struct PostSceneVisibilitySnapshot
{
    bool captured{false};
    QHash<QString, bool> visibility;
};

inline PostSceneVisibilitySnapshot& postSceneVisibilitySnapshot()
{
    static PostSceneVisibilitySnapshot state;
    return state;
}

inline bool isPostRegistryPropId(const QString& id)
{
    return id.startsWith(QStringLiteral("post."), Qt::CaseInsensitive);
}

inline void isolatePostProcessingScene(VisualWidgetService* service)
{
    if (!service)
        return;

    PostSceneVisibilitySnapshot& state = postSceneVisibilitySnapshot();
    const QList<QString> ids = service->allIds();

    if (!state.captured)
    {
        state.visibility.clear();
        state.captured = true;
    }

    // 不依赖 Geometry/网格组名或若干固定 Actor ID。网格筛选、边界条件、
    // 选中高亮等模块后续新增的 Prop 也必须在后处理视图中被压住。
    // 后处理期间新注册的非 post Prop 也补记进入前/刚创建时的可见状态，
    // 这样离开后处理后仍能正确恢复。
    for (const QString& id : ids)
    {
        if (isPostRegistryPropId(id))
            continue;
        if (!state.visibility.contains(id))
        {
            if (vtkProp* prop = service->getProp(id))
                state.visibility.insert(id, prop->GetVisibility() != 0);
        }
        service->setVisible(id, false, false);
    }
}

inline void restoreSceneAfterPostProcessing()
{
    PostSceneVisibilitySnapshot& state = postSceneVisibilitySnapshot();
    if (!state.captured)
        return;

    auto* service = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (service)
    {
        for (auto it = state.visibility.cbegin(); it != state.visibility.cend(); ++it)
        {
            if (service->getProp(it.key()))
                service->setVisible(it.key(), it.value(), false);
        }
    }

    state.visibility.clear();
    state.captured = false;
}

inline void setPostViewActive(bool active)
{
    // 必须先切换渲染策略，再广播“后处理视图状态变化”，这样事件处理器如果立即
    // Render，也会使用正确的 RenderView 级共面策略。
    setPostCoincidentTopologyActive(active);

    const bool previous = PublicVariables::instance().get(postViewActiveKey()).toBool();
    PublicVariables::instance().set(postViewActiveKey(), active,
                                    QStringLiteral("post_processing_view"));
    if (previous != active)
    {
        QVariantMap data;
        data.insert(QStringLiteral("active"), active);
        EventManager::instance().sendEvent(postViewActiveChangedEvent(), data);
    }

    if (!active)
        restoreSceneAfterPostProcessing();
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
    obj[QStringLiteral("representationMode")] = scene.representationMode;
    obj[QStringLiteral("scalarBarVisible")] = bar.visible;
    obj[QStringLiteral("scalarBarCustomTitle")] = bar.customTitle;
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
    scene.representationMode = qBound(0, obj.value(QStringLiteral("representationMode")).toInt(scene.representationMode), 2);
    bar.visible = obj.value(QStringLiteral("scalarBarVisible")).toBool(bar.visible);
    bar.customTitle = obj.value(QStringLiteral("scalarBarCustomTitle")).toString(bar.customTitle);
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

inline QString postResultFilterKey(const QString& category)
{
    if (category == QStringLiteral("MATYPE"))
        return QStringLiteral("post.result.filter.matype.checked");
    if (category == QStringLiteral("LAYER"))
        return QStringLiteral("post.result.filter.layer.checked");
    return QString();
}

inline QSet<int> postMeshCheckedValues(const QString& category)
{
    const QString key = postResultFilterKey(category);
    return key.isEmpty() ? QSet<int>{}
                         : postIntSetFromString(PublicVariables::instance().get(key).toString());
}

inline bool postMeshFilterIsCustom()
{
    if (!PublicVariables::instance()
             .get(QStringLiteral("post.result.filter.custom"))
             .toBool())
        return false;

    const QString storedProject = PublicVariables::instance()
                                      .get(QStringLiteral("post.result.filter.project"))
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

inline QVariantMap postCellValueCounts(vtkDataArray* array)
{
    QMap<int, int> counts;
    if (array)
    {
        for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
        {
            const int value = static_cast<int>(std::llround(array->GetTuple1(i)));
            counts[value] = counts.value(value) + 1;
        }
    }

    QVariantMap values;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        values.insert(QString::number(it.key()), it.value());
    return values;
}

inline QSet<int> postIntSetFromCountMap(const QVariantMap& counts)
{
    QSet<int> values;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        bool ok = false;
        const int value = it.key().toInt(&ok);
        if (ok)
            values.insert(value);
    }
    return values;
}

inline QString postResultFilterIntSetText(const QSet<int>& values)
{
    QList<int> ordered = values.values();
    std::sort(ordered.begin(), ordered.end());
    QStringList parts;
    parts.reserve(ordered.size());
    for (int value : ordered)
        parts.push_back(QString::number(value));
    return parts.join(QLatin1Char(','));
}

inline void postPublishResultFilterSource(vtkUnstructuredGrid* source)
{
    if (!source || !source->GetCellData())
        return;

    vtkDataArray* matype = source->GetCellData()->GetArray("MATYPE");
    vtkDataArray* layer = source->GetCellData()->GetArray("LAYER");
    if (!matype && !layer)
        return;

    const QVariantMap matypeCounts = postCellValueCounts(matype);
    const QVariantMap layerCounts = postCellValueCounts(layer);
    const QSet<int> allMatypes = postIntSetFromCountMap(matypeCounts);
    const QSet<int> allLayers = postIntSetFromCountMap(layerCounts);

    const QString project = PublicVariables::instance()
                                .get(PublicConstants::kProjectDirectoryKey)
                                .toString()
                                .trimmed();
    const QString storedProject = PublicVariables::instance()
                                      .get(QStringLiteral("post.result.filter.project"))
                                      .toString()
                                      .trimmed();
    const bool sameProject = !project.isEmpty() && project == storedProject;
    const bool custom = sameProject &&
                        PublicVariables::instance()
                            .get(QStringLiteral("post.result.filter.custom"))
                            .toBool();

    QSet<int> checkedMatypes =
        postIntSetFromString(PublicVariables::instance()
                                 .get(QStringLiteral("post.result.filter.matype.checked"))
                                 .toString());
    QSet<int> checkedLayers =
        postIntSetFromString(PublicVariables::instance()
                                 .get(QStringLiteral("post.result.filter.layer.checked"))
                                 .toString());

    if (!custom)
    {
        checkedMatypes = allMatypes;
        checkedLayers = allLayers;
    }
    else
    {
        checkedMatypes.intersect(allMatypes);
        checkedLayers.intersect(allLayers);
    }

    const QString sourceSignature =
        QStringLiteral("%1|C=%2|M=%3|L=%4")
            .arg(project)
            .arg(source->GetNumberOfCells())
            .arg(postResultFilterIntSetText(allMatypes))
            .arg(postResultFilterIntSetText(allLayers));
    const QString previousSignature =
        PublicVariables::instance()
            .get(QStringLiteral("post.result.filter.source.signature"))
            .toString();

    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.matype.counts"), matypeCounts,
        QStringLiteral("project_dir"));
    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.layer.counts"), layerCounts,
        QStringLiteral("project_dir"));
    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.matype.checked"),
        postResultFilterIntSetText(checkedMatypes), QStringLiteral("project_dir"));
    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.layer.checked"),
        postResultFilterIntSetText(checkedLayers), QStringLiteral("project_dir"));
    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.custom"), custom,
        QStringLiteral("project_dir"));
    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.project"), project,
        QStringLiteral("project_dir"));
    PublicVariables::instance().set(
        QStringLiteral("post.result.filter.source.signature"), sourceSignature,
        QStringLiteral("project_dir"));

    if (previousSignature != sourceSignature)
    {
        QVariantMap event;
        event.insert(QStringLiteral("matypeCounts"), matypeCounts);
        event.insert(QStringLiteral("layerCounts"), layerCounts);
        EventManager::instance().sendEvent(postResultFilterSourceChangedEvent(), event);
    }
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
    // 后处理只跟随工程域筛选：材料 + 分层。
    // KETYPE/TOPOLOGY_TYPE 属于网格检查维度，不参与结果显示联动。
    const QString project = PublicVariables::instance()
                                .get(PublicConstants::kProjectDirectoryKey)
                                .toString()
                                .trimmed();
    return QStringLiteral("%1|M=%2|L=%3")
        .arg(project)
        .arg(postSortedIntSetText(postMeshCheckedValues(QStringLiteral("MATYPE"))))
        .arg(postSortedIntSetText(postMeshCheckedValues(QStringLiteral("LAYER"))));
}

struct PostFilteredResultCacheEntry
{
    vtkWeakPointer<vtkUnstructuredGrid> sourceGrid;
    vtkMTimeType sourceMTime{0};
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
    vtkUnstructuredGrid* source, const QString& signature,
    PostMeshFilterReport& report)
{
    auto& cache = postFilteredResultCache();
    for (int i = cache.size() - 1; i >= 0; --i)
    {
        if (!cache[i].sourceGrid.GetPointer())
            cache.removeAt(i);
    }

    const vtkMTimeType sourceMTime = source ? source->GetMTime() : 0;
    for (PostFilteredResultCacheEntry& entry : cache)
    {
        if (entry.sourceGrid.GetPointer() == source &&
            entry.sourceMTime == sourceMTime &&
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
                                    const QString& signature,
                                    vtkUnstructuredGrid* output,
                                    const PostMeshFilterReport& report)
{
    if (!source || !output || signature.isEmpty())
        return;

    PostFilteredResultCacheEntry entry;
    entry.sourceGrid = source;
    entry.sourceMTime = source->GetMTime();
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
    report.customFilter = false;
    report.inputCells = source ? source->GetNumberOfCells() : 0;
    report.outputCells = report.inputCells;

    if (!source)
    {
        postMeshFilterReport() = report;
        return {};
    }

    // 结果文件本身就是后处理筛选的权威数据源。
    // PostResultGenerator 在生成 VTU 时已经把 MATYPE/LAYER/elem_id 写入 CellData，
    // 退化单元拆分/规范化也会同步复制 CellData，因此不需要、也不应该再映射回 Mesh。
    postPublishResultFilterSource(source);

    if (!report.requested || !postMeshFilterIsCustom())
    {
        postMeshFilterReport() = report;
        return postShallowGrid(source);
    }

    vtkDataArray* matype =
        source->GetCellData() ? source->GetCellData()->GetArray("MATYPE") : nullptr;
    vtkDataArray* layer =
        source->GetCellData() ? source->GetCellData()->GetArray("LAYER") : nullptr;

    const bool hasMatype =
        matype && matype->GetNumberOfTuples() >= source->GetNumberOfCells();
    const bool hasLayer =
        layer && layer->GetNumberOfTuples() >= source->GetNumberOfCells();

    if (!hasMatype && !hasLayer)
    {
        report.applied = false;
        report.fullyResolved = false;
        report.message = QStringLiteral("当前结果文件没有材料/分层分组信息");
        postMeshFilterReport() = report;
        return postShallowGrid(source);
    }

    const QSet<int> allMatypes = hasMatype ? postUniqueCellValues(matype) : QSet<int>{};
    const QSet<int> allLayers = hasLayer ? postUniqueCellValues(layer) : QSet<int>{};
    const QSet<int> checkedMatypes =
        hasMatype ? postMeshCheckedValues(QStringLiteral("MATYPE")) : allMatypes;
    const QSet<int> checkedLayers =
        hasLayer ? postMeshCheckedValues(QStringLiteral("LAYER")) : allLayers;

    const bool matypeRestricted = hasMatype && checkedMatypes != allMatypes;
    const bool layerRestricted = hasLayer && checkedLayers != allLayers;
    report.customFilter = matypeRestricted || layerRestricted;

    if (!report.customFilter)
    {
        report.applied = true;
        postMeshFilterReport() = report;
        return postShallowGrid(source);
    }

    const QString filterSignature = postMeshFilterSignature();
    PostMeshFilterReport cachedReport;
    if (auto cached =
            postCachedFilteredResult(source, filterSignature, cachedReport))
    {
        postMeshFilterReport() = cachedReport;
        return cached;
    }

    auto ids = vtkSmartPointer<vtkIdList>::New();
    ids->Allocate(source->GetNumberOfCells());

    for (vtkIdType cellId = 0; cellId < source->GetNumberOfCells(); ++cellId)
    {
        if (matypeRestricted)
        {
            const int value =
                static_cast<int>(std::llround(matype->GetTuple1(cellId)));
            if (!checkedMatypes.contains(value))
                continue;
        }

        if (layerRestricted)
        {
            const int value =
                static_cast<int>(std::llround(layer->GetTuple1(cellId)));
            if (!checkedLayers.contains(value))
                continue;
        }

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
    report.fullyResolved = true;
    report.outputCells = result->GetNumberOfCells();
    if (report.outputCells <= 0)
        report.message = QStringLiteral("当前材料/分层筛选下没有可显示的结果");

    qInfo() << "[PostFilter] result MATYPE/LAYER"
            << "input=" << report.inputCells
            << "output=" << report.outputCells
            << "matype=" << postSortedIntSetText(checkedMatypes)
            << "layer=" << postSortedIntSetText(checkedLayers);

    postStoreFilteredResult(source, filterSignature, result, report);
    postMeshFilterReport() = report;
    return result;
}

inline QByteArray postFaceKey(const QVector<vtkIdType>& pointIds)
{
    QVector<vtkIdType> sorted = pointIds;
    std::sort(sorted.begin(), sorted.end());

    QByteArray key;
    key.reserve(int(sizeof(vtkIdType)) * (sorted.size() + 1));
    const vtkIdType n = static_cast<vtkIdType>(sorted.size());
    key.append(reinterpret_cast<const char*>(&n), int(sizeof(vtkIdType)));
    for (vtkIdType id : sorted)
        key.append(reinterpret_cast<const char*>(&id), int(sizeof(vtkIdType)));
    return key;
}

// 后处理正常云图的“显示面”不是 generic GeometryFilter 的整个模型外壳。
// 它需要保留：
//   1) 全局外表面；
//   2) MATYPE/LAYER 之间的内部域界面；
// 并且一个公共界面只保留一份。
// 这样不透明时内部域由 Z-buffer 正常遮挡；降低透明度后，同一 Actor 中已经存在的
// 内部域界面会参与透明混合，不需要临时删除/重建域。
//
// 显示面完全由当前结果网格自身的 MATYPE/LAYER 与单元邻接关系生成，
// 不读取 Mesh 当前筛选网格或 Mesh Actor。这样结果筛选、透明显示和结果文件保持同源。
inline vtkSmartPointer<vtkPolyData> postBuildDomainAwareDisplaySurface(
    vtkUnstructuredGrid* resultGrid, VisualWidgetService* service)
{
    Q_UNUSED(service);

    auto out = vtkSmartPointer<vtkPolyData>::New();
    if (!resultGrid || resultGrid->GetNumberOfPoints() <= 0 ||
        resultGrid->GetNumberOfCells() <= 0)
        return out;

    vtkCellData* sourceCellData = resultGrid->GetCellData();
    vtkDataArray* matype =
        sourceCellData ? sourceCellData->GetArray("MATYPE") : nullptr;
    vtkDataArray* layer =
        sourceCellData ? sourceCellData->GetArray("LAYER") : nullptr;

    // 没有域信息的旧结果，退回普通外表面即可。
    if ((!matype || matype->GetNumberOfTuples() < resultGrid->GetNumberOfCells()) &&
        (!layer || layer->GetNumberOfTuples() < resultGrid->GetNumberOfCells()))
    {
        auto surface = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
        surface->SetInputData(resultGrid);
        surface->PassThroughPointIdsOn();
        surface->PassThroughCellIdsOn();
        surface->Update();
        if (surface->GetOutput())
            out->ShallowCopy(surface->GetOutput());
        return out;
    }

    struct FaceRecord
    {
        vtkIdType ownerCellId{-1};
        QVector<vtkIdType> pointIds;
        int cellType{VTK_EMPTY_CELL};
        int matype{0};
        int layer{0};
        int count{0};
        bool domainInterface{false};
    };

    QHash<QByteArray, FaceRecord> faces;
    faces.reserve(static_cast<int>(
        qMin<vtkIdType>(resultGrid->GetNumberOfCells() * 4,
                        std::numeric_limits<int>::max() / 2)));

    auto cellMatype = [&](vtkIdType cid) -> int
    {
        return matype && cid < matype->GetNumberOfTuples()
                   ? static_cast<int>(std::llround(matype->GetTuple1(cid)))
                   : 0;
    };
    auto cellLayer = [&](vtkIdType cid) -> int
    {
        return layer && cid < layer->GetNumberOfTuples()
                   ? static_cast<int>(std::llround(layer->GetTuple1(cid)))
                   : 0;
    };

    auto appendFace = [&](vtkIdType ownerCellId, vtkCell* face, char prefix)
    {
        if (!face || !face->GetPointIds() ||
            face->GetPointIds()->GetNumberOfIds() < 3)
            return;

        QVector<vtkIdType> pointIds;
        const vtkIdType originalPointCount = face->GetPointIds()->GetNumberOfIds();
        pointIds.reserve(static_cast<int>(originalPointCount));
        QSet<vtkIdType> seenPointIds;
        for (vtkIdType i = 0; i < face->GetPointIds()->GetNumberOfIds(); ++i)
        {
            const vtkIdType pid = face->GetPointIds()->GetId(i);
            // 输入拓扑异常时整张面作废。不能简单忽略越界点后继续插入，
            // 否则会把残余点误组装成另一种单元并破坏 PolyData 拓扑。
            if (pid < 0 || pid >= resultGrid->GetNumberOfPoints())
                return;
            // 退化实体的面可能仍携带重复节点。显示面先做一次轻量去重，
            // 避免生成零面积三角形/四边形，在透明模式下表现成碎线和尖片。
            if (seenPointIds.contains(pid))
                continue;
            seenPointIds.insert(pid);
            pointIds.push_back(pid);
        }
        if (pointIds.size() < 3)
            return;

        // 过滤几何上已经塌缩成线/点的面。使用 Newell 法，不做三角化。
        double nx = 0.0;
        double ny = 0.0;
        double nz = 0.0;
        for (int i = 0; i < pointIds.size(); ++i)
        {
            double a[3] = {0.0, 0.0, 0.0};
            double b[3] = {0.0, 0.0, 0.0};
            resultGrid->GetPoint(pointIds[i], a);
            resultGrid->GetPoint(pointIds[(i + 1) % pointIds.size()], b);
            nx += (a[1] - b[1]) * (a[2] + b[2]);
            ny += (a[2] - b[2]) * (a[0] + b[0]);
            nz += (a[0] - b[0]) * (a[1] + b[1]);
        }
        const double normalLengthSquared = nx * nx + ny * ny + nz * nz;
        if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-24)
            return;

        QByteArray key;
        key.reserve(1 + int(sizeof(vtkIdType)) * (pointIds.size() + 1));
        key.append(prefix);
        key.append(postFaceKey(pointIds));

        const int ownerMatype = cellMatype(ownerCellId);
        const int ownerLayer = cellLayer(ownerCellId);

        auto it = faces.find(key);
        if (it == faces.end())
        {
            FaceRecord record;
            record.ownerCellId = ownerCellId;
            record.pointIds = pointIds;
            // 保持正常面（含 Pixel/TriangleStrip）的原始 VTK 类型；只有去掉
            // 重复点、点数发生变化时才改成允许可变点数的 Polygon。
            record.cellType = static_cast<vtkIdType>(pointIds.size()) == originalPointCount
                                  ? face->GetCellType()
                                  : VTK_POLYGON;
            record.matype = ownerMatype;
            record.layer = ownerLayer;
            record.count = 1;
            faces.insert(key, record);
            return;
        }

        ++it->count;
        if (it->matype != ownerMatype || it->layer != ownerLayer)
            it->domainInterface = true;
    };

    for (vtkIdType cid = 0; cid < resultGrid->GetNumberOfCells(); ++cid)
    {
        vtkCell* cell = resultGrid->GetCell(cid);
        if (!cell)
            continue;

        const int dimension = cell->GetCellDimension();
        if (dimension == 3)
        {
            const int numberOfFaces = cell->GetNumberOfFaces();
            for (int f = 0; f < numberOfFaces; ++f)
                appendFace(cid, cell->GetFace(f), 'V');
        }
        else if (dimension == 2)
        {
            // 二维结果本身就是显示面，不做体单元邻接消隐。
            appendFace(cid, cell, 'S');
        }
    }

    out->SetPoints(resultGrid->GetPoints());
    if (resultGrid->GetPointData())
        out->GetPointData()->ShallowCopy(resultGrid->GetPointData());
    out->Allocate(faces.size());

    if (sourceCellData)
        out->GetCellData()->CopyAllocate(sourceCellData, faces.size());

    for (auto it = faces.constBegin(); it != faces.constEnd(); ++it)
    {
        const FaceRecord& record = it.value();

        // 一个 owner：全局外表面；
        // 两个以上 owner 但 MATYPE/LAYER 不同：内部域公共界面，只保留一份；
        // 同一域内部共享面：不显示。
        if (record.count > 1 && !record.domainInterface)
            continue;
        // 一个合法体网格公共面最多由两个体单元共享。多于两个 owner
        // 属于非流形/退化连接，透明显示时宁可跳过该异常面，也不要绘制成
        // 闪烁、颜色加深或碎片状的内部界面。
        if (record.count > 2)
            continue;
        if (record.ownerCellId < 0 || record.pointIds.size() < 3)
            continue;

        const vtkIdType newCellId =
            // 去重后的退化面已在记录阶段改成 Polygon，不能继续沿用原始
            // VTK_QUAD 等固定点数类型。
            out->InsertNextCell(record.cellType,
                                static_cast<vtkIdType>(record.pointIds.size()),
                                record.pointIds.constData());
        if (newCellId >= 0 && sourceCellData)
            out->GetCellData()->CopyData(
                sourceCellData, record.ownerCellId, newCellId);
    }

    out->Squeeze();
    return out;
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

inline QString resolvedPostScalarBarTitle(const QString& automaticTitle)
{
    const QString custom = postScalarBarStyle().customTitle.trimmed();
    return custom.isEmpty() ? automaticTitle : custom;
}

inline bool postActorSupportsRepresentationControl(const QString& id)
{
    const QString lower = id.toLower();
    // 线/文字/矢量符号本身不是结果实体，不跟随“表面/线框”切换。
    return !lower.contains(QStringLiteral("contour")) &&
           !lower.contains(QStringLiteral("label")) &&
           !lower.contains(QStringLiteral("vector")) &&
           !lower.contains(QStringLiteral("glyph")) &&
           !lower.contains(QStringLiteral("probe"));
}

inline void applyPostActorRepresentation(vtkActor* actor)
{
    if (!actor || !actor->GetProperty())
        return;

    vtkProperty* property = actor->GetProperty();
    switch (qBound(0, postSceneStyle().representationMode, 2))
    {
    case 1:
        property->SetRepresentationToSurface();
        property->EdgeVisibilityOn();
        property->SetEdgeColor(0.12, 0.12, 0.12);
        property->SetLineWidth(0.8);
        break;
    case 2:
        property->SetRepresentationToWireframe();
        property->EdgeVisibilityOff();
        property->SetLineWidth(1.0);
        break;
    default:
        property->SetRepresentationToSurface();
        property->EdgeVisibilityOff();
        break;
    }
}

inline void applyPostSceneRepresentation(VisualWidgetService* service, bool renderNow = true)
{
    if (!service)
        return;
    for (const QString& id : service->allIds())
    {
        if (!isPostRegistryPropId(id) || !postActorSupportsRepresentationControl(id))
            continue;
        if (auto* actor = vtkActor::SafeDownCast(service->getProp(id)))
            applyPostActorRepresentation(actor);
    }
    if (renderNow)
        service->render();
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
    const QString resolvedTitle = resolvedPostScalarBarTitle(title);
    scalarBar->SetTitle(resolvedTitle.toUtf8().constData());
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

    // 通用等值线只针对“当前可见表面/切面”生成二维等值线。
    // 不允许 vtkContourFilter 直接处理完整三维体单元，尤其是 VTK_POLYHEDRON：
    // 退化/兼容单元可能存在非流形内部三角化，直接 Contour 会产生
    // "A cell with a non-manifold triangulation ... cannot be contoured" 警告。
    // vtkDataSetSurfaceFilter 对已经是 vtkPolyData 的 Slice 输入开销很小，
    // 对 vtkUnstructuredGrid/Clip 输出则统一抽取外表面，因此所有页面共用一条安全管线。
    auto surface = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surface->SetInputConnection(inputPort);
    // 先真正执行上游，确认当前可见表面及节点标量有效。后续不能在一条尚未
    // 执行的管线上直接把 Mapper 设为 Static，否则 Mapper 不会再向上游请求更新。
    surface->Update();
    vtkPolyData* surfaceOutput = surface->GetOutput();
    vtkPointData* pointData = surfaceOutput ? surfaceOutput->GetPointData() : nullptr;
    vtkDataArray* scalarArray = pointData ? pointData->GetArray(name.constData()) : nullptr;
    if (!surfaceOutput || surfaceOutput->GetNumberOfPoints() <= 0 ||
        surfaceOutput->GetNumberOfCells() <= 0 || !scalarArray ||
        scalarArray->GetNumberOfComponents() != 1 ||
        scalarArray->GetNumberOfTuples() < surfaceOutput->GetNumberOfPoints())
        return nullptr;
    pointData->SetActiveScalars(name.constData());

    auto contour = vtkSmartPointer<vtkContourFilter>::New();
    contour->SetInputConnection(surface->GetOutputPort());
    contour->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    name.constData());
    contour->ComputeNormalsOff();
    contour->ComputeGradientsOff();
    contour->ComputeScalarsOn();
    contour->UseScalarTreeOn();
    const int levels = qMax(2, postContourStyle().levels);
    contour->SetNumberOfContours(levels);
    for (int i = 0; i < levels; ++i)
    {
        const double t = double(i + 1) / double(levels + 1);
        contour->SetValue(i, minV + (maxV - minV) * t);
    }

    auto stripper = vtkSmartPointer<vtkStripper>::New();
    stripper->SetInputConnection(contour->GetOutputPort());
    stripper->JoinContiguousSegmentsOn();
    stripper->Update();

    vtkPolyData* contourOutput = stripper->GetOutput();
    if (!contourOutput || contourOutput->GetNumberOfPoints() <= 0 ||
        contourOutput->GetNumberOfLines() <= 0)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(stripper->GetOutputPort());
    mapper->ScalarVisibilityOff();
    // 这里接的是算法端口而不是一份独立静态 PolyData。保持默认的动态管线，
    // 切块平面等上游发生变化后才能按 VTK 的 MTime 机制更新；未修改时不会重算。
    mapper->StaticOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetColor(0.10, 0.10, 0.10);
    actor->GetProperty()->SetOpacity(1.0);
    actor->GetProperty()->SetLineWidth(1.0);
    // 不在 Actor 层做 Tube、Shader 或局部 depth hack。
    // 共面关系由 setPostCoincidentTopologyActive() 在视图层统一处理。
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
    if (attached && postActorSupportsRepresentationControl(id))
    {
        if (auto* actor = vtkActor::SafeDownCast(service->getProp(id)))
            applyPostActorRepresentation(actor);
    }
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

    // 后处理是独占结果视图。以前只隐藏 Geometry + 4 个固定 mesh Actor，
    // 网格筛选/高亮/边界条件等其它 Prop 仍可能共面覆盖云图。现在按注册表
    // 快照并隐藏全部非 post Prop，退出后处理时再恢复进入前的可见状态。
    isolatePostProcessingScene(service);
    service->setGroupVisible(QStringLiteral("post"), postSceneStyle().visible, false);

    if (auto* renderer = service->renderer())
        renderer->UseFXAAOn();
    if (renderNow)
        service->render();
}
