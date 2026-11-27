#pragma once
#include <QString>

namespace MeshLengthUnit {
inline QString current() { return QStringLiteral("m"); }
inline bool confirmed() { return true; }
inline QString symbol() { return QStringLiteral("m"); }
inline QString squaredSymbol() { return QStringLiteral("m²"); }
inline QString interpretation() { return QStringLiteral("长度单位：m"); }
inline double metresPerCoordinate() { return 1.0; }
inline QString format(double value, int precision = 8) { return QStringLiteral("%1 m").arg(value, 0, 'g', precision); }
} // namespace MeshLengthUnit
