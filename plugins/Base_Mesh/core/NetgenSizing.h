#pragma once

#include <QJsonObject>
#include <QString>
#include <cmath>

namespace NetgenSizing
{
// Older projects stored only a numeric lower bound. Preserve that behavior.
// NETGEN's automatic lower size (0) works with both native presets and Custom;
// min-size mode is independent of the Fineness enum.
inline bool automaticFromProject(const QJsonObject& netgen, const QJsonObject& global)
{
    if (netgen.contains("automatic_min_size"))
        return netgen.value("automatic_min_size").toBool();
    return global.value("min_size").toDouble(0.5) == 0.0;
}

inline bool resolveMinimum(bool automatic, int fineness, double manualMinimum,
                           double maximum, double factor, double& effectiveMinimum,
                           QString& error)
{
    error.clear();
    if (!std::isfinite(maximum) || maximum <= 0.0 || !std::isfinite(factor) || factor <= 0.0
        || !std::isfinite(maximum * factor))
        error = QStringLiteral("目标尺寸和尺寸倍率必须为有效的正数。");
    else if (fineness < 0 || fineness > 5)
        error = QStringLiteral("NETGEN 精细度参数无效。");
    else if (!automatic && (!std::isfinite(manualMinimum) || manualMinimum < 0.0
                           || manualMinimum > maximum))
        error = QStringLiteral("手动最小尺寸必须大于等于 0，且不能大于目标尺寸。");
    if (!error.isEmpty()) return false;
    effectiveMinimum = automatic ? 0.0 : manualMinimum * factor;
    return true;
}
}
