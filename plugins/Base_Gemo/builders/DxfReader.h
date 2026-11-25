#pragma once

#include <QString>
#include <QVector>
#include <gp_Pnt.hxx>

// All DXF entry points use GDAL for decoding, blocks, OCS and curve tessellation.
// Consumers only convert the resulting coordinates into sketch/CAD/terrain data.
namespace GeometryDxf
{
struct Entity
{
    QString layer;
    QString handle;
    bool closed = false;
    QVector<gp_Pnt> points;
};

void initializeGdal();
QString normalizeLayerName(const QString& name);
bool read(const QString& path, QVector<Entity>& entities, QString& diagnostic);
}
