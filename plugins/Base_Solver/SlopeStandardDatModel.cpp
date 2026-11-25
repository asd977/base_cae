#include "SlopeStandardDatModel.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <cmath>
#include <algorithm>
#include <limits>

namespace
{
QString realText(double value)
{
    return QString::number(value, 'g', 15);
}

bool fail(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
    return false;
}
}

bool SlopeStandardDatModel::parse(const QString& text, SlopeStandardDatModel* model,
                                  QString* errorMessage)
{
    if (!model)
        return fail(errorMessage, QStringLiteral("内部错误：没有可写入的参数模型。"));

    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    int titleLine = 0;
    while (titleLine < lines.size() && lines.at(titleLine).trimmed().isEmpty())
        ++titleLine;
    if (titleLine >= lines.size())
        return fail(errorMessage, QStringLiteral("DAT 内容为空。"));

    SlopeStandardDatModel parsed;
    parsed.projectTitle = lines.at(titleLine).trimmed();
    const QString tokenText = lines.mid(titleLine + 1).join(QLatin1Char(' '));
    const QStringList tokens = tokenText.split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts);
    int cursor = 0;
    auto takeToken = [&](const QString& field, QString* value)
    {
        if (cursor >= tokens.size())
            return fail(errorMessage, QStringLiteral("DAT 缺少字段：%1。").arg(field));
        *value = tokens.at(cursor++);
        return true;
    };
    auto takeInt = [&](const QString& field, int* value)
    {
        QString token;
        if (!takeToken(field, &token))
            return false;
        bool ok = false;
        const int parsedValue = token.toInt(&ok);
        if (!ok)
            return fail(errorMessage,
                        QStringLiteral("字段“%1”应为整数，当前值为“%2”。").arg(field, token));
        *value = parsedValue;
        return true;
    };
    auto takeReal = [&](const QString& field, double* value)
    {
        QString token;
        if (!takeToken(field, &token))
            return false;
        token.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);
        bool ok = false;
        const double parsedValue = token.toDouble(&ok);
        if (!ok || !std::isfinite(parsedValue))
            return fail(errorMessage,
                        QStringLiteral("字段“%1”应为有效数值，当前值为“%2”。").arg(field, token));
        *value = parsedValue;
        return true;
    };

    if (!takeInt(QStringLiteral("计算模式"), &parsed.mode)
        || !takeReal(QStringLiteral("搜索步长"), &parsed.searchStep)
        || !takeInt(QStringLiteral("计算方法"), &parsed.method)
        || !takeReal(QStringLiteral("滑弧半径"), &parsed.radius)
        || !takeReal(QStringLiteral("圆心 X"), &parsed.circleX)
        || !takeReal(QStringLiteral("圆心 Y"), &parsed.circleY)
        || !takeReal(QStringLiteral("水的容重"), &parsed.waterUnitWeight)
        || !takeInt(QStringLiteral("条分数"), &parsed.sliceCount))
        return false;

    int count = 0;
    if (!takeInt(QStringLiteral("边界线数量"), &count) || count < 0 || count > 100000)
        return fail(errorMessage, QStringLiteral("边界线数量无效。"));
    parsed.segments.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        SlopeStandardSegment item;
        if (!takeInt(QStringLiteral("边界线 %1 起点").arg(i + 1), &item.startNode)
            || !takeInt(QStringLiteral("边界线 %1 终点").arg(i + 1), &item.endNode)
            || !takeInt(QStringLiteral("边界线 %1 材料号").arg(i + 1), &item.soilId))
            return false;
        parsed.segments.append(item);
    }

    if (!takeReal(QStringLiteral("坡外水位"), &parsed.outsideWaterLevel)
        || !takeInt(QStringLiteral("土层数量"), &count) || count < 0 || count > 100000)
        return fail(errorMessage, QStringLiteral("土层数量无效。"));
    parsed.soils.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        SlopeStandardSoil item;
        if (!takeInt(QStringLiteral("材料 %1 ID").arg(i + 1), &item.id)
            || !takeReal(QStringLiteral("材料 %1 内摩擦角").arg(i + 1), &item.frictionAngle)
            || !takeReal(QStringLiteral("材料 %1 黏聚力").arg(i + 1), &item.cohesion)
            || !takeReal(QStringLiteral("材料 %1 天然容重").arg(i + 1), &item.naturalUnitWeight)
            || !takeReal(QStringLiteral("材料 %1 饱和容重").arg(i + 1), &item.saturatedUnitWeight)
            || !takeInt(QStringLiteral("材料 %1 孔压方法").arg(i + 1), &item.poreMethod)
            || !takeReal(QStringLiteral("材料 %1 Ru").arg(i + 1), &item.ru))
            return false;
        parsed.soils.append(item);
    }

    if (!takeInt(QStringLiteral("节点数量"), &count) || count < 0 || count > 100000)
        return fail(errorMessage, QStringLiteral("节点数量无效。"));
    parsed.nodes.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        SlopeStandardNode item;
        if (!takeInt(QStringLiteral("节点 %1 ID").arg(i + 1), &item.id)
            || !takeReal(QStringLiteral("节点 %1 X").arg(i + 1), &item.x)
            || !takeReal(QStringLiteral("节点 %1 Y").arg(i + 1), &item.y))
            return false;
        parsed.nodes.append(item);
    }

    if (!takeInt(QStringLiteral("外坡面数量"), &count) || count < 0 || count > 100000)
        return fail(errorMessage, QStringLiteral("外坡面数量无效。"));
    parsed.surfaceLoads.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        SlopeStandardSurfaceLoad item;
        if (!takeInt(QStringLiteral("外坡面 %1 线段号").arg(i + 1), &item.segmentId)
            || !takeReal(QStringLiteral("外坡面 %1 荷载").arg(i + 1), &item.force))
            return false;
        parsed.surfaceLoads.append(item);
    }

    if (!takeReal(QStringLiteral("边坡高度"), &parsed.slopeHeight)
        || !takeInt(QStringLiteral("地震开关"), &parsed.earthquakeEnabled)
        || !takeInt(QStringLiteral("地震烈度"), &parsed.earthquakeIntensity)
        || !takeInt(QStringLiteral("地震组合模式"), &parsed.earthquakeMode)
        || !takeInt(QStringLiteral("水平地震方向"), &parsed.earthquakeDirectionH)
        || !takeInt(QStringLiteral("竖向地震方向"), &parsed.earthquakeDirectionV)
        || !takeInt(QStringLiteral("浸润线数量"), &count) || count < 0 || count > 100000)
        return fail(errorMessage, QStringLiteral("浸润线数量无效。"));
    parsed.phreaticSegments.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        SlopeStandardSegment item;
        if (!takeInt(QStringLiteral("浸润线 %1 起点").arg(i + 1), &item.startNode)
            || !takeInt(QStringLiteral("浸润线 %1 终点").arg(i + 1), &item.endNode)
            || !takeInt(QStringLiteral("浸润线 %1 材料号").arg(i + 1), &item.soilId))
            return false;
        parsed.phreaticSegments.append(item);
    }

    if (!parsed.validate(errorMessage))
        return false;
    *model = parsed;
    return true;
}

QString SlopeStandardDatModel::serialize() const
{
    QString output;
    QTextStream stream(&output);
    stream << projectTitle.trimmed() << '\n';
    stream << mode << ' ' << realText(searchStep) << '\n';
    stream << method << '\n';
    stream << realText(radius) << ' ' << realText(circleX) << ' ' << realText(circleY) << '\n';
    stream << realText(waterUnitWeight) << '\n';
    stream << sliceCount << '\n';
    stream << segments.size() << '\n';
    for (const auto& item : segments)
        stream << item.startNode << ' ' << item.endNode << ' ' << item.soilId << '\n';
    stream << realText(outsideWaterLevel) << '\n';
    stream << soils.size() << '\n';
    for (const auto& item : soils)
        stream << item.id << ' ' << realText(item.frictionAngle) << ' '
               << realText(item.cohesion) << ' ' << realText(item.naturalUnitWeight) << ' '
               << realText(item.saturatedUnitWeight) << ' ' << item.poreMethod << ' '
               << realText(item.poreMethod == 2 ? item.ru : 0.0) << '\n';
    stream << nodes.size() << '\n';
    for (const auto& item : nodes)
        stream << item.id << ' ' << realText(item.x) << ' ' << realText(item.y) << '\n';
    stream << surfaceLoads.size() << '\n';
    for (const auto& item : surfaceLoads)
        stream << item.segmentId << ' ' << realText(item.force) << '\n';
    stream << realText(slopeHeight) << ' ' << earthquakeEnabled << ' ' << earthquakeIntensity
           << ' ' << earthquakeMode << ' ' << earthquakeDirectionH << ' '
           << earthquakeDirectionV << '\n';
    stream << phreaticSegments.size() << '\n';
    for (const auto& item : phreaticSegments)
        stream << item.startNode << ' ' << item.endNode << ' ' << item.soilId << '\n';
    return output;
}

bool SlopeStandardDatModel::validate(QString* errorMessage) const
{
    if (projectTitle.trimmed().isEmpty())
        return fail(errorMessage, QStringLiteral("请填写算例名称。"));
    if (projectTitle.contains(QLatin1Char('\n')) || projectTitle.contains(QLatin1Char('\r')))
        return fail(errorMessage, QStringLiteral("算例名称不能包含换行。"));
    if (mode != 0 && mode != 1)
        return fail(errorMessage, QStringLiteral("计算模式必须是固定圆弧或自动搜索。"));
    if (method < 1 || method > 3)
        return fail(errorMessage, QStringLiteral("请选择有效的规范法计算方法。"));
    if (!std::isfinite(searchStep) || searchStep <= 0.0)
        return fail(errorMessage, QStringLiteral("搜索步长必须大于 0。"));
    if (!std::isfinite(radius) || radius <= 0.0)
        return fail(errorMessage, QStringLiteral("滑弧半径必须大于 0。"));
    if (sliceCount < 1 || sliceCount > 200)
        return fail(errorMessage, QStringLiteral("条分数必须在 1～200 之间。"));
    if (nodes.isEmpty())
        return fail(errorMessage, QStringLiteral("至少需要一个几何节点。"));
    if (soils.isEmpty())
        return fail(errorMessage, QStringLiteral("至少需要一个土层参数。"));
    if (segments.isEmpty())
        return fail(errorMessage, QStringLiteral("至少需要一条边界线。"));
    if (surfaceLoads.isEmpty() || surfaceLoads.size() > 50)
        return fail(errorMessage, QStringLiteral("外坡面线段数量必须在 1～50 之间。"));

    for (int i = 0; i < nodes.size(); ++i)
    {
        if (nodes.at(i).id != i + 1)
            return fail(errorMessage, QStringLiteral("节点 ID 必须从 1 开始连续编号。"));
        if (!std::isfinite(nodes.at(i).x) || !std::isfinite(nodes.at(i).y))
            return fail(errorMessage, QStringLiteral("第 %1 个节点坐标无效。").arg(i + 1));
    }
    const int globalPoreMethod = poreMethod();
    if (globalPoreMethod != -1 && globalPoreMethod != 0 && globalPoreMethod != 2)
        return fail(errorMessage, QStringLiteral("孔压方法必须选择总应力法、浸润线法或 Ru 法。"));
    for (int i = 0; i < soils.size(); ++i)
    {
        const auto& soil = soils.at(i);
        if (soil.id != i + 1)
            return fail(errorMessage, QStringLiteral("土层 ID 必须从 1 开始连续编号。"));
        if (soil.poreMethod != globalPoreMethod)
            return fail(errorMessage, QStringLiteral("所有土层必须使用同一种孔压方法。"));
        if (!std::isfinite(soil.frictionAngle) || !std::isfinite(soil.cohesion)
            || !std::isfinite(soil.naturalUnitWeight)
            || !std::isfinite(soil.saturatedUnitWeight) || !std::isfinite(soil.ru))
            return fail(errorMessage, QStringLiteral("土层 %1 含有无效数值。").arg(i + 1));
        if (soil.frictionAngle < 0.0)
            return fail(errorMessage,
                        QStringLiteral("土层 %1 的内摩擦角 φ 不能小于 0。").arg(i + 1));
        if (soil.cohesion < 0.0)
            return fail(errorMessage,
                        QStringLiteral("土层 %1 的黏聚力 c 不能小于 0。").arg(i + 1));
        if (soil.naturalUnitWeight <= 0.0)
            return fail(errorMessage,
                        QStringLiteral("土层 %1 的天然容重 γ 必须大于 0。").arg(i + 1));
        if (soil.saturatedUnitWeight <= 0.0)
            return fail(errorMessage,
                        QStringLiteral("土层 %1 的饱和容重 γsat 必须大于 0。").arg(i + 1));
        if (soil.saturatedUnitWeight < soil.naturalUnitWeight)
            return fail(errorMessage,
                        QStringLiteral("土层 %1 的饱和容重 γsat 不能小于天然容重 γ。")
                            .arg(i + 1));
        if (soil.ru < 0.0)
            return fail(errorMessage,
                        QStringLiteral("土层 %1 的孔压系数 Ru 不能小于 0。").arg(i + 1));
    }
    for (int i = 0; i < segments.size(); ++i)
    {
        const auto& segment = segments.at(i);
        if (segment.startNode < 1 || segment.startNode > nodes.size()
            || segment.endNode < 1 || segment.endNode > nodes.size())
            return fail(errorMessage,
                        QStringLiteral("第 %1 条边界线引用了不存在的节点。").arg(i + 1));
        if (segment.soilId < 1 || segment.soilId > soils.size())
            return fail(errorMessage,
                        QStringLiteral("第 %1 条边界线引用了不存在的土层。").arg(i + 1));
        const auto& a = nodes.at(segment.startNode - 1);
        const auto& b = nodes.at(segment.endNode - 1);
        if (std::hypot(a.x - b.x, a.y - b.y) < 1.0e-4)
            return fail(errorMessage, QStringLiteral("第 %1 条边界线长度过小。").arg(i + 1));
    }
    for (int i = 0; i < surfaceLoads.size(); ++i)
    {
        if (surfaceLoads.at(i).segmentId < 1
            || surfaceLoads.at(i).segmentId > segments.size())
            return fail(errorMessage,
                        QStringLiteral("第 %1 个外坡面引用了不存在的边界线。").arg(i + 1));
        if (!std::isfinite(surfaceLoads.at(i).force))
            return fail(errorMessage, QStringLiteral("第 %1 个外坡面荷载无效。").arg(i + 1));
    }
    if (globalPoreMethod == 0 && phreaticSegments.isEmpty())
        return fail(errorMessage, QStringLiteral("采用浸润线法时，必须定义至少一条浸润线段。"));
    for (int i = 0; i < phreaticSegments.size(); ++i)
    {
        const auto& segment = phreaticSegments.at(i);
        if (segment.startNode < 1 || segment.startNode > nodes.size()
            || segment.endNode < 1 || segment.endNode > nodes.size())
            return fail(errorMessage,
                        QStringLiteral("第 %1 条浸润线引用了不存在的节点。").arg(i + 1));
        if (segment.soilId < 1 || segment.soilId > soils.size())
            return fail(errorMessage,
                        QStringLiteral("第 %1 条浸润线引用了不存在的土层。").arg(i + 1));
    }
    if (earthquakeEnabled != 0 && earthquakeEnabled != 1)
        return fail(errorMessage, QStringLiteral("地震开关无效。"));
    if (earthquakeMode < 0 || earthquakeMode > 2)
        return fail(errorMessage, QStringLiteral("地震组合模式无效。"));
    if (std::abs(earthquakeDirectionH) > 1 || std::abs(earthquakeDirectionV) > 1)
        return fail(errorMessage, QStringLiteral("地震方向必须为 -1、0 或 1。"));
    return true;
}

bool SlopeStandardDatModel::ensureSearchCircleNearSection()
{
    if (mode != 1 || nodes.isEmpty())
        return false;
    double xmin = std::numeric_limits<double>::max();
    double xmax = std::numeric_limits<double>::lowest();
    double ymin = xmin;
    double ymax = xmax;
    for (const auto& node : nodes)
    {
        if (!std::isfinite(node.x) || !std::isfinite(node.y))
            return false;
        xmin = std::min(xmin, node.x);
        xmax = std::max(xmax, node.x);
        ymin = std::min(ymin, node.y);
        ymax = std::max(ymax, node.y);
    }
    const double scale = std::max({xmax - xmin, ymax - ymin, 1.0});
    // The native search clips seeds to these bounds, then explores locally.
    // A retained example circle (5, 12, R=12.8) cannot seed a GIS section.
    const bool usable = std::isfinite(radius) && std::isfinite(circleX)
        && std::isfinite(circleY) && radius >= 0.15 * scale && radius <= 8.0 * scale
        && circleX >= xmin - 2.5 * scale && circleX <= xmax + 2.5 * scale
        && circleY >= ymin - 2.0 * scale && circleY <= ymax + 4.0 * scale
        && circleX + radius >= xmin && circleX - radius <= xmax
        && circleY + radius >= ymin && circleY - radius <= ymax;
    if (usable)
        return false;
    circleX = xmin + 0.5 * (xmax - xmin);
    circleY = ymax + 0.1 * scale;
    radius = 0.6 * scale;
    return true;
}

int SlopeStandardDatModel::poreMethod() const
{
    return soils.isEmpty() ? -1 : soils.first().poreMethod;
}
