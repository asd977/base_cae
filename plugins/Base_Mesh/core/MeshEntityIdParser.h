#pragma once
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace MeshEntityIdParser {
inline bool parseEntityIds(const QString& text, QSet<int>& ids, QString& err)
{
    ids.clear();
    err.clear();
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return true;
    const QStringList tokens = trimmed.split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
    for (const QString& token : tokens)
    {
        const int dash = token.indexOf(QLatin1Char('-'));
        if (dash > 0)
        {
            bool ok1 = false, ok2 = false;
            const int a = token.left(dash).toInt(&ok1);
            const int b = token.mid(dash + 1).toInt(&ok2);
            if (!ok1 || !ok2 || a <= 0 || b < a)
            {
                err = QStringLiteral("无效的编号区间：%1").arg(token);
                return false;
            }
            for (int v = a; v <= b; ++v) ids.insert(v);
        }
        else
        {
            bool ok = false;
            const int v = token.toInt(&ok);
            if (!ok || v <= 0)
            {
                err = QStringLiteral("无效的实体编号：%1").arg(token);
                return false;
            }
            ids.insert(v);
        }
    }
    return true;
}
} // namespace MeshEntityIdParser
