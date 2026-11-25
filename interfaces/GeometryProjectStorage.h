#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

// Project-owned, compressed BREP assets. Names are random IDs, never geometry
// hashes. Commit every referenced asset before committing the project JSON.
namespace GeometryProjectStorage
{
constexpr qsizetype maxBrepBytes = 256 * 1024 * 1024;
inline bool safePath(const QString& path)
{
    static const QRegularExpression pattern(
        QStringLiteral("^geometry-data/[0-9a-f-]{36}\\.brep\\.qz$"));
    return pattern.match(path).hasMatch();
}
inline bool read(const QString& root, const QString& relative, QByteArray& raw)
{
    raw.clear();
    if (!safePath(relative) || QFileInfo(QDir(root).filePath("geometry-data")).isSymLink()) return false;
    const QString path = QDir(root).filePath(relative);
    const QFileInfo info(path);
    QFile file(path);
    if (!info.isFile() || info.isSymLink() || info.size() > maxBrepBytes
        || !file.open(QIODevice::ReadOnly)) return false;
    const auto compressed = file.readAll();
    if (compressed.size() < 5) return false;
    const quint32 bytes = (quint32(quint8(compressed[0])) << 24)
        | (quint32(quint8(compressed[1])) << 16) | (quint32(quint8(compressed[2])) << 8)
        | quint32(quint8(compressed[3]));
    if (!bytes || bytes > quint32(maxBrepBytes)) return false;
    raw = qUncompress(compressed);
    return raw.size() == qsizetype(bytes);
}
inline bool externalize(const QString& root, QJsonArray& items)
{
    for (qsizetype i = 0; i < items.size(); ++i)
    {
        auto item = items[i].toObject();
        const auto raw = item.value("cachedBrep").toString().toUtf8();
        if (raw.size() < 16384 || raw.size() > maxBrepBytes) continue;
        const QString folder = QDir(root).filePath("geometry-data");
        if (QFileInfo(folder).isSymLink() || !QDir().mkpath(folder)) return false;
        const QString relative = "geometry-data/" + QUuid::createUuid().toString(QUuid::WithoutBraces)
            + ".brep.qz";
        QSaveFile file(QDir(root).filePath(relative));
        const auto compressed = qCompress(raw, 6);
        if (!file.open(QIODevice::WriteOnly) || file.write(compressed) != compressed.size()
            || !file.commit()) return false;
        item.remove("cachedBrep");
        item.insert("cachedBrepAsset", relative);
        items[i] = item;
    }
    return true;
}
}
