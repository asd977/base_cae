#pragma once

#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// Snapshot-local CAD UI ID -> index in the EXACT ExportedModel.brep.
// This is not a persistent face identifier. Invalid/outdated maps fail closed.
class SmeshShapeIndexMap
{
public:
    static SmeshShapeIndexMap load(const QString& brepPath)
    {
        SmeshShapeIndexMap result;
        QFile manifest(brepPath + QStringLiteral(".shape-index.json"));
        if (!manifest.open(QIODevice::ReadOnly))
        {
            result.m_error = QStringLiteral("几何索引清单不存在，请重新生成几何并重试：%1").arg(manifest.fileName());
            return result;
        }
        QJsonParseError parse{};
        const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject())
        {
            result.m_error = QStringLiteral("几何索引清单格式不正确：%1").arg(parse.errorString());
            return result;
        }
        result.m_object = document.object();
        if (result.m_object.value(QStringLiteral("format")).toInt() != 1)
        {
            result.m_error = QStringLiteral("几何索引版本不兼容，请重新导出几何。");
            return result;
        }
        // GeometryWorkbench publishes the BREP and its index map together and
        // invalidates both when topology changes. Do not scan the BREP again.
        const QFileInfo brep(brepPath);
        if (!brep.isFile() || brep.size() <= 0)
        {
            result.m_error = QStringLiteral("BREP 不存在或为空：%1").arg(brepPath);
            return result;
        }
        result.m_valid = true;
        return result;
    }

    bool valid() const { return m_valid; }
    QString error() const { return m_error; }

    int uiCount(int dimension) const
    {
        return m_valid && dimension >= 1 && dimension <= 3
            ? m_object.value(QString::number(dimension)).toArray().size() : 0;
    }

    int requireIndex(int dimension, int uiId) const
    {
        if (!m_valid || dimension < 1 || dimension > 3 || uiId <= 0) return 0;
        const QJsonArray entries = m_object.value(QString::number(dimension)).toArray();
        if (uiId > entries.size()) return 0;
        const int index = entries.at(uiId - 1).toInt();
        return index > 0 && index <= m_object.value(QStringLiteral("count%1").arg(dimension)).toInt()
            ? index : 0;
    }
private:
    QJsonObject m_object;
    QString m_error;
    bool m_valid = false;
};
