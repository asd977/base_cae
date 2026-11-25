#include "FeatureData.h"

#include "GeometryFeatureRegistry.h"

#include <QJsonArray>
#include <QObject>

namespace
{
void appendUuid(QStringList& values, const QString& value, const QString& selfUuid)
{
    if (value.isEmpty() || value == selfUuid || values.contains(value))
        return;
    values.push_back(value);
}

void appendArray(QStringList& values, const QJsonArray& arr, const QString& selfUuid)
{
    for (const auto& v : arr)
        appendUuid(values, v.toString(), selfUuid);
}

QJsonArray toJsonArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& value : values)
        arr.append(value);
    return arr;
}

QString numberText(double value, int precision = 3)
{
    QString text = QString::number(value, 'f', precision);
    while (text.contains('.') && text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text;
}

QString planeText(int planeType)
{
    switch (planeType)
    {
    case 1:
        return QStringLiteral("XZ");
    case 2:
        return QStringLiteral("YZ");
    case 0:
    default:
        return QStringLiteral("XY");
    }
}

} // namespace

bool FeatureData::isValid() const
{
    return !uuid.isEmpty() && !type.isEmpty();
}

bool FeatureData::hasMissingInputs() const
{
    return !missingInputUuids.isEmpty();
}

QJsonObject FeatureData::toJson() const
{
    QJsonObject obj;
    const QString canonicalType = GeometryFeatures::normalizeType(type, kind);
    const QString canonicalKind = GeometryFeatures::normalizeKind(canonicalType, kind);

    // Legacy fields：继续保留旧工程格式，避免影响已有加载逻辑。
    obj[QStringLiteral("uuid")] = uuid;
    obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("type")] = canonicalType;
    obj[QStringLiteral("module")] = module;
    obj[QStringLiteral("kind")] = canonicalKind;
    obj[QStringLiteral("visible")] = visible;
    obj[QStringLiteral("state")] = state;
    obj[QStringLiteral("cachedBrep")] = cachedBrep;
    if (!cachedBrepAsset.isEmpty())
        obj[QStringLiteral("cachedBrepAsset")] = cachedBrepAsset;
    if (!faceGroups.isEmpty())
    {
        QJsonArray fgArr;
        for (const auto& fg : faceGroups)
        {
            QJsonObject fgo;
            fgo[QStringLiteral("name")] = fg.name;
            fgo[QStringLiteral("role")] = fg.role;
            QJsonArray fidxArr;
            for (int idx : fg.faceIndices) fidxArr.append(idx);
            fgo[QStringLiteral("faceIndices")] = fidxArr;
            fgArr.append(fgo);
        }
        obj[QStringLiteral("faceGroups")] = fgArr;
    }

    // New FeatureData schema：统一保存特征关系、缓存和参数摘要。
    QJsonObject feature;
    feature[QStringLiteral("schema")] = QStringLiteral("GeometryFeatureData.v2");
    feature[QStringLiteral("uuid")] = uuid;
    feature[QStringLiteral("title")] = title;
    feature[QStringLiteral("type")] = canonicalType;
    feature[QStringLiteral("module")] = module;
    feature[QStringLiteral("kind")] = canonicalKind;
    feature[QStringLiteral("visible")] = visible;
    feature[QStringLiteral("state")] = state;
    if (!faceGroups.isEmpty())
    {
        QJsonArray fgArr;
        for (const auto& fg : faceGroups)
        {
            QJsonObject fgo;
            fgo[QStringLiteral("name")] = fg.name;
            fgo[QStringLiteral("role")] = fg.role;
            QJsonArray fidxArr;
            for (int idx : fg.faceIndices) fidxArr.append(idx);
            fgo[QStringLiteral("faceIndices")] = fidxArr;
            fgArr.append(fgo);
        }
        feature[QStringLiteral("faceGroups")] = fgArr;
    }
    feature[QStringLiteral("inputUuids")] = toJsonArray(inputUuids);
    feature[QStringLiteral("missingInputUuids")] = toJsonArray(missingInputUuids);
    feature[QStringLiteral("parameterSummary")] = parameterSummary(canonicalType, canonicalKind, state);
    obj[QStringLiteral("featureData")] = feature;
    return obj;
}

FeatureData FeatureData::fromJson(const QJsonObject& obj)
{
    const QJsonObject feature = obj.value(QStringLiteral("featureData")).toObject();
    const QJsonObject& src = feature.isEmpty() ? obj : feature;

    FeatureData data;
    data.uuid = src.value(QStringLiteral("uuid")).toString(obj.value(QStringLiteral("uuid")).toString());
    data.title = src.value(QStringLiteral("title")).toString(obj.value(QStringLiteral("title")).toString());
    data.type = src.value(QStringLiteral("type")).toString(obj.value(QStringLiteral("type")).toString());
    data.module = src.value(QStringLiteral("module")).toString(obj.value(QStringLiteral("module")).toString());
    data.kind = src.value(QStringLiteral("kind")).toString(obj.value(QStringLiteral("kind")).toString());
    data.type = GeometryFeatures::normalizeType(data.type, data.kind);
    data.kind = GeometryFeatures::normalizeKind(data.type, data.kind);
    data.visible = src.contains(QStringLiteral("visible"))
                       ? src.value(QStringLiteral("visible")).toBool(true)
                       : obj.value(QStringLiteral("visible")).toBool(true);
    data.state = src.value(QStringLiteral("state")).toObject(obj.value(QStringLiteral("state")).toObject());
    data.cachedBrep = src.value(QStringLiteral("cachedBrep")).toString(obj.value(QStringLiteral("cachedBrep")).toString());
    data.cachedBrepAsset = src.value(QStringLiteral("cachedBrepAsset")).toString(obj.value(QStringLiteral("cachedBrepAsset")).toString());

    const QJsonArray fgArr = src.contains(QStringLiteral("faceGroups"))
                                 ? src.value(QStringLiteral("faceGroups")).toArray()
                                 : obj.value(QStringLiteral("faceGroups")).toArray();
    for (const auto& v : fgArr)
    {
        const QJsonObject fgo = v.toObject();
        CadBody::FaceGroup fg;
        fg.name = fgo.value(QStringLiteral("name")).toString();
        fg.role = fgo.value(QStringLiteral("role")).toString();
        const QJsonArray fidxArr = fgo.value(QStringLiteral("faceIndices")).toArray();
        for (const auto& idxVal : fidxArr)
            fg.faceIndices.append(idxVal.toInt());
        data.faceGroups.append(fg);
    }

    const QJsonArray inputArr = src.value(QStringLiteral("inputUuids")).toArray();
    for (const auto& v : inputArr)
        appendUuid(data.inputUuids, v.toString(), data.uuid);
    if (data.inputUuids.isEmpty())
        data.inputUuids = collectInputUuids(data.state, data.uuid);

    const QJsonArray missingArr = src.value(QStringLiteral("missingInputUuids")).toArray();
    for (const auto& v : missingArr)
        appendUuid(data.missingInputUuids, v.toString(), data.uuid);

    return data;
}

QStringList FeatureData::collectInputUuids(const QJsonObject& state, const QString& selfUuid)
{
    QStringList inputs;
    appendUuid(inputs, state.value(QStringLiteral("sourceUuid")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("profileUuid")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("pathUuid")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("targetUuid")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("targetGeology")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("toolUuid")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("importUuid")).toString(), selfUuid);
    appendUuid(inputs, state.value(QStringLiteral("splitUuid")).toString(), selfUuid);

    appendArray(inputs, state.value(QStringLiteral("selectedKeys")).toArray(), selfUuid);
    appendArray(inputs, state.value(QStringLiteral("operationKeys")).toArray(), selfUuid);
    appendArray(inputs, state.value(QStringLiteral("profileUuids")).toArray(), selfUuid);
    appendArray(inputs, state.value(QStringLiteral("sourceUuids")).toArray(), selfUuid);
    const QJsonArray faceSelections = state.value(QStringLiteral("faceSelections")).toArray();
    for (const QJsonValue& value : faceSelections)
        appendUuid(inputs, value.toObject().value(QStringLiteral("uuid")).toString(), selfUuid);
    return inputs;
}

QString FeatureData::parameterSummary(const QString& type, const QString& kind, const QJsonObject& state)
{
    const QString featureKind = GeometryFeatures::normalizeKind(type, kind);

    if (featureKind == QStringLiteral("Box"))
        return QObject::tr("长=%1，宽=%2，高=%3")
            .arg(numberText(state.value(QStringLiteral("length")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("width")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("height")).toDouble()));

    if (featureKind == QStringLiteral("Sphere"))
        return QObject::tr("半径=%1").arg(numberText(state.value(QStringLiteral("radius")).toDouble()));

    if (featureKind == QStringLiteral("Cylinder"))
        return QObject::tr("半径=%1，高=%2")
            .arg(numberText(state.value(QStringLiteral("radius")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("height")).toDouble()));

    if (featureKind == QStringLiteral("Cone"))
        return QObject::tr("下半径=%1，上半径=%2，高=%3")
            .arg(numberText(state.value(QStringLiteral("bottomRadius")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("topRadius")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("height")).toDouble()));

    if (featureKind == QStringLiteral("Torus"))
        return QObject::tr("主半径=%1，管半径=%2")
            .arg(numberText(state.value(QStringLiteral("majorRadius")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("minorRadius")).toDouble()));

    if (featureKind == QStringLiteral("GravityDam"))
        return QObject::tr("坝高=%1，坝顶宽=%2，长度=%3")
            .arg(numberText(state.value(QStringLiteral("height")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("crestWidth")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("length")).toDouble()));

    if (featureKind == QStringLiteral("EarthRockfillDam"))
        return QObject::tr("坝高=%1，坝顶宽=%2，分层=%3")
            .arg(numberText(state.value(QStringLiteral("h")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("crest")).toDouble()))
            .arg(state.value(QStringLiteral("layers")).toInt());

    if (featureKind == QStringLiteral("SlopeTemplate"))
        return QObject::tr("坡高=%1，平台宽=%2，级数=%3")
            .arg(numberText(state.value(QStringLiteral("h")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("bw")).toDouble()))
            .arg(state.value(QStringLiteral("bc")).toInt());

    if (featureKind == QStringLiteral("CavernTemplate"))
        return QObject::tr("跨度=%1，直墙高=%2，开挖级数=%3")
            .arg(numberText(state.value(QStringLiteral("span")).toDouble()))
            .arg(numberText(state.value(QStringLiteral("wh")).toDouble()))
            .arg(state.value(QStringLiteral("stage")).toInt());

    if (featureKind == QStringLiteral("Extrude") || featureKind == QStringLiteral("CutExtrude"))
        return QObject::tr("高度=%1，方向=%2")
            .arg(numberText(state.value(QStringLiteral("height")).toDouble(state.value(QStringLiteral("depth")).toDouble())))
            .arg(state.value(QStringLiteral("direction")).toInt(4));

    if (featureKind == QStringLiteral("Revolve"))
        return QObject::tr("角度=%1，轴=%2")
            .arg(numberText(state.value(QStringLiteral("angle")).toDouble(360.0)))
            .arg(state.contains(QStringLiteral("axisDx")) ? QObject::tr("任意轴")
                                                       : QString::number(state.value(QStringLiteral("axis")).toInt(2)));

    if (featureKind == QStringLiteral("Fillet"))
        return QObject::tr("半径=%1").arg(numberText(state.value(QStringLiteral("radius")).toDouble()));

    if (featureKind == QStringLiteral("Chamfer"))
        return QObject::tr("距离=%1").arg(numberText(state.value(QStringLiteral("distance")).toDouble()));

    if (featureKind == QStringLiteral("Pattern"))
    {
        if (state.value(QStringLiteral("patternMode")).toString()
                .compare(QStringLiteral("polar"), Qt::CaseInsensitive) == 0)
        {
            const int count = state.value(QStringLiteral("polarCount")).toInt(8);
            const double angle = state.value(QStringLiteral("polarAngleDeg")).toDouble(360.0);
            return QObject::tr("圆周 %1 个，%2°")
                .arg(count)
                .arg(numberText(angle));
        }

        auto directionCount = [&state](int index)
        {
            const QString prefix = QStringLiteral("direction%1").arg(index + 1);
            if (state.contains(prefix + QStringLiteral("Count")))
                return state.value(prefix + QStringLiteral("Count")).toInt(1);
            if (index == 0)
                return state.value(QStringLiteral("countX")).toInt(
                    state.value(QStringLiteral("count")).toInt(4));
            return state.value(index == 1 ? QStringLiteral("countY")
                                          : QStringLiteral("countZ")).toInt(1);
        };

        const int count1 = directionCount(0);
        const int count2 = directionCount(1);
        const int count3 = directionCount(2);
        return QObject::tr("%1 个（%2×%3×%4）")
            .arg(count1 * count2 * count3)
            .arg(count1)
            .arg(count2)
            .arg(count3);
    }

    return QObject::tr("参数已保存");
}


QString FeatureData::typeLabel(const QString& type, const QString& kind)
{
    if (const auto* descriptor = GeometryFeatures::descriptorFor(type, kind))
        return descriptor->label;
    return QObject::tr("几何");
}

bool FeatureData::requiresInputs(const QString& type, const QString& kind)
{
    return isFeatureLike(type, kind) && !isPrimitiveLike(type, kind);
}

bool FeatureData::isPrimitiveLike(const QString& type, const QString& kind)
{
    return GeometryFeatures::isPrimitiveLike(type, kind);
}

bool FeatureData::isFeatureLike(const QString& type, const QString& kind)
{
    return GeometryFeatures::isFeatureLike(type, kind);
}

bool FeatureData::canEditParameters(const QString& type, const QString& kind)
{
    Q_UNUSED(kind);
    return !type.isEmpty();
}

QString FeatureData::editabilityText(const QString& type, const QString& kind,
                                     const QStringList& inputUuids,
                                     const QStringList& missingInputUuids,
                                     bool hasShape, bool hasCachedBrep)
{
    if (!canEditParameters(type, kind))
        return QObject::tr("不可编辑：缺少参数页。");

    if (!missingInputUuids.isEmpty())
    {
        if (hasCachedBrep)
            return QObject::tr("来源已缺失；当前显示的是工程中已保存的几何结果。");
        return QObject::tr("来源缺失。");
    }

    if (requiresInputs(type, kind) && inputUuids.isEmpty())
        return QObject::tr("未选择来源。");

    if (hasShape)
        return QObject::tr("可编辑。");

    if (hasCachedBrep)
        return QObject::tr("可从工程中已保存的几何结果恢复。");

    return QObject::tr("尚未生成。");
}
