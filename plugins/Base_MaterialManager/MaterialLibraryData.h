#ifndef MATERIALLIBRARYDATA_H
#define MATERIALLIBRARYDATA_H

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct MaterialEntry
{
    QString id;
    QString name;
    QMap<QString, QString> properties;
    QSet<int> appliedMatypes;
};

struct MaterialModel
{
    QString key;
    QString displayName;

    int ib = 0;                       // IB 固定属性：默认 0

    QStringList columns;              // 列 key（用于 properties / 导出 / 存档）
    QStringList displayColumns;       // 列展示名（用于 UI 表头）
    QVector<int> columnOutputRows;    // 输出行号映射（1-based 行号；<=0 表示不输出）

    QVector<MaterialEntry> materials;
};

QVector<MaterialModel> createDefaultMaterialModels();

#endif // MATERIALLIBRARYDATA_H
