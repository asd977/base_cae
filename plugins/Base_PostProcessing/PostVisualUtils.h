#pragma once

#include <services/VisualWidgetService.h>
#include "PostMeshEdges.h"
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
#include <vtkDoubleArray.h>
#include <vtkExtractCells.h>
#include <vtkFeatureEdges.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkStaticCleanUnstructuredGrid.h>
#include <vtkLookupTable.h>
#include <vtkLight.h>
#include <vtkLightCollection.h>
#include <vtkLightKit.h>
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
#include <QComboBox>
#include <QDialog>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include "CompactDoubleSpinBox.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
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
    bool reverseColors{false};
    QString customTitle;
    int titleFont{14};
    int labelFont{11};
    int precision{4};
    int tickCount{7};
    // 0=VTK auto; 1=exact equal-interval count; 2=specified nice step.
    int tickMode{0};
    double tickStep{1.0};
    int widthPercent{8};
    int heightPercent{62};
    bool hasCustomPosition{false};
    double positionX{0.0};
    double positionY{0.0};
};

struct PostContourStyle
{
    bool enabled{false};
    // 黑白等值线模式下隐藏云图着色，只保留浅色表面、黑色等值线和数值。
    bool monochrome{false};
    int levels{10};
    bool labels{false};
    int labelFont{10};
};

struct PostFieldDisplayStyle
{
    // 仅用于有明确离散语义的结果：0=正常显示，2=隐藏零值区域。
    int zeroDisplayMode{0};
    bool thresholdEnabled{false};
    double threshold{0.0};
    int belowTransparency{80};
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
    static constexpr bool showSurfaceOutline=false; // Temporarily unavailable, including saved projects.
    bool showContextOutline{true};
    bool showContextLines{false};
    int surfaceInterpolation{1}; // 0=Flat, 1=Gouraud (ParaView default).
    bool interpolateScalarsBeforeMapping{true};
    bool computePointNormals{false}; // ParaView Surface Normals defaults.
    bool splitSharpEdges{true};
    double normalsFeatureAngle{30.0};
    double meshEdgeWidth{1.0};
    double meshEdgeOpacity{1.0};
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
    bool supportsThreshold{false};
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

inline QString postNormalizedFieldKey(const QString& fieldName)
{
    QString key = fieldName.trimmed().toUpper();
    key.remove(QRegularExpression(QStringLiteral("(?:\\([^()]*\\)|\\[[^\\[\\]]*\\])\\s*$")));
    key.remove(QRegularExpression(QStringLiteral("\\s+")));
    return key;
}

inline bool postIsFailureTypeField(const QString& fieldName)
{
    const QString key = postNormalizedFieldKey(fieldName);
    return key == QStringLiteral("FAILURE_ZONE") ||
           key == QStringLiteral("FAILURE_TYPE");
}

inline bool postIsPlasticZoneStressField(const QString& fieldName)
{
    const QString key = postNormalizedFieldKey(fieldName);
    return key == QStringLiteral("PLASTIC_ZONE_PRS1") ||
           key == QStringLiteral("MAX_PLASTIC_STRESS");
}

inline QHash<QString, PostFieldDisplayStyle>& postFieldDisplayStyles()
{
    static QHash<QString, PostFieldDisplayStyle> styles;
    return styles;
}

inline PostFieldDisplayStyle& postFieldDisplayStyle(const QString& fieldName)
{
    const QString key = fieldName.trimmed();
    auto& styles = postFieldDisplayStyles();
    if (!styles.contains(key))
    {
        PostFieldDisplayStyle style;
        // 结果范围只控制颜色映射，不自动裁剪几何。
        // 破坏类型也完整显示 0/1/2 三类区域。
        style.zeroDisplayMode = 0;
        styles.insert(key, style);
    }
    return styles[key];
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
           context.valid && context.supportsScalarBar &&
           !(postContourStyle().enabled && postContourStyle().monochrome);
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
    double previousFactor{0.0};
    double previousUnits{0.0};
    double previousLineFactor{0.0};
    double previousLineUnits{0.0};
    double previousPointUnits{0.0};
};

inline PostCoincidentTopologyState& postCoincidentTopologyState()
{
    static PostCoincidentTopologyState state;
    return state;
}

// ParaView 将“面与线/点共面”的处理作为 RenderView 级策略，而不是让每个
// Contour/Edge Actor 自己写 shader 或反复调局部 depth 参数。
// 使用 ParaView 6.0 RenderViewSettings 默认的全局原生偏移；退出后处理时
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
        vtkMapper::GetResolveCoincidentTopologyPolygonOffsetParameters(
            state.previousFactor, state.previousUnits);
        vtkMapper::GetResolveCoincidentTopologyLineOffsetParameters(state.previousLineFactor,state.previousLineUnits);
        vtkMapper::GetResolveCoincidentTopologyPointOffsetParameter(state.previousPointUnits);

        vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
        vtkMapper::SetResolveCoincidentTopologyPolygonOffsetFaces(1);
        vtkMapper::SetResolveCoincidentTopologyPolygonOffsetParameters(0.0, 0.0);
        vtkMapper::SetResolveCoincidentTopologyLineOffsetParameters(0.0, -4.0);
        vtkMapper::SetResolveCoincidentTopologyPointOffsetParameter(-8.0);
        state.applied = true;
        return;
    }

    if (!state.applied)
        return;

    vtkMapper::SetResolveCoincidentTopology(state.previousMode);
    vtkMapper::SetResolveCoincidentTopologyPolygonOffsetFaces(
        state.previousOffsetFaces);
    vtkMapper::SetResolveCoincidentTopologyPolygonOffsetParameters(
        state.previousFactor, state.previousUnits);
    vtkMapper::SetResolveCoincidentTopologyLineOffsetParameters(state.previousLineFactor,state.previousLineUnits);
    vtkMapper::SetResolveCoincidentTopologyPointOffsetParameter(state.previousPointUnits);
    state.applied = false;
}

struct PostLightingState
{
    vtkWeakPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkLightKit> kit;
    QVector<vtkSmartPointer<vtkLight>> previousLights;
    bool previousAutomaticLightCreation{true};
};

inline PostLightingState& postLightingState()
{
    static PostLightingState state;
    return state;
}

inline void setPostLightingActive(bool active)
{
    auto& state=postLightingState();
    auto* service=ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer=service?service->renderer():nullptr;
    if(active && state.kit && state.renderer==renderer)return;
    if(state.kit) {
        if(state.renderer) {
            state.kit->RemoveLightsFromRenderer(state.renderer);
            for(const auto& light:state.previousLights)state.renderer->AddLight(light);
            state.renderer->SetAutomaticLightCreation(state.previousAutomaticLightCreation);
        }
        state.kit=nullptr;state.renderer=nullptr;state.previousLights.clear();
    }
    if(!active || !renderer)return;
    state.renderer=renderer;
    state.previousAutomaticLightCreation=renderer->GetAutomaticLightCreation();
    auto* lights=renderer->GetLights();lights->InitTraversal();
    while(auto* light=lights->GetNextItem())state.previousLights.append(light);
    renderer->RemoveAllLights();renderer->AutomaticLightCreationOff();
    state.kit=vtkSmartPointer<vtkLightKit>::New();
    // ParaView 6.0 views_remotingviews.xml: native key/fill/back/head lights.
    state.kit->SetKeyLightWarmth(.60);state.kit->SetKeyLightIntensity(.75);
    state.kit->SetKeyLightElevation(50);state.kit->SetKeyLightAzimuth(10);
    state.kit->SetFillLightWarmth(.40);state.kit->SetKeyToFillRatio(3);
    state.kit->SetFillLightElevation(-75);state.kit->SetFillLightAzimuth(-10);
    state.kit->SetBackLightWarmth(.50);state.kit->SetKeyToBackRatio(3.5);
    state.kit->SetBackLightElevation(0);state.kit->SetBackLightAzimuth(110);
    state.kit->SetHeadLightWarmth(.50);state.kit->SetKeyToHeadRatio(3);
    state.kit->SetMaintainLuminance(0);
    state.kit->AddLightsToRenderer(renderer);
}

inline void applyPostSurfaceLighting(vtkActor* actor)
{
    if(!actor)return;
    auto* property=actor->GetProperty();
    property->LightingOn();
    property->SetInterpolation(postSceneStyle().surfaceInterpolation==0?VTK_FLAT:VTK_GOURAUD);
    property->SetAmbient(0.0);property->SetDiffuse(1.0);
    property->SetSpecular(0.0);property->SetSpecularPower(100.0);
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
        // A module mask may already hide this prop. Do not turn that effective
        // hidden state into a persistent object choice when isolating results.
        service->setTemporaryVisible(id, false, false);
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
                service->setTemporaryVisible(it.key(), it.value(), false);
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
    setPostLightingActive(active);

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
                                      bool supportsScalarBar = true, bool supportsThreshold = false)
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
    context.supportsThreshold = supportsThreshold;

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
    obj[QStringLiteral("meshEdgeWidth")] = scene.meshEdgeWidth;
    obj[QStringLiteral("meshEdgeOpacity")] = scene.meshEdgeOpacity;
    obj[QStringLiteral("showSurfaceOutline")] = scene.showSurfaceOutline;
    obj[QStringLiteral("showContextOutline")] = scene.showContextOutline;
    obj[QStringLiteral("showContextLines")] = scene.showContextLines;
    obj[QStringLiteral("surfaceInterpolation")] = PostSceneStyle{}.surfaceInterpolation;
    obj[QStringLiteral("interpolateScalarsBeforeMapping")] = PostSceneStyle{}.interpolateScalarsBeforeMapping;
    obj[QStringLiteral("computePointNormals")] = PostSceneStyle{}.computePointNormals;
    obj[QStringLiteral("splitSharpEdges")] = PostSceneStyle{}.splitSharpEdges;
    obj[QStringLiteral("normalsFeatureAngle")] = PostSceneStyle{}.normalsFeatureAngle;
    obj[QStringLiteral("scalarBarVisible")] = bar.visible;
    obj[QStringLiteral("scalarBarReverseColors")] = bar.reverseColors;
    obj[QStringLiteral("scalarBarCustomTitle")] = bar.customTitle;
    obj[QStringLiteral("scalarBarTitleFont")] = bar.titleFont;
    obj[QStringLiteral("scalarBarLabelFont")] = bar.labelFont;
    obj[QStringLiteral("scalarBarPrecision")] = bar.precision;
    obj[QStringLiteral("scalarBarTickCount")] = bar.tickCount;
    obj[QStringLiteral("scalarBarTickMode")] = bar.tickMode;
    obj[QStringLiteral("scalarBarTickStep")] = bar.tickStep;
    obj[QStringLiteral("scalarBarWidth")] = bar.widthPercent;
    obj[QStringLiteral("scalarBarHeight")] = bar.heightPercent;
    obj[QStringLiteral("scalarBarHasPosition")] = bar.hasCustomPosition;
    obj[QStringLiteral("scalarBarPositionX")] = bar.positionX;
    obj[QStringLiteral("scalarBarPositionY")] = bar.positionY;
    obj[QStringLiteral("contourEnabled")] = contour.enabled;
    obj[QStringLiteral("contourMonochrome")] = contour.monochrome;
    obj[QStringLiteral("contourLevels")] = contour.levels;
    obj[QStringLiteral("contourLabels")] = contour.labels;
    obj[QStringLiteral("contourLabelFont")] = contour.labelFont;
    QJsonObject fieldDisplay;
    const auto& displayStyles = postFieldDisplayStyles();
    for (auto it = displayStyles.constBegin(); it != displayStyles.constEnd(); ++it)
    {
        QJsonObject display;
        display[QStringLiteral("zeroDisplayMode")] = it.value().zeroDisplayMode;
        display[QStringLiteral("thresholdEnabled")] = it.value().thresholdEnabled;
        display[QStringLiteral("threshold")] = it.value().threshold;
        display[QStringLiteral("belowTransparency")] = it.value().belowTransparency;
        fieldDisplay[it.key()] = display;
    }
    obj[QStringLiteral("fieldDisplay")] = fieldDisplay;

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
    scene.showContextOutline = obj.value(QStringLiteral("showContextOutline")).toBool(scene.showContextOutline);
    scene.showContextLines = obj.value(QStringLiteral("showContextLines")).toBool(false);
    scene.representationMode = qBound(0, obj.value(QStringLiteral("representationMode")).toInt(scene.representationMode), 2);
    // These are application defaults, not project/user settings. Ignore older
    // projects that persisted the advanced controls while they were available.
    const PostSceneStyle defaults;
    scene.surfaceInterpolation=defaults.surfaceInterpolation;
    scene.interpolateScalarsBeforeMapping=defaults.interpolateScalarsBeforeMapping;
    scene.computePointNormals=defaults.computePointNormals;
    scene.splitSharpEdges=defaults.splitSharpEdges;
    scene.normalsFeatureAngle=defaults.normalsFeatureAngle;
    const double edgeWidth=obj.value(QStringLiteral("meshEdgeWidth")).toDouble(1.0);
    const double edgeOpacity=obj.value(QStringLiteral("meshEdgeOpacity")).toDouble(1.0);
    scene.meshEdgeWidth=std::isfinite(edgeWidth)?qBound(1.0,edgeWidth,5.0):1.0;
    scene.meshEdgeOpacity=std::isfinite(edgeOpacity)?qBound(0.0,edgeOpacity,1.0):1.0;
    bar.visible = obj.value(QStringLiteral("scalarBarVisible")).toBool(bar.visible);
    bar.reverseColors = obj.value(QStringLiteral("scalarBarReverseColors")).toBool(bar.reverseColors);
    bar.customTitle = obj.value(QStringLiteral("scalarBarCustomTitle")).toString(bar.customTitle);
    bar.titleFont = qBound(8, obj.value(QStringLiteral("scalarBarTitleFont")).toInt(bar.titleFont), 36);
    bar.labelFont = qBound(8, obj.value(QStringLiteral("scalarBarLabelFont")).toInt(bar.labelFont), 32);
    bar.precision = qBound(1, obj.value(QStringLiteral("scalarBarPrecision")).toInt(bar.precision), 10);
    bar.tickCount = qBound(2, obj.value(QStringLiteral("scalarBarTickCount")).toInt(bar.tickCount), 64);
    bar.tickMode = qBound(0, obj.value(QStringLiteral("scalarBarTickMode")).toInt(bar.tickMode), 2);
    const double savedTickStep = obj.value(QStringLiteral("scalarBarTickStep")).toDouble(bar.tickStep);
    bar.tickStep = std::isfinite(savedTickStep) && savedTickStep > 0.0 ? savedTickStep : 1.0;
    bar.widthPercent = qBound(5, obj.value(QStringLiteral("scalarBarWidth")).toInt(bar.widthPercent), 25);
    bar.heightPercent = qBound(25, obj.value(QStringLiteral("scalarBarHeight")).toInt(bar.heightPercent), 90);
    bar.hasCustomPosition = obj.value(QStringLiteral("scalarBarHasPosition")).toBool(bar.hasCustomPosition);
    bar.positionX = obj.value(QStringLiteral("scalarBarPositionX")).toDouble(bar.positionX);
    bar.positionY = obj.value(QStringLiteral("scalarBarPositionY")).toDouble(bar.positionY);
    contour.enabled = obj.value(QStringLiteral("contourEnabled")).toBool(contour.enabled);
    contour.monochrome = obj.value(QStringLiteral("contourMonochrome")).toBool(contour.monochrome);
    contour.levels = qBound(2, obj.value(QStringLiteral("contourLevels")).toInt(contour.levels), 30);
    contour.labels = obj.value(QStringLiteral("contourLabels")).toBool(contour.labels);
    contour.labelFont = qBound(8, obj.value(QStringLiteral("contourLabelFont")).toInt(contour.labelFont), 28);
    postFieldDisplayStyles().clear();
    const QJsonObject fieldDisplay = obj.value(QStringLiteral("fieldDisplay")).toObject();
    for (auto it = fieldDisplay.constBegin(); it != fieldDisplay.constEnd(); ++it)
    {
        PostFieldDisplayStyle style;
        // 不再提供“隐藏未破坏区域”或通用 0 值隐藏功能。
        // 兼容旧工程时也强制恢复完整几何显示。
        style.zeroDisplayMode = 0;
        const auto display = it.value().toObject();
        style.thresholdEnabled = display.value(QStringLiteral("thresholdEnabled")).toBool(false);
        style.threshold = display.value(QStringLiteral("threshold")).toDouble(0.0);
        if (!std::isfinite(style.threshold)) style.thresholdEnabled = false;
        style.belowTransparency = qBound(0, display.value(QStringLiteral("belowTransparency")).toInt(80), 100);
        postFieldDisplayStyles().insert(it.key(), style);
    }

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

    struct PublishedFilterSourceCache
    {
        vtkWeakPointer<vtkUnstructuredGrid> grid;
        vtkMTimeType mtime{0};
        QString project;
        bool custom{false};
    };
    static PublishedFilterSourceCache lastPublished;
    if (lastPublished.grid.GetPointer() == source &&
        lastPublished.mtime == source->GetMTime() &&
        lastPublished.project == project &&
        lastPublished.custom == custom)
    {
        return;
    }

    const QVariantMap matypeCounts = postCellValueCounts(matype);
    const QVariantMap layerCounts = postCellValueCounts(layer);
    const QSet<int> allMatypes = postIntSetFromCountMap(matypeCounts);
    const QSet<int> allLayers = postIntSetFromCountMap(layerCounts);

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

    lastPublished.grid = source;
    lastPublished.mtime = source->GetMTime();
    lastPublished.project = project;
    lastPublished.custom = custom;

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
    PostMeshFilterReport& report, bool readOnly = false)
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
            return readOnly ? entry.output : postShallowGrid(entry.output);
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

inline vtkSmartPointer<vtkUnstructuredGrid> postFilteredResultGrid(
    vtkUnstructuredGrid* source, bool readOnly = false)
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

    auto passthrough = [source, readOnly]() -> vtkSmartPointer<vtkUnstructuredGrid>
    {
        return readOnly ? vtkSmartPointer<vtkUnstructuredGrid>(source)
                        : postShallowGrid(source);
    };

    if (!report.requested || !postMeshFilterIsCustom())
    {
        postMeshFilterReport() = report;
        return passthrough();
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
        return passthrough();
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
        return passthrough();
    }

    const QString filterSignature = postMeshFilterSignature();
    PostMeshFilterReport cachedReport;
    if (auto cached =
            postCachedFilteredResult(source, filterSignature, cachedReport, readOnly))
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

inline vtkSmartPointer<vtkUnstructuredGrid> postApplyZeroHideFilter(
    vtkUnstructuredGrid* source, const QString& fieldName, bool readOnly = false)
{
    auto passthrough = [source, readOnly]() -> vtkSmartPointer<vtkUnstructuredGrid>
    {
        return readOnly ? vtkSmartPointer<vtkUnstructuredGrid>(source)
                        : postShallowGrid(source);
    };
    if (!source || fieldName.isEmpty() ||
        postFieldDisplayStyle(fieldName).zeroDisplayMode != 2)
        return passthrough();

    const QByteArray encoded = fieldName.toUtf8();
    vtkDataArray* cellArray = source->GetCellData()
                                  ? source->GetCellData()->GetArray(encoded.constData())
                                  : nullptr;
    vtkDataArray* pointArray = source->GetPointData()
                                   ? source->GetPointData()->GetArray(encoded.constData())
                                   : nullptr;
    if ((!cellArray || cellArray->GetNumberOfTuples() < source->GetNumberOfCells()) &&
        (!pointArray || pointArray->GetNumberOfTuples() < source->GetNumberOfPoints()))
        return passthrough();

    double range[2]{0.0, 0.0};
    if (cellArray)
        cellArray->GetRange(range);
    else
        pointArray->GetRange(range);
    const double tolerance = std::max({std::abs(range[0]), std::abs(range[1]), 1.0}) * 1.0e-12;

    auto ids = vtkSmartPointer<vtkIdList>::New();
    ids->Allocate(source->GetNumberOfCells());
    for (vtkIdType cellId = 0; cellId < source->GetNumberOfCells(); ++cellId)
    {
        bool keep = false;
        if (cellArray && cellId < cellArray->GetNumberOfTuples())
        {
            keep = std::abs(cellArray->GetTuple1(cellId)) > tolerance;
        }
        else if (pointArray)
        {
            vtkCell* cell = source->GetCell(cellId);
            vtkIdList* pointIds = cell ? cell->GetPointIds() : nullptr;
            if (pointIds)
            {
                for (vtkIdType i = 0; i < pointIds->GetNumberOfIds(); ++i)
                {
                    const vtkIdType pointId = pointIds->GetId(i);
                    if (pointId >= 0 && pointId < pointArray->GetNumberOfTuples() &&
                        std::abs(pointArray->GetTuple1(pointId)) > tolerance)
                    {
                        keep = true;
                        break;
                    }
                }
            }
        }
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
        const double tableT = double(i) / double(tableSize - 1);
        const double t = postScalarBarStyle().reverseColors ? 1.0 - tableT : tableT;
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

inline void applyPostZeroDisplayToLookupTable(vtkLookupTable* lut,
                                              const QString& fieldName,
                                              double minV, double maxV)
{
    if (!lut || fieldName.isEmpty())
        return;
    const int zeroMode = postFieldDisplayStyle(fieldName).zeroDisplayMode;
    if (zeroMode <= 0 || minV > 0.0 || maxV < 0.0)
        return;

    const vtkIdType count = lut->GetNumberOfTableValues();
    if (count <= 0)
        return;
    if (!(maxV > minV))
    {
        if (std::abs(minV) <= 1.0e-14 && std::abs(maxV) <= 1.0e-14)
        {
            for (vtkIdType i = 0; i < count; ++i)
            {
                double rgba[4]{0.0, 0.0, 0.0, 1.0};
                lut->GetTableValue(i, rgba);
                lut->SetTableValue(i, rgba[0], rgba[1], rgba[2], 0.0);
            }
            lut->Build();
        }
        return;
    }
    const double t = std::clamp((0.0 - minV) / (maxV - minV), 0.0, 1.0);
    const vtkIdType index = static_cast<vtkIdType>(std::llround(t * double(count - 1)));
    double rgba[4]{0.0, 0.0, 0.0, 1.0};
    lut->GetTableValue(index, rgba);
    lut->SetTableValue(index, rgba[0], rgba[1], rgba[2], 0.0);
    lut->Build();
}

inline void applyPostFieldLookupTable(vtkLookupTable* lut, const QString& fieldName,
                                      double minV, double maxV)
{
    if (!lut)
        return;

    if (postIsFailureTypeField(fieldName))
    {
        // 破坏类型属于离散结果，只允许三种颜色：0未破坏、1剪切破坏、2拉伸破坏。
        lut->SetNumberOfTableValues(3);
        lut->SetTableRange(-0.5, 2.5);
        lut->SetTableValue(0, 0.78, 0.78, 0.78, 1.0);
        lut->SetTableValue(1, 0.96, 0.58, 0.10, 1.0);
        lut->SetTableValue(2, 0.86, 0.12, 0.12, 1.0);
        lut->SetNanColor(0.55, 0.55, 0.55, 1.0);
        if (postFieldDisplayStyle(fieldName).zeroDisplayMode == 2)
            lut->SetTableValue(0, 0.78, 0.78, 0.78, 0.0);
        lut->Build();
        return;
    }

    applyStandardPostLookupTable(lut, minV, maxV);
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
           !lower.contains(QStringLiteral("probe")) &&
           !lower.contains(QStringLiteral("mesh-edges")) &&
           !lower.contains(QStringLiteral("context-model")) &&
           !lower.contains(QStringLiteral("outline"));
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
        property->SetEdgeVisibility(postSceneStyle().meshEdgeOpacity>=1.0);
        property->SetEdgeColor(0.0,0.0,0.0);
        property->SetLineWidth(postSceneStyle().meshEdgeWidth);
        break;
    case 2:
        property->SetRepresentationToWireframe();
        property->EdgeVisibilityOff();
        property->SetLineWidth(postSceneStyle().meshEdgeWidth);
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
    PostMeshEdges::prune(service);
    for (const QString& id : service->allIds())
    {
        if (!isPostRegistryPropId(id) || !postActorSupportsRepresentationControl(id))
            continue;
        if (auto* actor = vtkActor::SafeDownCast(service->getProp(id))) {
            applyPostActorRepresentation(actor);
            const auto& style=postSceneStyle();
            PostMeshEdges::update(service,id,actor,style.representationMode==1 && style.meshEdgeOpacity>0 && style.meshEdgeOpacity<1,
                                  style.meshEdgeOpacity,style.meshEdgeWidth);
        }
    }
    if (renderNow)
        service->render();
}

// A readable initial step for "fixed step" when a new field is many orders
// of magnitude smaller than the previous one (e.g. velocity in mm/s).
inline double suggestedPostScalarTickStep(double lo, double hi, int desiredTicks)
{
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi <= lo)
        return 1.0;
    const double raw = (hi - lo) / double(std::max(1, desiredTicks - 1));
    if (!(raw > 0.0) || !std::isfinite(raw))
        return 1.0;
    const double decade = std::pow(10.0, std::floor(std::log10(raw)));
    const double ratio = raw / decade;
    const double unit = ratio <= 1.0 ? 1.0 : (ratio <= 2.0 ? 2.0 : (ratio <= 5.0 ? 5.0 : 10.0));
    return unit * decade;
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
    const bool failureType = postDisplayFieldContext().valid &&
                             postIsFailureTypeField(postDisplayFieldContext().fieldName);
    scalarBar->SetNumberOfLabels(failureType ? 3 : style.tickCount);
    // Failure types are discrete categories 0/1/2 and always keep exact labels.
    // For continuous fields, vtkScalarBarActor supports explicit tick positions.
    if (failureType)
    {
        auto labels = vtkSmartPointer<vtkDoubleArray>::New();
        for (int i = 0; i <= 2; ++i)
            labels->InsertNextValue(static_cast<double>(i));
        scalarBar->SetCustomLabels(labels);
        scalarBar->UseCustomLabelsOn();
    }
    else if (style.tickMode != 0 && scalarBar->GetLookupTable())
    {
        const double* range = scalarBar->GetLookupTable()->GetRange();
        const double lo = range[0];
        const double hi = range[1];
        if (std::isfinite(lo) && std::isfinite(hi) && hi > lo)
        {
            auto labels = vtkSmartPointer<vtkDoubleArray>::New();
            if (style.tickMode == 1)
            {
                const int count = qBound(2, style.tickCount, 64);
                for (int i = 0; i < count; ++i)
                    labels->InsertNextValue(i == count - 1 ? hi :
                                            lo + (hi - lo) * double(i) / double(count - 1));
            }
            else if (style.tickMode == 2 && style.tickStep > 0.0 &&
                     std::isfinite(style.tickStep))
            {
                // ScalarBar allows 64 labels. If there are more, use a whole
                // multiple of the user's step so the displayed labels stay regular.
                const double n = (hi - lo) / style.tickStep;
                const double stride = std::max(1.0, std::ceil(n / 62.0));
                const double effectiveStep = style.tickStep * stride;
                const double first = std::ceil((lo - effectiveStep * 1e-10) /
                                               effectiveStep) * effectiveStep;
                for (int i = 0; i < 64; ++i)
                {
                    const double v = first + effectiveStep * i;
                    if (!std::isfinite(v) || v > hi + effectiveStep * 1e-9)
                        break;
                    if (v >= lo - effectiveStep * 1e-9)
                        labels->InsertNextValue(std::abs(v) < effectiveStep * 1e-12 ? 0.0 : v);
                }
            }
            if (labels->GetNumberOfTuples() > 0)
            {
                scalarBar->SetCustomLabels(labels);
                scalarBar->UseCustomLabelsOn();
            }
        }
    }
    QByteArray labelFormat("%.0f");
    if (!failureType)
    {
        const int significantDigits = qBound(1, style.precision, 10);
        const double* limits = scalarBar->GetLookupTable() ? scalarBar->GetLookupTable()->GetRange() : nullptr;
        double labelStep = limits ? (limits[1]-limits[0]) / std::max(1, style.tickCount-1) : 1.0;
        if (scalarBar->GetUseCustomLabels() && scalarBar->GetCustomLabels())
        {
            auto* values = scalarBar->GetCustomLabels();
            for (vtkIdType i = 1; i < values->GetNumberOfTuples(); ++i)
            {
                const double gap = values->GetValue(i)-values->GetValue(i-1);
                if (gap > 0.0) labelStep = std::min(labelStep, gap);
            }
        }
        const int stepDecimals = std::isfinite(labelStep) && labelStep > 0.0
                                     ? std::max(0, int(std::ceil(-std::log10(labelStep)))) : 0;
        const double* range = scalarBar->GetLookupTable()
                                  ? scalarBar->GetLookupTable()->GetRange() : nullptr;
        const double magnitude = range ? std::max(std::abs(range[0]), std::abs(range[1])) : 0.0;
        // %g switches from fixed to e-notation at different powers for each tick.
        // Choose once per legend, so upper/lower/inner labels all look consistent.
        if (std::isfinite(magnitude) && magnitude > 0.0 &&
            (magnitude < 1.0e-3 || magnitude >= 1.0e5))
        {
            const int distinguishDigits = std::isfinite(labelStep) && labelStep > 0.0
                                              ? int(std::ceil(std::log10(magnitude / labelStep))) : 0;
            labelFormat = QStringLiteral("%.%1e").arg(qBound(0, std::max(significantDigits-1, distinguishDigits), 16)).toLatin1();
        }
        else if (std::isfinite(magnitude) && magnitude > 0.0)
        {
            const int integerDigits = std::max(1, static_cast<int>(std::floor(std::log10(magnitude))) + 1);
            const int decimalDigits = qBound(0, std::max(significantDigits - integerDigits, stepDecimals), 16);
            labelFormat = QStringLiteral("%.%1f").arg(decimalDigits).toLatin1();
        }
        else
        {
            labelFormat = QStringLiteral("%.%1f").arg(significantDigits).toLatin1();
        }
    }
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
    service->setScalarBarTitle(resolvedTitle, false);
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
        rangeMinSpin = new CompactDoubleSpinBox(rangeGroup);
        rangeMaxSpin = new CompactDoubleSpinBox(rangeGroup);
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

    auto* reverseColors = new QCheckBox(QStringLiteral("反转颜色"), scalarBarGroup);
    reverseColors->setChecked(bar.reverseColors);
    scalarBarForm->addRow(reverseColors);

    QSpinBox* titleFont = makeSpin(8, 36, bar.titleFont);
    QSpinBox* labelFont = makeSpin(8, 32, bar.labelFont);
    QSpinBox* precision = makeSpin(1, 10, bar.precision);
    QSpinBox* ticks = makeSpin(2, 64, bar.tickCount);
    auto* tickMode = new QComboBox(scalarBarGroup);
    tickMode->addItem(QStringLiteral("自动刻度"), 0);
    tickMode->addItem(QStringLiteral("等分数量"), 1);
    tickMode->addItem(QStringLiteral("固定步长"), 2);
    tickMode->setCurrentIndex(bar.tickMode);
    auto* tickStep = new CompactDoubleSpinBox(scalarBarGroup);
    tickStep->setRange(1.0e-12, 1.0e100);
    tickStep->setDecimals(12);
    tickStep->setValue(bar.tickStep);
    tickStep->setKeyboardTracking(false);
    QObject::connect(tickMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     tickStep, [tickStep, ticks, service](int index)
    {
        tickStep->setEnabled(index == 2);
        if (index != 2 || !service || !service->scalarBarActor())
            return;
        auto* lut = service->scalarBarActor()->GetLookupTable();
        if (!lut)
            return;
        const double* limits = lut->GetRange();
        const double span = limits[1] - limits[0];
        if (std::isfinite(span) && span > 0.0 && tickStep->value() > span)
            tickStep->setValue(suggestedPostScalarTickStep(limits[0], limits[1], ticks->value()));
    });
    tickStep->setEnabled(bar.tickMode == 2);
    QSpinBox* width = makeSpin(5, 25, bar.widthPercent);
    QSpinBox* height = makeSpin(25, 90, bar.heightPercent);
    width->setSuffix(QStringLiteral(" %"));
    height->setSuffix(QStringLiteral(" %"));

    scalarBarForm->addRow(QStringLiteral("标题字号"), titleFont);
    scalarBarForm->addRow(QStringLiteral("刻度字号"), labelFont);
    scalarBarForm->addRow(QStringLiteral("数值位数"), precision);
    scalarBarForm->addRow(QStringLiteral("刻度模式"), tickMode);
    scalarBarForm->addRow(QStringLiteral("刻度数量"), ticks);
    scalarBarForm->addRow(QStringLiteral("刻度步长"), tickStep);
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
        auto* contourMode = new QComboBox(contourGroup);
        contourMode->addItem(QStringLiteral("叠加云图"), 0);
        contourMode->addItem(QStringLiteral("黑白等值线"), 1);
        contourMode->setCurrentIndex(contourMode->findData(contour.monochrome ? 1 : 0));
        contourMode->setObjectName(QStringLiteral("postContourModeCombo"));
        contourLevels = makeSpin(2, 30, contour.levels);
        contourForm->addRow(contourEnabled);
        contourForm->addRow(QStringLiteral("显示方式"), contourMode);
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

    bar.reverseColors = reverseColors->isChecked();
    bar.titleFont = titleFont->value();
    bar.labelFont = labelFont->value();
    bar.precision = precision->value();
    bar.tickCount = ticks->value();
    bar.tickMode = tickMode->currentData().toInt();
    bar.tickStep = tickStep->value();
    bar.widthPercent = width->value();
    bar.heightPercent = height->value();

    if (supportsContours)
    {
        PostContourStyle& contour = postContourStyle();
        contour.enabled = contourEnabled && contourEnabled->isChecked();
        if (auto* contourMode = dialog.findChild<QComboBox*>(QStringLiteral("postContourModeCombo")))
            contour.monochrome = contourMode->currentData().toInt() == 1;
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

inline vtkSmartPointer<vtkActor> createPostContextModelActor(vtkUnstructuredGrid* displayGrid)
{
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
        return nullptr;
    // Strip scalar arrays and extract outer surface with VTK 9 multithreaded vtkGeometryFilter
    // instead of feeding the full 3D unstructured grid into single-threaded vtkDataSetMapper.
    auto geometryOnly = vtkSmartPointer<vtkUnstructuredGrid>::New();
    geometryOnly->ShallowCopy(displayGrid);
    geometryOnly->GetPointData()->Initialize();
    geometryOnly->GetCellData()->Initialize();
    geometryOnly->GetFieldData()->Initialize();

    auto surface = vtkSmartPointer<vtkGeometryFilter>::New();
    surface->SetInputData(geometryOnly);
    surface->Update();

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    if (surface->GetOutput())
        poly->ShallowCopy(surface->GetOutput());
    if (poly->GetNumberOfCells() <= 0)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(poly);
    mapper->ScalarVisibilityOff();
    mapper->StaticOn();
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->SetRepresentationToSurface();
    actor->GetProperty()->EdgeVisibilityOff();
    actor->GetProperty()->SetColor(0.76, 0.80, 0.86);
    actor->GetProperty()->SetOpacity(0.14);
    actor->GetProperty()->LightingOff();
    return actor;
}

inline vtkSmartPointer<vtkActor> createPostContextOutlineActor(vtkUnstructuredGrid* displayGrid)
{
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0 || displayGrid->GetNumberOfPoints() <= 0)
        return nullptr;

    auto geometryOnly = vtkSmartPointer<vtkUnstructuredGrid>::New();
    geometryOnly->ShallowCopy(displayGrid);
    geometryOnly->GetPointData()->Initialize();
    geometryOnly->GetCellData()->Initialize();
    geometryOnly->GetFieldData()->Initialize();

    auto weld = vtkSmartPointer<vtkStaticCleanUnstructuredGrid>::New();
    weld->SetInputData(geometryOnly);
    weld->SetTolerance(0.0);
    weld->Update();

    auto surface = vtkSmartPointer<vtkGeometryFilter>::New();
    surface->SetInputConnection(weld->GetOutputPort());
    surface->Update();

    auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
    edges->SetInputConnection(surface->GetOutputPort());
    edges->BoundaryEdgesOn();
    edges->FeatureEdgesOn();
    edges->SetFeatureAngle(42.0);
    edges->ManifoldEdgesOff();
    edges->NonManifoldEdgesOff();
    edges->ColoringOff();
    edges->Update();

    auto lines = vtkSmartPointer<vtkPolyData>::New();
    if (edges->GetOutput())
        lines->ShallowCopy(edges->GetOutput());
    if (lines->GetNumberOfLines() <= 0 && lines->GetNumberOfCells() <= 0)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(lines);
    mapper->ScalarVisibilityOff();
    mapper->StaticOn();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetColor(0.20, 0.25, 0.32);
    actor->GetProperty()->SetOpacity(0.68);
    actor->GetProperty()->SetLineWidth(1.2);
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
    if (existing && existing != prop.GetPointer()) {
        PostMeshEdges::remove(service,id);
        service->removeProp(id, false);
    }

    const bool ok = service->addProp(id, prop, QStringLiteral("post"), displayName,
                                     false, false);
    const bool attached = ok || service->getProp(id) == prop.GetPointer();
    if (attached && postActorSupportsRepresentationControl(id))
    {
        if (auto* actor = vtkActor::SafeDownCast(service->getProp(id))) {
            applyPostActorRepresentation(actor);
            const auto& style=postSceneStyle();
            PostMeshEdges::update(service,id,actor,style.representationMode==1 && style.meshEdgeOpacity>0 && style.meshEdgeOpacity<1,
                                  style.meshEdgeOpacity,style.meshEdgeWidth);
        }
    }
    if (attached && !postSceneStyle().visible)
        service->setTemporaryVisible(id, false, false);
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
    if(existing && (!prop.GetPointer() || existing==prop.GetPointer()))PostMeshEdges::remove(service,id);
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
    PostMeshEdges::remove(service,id);
    const bool ok = service->removeProp(id, false);
    if (renderNow)
        service->render();
    return ok;
}

inline void showPostProcessingOnly(VisualWidgetService* service, bool renderNow = true)
{
    // 先记录进入前的显隐，再广播后处理状态；显示控制面板的事件处理器
    // 会临时压住网格，不能把事件处理后的隐藏状态当作原始状态保存。
    isolatePostProcessingScene(service);
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
