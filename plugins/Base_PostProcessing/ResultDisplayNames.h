#pragma once

#include <QComboBox>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace ResultDisplayNames
{
inline QString unitOf(const QString& rawName)
{
    static const QRegularExpression unitRegex(
        QStringLiteral("(?:\\(([^()]*)\\)|\\[([^\\[\\]]*)\\])\\s*$"));
    const QRegularExpressionMatch match = unitRegex.match(rawName.trimmed());
    if (!match.hasMatch())
        return QString();
    return !match.captured(1).isEmpty() ? match.captured(1).trimmed()
                                         : match.captured(2).trimmed();
}

inline QString keyOf(const QString& rawName)
{
    QString key = rawName.trimmed();
    static const QRegularExpression unitRegex(
        QStringLiteral("(?:\\(([^()]*)\\)|\\[([^\\[\\]]*)\\])\\s*$"));
    key.remove(unitRegex);
    key.remove(QRegularExpression(QStringLiteral("\\s+")));
    return key.toUpper();
}

inline QString displayName(const QString& rawName)
{
    const QString raw = rawName.trimmed();
    if (raw.isEmpty())
        return raw;

    const QString unit = unitOf(raw);
    const QString key = keyOf(raw);
    const bool velocityUnit = unit.contains(QLatin1Char('/')) &&
                              unit.contains(QLatin1Char('s'), Qt::CaseInsensitive);

    QString translated;
    if (key == QStringLiteral("UX") || key == QStringLiteral("U1") ||
        key == QStringLiteral("DX"))
        translated = velocityUnit ? QStringLiteral("X速度 v_x") : QStringLiteral("X位移 u_x");
    else if (key == QStringLiteral("UY") || key == QStringLiteral("U2") ||
             key == QStringLiteral("DY"))
        translated = velocityUnit ? QStringLiteral("Y速度 v_y") : QStringLiteral("Y位移 u_y");
    else if (key == QStringLiteral("UZ") || key == QStringLiteral("U3") ||
             key == QStringLiteral("DZ"))
        translated = velocityUnit ? QStringLiteral("Z速度 v_z") : QStringLiteral("Z位移 u_z");
    else if (key == QStringLiteral("UH") || key == QStringLiteral("DISP") ||
             key == QStringLiteral("U") || key == QStringLiteral("UMAG"))
        translated = QStringLiteral("位移模 |u|");
    else
    {
        static const QHash<QString, QString> names = {
            {QStringLiteral("PRS1"), QStringLiteral("最大主应力 σ_1")},
            {QStringLiteral("PRS2"), QStringLiteral("中主应力 σ_2")},
            {QStringLiteral("PRS3"), QStringLiteral("最小主应力 σ_3")},
            {QStringLiteral("SIG1"), QStringLiteral("最大主应力 σ_1")},
            {QStringLiteral("SIG2"), QStringLiteral("中主应力 σ_2")},
            {QStringLiteral("SIG3"), QStringLiteral("最小主应力 σ_3")},
            {QStringLiteral("TMAX"), QStringLiteral("最大剪应力 τ_max")},
            {QStringLiteral("SXX"), QStringLiteral("X正应力 σ_x")},
            {QStringLiteral("SYY"), QStringLiteral("Y正应力 σ_y")},
            {QStringLiteral("SZZ"), QStringLiteral("Z正应力 σ_z")},
            {QStringLiteral("SXY"), QStringLiteral("XY剪应力 τ_xy")},
            {QStringLiteral("SYZ"), QStringLiteral("YZ剪应力 τ_yz")},
            {QStringLiteral("SZX"), QStringLiteral("ZX剪应力 τ_zx")},
            {QStringLiteral("SXZ"), QStringLiteral("XZ剪应力 τ_xz")},
            {QStringLiteral("MISES"), QStringLiteral("等效应力 σ_v")},
            {QStringLiteral("VONMISES"), QStringLiteral("等效应力 σ_v")},
            {QStringLiteral("SL"), QStringLiteral("应力水平")},

            {QStringLiteral("EXX"), QStringLiteral("X应变 ε_x")},
            {QStringLiteral("EYY"), QStringLiteral("Y应变 ε_y")},
            {QStringLiteral("EZZ"), QStringLiteral("Z应变 ε_z")},
            {QStringLiteral("EXY"), QStringLiteral("XY剪应变 γ_xy")},
            {QStringLiteral("EYZ"), QStringLiteral("YZ剪应变 γ_yz")},
            {QStringLiteral("EZX"), QStringLiteral("ZX剪应变 γ_zx")},
            {QStringLiteral("EXZ"), QStringLiteral("XZ剪应变 γ_xz")},
            {QStringLiteral("E1"), QStringLiteral("最大主应变 ε_1")},
            {QStringLiteral("E2"), QStringLiteral("中主应变 ε_2")},
            {QStringLiteral("E3"), QStringLiteral("最小主应变 ε_3")},
            {QStringLiteral("PEXX"), QStringLiteral("X塑性应变 εp_x")},
            {QStringLiteral("PEYY"), QStringLiteral("Y塑性应变 εp_y")},
            {QStringLiteral("PEZZ"), QStringLiteral("Z塑性应变 εp_z")},
            {QStringLiteral("PEXY"), QStringLiteral("XY塑性剪应变 γp_xy")},
            {QStringLiteral("PEYZ"), QStringLiteral("YZ塑性剪应变 γp_yz")},
            {QStringLiteral("PEZX"), QStringLiteral("ZX塑性剪应变 γp_zx")},
            {QStringLiteral("PEXZ"), QStringLiteral("XZ塑性剪应变 γp_xz")},
            {QStringLiteral("PE1"), QStringLiteral("最大主塑性应变 εp_1")},
            {QStringLiteral("PE2"), QStringLiteral("中主塑性应变 εp_2")},
            {QStringLiteral("PE3"), QStringLiteral("最小主塑性应变 εp_3")},
            {QStringLiteral("PEMAX"), QStringLiteral("最大塑性剪应变")},
            {QStringLiteral("PEEQ"), QStringLiteral("等效塑性应变")},
            {QStringLiteral("FAILURE_ZONE"), QStringLiteral("全部破坏区（0未破坏，1破坏）")},
            {QStringLiteral("SHEAR_FAILURE"), QStringLiteral("剪切破坏区（0否，1是）")},
            {QStringLiteral("TENSILE_FAILURE"), QStringLiteral("拉伸破坏区（0否，1是）")},

            {QStringLiteral("HEAD"), QStringLiteral("水头 H")},
            {QStringLiteral("HP"), QStringLiteral("水头 H")},
            {QStringLiteral("P"), QStringLiteral("孔压 p")},
            {QStringLiteral("PRESSURE"), QStringLiteral("孔压 p")},
            {QStringLiteral("PWP"), QStringLiteral("孔压 p")},
            {QStringLiteral("POR"), QStringLiteral("孔压 p")},
            {QStringLiteral("VX"), QStringLiteral("X速度 v_x")},
            {QStringLiteral("VY"), QStringLiteral("Y速度 v_y")},
            {QStringLiteral("VZ"), QStringLiteral("Z速度 v_z")},
            {QStringLiteral("VMAG"), QStringLiteral("速度模 |v|")},

            {QStringLiteral("TEMP"), QStringLiteral("温度 T")},
            {QStringLiteral("TEMPERATURE"), QStringLiteral("温度 T")},
            {QStringLiteral("SAT"), QStringLiteral("饱和度")},
            {QStringLiteral("SATURATION"), QStringLiteral("饱和度")},
            {QStringLiteral("DAMAGE"), QStringLiteral("损伤变量")},
            {QStringLiteral("FOS"), QStringLiteral("安全系数")},
            {QStringLiteral("FS"), QStringLiteral("安全系数")},

            {QStringLiteral("MATYPE"), QStringLiteral("材料号")},
            {QStringLiteral("MATERIAL"), QStringLiteral("材料号")},
            {QStringLiteral("LAYER"), QStringLiteral("层号")},
            {QStringLiteral("KETYPE"), QStringLiteral("单元类型")}};
        translated = names.value(key);
    }

    if (translated.isEmpty())
        return raw;
    return unit.isEmpty() ? translated : QStringLiteral("%1（%2）").arg(translated, unit);
}

inline QString scalarBarName(const QString& rawName)
{
    const QString raw = rawName.trimmed();
    if (raw.isEmpty())
        return raw;

    const QString unit = unitOf(raw);
    const QString key = keyOf(raw);
    const bool velocityUnit = unit.contains(QLatin1Char('/')) &&
                              unit.contains(QLatin1Char('s'), Qt::CaseInsensitive);

    QString symbol;
    if (key == QStringLiteral("UX") || key == QStringLiteral("U1") ||
        key == QStringLiteral("DX"))
        symbol = velocityUnit ? QStringLiteral("v_x") : QStringLiteral("u_x");
    else if (key == QStringLiteral("UY") || key == QStringLiteral("U2") ||
             key == QStringLiteral("DY"))
        symbol = velocityUnit ? QStringLiteral("v_y") : QStringLiteral("u_y");
    else if (key == QStringLiteral("UZ") || key == QStringLiteral("U3") ||
             key == QStringLiteral("DZ"))
        symbol = velocityUnit ? QStringLiteral("v_z") : QStringLiteral("u_z");
    else if (key == QStringLiteral("UH") || key == QStringLiteral("DISP") ||
             key == QStringLiteral("U") || key == QStringLiteral("UMAG"))
        symbol = QStringLiteral("|u|");
    else
    {
        static const QHash<QString, QString> names = {
            {QStringLiteral("PRS1"), QStringLiteral("σ_1")},
            {QStringLiteral("PRS2"), QStringLiteral("σ_2")},
            {QStringLiteral("PRS3"), QStringLiteral("σ_3")},
            {QStringLiteral("SIG1"), QStringLiteral("σ_1")},
            {QStringLiteral("SIG2"), QStringLiteral("σ_2")},
            {QStringLiteral("SIG3"), QStringLiteral("σ_3")},
            {QStringLiteral("TMAX"), QStringLiteral("τ_max")},
            {QStringLiteral("SXX"), QStringLiteral("σ_x")},
            {QStringLiteral("SYY"), QStringLiteral("σ_y")},
            {QStringLiteral("SZZ"), QStringLiteral("σ_z")},
            {QStringLiteral("SXY"), QStringLiteral("τ_xy")},
            {QStringLiteral("SYZ"), QStringLiteral("τ_yz")},
            {QStringLiteral("SZX"), QStringLiteral("τ_zx")},
            {QStringLiteral("SXZ"), QStringLiteral("τ_xz")},
            {QStringLiteral("MISES"), QStringLiteral("σ_v")},
            {QStringLiteral("VONMISES"), QStringLiteral("σ_v")},
            {QStringLiteral("SL"), QStringLiteral("SL")},

            {QStringLiteral("EXX"), QStringLiteral("ε_x")},
            {QStringLiteral("EYY"), QStringLiteral("ε_y")},
            {QStringLiteral("EZZ"), QStringLiteral("ε_z")},
            {QStringLiteral("EXY"), QStringLiteral("γ_xy")},
            {QStringLiteral("EYZ"), QStringLiteral("γ_yz")},
            {QStringLiteral("EZX"), QStringLiteral("γ_zx")},
            {QStringLiteral("EXZ"), QStringLiteral("γ_xz")},
            {QStringLiteral("E1"), QStringLiteral("ε_1")},
            {QStringLiteral("E2"), QStringLiteral("ε_2")},
            {QStringLiteral("E3"), QStringLiteral("ε_3")},
            {QStringLiteral("PEXX"), QStringLiteral("εp_x")},
            {QStringLiteral("PEYY"), QStringLiteral("εp_y")},
            {QStringLiteral("PEZZ"), QStringLiteral("εp_z")},
            {QStringLiteral("PEXY"), QStringLiteral("γp_xy")},
            {QStringLiteral("PEYZ"), QStringLiteral("γp_yz")},
            {QStringLiteral("PEZX"), QStringLiteral("γp_zx")},
            {QStringLiteral("PEXZ"), QStringLiteral("γp_xz")},
            {QStringLiteral("PE1"), QStringLiteral("εp_1")},
            {QStringLiteral("PE2"), QStringLiteral("εp_2")},
            {QStringLiteral("PE3"), QStringLiteral("εp_3")},
            {QStringLiteral("PEMAX"), QStringLiteral("γp_max")},
            {QStringLiteral("PEEQ"), QStringLiteral("εp_eq")},
            {QStringLiteral("FAILURE_ZONE"), QStringLiteral("破坏区")},
            {QStringLiteral("SHEAR_FAILURE"), QStringLiteral("剪切破坏区")},
            {QStringLiteral("TENSILE_FAILURE"), QStringLiteral("拉伸破坏区")},

            {QStringLiteral("HEAD"), QStringLiteral("H")},
            {QStringLiteral("HP"), QStringLiteral("H")},
            {QStringLiteral("P"), QStringLiteral("p")},
            {QStringLiteral("PRESSURE"), QStringLiteral("p")},
            {QStringLiteral("PWP"), QStringLiteral("p")},
            {QStringLiteral("POR"), QStringLiteral("p")},
            {QStringLiteral("VX"), QStringLiteral("v_x")},
            {QStringLiteral("VY"), QStringLiteral("v_y")},
            {QStringLiteral("VZ"), QStringLiteral("v_z")},
            {QStringLiteral("VMAG"), QStringLiteral("|v|")},

            {QStringLiteral("TEMP"), QStringLiteral("T")},
            {QStringLiteral("TEMPERATURE"), QStringLiteral("T")},
            {QStringLiteral("SAT"), QStringLiteral("SAT")},
            {QStringLiteral("SATURATION"), QStringLiteral("SAT")},
            {QStringLiteral("DAMAGE"), QStringLiteral("DAMAGE")},
            {QStringLiteral("FOS"), QStringLiteral("FOS")},
            {QStringLiteral("FS"), QStringLiteral("FS")},
            {QStringLiteral("MATYPE"), QStringLiteral("MATYPE")},
            {QStringLiteral("MATERIAL"), QStringLiteral("MATERIAL")},
            {QStringLiteral("LAYER"), QStringLiteral("LAYER")},
            {QStringLiteral("KETYPE"), QStringLiteral("KETYPE")}};
        symbol = names.value(key);
    }

    if (symbol.isEmpty())
    {
        symbol = raw;
        static const QRegularExpression unitRegex(
            QStringLiteral("(?:\\(([^()]*)\\)|\\[([^\\[\\]]*)\\])\\s*$"));
        symbol.remove(unitRegex);
        symbol = symbol.trimmed();
    }
    if (symbol.isEmpty())
        symbol = key;
    return unit.isEmpty() ? symbol : QStringLiteral("%1 (%2)").arg(symbol, unit);
}

inline QString internalName(const QComboBox* combo)
{
    if (!combo)
        return QString();
    const QString raw = combo->currentData(Qt::UserRole).toString();
    return raw.isEmpty() ? combo->currentText() : raw;
}

inline int findInternalName(const QComboBox* combo, const QString& rawName)
{
    if (!combo)
        return -1;
    int index = combo->findData(rawName, Qt::UserRole);
    if (index < 0)
        index = combo->findText(rawName);
    return index;
}

inline void addScalarItem(QComboBox* combo, const QString& rawName)
{
    if (combo)
        combo->addItem(displayName(rawName), rawName);
}

inline void populateCombo(QComboBox* combo, const QStringList& rawNames,
                          const QString& preferredRawName = QString())
{
    if (!combo)
        return;
    combo->blockSignals(true);
    combo->clear();
    for (const QString& rawName : rawNames)
        addScalarItem(combo, rawName);
    int index = findInternalName(combo, preferredRawName);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);
    combo->blockSignals(false);
}
} // namespace ResultDisplayNames
