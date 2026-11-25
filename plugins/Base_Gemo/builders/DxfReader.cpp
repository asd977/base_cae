#include "DxfReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cpl_string.h>
#include <QObject>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace GeometryDxf
{
void initializeGdal()
{
    static const bool initialized = [] {
        const QString appDir = QCoreApplication::applicationDirPath();
        if (QDir(appDir + "/gdal-data").exists())
            CPLSetConfigOption("GDAL_DATA", (appDir + "/gdal-data").toUtf8().constData());
        if (QDir(appDir + "/proj-data").exists())
            CPLSetConfigOption("PROJ_DATA", (appDir + "/proj-data").toUtf8().constData());
        GDALAllRegister();
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace
{
// Some CAD exporters declare ANSI_936 while actually writing UTF-8 names.
// Inspect raw name groups instead of decoding UTF-8 twice through that header.
bool hasUtf8Names(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.peek(22).startsWith("AutoCAD Binary DXF"))
        return false;
    bool nonAscii = false;
    while (!file.atEnd())
    {
        const auto code = file.readLine().trimmed();
        const auto value = file.readLine().trimmed();
        if (code != "8" && code != "2") continue;
        if (!value.isValidUtf8()) return false;
        for (const auto c : value)
            if (static_cast<unsigned char>(c) >= 128) nonAscii = true;
    }
    return nonAscii;
}

class EncodingScope
{
public:
    explicit EncodingScope(bool utf8)
    {
        const char* previous = CPLGetThreadLocalConfigOption("DXF_ENCODING", nullptr);
        m_hadPrevious = previous != nullptr;
        if (previous) m_previous = previous;
        if (utf8) CPLSetThreadLocalConfigOption("DXF_ENCODING", "UTF-8");
    }
    ~EncodingScope()
    {
        CPLSetThreadLocalConfigOption("DXF_ENCODING", m_hadPrevious ? m_previous.constData() : nullptr);
    }
private:
    QByteArray m_previous;
    bool m_hadPrevious = false;
};

QString field(const OGRFeature& feature, const char* name)
{
    const int index = feature.GetFieldIndex(name);
    return index >= 0 && feature.IsFieldSetAndNotNull(index)
        ? QString::fromUtf8(feature.GetFieldAsString(index)) : QString();
}
}

QString normalizeLayerName(const QString& name)
{
    // Legacy terrain imports dropped the final byte of these standard contour
    // labels after interpreting UTF-8 as GBK. Only these exact aliases are known.
    for (const auto& pair : {qMakePair(QStringLiteral("璁℃洸绾"), QStringLiteral("计曲线")),
                             qMakePair(QStringLiteral("棣栨洸绾"), QStringLiteral("首曲线"))})
    {
        QString oldName = pair.first;
        if (name.endsWith(oldName + QChar::ReplacementCharacter))
            oldName += QChar::ReplacementCharacter;
        if (name.endsWith(oldName))
            return name.left(name.size() - oldName.size()) + pair.second;
    }
    // General repair is allowed only when both encodings round-trip losslessly.
    const auto utf8 = name.toUtf8();
    char* raw = CPLRecode(utf8.constData(), "UTF-8", "CP936");
    const QByteArray bytes(raw ? raw : "");
    CPLFree(raw);
    if (bytes == utf8 || !bytes.isValidUtf8()) return name;
    raw = CPLRecode(bytes.constData(), "CP936", "UTF-8");
    const QByteArray roundTrip(raw ? raw : "");
    CPLFree(raw);
    return roundTrip == utf8 ? QString::fromUtf8(bytes) : name;
}

bool read(const QString& path, QVector<Entity>& entities, QString& diagnostic)
{
    entities.clear();
    diagnostic.clear();
    initializeGdal();

    // Keep each expanded block entity separate so its layer survives in sketches.
    const char* drivers[] = {"DXF", nullptr};
    const char* options[] = {"INLINE_BLOCKS=YES", "MERGE_BLOCK_GEOMETRIES=NO",
                             "CLOSED_LINE_AS_POLYGON=NO", nullptr};
    const EncodingScope encoding(hasUtf8Names(path));
    CPLErrorReset();
    std::unique_ptr<GDALDataset, decltype(&GDALClose)> dataset(
        static_cast<GDALDataset*>(GDALOpenEx(path.toUtf8().constData(),
            GDAL_OF_VECTOR | GDAL_OF_READONLY, drivers, options, nullptr)), GDALClose);
    if (!dataset)
    {
        diagnostic = QObject::tr("无法读取 DXF：%1\n%2")
                         .arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
        return false;
    }

    QVector<Entity> result;
    int ignored = 0;
    try
    {
        for (int l = 0; l < dataset->GetLayerCount(); ++l)
        {
            auto* layer = dataset->GetLayer(l);
            if (!layer) continue;
            layer->ResetReading();
            for (;;)
            {
                CPLErrorReset();
                OGRFeatureUniquePtr feature(layer->GetNextFeature());
                if (CPLGetLastErrorType() >= CE_Failure)
                {
                    diagnostic = QObject::tr("DXF 读取失败：%1")
                                     .arg(QString::fromUtf8(CPLGetLastErrorMsg()));
                    return false;
                }
                if (!feature) break;

                const QString subclasses = field(*feature, "SubClasses");
                const int paperSpace = feature->GetFieldIndex("PaperSpace");
                const char* style = feature->GetStyleString();
                if ((paperSpace >= 0 && feature->GetFieldAsInteger(paperSpace) != 0) ||
                    subclasses.contains("AcDbText") || subclasses.contains("AcDbMText") ||
                    subclasses.contains("AcDbAttribute") || subclasses.contains("AcDbDimension") ||
                    (style && QByteArray(style).startsWith("LABEL(")))
                {
                    ++ignored;
                    continue;
                }

                const auto* geometry = feature->GetGeometryRef();
                if (!geometry || geometry->IsEmpty()) { ++ignored; continue; }
                QString layerName = field(*feature, "Layer");
                if (layerName.isEmpty()) layerName = QString::fromUtf8(layer->GetName());
                QString handle = field(*feature, "EntityHandle");
                if (handle.isEmpty()) handle = QString::number(feature->GetFID());

                auto append = [&](const OGRLineString* line, const OGRPoint* point) {
                    Entity entity;
                    entity.layer = layerName;
                    entity.handle = handle;
                    const int count = line ? line->getNumPoints() : 1;
                    for (int i = 0; i < count; ++i)
                    {
                        // GDAL already applies elevation and OCS/block transforms.
                        // In particular, a valid Z=0 must never be overwritten.
                        const double x = line ? line->getX(i) : point->getX();
                        const double y = line ? line->getY(i) : point->getY();
                        const double z = line ? line->getZ(i) : point->getZ();
                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                            throw std::runtime_error("DXF 包含无效坐标。");
                        const gp_Pnt p(x, y, z);
                        if (entity.points.isEmpty() || entity.points.last().Distance(p) > 1e-9)
                            entity.points.push_back(p);
                    }
                    if (line && entity.points.size() > 2 &&
                        entity.points.first().Distance(entity.points.last()) <= 1e-7)
                    {
                        entity.closed = true;
                        entity.points.removeLast();
                    }
                    if (entity.points.isEmpty() || (line && entity.points.size() < 2))
                        return;
                    if (result.size() >= 100000)
                        throw std::runtime_error("DXF 展开后的对象超过 100000 个。");
                    result.push_back(std::move(entity));
                };

                std::function<void(const OGRGeometry*)> extract = [&](const OGRGeometry* g) {
                    if (!g || g->IsEmpty()) return;
                    const auto type = wkbFlatten(g->getGeometryType());
                    if (type == wkbPoint)
                        append(nullptr, g->toPoint());
                    else if (type == wkbLineString || type == wkbLinearRing)
                        append(g->toLineString(), nullptr);
                    else if (type == wkbPolygon || type == wkbTriangle)
                    {
                        const auto* polygon = g->toPolygon();
                        if (polygon->getExteriorRing()) append(polygon->getExteriorRing(), nullptr);
                        for (int i = 0; i < polygon->getNumInteriorRings(); ++i)
                            append(polygon->getInteriorRing(i), nullptr);
                    }
                    else if (OGR_GT_IsSubClassOf(type, wkbGeometryCollection))
                    {
                        const auto* collection = g->toGeometryCollection();
                        for (int i = 0; i < collection->getNumGeometries(); ++i)
                            extract(collection->getGeometryRef(i));
                    }
                    else if (g->hasCurveGeometry())
                    {
                        std::unique_ptr<OGRGeometry> linear(g->getLinearGeometry());
                        if (linear && !linear->hasCurveGeometry()) extract(linear.get());
                        else ++ignored;
                    }
                    else
                        ++ignored;
                };
                extract(geometry);
            }
        }
    }
    catch (const std::exception& error)
    {
        diagnostic = QString::fromUtf8(error.what());
        return false;
    }

    if (result.isEmpty())
    {
        diagnostic = QObject::tr("DXF 中没有可导入的模型空间曲线或高程点。");
        return false;
    }
    entities = std::move(result);
    diagnostic = QObject::tr("GDAL 已读取 %1 个对象，保留图层和 XYZ；圆弧与样条按折线导入。")
                     .arg(entities.size());
    if (ignored)
        diagnostic += QObject::tr(" %1 个标注、布局或不支持的对象未导入。").arg(ignored);
    return true;
}
}
