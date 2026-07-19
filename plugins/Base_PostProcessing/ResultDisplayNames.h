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
        translated = velocityUnit ? QStringLiteral("X速度 vₓ") : QStringLiteral("X位移 uₓ");
    else if (key == QStringLiteral("UY") || key == QStringLiteral("U2") ||
             key == QStringLiteral("DY"))
        translated = velocityUnit ? QStringLiteral("Y速度 vᵧ") : QStringLiteral("Y位移 uᵧ");
    else if (key == QStringLiteral("UZ") || key == QStringLiteral("U3") ||
             key == QStringLiteral("DZ"))
        translated = velocityUnit ? QStringLiteral("Z速度 v_z") : QStringLiteral("Z位移 u_z");
    else if (key == QStringLiteral("UH") || key == QStringLiteral("DISP") ||
             key == QStringLiteral("U") || key == QStringLiteral("UMAG"))
        translated = QStringLiteral("位移模 |u|");
    else
    {
        static const QHash<QString, QString> names = {
            {QStringLiteral("PRS1"), QStringLiteral("最大主应力 σ₁")},
            {QStringLiteral("PRS2"), QStringLiteral("中主应力 σ₂")},
            {QStringLiteral("PRS3"), QStringLiteral("最小主应力 σ₃")},
            {QStringLiteral("SIG1"), QStringLiteral("最大主应力 σ₁")},
            {QStringLiteral("SIG2"), QStringLiteral("中主应力 σ₂")},
            {QStringLiteral("SIG3"), QStringLiteral("最小主应力 σ₃")},
            {QStringLiteral("TMAX"), QStringLiteral("最大剪应力 τmax")},
            {QStringLiteral("SXX"), QStringLiteral("X正应力 σₓ")},
            {QStringLiteral("SYY"), QStringLiteral("Y正应力 σᵧ")},
            {QStringLiteral("SZZ"), QStringLiteral("Z正应力 σ_z")},
            {QStringLiteral("SXY"), QStringLiteral("XY剪应力 τₓᵧ")},
            {QStringLiteral("SYZ"), QStringLiteral("YZ剪应力 τᵧ_z")},
            {QStringLiteral("SZX"), QStringLiteral("ZX剪应力 τ_zₓ")},
            {QStringLiteral("SXZ"), QStringLiteral("XZ剪应力 τₓ_z")},
            {QStringLiteral("MISES"), QStringLiteral("等效应力 σv")},
            {QStringLiteral("VONMISES"), QStringLiteral("等效应力 σv")},
            {QStringLiteral("SL"), QStringLiteral("应力水平")},

            {QStringLiteral("EXX"), QStringLiteral("X应变 εₓ")},
            {QStringLiteral("EYY"), QStringLiteral("Y应变 εᵧ")},
            {QStringLiteral("EZZ"), QStringLiteral("Z应变 ε_z")},
            {QStringLiteral("EXY"), QStringLiteral("XY剪应变 γₓᵧ")},
            {QStringLiteral("EYZ"), QStringLiteral("YZ剪应变 γᵧ_z")},
            {QStringLiteral("EZX"), QStringLiteral("ZX剪应变 γ_zₓ")},
            {QStringLiteral("EXZ"), QStringLiteral("XZ剪应变 γₓ_z")},
            {QStringLiteral("E1"), QStringLiteral("最大主应变 ε₁")},
            {QStringLiteral("E2"), QStringLiteral("中主应变 ε₂")},
            {QStringLiteral("E3"), QStringLiteral("最小主应变 ε₃")},
            {QStringLiteral("PEXX"), QStringLiteral("X塑性应变")},
            {QStringLiteral("PEYY"), QStringLiteral("Y塑性应变")},
            {QStringLiteral("PEZZ"), QStringLiteral("Z塑性应变")},
            {QStringLiteral("PEXY"), QStringLiteral("XY塑性剪应变")},
            {QStringLiteral("PEYZ"), QStringLiteral("YZ塑性剪应变")},
            {QStringLiteral("PEZX"), QStringLiteral("ZX塑性剪应变")},
            {QStringLiteral("PEXZ"), QStringLiteral("XZ塑性剪应变")},

            {QStringLiteral("HEAD"), QStringLiteral("水头 H")},
            {QStringLiteral("HP"), QStringLiteral("水头 H")},
            {QStringLiteral("P"), QStringLiteral("孔压 p")},
            {QStringLiteral("PRESSURE"), QStringLiteral("孔压 p")},
            {QStringLiteral("PWP"), QStringLiteral("孔压 p")},
            {QStringLiteral("POR"), QStringLiteral("孔压 p")},
            {QStringLiteral("VX"), QStringLiteral("X速度 vₓ")},
            {QStringLiteral("VY"), QStringLiteral("Y速度 vᵧ")},
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
