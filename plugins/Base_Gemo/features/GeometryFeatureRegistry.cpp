#include "GeometryFeatureRegistry.h"

#include "FeatureData.h"
#include "FeatureBuilder.h"
#include "IGeometryService.h"
#include "GeometryBuildUtils.h"
#include "ImportGeometryBuilder.h"
#include "SketchDocument.h"
#include "SketchShapeBuilder.h"
#include <GeometryDisplayManager.h>

#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>

#include <BooleanPanel.h>
#include <ExtrudePanel.h>
#include <FeatureOperationPanels.h>
#include <ImportExportPanel.h>
#include <PatternPanel.h>
#include <PrimitivePanel.h>
#include <PublicConstants.h>
#include <SketchPanel.h>
#include <CavernTemplateDialog.h>
#include "../terrain/TerrainSurfacePanel.h"
#include "../terrain/TerrainSurfaceBuilder.h"
#include <EarthRockfillDamDialog.h>
#include <GravityDamDialog.h>
#include <SlopeTemplateDialog.h>

#include <QJsonArray>
#include <QVector>

#include <vector>
#include <cmath>

namespace
{
using GeometryFeatures::FeatureDescriptor;

QStringList jsonStringList(const QJsonObject& state, const QString& key)
{
    QStringList values;
    const QJsonArray arr = state.value(key).toArray();
    for (const auto& value : arr)
        values.append(value.toString());
    return values;
}

double coordinateValue(const QJsonObject& state, QChar axis, double defaultValue = 0.0)
{
    const QString canonicalKey(1, axis.toLower());
    if (state.contains(canonicalKey))
        return state.value(canonicalKey).toDouble(defaultValue);

    // 兼容旧工程中使用大写 X/Y/Z 保存的坐标字段。
    return state.value(QString(1, axis.toUpper())).toDouble(defaultValue);
}

int reversedDirection(int direction)
{
    switch (direction)
    {
    case 0: return 1;
    case 1: return 0;
    case 2: return 3;
    case 3: return 2;
    case 4: return 5;
    case 5: return 4;
    default: return direction;
    }
}

StepImportOptions stepImportOptionsFromState(const QJsonObject& state)
{
    StepImportOptions options;
    options.splitMode = state.contains(QStringLiteral("stepSplitMode"))
        ? static_cast<StepImportSplitMode>(
              state.value(QStringLiteral("stepSplitMode")).toInt(
                  static_cast<int>(StepImportSplitMode::Part)))
        : StepImportSplitMode::Body;
    options.preserveColors =
        state.value(QStringLiteral("stepPreserveColors")).toBool(true);
    options.includeHidden =
        state.value(QStringLiteral("stepIncludeHidden")).toBool(false);
    options.groupByAssembly =
        state.value(QStringLiteral("stepGroupByAssembly")).toBool(true);
    return options;
}

int cutExtrudeDirection(const QJsonObject& state)
{
    int direction = state.value(QStringLiteral("direction")).toInt(4);
    if (direction == -1)
    {
        int suggestedDirection = 4;
        FeatureBuilder::analyzeExtrudeProfile(state.value(QStringLiteral("profileUuid")).toString(),
                                              nullptr,
                                              &suggestedDirection,
                                              nullptr);
        direction = suggestedDirection;
    }
    if (state.value(QStringLiteral("reverse")).toBool(false))
        direction = reversedDirection(direction);
    return direction;
}

ExtrudeParameters extrudeParameters(const QJsonObject& state)
{
    ExtrudeParameters parameters;
    parameters.distance = state.contains(QStringLiteral("distance"))
        ? state.value(QStringLiteral("distance")).toDouble(10.0)
        : std::abs(state.value(QStringLiteral("height")).toDouble(10.0));
    parameters.secondDistance = state.value(QStringLiteral("secondDistance"))
        .toDouble(parameters.distance);

    const int schema = state.value(QStringLiteral("extrudeSchema")).toInt(0);
    if (schema >= 2)
    {
        parameters.extentMode = ExtrudeExtentMode(state.value(QStringLiteral("extentMode"))
            .toInt(int(ExtrudeExtentMode::OneSide)));
        parameters.directionMode = ExtrudeDirectionMode(state.value(QStringLiteral("directionMode"))
            .toInt(int(ExtrudeDirectionMode::ProfileNormal)));
        parameters.resultMode = ExtrudeResultMode::Auto;
        parameters.reverse = state.value(QStringLiteral("reverse")).toBool(false);
        parameters.customDirection = QVector3D(
            float(state.value(QStringLiteral("customX")).toDouble(0.0)),
            float(state.value(QStringLiteral("customY")).toDouble(0.0)),
            float(state.value(QStringLiteral("customZ")).toDouble(1.0)));
        return parameters;
    }

    parameters.extentMode = ExtrudeExtentMode::OneSide;
    parameters.resultMode = ExtrudeResultMode::Auto;
    parameters.reverse = state.value(QStringLiteral("reverse")).toBool(
        state.value(QStringLiteral("height")).toDouble(10.0) < 0.0);

    int legacyMode = -1;
    if (state.contains(QStringLiteral("directionMode")))
    {
        legacyMode = state.value(QStringLiteral("directionMode")).toInt(-1);
    }
    else if (state.contains(QStringLiteral("direction")))
    {
        // 更早数据以 height 与 direction 的符号共同表达最终方向。
        legacyMode = state.value(QStringLiteral("direction")).toInt(-1);
    }

    switch (legacyMode)
    {
    case 0: parameters.directionMode = ExtrudeDirectionMode::XAxis; break;
    case 1: parameters.directionMode = ExtrudeDirectionMode::XAxis; parameters.reverse = !parameters.reverse; break;
    case 2: parameters.directionMode = ExtrudeDirectionMode::YAxis; break;
    case 3: parameters.directionMode = ExtrudeDirectionMode::YAxis; parameters.reverse = !parameters.reverse; break;
    case 4: parameters.directionMode = ExtrudeDirectionMode::ZAxis; break;
    case 5: parameters.directionMode = ExtrudeDirectionMode::ZAxis; parameters.reverse = !parameters.reverse; break;
    case -1:
    default: parameters.directionMode = ExtrudeDirectionMode::ProfileNormal; break;
    }
    return parameters;
}

QVector<CadFaceRef> extrudeFaceRefs(const QJsonObject& state)
{
    QVector<CadFaceRef> refs;
    const QJsonArray values = state.value(QStringLiteral("faceSelections")).toArray();
    refs.reserve(values.size());
    for (const QJsonValue& value : values)
    {
        const QJsonObject item = value.toObject();
        CadFaceRef ref;
        ref.uuid = item.value(QStringLiteral("uuid")).toString();
        ref.faceIndex = item.value(QStringLiteral("face")).toInt(0);
        if (!ref.uuid.isEmpty() && ref.faceIndex > 0)
            refs.push_back(ref);
    }
    return refs;
}

RevolveParameters revolveParameters(const QJsonObject& state)
{
    RevolveParameters parameters;
    parameters.angleDeg = state.value(QStringLiteral("angle")).toDouble(360.0);
    const int sourceFaceIndex = state.value(QStringLiteral("sourceFaceIndex")).toInt(0);
    if (sourceFaceIndex > 0)
    {
        parameters.sourceFace.uuid = state.value(QStringLiteral("sourceUuid")).toString();
        parameters.sourceFace.faceIndex = sourceFaceIndex;
    }
    parameters.axisOrigin = QVector3D(
        float(state.value(QStringLiteral("ox")).toDouble(0.0)),
        float(state.value(QStringLiteral("oy")).toDouble(0.0)),
        float(state.value(QStringLiteral("oz")).toDouble(0.0)));

    if (state.value(QStringLiteral("revolveSchema")).toInt(0) >= 2 ||
        state.contains(QStringLiteral("axisDx")))
    {
        parameters.axisDirection = QVector3D(
            float(state.value(QStringLiteral("axisDx")).toDouble(1.0)),
            float(state.value(QStringLiteral("axisDy")).toDouble(0.0)),
            float(state.value(QStringLiteral("axisDz")).toDouble(0.0)));
    }
    else
    {
        switch (state.value(QStringLiteral("axis")).toInt(2))
        {
        case 0: parameters.axisDirection = QVector3D(1.0f, 0.0f, 0.0f); break;
        case 1: parameters.axisDirection = QVector3D(0.0f, 1.0f, 0.0f); break;
        case 2:
        default: parameters.axisDirection = QVector3D(0.0f, 0.0f, 1.0f); break;
        }
    }
    return parameters;
}

void setMessage(QString* message, const QString& text)
{
    if (message)
        *message = text;
}

bool typeMatches(const FeatureDescriptor& descriptor, const QString& type)
{
    return !type.isEmpty() &&
           (descriptor.type.compare(type, Qt::CaseInsensitive) == 0 ||
            descriptor.typeAliases.contains(type, Qt::CaseInsensitive));
}

bool kindMatches(const FeatureDescriptor& descriptor, const QString& kind)
{
    return !kind.isEmpty() &&
           (descriptor.kind.compare(kind, Qt::CaseInsensitive) == 0 ||
            descriptor.kindAliases.contains(kind, Qt::CaseInsensitive));
}

bool rebuildUnavailable(const FeatureData&, IGeometryService&, QString* message)
{
    setMessage(message, QStringLiteral("该几何类型没有可参数化重建入口。"));
    return false;
}

bool rebuildSketch2D(const FeatureData& feature, IGeometryService&, QString* message)
{
    using namespace GeometrySketch;

    try
    {
        const QJsonObject& state = feature.state;

        PlaneSettings settings;
        switch (state.value(QStringLiteral("plane")).toInt(static_cast<int>(Plane::XY)))
        {
        case static_cast<int>(Plane::XZ):
            settings.plane = Plane::XZ;
            break;
        case static_cast<int>(Plane::YZ):
            settings.plane = Plane::YZ;
            break;
        case static_cast<int>(Plane::XY):
        default:
            settings.plane = Plane::XY;
            break;
        }

        settings.offset = state.value(QStringLiteral("offset")).toDouble(0.0);
        settings.gridStep = qMax(1.0e-6,
                                 state.value(QStringLiteral("gridStep")).toDouble(1.0));
        settings.showAxes = state.value(QStringLiteral("showAxes")).toBool(false);
        settings.outputMode =
            state.value(QStringLiteral("outputMode")).toInt(0) ==
                    static_cast<int>(OutputMode::Curve)
                ? OutputMode::Curve
                : OutputMode::Entity;

        std::vector<SketchDocument> documents;
        const QJsonArray committed =
            state.value(QStringLiteral("committedDocuments")).toArray();
        documents.reserve(static_cast<std::size_t>(committed.size() + 1));

        for (const QJsonValue& value : committed)
        {
            SketchDocument document;
            document.fromJson(value.toObject());
            if (document.isRenderable())
                documents.push_back(document);
        }

        SketchDocument activeDocument;
        activeDocument.fromJson(state.value(QStringLiteral("document")).toObject());
        if (activeDocument.isRenderable())
            documents.push_back(activeDocument);

        if (documents.empty())
        {
            setMessage(message, QStringLiteral("草图中没有可恢复的有效轮廓。"));
            return false;
        }

        const ShapeBuildResult result = SketchShapeBuilder::buildMany(documents, settings);
        if (!result.ok || result.shape.IsNull())
        {
            setMessage(message,
                       result.message.isEmpty()
                           ? QStringLiteral("草图自动恢复失败。")
                           : result.message);
            return false;
        }

        GeometryBuildUtils::registerShape(feature.uuid, feature.title,
                                          new TopoDS_Shape(result.shape));
        setMessage(message, QString());
        return true;
    }
    catch (const Standard_Failure& failure)
    {
        setMessage(message,
                   QStringLiteral("草图恢复异常：%1")
                       .arg(QString::fromLocal8Bit(
                           failure.GetMessageString() ? failure.GetMessageString() :
                                                        "OCCT exception")));
        return false;
    }
    catch (const std::exception& error)
    {
        setMessage(message,
                   QStringLiteral("草图恢复异常：%1")
                       .arg(QString::fromLocal8Bit(error.what())));
        return false;
    }
}

const QVector<FeatureDescriptor>& descriptors()
{
    static const QVector<FeatureDescriptor> items = {
        {
            PublicConstants::kPagePreprocessImportGeometry,
            QStringLiteral("ImportGeometry"),
            QStringLiteral("导入几何"),
            {},
            {},
            true,
            false,
            false,
            [] { return new GeometryImportWidget(); },
            [](const FeatureData& feature, IGeometryService& service, QString* message)
            {
                ImportGeometryBuilder::setStepImportOptions(
                    feature.uuid, stepImportOptionsFromState(feature.state));
                QString error;
                const bool ok = service.recreateImportGeometry(feature.uuid,
                                                               feature.title,
                                                               feature.state.value(QStringLiteral("path")).toString(),
                                                               error);
                setMessage(message, error);
                return ok;
            }
        },
        {
            PublicConstants::kPageGeomSketch2D,
            QStringLiteral("Sketch2D"),
            QStringLiteral("草图"),
            {},
            {},
            true,
            false,
            true,
            [] { return new Sketch2DDialog(); },
            rebuildSketch2D
        },
        {
            PublicConstants::kPageGeomTerrainSurface,
            QStringLiteral("TerrainSurface"), QStringLiteral("等高线地形"), {}, {},
            true, false, false,
            [] { return new TerrainSurfacePanel(); },
            [](const FeatureData& feature, IGeometryService&, QString* message)
            {
                if (!feature.state.value("generated").toBool()) return true;
                const auto result = GeometryTerrain::TerrainSurfaceBuilder::build(
                    GeometryTerrain::TerrainSurfaceBuilder::sourcesFromState(feature.state),
                    feature.state.value("spacing").toDouble(5),
                    GeometryTerrain::TerrainSurfaceBuilder::domainMode(feature.state));
                if (!result.ok()) { setMessage(message, result.error); return false; }
                GeometryDisplayManager::setTerrainPresentation(feature.uuid);
                GeometryBuildUtils::registerShape(feature.uuid,feature.title,new TopoDS_Shape(result.shape));
                setMessage(message,result.summary()); return true;
            }
        },
        {
            PublicConstants::kPagePreprocessBox,
            QStringLiteral("Box"),
            QStringLiteral("长方体"),
            {},
            {},
            true,
            false,
            false,
            [] { return new BoxDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateBox(feature.uuid, feature.title,
                                           state.value(QStringLiteral("length")).toDouble(),
                                           state.value(QStringLiteral("width")).toDouble(),
                                           state.value(QStringLiteral("height")).toDouble(),
                                           coordinateValue(state, QLatin1Char('x')),
                                           coordinateValue(state, QLatin1Char('y')),
                                           coordinateValue(state, QLatin1Char('z')));
            }
        },
        {
            PublicConstants::kPagePreprocessSphere,
            QStringLiteral("Sphere"),
            QStringLiteral("球体"),
            {},
            {},
            true,
            false,
            false,
            [] { return new SphereDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateSphere(feature.uuid, feature.title,
                                              state.value(QStringLiteral("radius")).toDouble(),
                                              coordinateValue(state, QLatin1Char('x')),
                                              coordinateValue(state, QLatin1Char('y')),
                                              coordinateValue(state, QLatin1Char('z')));
            }
        },
        {
            PublicConstants::kPagePreprocessCone,
            QStringLiteral("Cone"),
            QStringLiteral("圆锥"),
            {},
            {},
            true,
            false,
            false,
            [] { return new ConeDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateCone(feature.uuid, feature.title,
                                            state.value(QStringLiteral("bottomRadius")).toDouble(),
                                            state.value(QStringLiteral("topRadius")).toDouble(),
                                            state.value(QStringLiteral("height")).toDouble(),
                                            coordinateValue(state, QLatin1Char('x')),
                                            coordinateValue(state, QLatin1Char('y')),
                                            coordinateValue(state, QLatin1Char('z')));
            }
        },
        {
            PublicConstants::kPagePreprocessCylinder,
            QStringLiteral("Cylinder"),
            QStringLiteral("圆柱"),
            {},
            {},
            true,
            false,
            false,
            [] { return new CylinderDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateCylinder(feature.uuid, feature.title,
                                                state.value(QStringLiteral("radius")).toDouble(),
                                                state.value(QStringLiteral("height")).toDouble(),
                                                coordinateValue(state, QLatin1Char('x')),
                                                coordinateValue(state, QLatin1Char('y')),
                                                coordinateValue(state, QLatin1Char('z')));
            }
        },
        {
            PublicConstants::kPagePreprocessTorus,
            QStringLiteral("Torus"),
            QStringLiteral("圆环"),
            {},
            {},
            true,
            false,
            false,
            [] { return new TorusDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateTorus(feature.uuid, feature.title,
                                             state.value(QStringLiteral("majorRadius")).toDouble(),
                                             state.value(QStringLiteral("minorRadius")).toDouble(),
                                             coordinateValue(state, QLatin1Char('x')),
                                             coordinateValue(state, QLatin1Char('y')),
                                             coordinateValue(state, QLatin1Char('z')));
            }
        },
        {
            PublicConstants::kPagePreprocessGravityDam,
            QStringLiteral("GravityDam"),
            QStringLiteral("重力坝"),
            {},
            {},
            true,
            false,
            false,
            [] { return new GravityDamDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateGravityDam(
                    feature.uuid, feature.title,
                    state.value(QStringLiteral("height")).toDouble(),
                    state.value(QStringLiteral("crestWidth")).toDouble(),
                    state.value(QStringLiteral("upstreamSlope")).toDouble(),
                    state.value(QStringLiteral("downstreamSlope")).toDouble(),
                    state.value(QStringLiteral("length")).toDouble(),
                    state.value(QStringLiteral("layerCount")).toInt(),
                    coordinateValue(state, QLatin1Char('x')),
                    coordinateValue(state, QLatin1Char('y')),
                    coordinateValue(state, QLatin1Char('z')));
            }
        },
        {
            PublicConstants::kPagePreprocessEarthRockfillDam,
            QStringLiteral("EarthRockfillDam"),
            QStringLiteral("土石坝"),
            {},
            {},
            true,
            false,
            false,
            [] { return new EarthRockfillDamDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateEarthRockfillDam(
                    feature.uuid, feature.title,
                    state.value(QStringLiteral("h")).toDouble(),
                    state.value(QStringLiteral("crest")).toDouble(),
                    state.contains(QStringLiteral("upW"))
                        ? state.value(QStringLiteral("upW")).toDouble()
                        : state.value(QStringLiteral("up")).toDouble(),
                    state.contains(QStringLiteral("downW"))
                        ? state.value(QStringLiteral("downW")).toDouble()
                        : state.value(QStringLiteral("down")).toDouble(),
                    state.value(QStringLiteral("len")).toDouble(),
                    state.value(QStringLiteral("layers")).toInt(),
                    state.contains(QStringLiteral("coreTop"))
                        ? state.value(QStringLiteral("coreTop")).toDouble()
                        : state.value(QStringLiteral("core")).toDouble(),
                    state.value(QStringLiteral("coreBottom")).toDouble(22.0),
                    state.value(QStringLiteral("filter")).toDouble(4.0),
                    state.value(QStringLiteral("coreOffset")).toDouble(0.0));
            }
        },
        {
            PublicConstants::kPagePreprocessSlopeTemplate,
            QStringLiteral("SlopeTemplate"),
            QStringLiteral("边坡"),
            {},
            {},
            true,
            false,
            false,
            [] { return new SlopeTemplateDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateSlopeTemplate(
                    feature.uuid, feature.title,
                    state.value(QStringLiteral("h")).toDouble(),
                    state.value(QStringLiteral("bw")).toDouble(),
                    state.contains(QStringLiteral("w"))
                        ? state.value(QStringLiteral("w")).toDouble()
                        : state.value(QStringLiteral("s")).toDouble(),
                    state.value(QStringLiteral("l")).toDouble(),
                    state.value(QStringLiteral("bc")).toInt(),
                    state.value(QStringLiteral("top")).toDouble(20.0),
                    state.value(QStringLiteral("toe")).toDouble(30.0),
                    state.value(QStringLiteral("baseH")).toDouble(15.0));
            }
        },
        {
            PublicConstants::kPagePreprocessCavernTemplate,
            QStringLiteral("CavernTemplate"),
            QStringLiteral("洞室"),
            {},
            {},
            true,
            false,
            false,
            [] { return new CavernTemplateDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                const double stages = state.contains(QStringLiteral("stage"))
                    ? state.value(QStringLiteral("stage")).toDouble()
                    : qMax(1, qRound(state.value(QStringLiteral("lining")).toDouble()));
                return service.recreateCavernTemplate(
                    feature.uuid, feature.title,
                    state.value(QStringLiteral("span")).toDouble(),
                    state.value(QStringLiteral("wh")).toDouble(),
                    state.value(QStringLiteral("ah")).toDouble(),
                    state.value(QStringLiteral("len")).toDouble(),
                    stages,
                    state.value(QStringLiteral("sideRock")).toDouble(18.0),
                    state.value(QStringLiteral("topRock")).toDouble(16.0),
                    state.value(QStringLiteral("bottomRock")).toDouble(8.0));
            }
        },
        {
            PublicConstants::kPagePreprocessCircle,
            QStringLiteral("Circle"),
            QStringLiteral("圆"),
            {},
            {},
            true,
            false,
            true,
            [] { return new CircleDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateCircle(feature.uuid, feature.title,
                                              state.value(QStringLiteral("radius")).toDouble(),
                                              coordinateValue(state, QLatin1Char('x')),
                                              coordinateValue(state, QLatin1Char('y')),
                                              coordinateValue(state, QLatin1Char('z')),
                                              state.value(QStringLiteral("plane")).toInt());
            }
        },
        {
            PublicConstants::kPagePreprocessEllipse,
            QStringLiteral("Ellipse"),
            QStringLiteral("椭圆"),
            {},
            {},
            true,
            false,
            true,
            [] { return new EllipseDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateEllipse(feature.uuid, feature.title,
                                               state.value(QStringLiteral("majorRadius")).toDouble(),
                                               state.value(QStringLiteral("minorRadius")).toDouble(),
                                               coordinateValue(state, QLatin1Char('x')),
                                               coordinateValue(state, QLatin1Char('y')),
                                               coordinateValue(state, QLatin1Char('z')),
                                               state.value(QStringLiteral("plane")).toInt());
            }
        },
        {
            PublicConstants::kPagePreprocessRectangle,
            QStringLiteral("Rectangle"),
            QStringLiteral("矩形"),
            {},
            {},
            true,
            false,
            true,
            [] { return new RectangleDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateRectangle(feature.uuid, feature.title,
                                                 state.value(QStringLiteral("length")).toDouble(),
                                                 state.value(QStringLiteral("width")).toDouble(),
                                                 coordinateValue(state, QLatin1Char('x')),
                                                 coordinateValue(state, QLatin1Char('y')),
                                                 coordinateValue(state, QLatin1Char('z')),
                                                 state.value(QStringLiteral("plane")).toInt());
            }
        },
        {
            PublicConstants::kPagePreprocessPolygon,
            QStringLiteral("Polygon"),
            QStringLiteral("多边形"),
            {},
            {},
            true,
            false,
            true,
            [] { return new PolygonDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreatePolygon(feature.uuid, feature.title,
                                               state.value(QStringLiteral("radius")).toDouble(),
                                               state.value(QStringLiteral("sides")).toInt(),
                                               state.value(QStringLiteral("rotateDeg")).toDouble(),
                                               coordinateValue(state, QLatin1Char('x')),
                                               coordinateValue(state, QLatin1Char('y')),
                                               coordinateValue(state, QLatin1Char('z')),
                                               state.value(QStringLiteral("plane")).toInt());
            }
        },
        {
            PublicConstants::kPagePostProcessOccBoolean,
            QStringLiteral("BooleanFuse"),
            QStringLiteral("并集"),
            {QStringLiteral("occboolen")},
            {QStringLiteral("BooleanUnion")},
            false,
            true,
            false,
            [] { return new OccBooleanFuseWidget(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateBooleanFuse(feature.uuid,
                                                   feature.title,
                                                   jsonStringList(state, QStringLiteral("selectedKeys")),
                                                   jsonStringList(state, QStringLiteral("operationKeys")),
                                                   state.value(QStringLiteral("resultBrep")).toString());
            }
        },
        {
            PublicConstants::kPagePostProcessOccBoolean,
            QStringLiteral("BooleanCut"),
            QStringLiteral("差集"),
            {QStringLiteral("occboolen")},
            {},
            false,
            true,
            false,
            [] { return new OccBooleanCutWidget(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateBooleanCut(feature.uuid,
                                                  feature.title,
                                                  jsonStringList(state, QStringLiteral("selectedKeys")),
                                                  jsonStringList(state, QStringLiteral("operationKeys")),
                                                  state.value(QStringLiteral("resultBrep")).toString());
            }
        },
        {
            PublicConstants::kPagePostProcessOccBoolean,
            QStringLiteral("BooleanCommon"),
            QStringLiteral("交集"),
            {QStringLiteral("occboolen")},
            {},
            false,
            true,
            false,
            [] { return new OccBooleanCommonWidget(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateBooleanCommon(feature.uuid,
                                                     feature.title,
                                                     jsonStringList(state, QStringLiteral("selectedKeys")),
                                                     jsonStringList(state, QStringLiteral("operationKeys")),
                                                     state.value(QStringLiteral("resultBrep")).toString());
            }
        },
        {
            PublicConstants::kPageGeomExtrude,
            QStringLiteral("Extrude"),
            QStringLiteral("拉伸"),
            {},
            {},
            false,
            true,
            false,
            [] { return new ExtrudeDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateExtrude(feature.uuid, feature.title,
                                               state.value(QStringLiteral("profileUuid")).toString(),
                                               extrudeFaceRefs(state),
                                               extrudeParameters(state));
            }
        },
        {
            PublicConstants::kPageGeomRevolve,
            QStringLiteral("Revolve"),
            QStringLiteral("旋转生成"),
            {},
            {},
            false,
            true,
            false,
            [] { return new RevolveDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateRevolve(feature.uuid, feature.title,
                                               state.value(QStringLiteral("sourceUuid")).toString(),
                                               revolveParameters(state));
            }
        },
        {
            PublicConstants::kPageGeomSweep,
            QStringLiteral("Sweep"),
            QStringLiteral("扫掠"),
            {},
            {},
            false,
            true,
            false,
            [] { return new SweepDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateSweep(feature.uuid, feature.title,
                                             state.value(QStringLiteral("profileUuid")).toString(),
                                             state.value(QStringLiteral("pathUuid")).toString());
            }
        },
        {
            PublicConstants::kPageGeomLoft,
            QStringLiteral("Loft"),
            QStringLiteral("放样"),
            {},
            {},
            false,
            true,
            false,
            [] { return new LoftDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateLoft(feature.uuid,
                                            feature.title,
                                            jsonStringList(state, QStringLiteral("profileUuids")),
                                            state.value(QStringLiteral("makeSolid")).toBool(true),
                                            state.value(QStringLiteral("ruled")).toBool(false));
            }
        },
        {
            QStringLiteral("cut_extrude"),
            QStringLiteral("CutExtrude"),
            QStringLiteral("拉伸切除"),
            {QStringLiteral("CutExtrude")},
            {QStringLiteral("cut_extrude")},
            false,
            true,
            false,
            [] { return new CutExtrudeDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateCutExtrude(feature.uuid, feature.title,
                                                  state.value(QStringLiteral("targetUuid")).toString(),
                                                  state.value(QStringLiteral("profileUuid")).toString(),
                                                  state.value(QStringLiteral("depth")).toDouble(10.0),
                                                  cutExtrudeDirection(state));
            }
        },
        {
            PublicConstants::kPageGeomPattern,
            QStringLiteral("Pattern"),
            QStringLiteral("阵列"),
            {},
            {},
            false,
            true,
            false,
            [] { return new PatternDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                auto readDirection = [&state](int index)
                {
                    const QString prefix = QStringLiteral("direction%1").arg(index + 1);
                    PatternDirection direction;
                    if (state.contains(prefix + QStringLiteral("Count")))
                    {
                        direction.count = state.value(prefix + QStringLiteral("Count")).toInt(1);
                        direction.step = QVector3D(
                            float(state.value(prefix + QStringLiteral("X")).toDouble()),
                            float(state.value(prefix + QStringLiteral("Y")).toDouble()),
                            float(state.value(prefix + QStringLiteral("Z")).toDouble()));
                        return direction;
                    }

                    if (index == 0)
                    {
                        direction.count = state.value(QStringLiteral("countX")).toInt(
                            state.value(QStringLiteral("count")).toInt(4));
                        direction.step = QVector3D(
                            float(state.value(QStringLiteral("dx")).toDouble(10.0)), 0.0f, 0.0f);
                    }
                    else if (index == 1)
                    {
                        direction.count = state.value(QStringLiteral("countY")).toInt(1);
                        direction.step = QVector3D(
                            0.0f, float(state.value(QStringLiteral("dy")).toDouble()), 0.0f);
                    }
                    else
                    {
                        direction.count = state.value(QStringLiteral("countZ")).toInt(1);
                        direction.step = QVector3D(
                            0.0f, 0.0f, float(state.value(QStringLiteral("dz")).toDouble()));
                    }
                    return direction;
                };

                if (state.value(QStringLiteral("patternMode")).toString()
                        .compare(QStringLiteral("polar"), Qt::CaseInsensitive) == 0)
                {
                    PolarPatternParameters parameters;
                    parameters.count = state.value(QStringLiteral("polarCount")).toInt(8);
                    parameters.totalAngleDeg =
                        state.value(QStringLiteral("polarAngleDeg")).toDouble(360.0);
                    parameters.axisOrigin = QVector3D(
                        float(state.value(QStringLiteral("polarOriginX")).toDouble()),
                        float(state.value(QStringLiteral("polarOriginY")).toDouble()),
                        float(state.value(QStringLiteral("polarOriginZ")).toDouble()));
                    parameters.axisDirection = QVector3D(
                        float(state.value(QStringLiteral("polarAxisX")).toDouble()),
                        float(state.value(QStringLiteral("polarAxisY")).toDouble()),
                        float(state.value(QStringLiteral("polarAxisZ")).toDouble(1.0)));
                    return service.recreatePolarPattern(
                        feature.uuid, feature.title,
                        state.value(QStringLiteral("sourceUuid")).toString(), parameters);
                }

                return service.recreatePattern(feature.uuid, feature.title,
                                               state.value(QStringLiteral("sourceUuid")).toString(),
                                               readDirection(0), readDirection(1), readDirection(2));
            }
        },
        {
            PublicConstants::kPageGeomTransform,
            QStringLiteral("Transform"),
            QStringLiteral("对象变换"),
            {},
            {},
            false,
            true,
            false,
            [] { return new TransformDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateTransform(feature.uuid, feature.title,
                                                 state.value(QStringLiteral("sourceUuid")).toString(),
                                                 state.value(QStringLiteral("dx")).toDouble(10.0),
                                                 state.value(QStringLiteral("dy")).toDouble(0.0),
                                                 state.value(QStringLiteral("dz")).toDouble(0.0),
                                                 state.value(QStringLiteral("rx")).toDouble(0.0),
                                                 state.value(QStringLiteral("ry")).toDouble(0.0),
                                                 state.value(QStringLiteral("rz")).toDouble(0.0),
                                                 state.contains(QStringLiteral("pivotMode"))
                                                     ? state.value(QStringLiteral("pivotMode")).toInt(1)
                                                     : 0,
                                                 state.value(QStringLiteral("pivotX")).toDouble(0.0),
                                                 state.value(QStringLiteral("pivotY")).toDouble(0.0),
                                                 state.value(QStringLiteral("pivotZ")).toDouble(0.0),
                                                 state.contains(QStringLiteral("orderVersion"))
                                                     ? state.value(QStringLiteral("orderVersion")).toInt(2)
                                                     : 1);
            }
        },
        {
            PublicConstants::kPageGeomMirror,
            QStringLiteral("Mirror"),
            QStringLiteral("镜像"),
            {},
            {},
            false,
            true,
            false,
            [] { return new MirrorDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateMirror(feature.uuid, feature.title,
                                              state.value(QStringLiteral("sourceUuid")).toString(),
                                              state.value(QStringLiteral("plane")).toInt(0),
                                              state.value(QStringLiteral("offset")).toDouble(0.0));
            }
        },
        {
            PublicConstants::kPageGeomFillet,
            QStringLiteral("Fillet"),
            QStringLiteral("圆角"),
            {},
            {},
            false,
            true,
            false,
            [] { return new FilletDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateFillet(feature.uuid, feature.title,
                                              state.value(QStringLiteral("sourceUuid")).toString(),
                                              state.value(QStringLiteral("radius")).toDouble(0.2),
                                              state.value(QStringLiteral("edgeSelection")).toString());
            }
        },
        {
            PublicConstants::kPageGeomChamfer,
            QStringLiteral("Chamfer"),
            QStringLiteral("倒角"),
            {},
            {},
            false,
            true,
            false,
            [] { return new ChamferDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateChamfer(feature.uuid, feature.title,
                                               state.value(QStringLiteral("sourceUuid")).toString(),
                                               state.value(QStringLiteral("distance")).toDouble(0.2),
                                               state.value(QStringLiteral("edgeSelection")).toString());
            }
        },
        {
            PublicConstants::kPageGeomSplit,
            QStringLiteral("Split"),
            QStringLiteral("分割"),
            {},
            {},
            false,
            true,
            false,
            [] { return new SplitDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateSplit(feature.uuid, feature.title,
                                             state.value(QStringLiteral("sourceUuid")).toString(),
                                             state.value(QStringLiteral("toolUuid")).toString());
            }
        },
        {
            QStringLiteral("geom_step_import_part"),
            QStringLiteral("StepImportPart"),
            QStringLiteral("STEP 实体"),
            {},
            {},
            false,
            true,
            false,
            [] { return new StepImportPartDialog(); },
            [](const FeatureData& feature, IGeometryService&, QString* message)
            {
                const QJsonObject& state = feature.state;
                QString error;
                const bool ok = ImportGeometryBuilder::recreateStepImportPart(
                    feature.uuid,
                    feature.title,
                    state.value(QStringLiteral("importUuid")).toString(),
                    state.value(QStringLiteral("partIndex")).toInt(-1),
                    state.value(QStringLiteral("sourceKey")).toString(),
                    state.value(QStringLiteral("originalName")).toString(),
                    state.value(QStringLiteral("assemblyPath")).toString(),
                    state.value(QStringLiteral("colorHex")).toString(),
                    error);
                setMessage(message, error);
                return ok;
            }
        },
        {
            QStringLiteral("geom_split_part"),
            QStringLiteral("SplitPart"),
            QStringLiteral("分割块"),
            {},
            {},
            false,
            true,
            false,
            [] { return new SplitPartDialog(); },
            [](const FeatureData& feature, IGeometryService& service, QString*)
            {
                const QJsonObject& state = feature.state;
                return service.recreateSplitPart(
                    feature.uuid, feature.title,
                    state.value(QStringLiteral("splitUuid")).toString(),
                    state.value(QStringLiteral("partIndex")).toInt(-1));
            }
        }
    };
    return items;
}
} // namespace

namespace GeometryFeatures
{

const FeatureDescriptor* descriptorFor(const QString& type, const QString& kind)
{
    const auto& items = descriptors();

    if (!type.isEmpty() && !kind.isEmpty())
    {
        for (const FeatureDescriptor& descriptor : items)
        {
            if (typeMatches(descriptor, type) && kindMatches(descriptor, kind))
                return &descriptor;
        }
    }

    if (!kind.isEmpty())
    {
        for (const FeatureDescriptor& descriptor : items)
        {
            if (kindMatches(descriptor, kind))
                return &descriptor;
        }
    }

    if (!type.isEmpty())
    {
        for (const FeatureDescriptor& descriptor : items)
        {
            if (typeMatches(descriptor, type))
                return &descriptor;
        }
    }

    return nullptr;
}

const FeatureDescriptor* descriptorForKind(const QString& kind)
{
    if (kind.isEmpty())
        return nullptr;
    for (const FeatureDescriptor& descriptor : descriptors())
    {
        if (kindMatches(descriptor, kind))
            return &descriptor;
    }
    return nullptr;
}

BasePage* createPage(const QString& type, const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorFor(type, kind);
    if (!descriptor || !descriptor->createPage)
        return nullptr;
    return descriptor->createPage();
}

QString normalizeType(const QString& type, const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorFor(type, kind);
    if (descriptor)
        return descriptor->type;
    return type;
}

QString normalizeKind(const QString& type, const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorFor(type, kind);
    if (descriptor)
        return descriptor->kind;
    return kind;
}

QString labelForKind(const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorForKind(kind);
    if (descriptor)
        return descriptor->label;
    return QStringLiteral("几何");
}

bool rebuildFeature(const FeatureData& feature, IGeometryService& service, QString* message)
{
    const FeatureDescriptor* descriptor = descriptorFor(feature.type, feature.kind);
    if (!descriptor || !descriptor->rebuild)
    {
        setMessage(message, QStringLiteral("未注册的几何类型。"));
        return false;
    }
    return descriptor->rebuild(feature, service, message);
}

bool isPrimitiveLike(const QString& type, const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorFor(type, kind);
    return descriptor && descriptor->primitiveLike;
}

bool isFeatureLike(const QString& type, const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorFor(type, kind);
    return descriptor && descriptor->featureLike;
}

bool isSketchProfileKind(const QString& kind)
{
    const FeatureDescriptor* descriptor = descriptorForKind(kind);
    return descriptor && descriptor->sketchProfile;
}

QStringList consumedInputUuidsForFinalModel(const QString& type, const QString& kind,
                                               const QJsonObject& state,
                                               const QString& selfUuid)
{
    const QString normalizedKind = normalizeKind(type, kind);
    QStringList values;

    const auto append = [&](const QString& uuid)
    {
        if (!uuid.isEmpty() && uuid != selfUuid && !values.contains(uuid))
            values.push_back(uuid);
    };
    const auto appendArray = [&](const QString& key)
    {
        const QJsonArray array = state.value(key).toArray();
        for (const QJsonValue& value : array)
            append(value.toString());
    };

    // 这里只返回“被结果替代”的输入，而不是所有依赖对象。
    // 例如扫掠路径和分割工具只是参考/工具几何，不能从默认导出和网格集合中消失。
    if (normalizedKind == QStringLiteral("BooleanFuse") ||
        normalizedKind == QStringLiteral("BooleanCut") ||
        normalizedKind == QStringLiteral("BooleanCommon"))
    {
        appendArray(QStringLiteral("operationKeys"));
        if (values.isEmpty())
            appendArray(QStringLiteral("selectedKeys"));
    }
    else if (normalizedKind == QStringLiteral("Extrude"))
    {
        append(state.value(QStringLiteral("profileUuid")).toString());
    }
    else if (normalizedKind == QStringLiteral("Revolve") ||
             normalizedKind == QStringLiteral("Pattern") ||
             normalizedKind == QStringLiteral("Transform") ||
             normalizedKind == QStringLiteral("Mirror") ||
             normalizedKind == QStringLiteral("Fillet") ||
             normalizedKind == QStringLiteral("Chamfer"))
    {
        append(state.value(QStringLiteral("sourceUuid")).toString());
    }
    else if (normalizedKind == QStringLiteral("Sweep"))
    {
        append(state.value(QStringLiteral("profileUuid")).toString());
    }
    else if (normalizedKind == QStringLiteral("Loft"))
    {
        appendArray(QStringLiteral("profileUuids"));
    }
    else if (normalizedKind == QStringLiteral("CutExtrude"))
    {
        append(state.value(QStringLiteral("targetUuid")).toString());
        append(state.value(QStringLiteral("profileUuid")).toString());
    }
    else if (normalizedKind == QStringLiteral("Split"))
    {
        append(state.value(QStringLiteral("sourceUuid")).toString());
    }
    else if (normalizedKind == QStringLiteral("SplitPart"))
    {
        append(state.value(QStringLiteral("splitUuid")).toString());
    }
    else if (normalizedKind == QStringLiteral("StepImportPart"))
    {
        append(state.value(QStringLiteral("importUuid")).toString());
    }

    // ImportGeometry、草图和基础体不替代任何已有最终对象。
    return values;
}

} // namespace GeometryFeatures
