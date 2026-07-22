#include "NetgenMeshGenerator.h"

#include <BOPAlgo_Builder.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Precision.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Pnt2d.hxx>
#include <TopoDS_Shape.hxx>

namespace nglib
{
#include <nglib.h>
#include <nglib_occ.h>
}
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridWriter.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QTextStream>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

using namespace nglib;

namespace
{
constexpr int kDefaultCalculationMatype = 3;
constexpr int kMaxSolverMatype = 300;
constexpr int kDefaultConstructionLayer = 1;
constexpr int kElemTypeHex8FromTet = 4;

void setError(QString* errMsg, const QString& msg)
{
    if (errMsg)
        *errMsg = msg;
}

QString resultText(Ng_Result result)
{
    switch (result)
    {
    case NG_OK:
        return QStringLiteral("OK");
    case NG_ERROR:
        return QStringLiteral("NG_ERROR");
    case NG_SURFACE_INPUT_ERROR:
        return QStringLiteral("NG_SURFACE_INPUT_ERROR");
    case NG_VOLUME_FAILURE:
        return QStringLiteral("NG_VOLUME_FAILURE");
    case NG_STL_INPUT_ERROR:
        return QStringLiteral("NG_STL_INPUT_ERROR");
    case NG_SURFACE_FAILURE:
        return QStringLiteral("NG_SURFACE_FAILURE");
    case NG_FILE_NOT_FOUND:
        return QStringLiteral("NG_FILE_NOT_FOUND");
    default:
        return QStringLiteral("Netgen error %1").arg(static_cast<int>(result));
    }
}

void ensureNetgenInitialized()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       Ng_Init();
                   });
}

struct ConformalGeometryInfo
{
    QString geometryPath;
    // Keep the pre-fragment OCC shape for matching the UI entity IDs. Local
    // size restrictions are spatial, so sampling the original entity remains
    // valid after the material regions are made conformal.
    TopoDS_Shape controlShape;
    int sourceRegionCount = 0;
    int outputRegionCount = 0;
    int sharedInterfaceCount = 0;
    bool processed = false;
};

bool loadStepShape(const QString& path, TopoDS_Shape& shape, QString* errMsg)
{
    STEPControl_Reader reader;
    const QByteArray encodedPath = QFile::encodeName(QFileInfo(path).absoluteFilePath());
    const IFSelect_ReturnStatus status = reader.ReadFile(encodedPath.constData());
    if (status != IFSelect_RetDone)
    {
        setError(errMsg, QStringLiteral("OCC 无法读取 STEP 几何：%1").arg(path));
        return false;
    }

    const Standard_Integer transferred = reader.TransferRoots();
    if (transferred <= 0)
    {
        setError(errMsg, QStringLiteral("STEP 中没有可转换的几何根节点：%1").arg(path));
        return false;
    }

    shape = reader.OneShape();
    if (shape.IsNull())
    {
        setError(errMsg, QStringLiteral("STEP 转换后得到空几何：%1").arg(path));
        return false;
    }
    return true;
}

bool prepareConformalGeometry(const NetgenMeshingRequest& request,
                              QTemporaryFile& temporaryBrep,
                              ConformalGeometryInfo& info,
                              QString* errMsg)
{
    info.geometryPath = request.inputGeometryPath;

    const QFileInfo inputInfo(request.inputGeometryPath);
    const QString suffix = inputInfo.suffix().toLower();
    if (suffix != QStringLiteral("step") && suffix != QStringLiteral("stp"))
        return true;

    TopoDS_Shape importedShape;
    if (!loadStepShape(request.inputGeometryPath, importedShape, errMsg))
        return false;
    info.controlShape = importedShape;

    const TopAbs_ShapeEnum regionType = request.dimension == 3 ? TopAbs_SOLID : TopAbs_FACE;
    TopTools_IndexedMapOfShape sourceRegions;
    TopExp::MapShapes(importedShape, regionType, sourceRegions);
    info.sourceRegionCount = sourceRegions.Extent();
    info.outputRegionCount = sourceRegions.Extent();

    if (!request.conformalizeRegions)
        return true;

    // 单一区域没有材料界面，不需要做布尔碎片化。
    if (sourceRegions.Extent() <= 1)
        return true;

    TopTools_ListOfShape arguments;
    for (Standard_Integer i = 1; i <= sourceRegions.Extent(); ++i)
        arguments.Append(sourceRegions(i));

    BOPAlgo_Builder builder;
    builder.SetArguments(arguments);
    builder.SetRunParallel(Standard_False);
    builder.SetNonDestructive(Standard_True);
    builder.SetFuzzyValue(std::max(request.tolerance, Precision::Confusion()));
    builder.Perform();

    if (builder.HasErrors() || builder.Shape().IsNull())
    {
        setError(errMsg, QStringLiteral(
            "相邻几何区域无法建立共享拓扑。请检查材料分区之间是否存在间隙、重叠或自交。"));
        return false;
    }

    TopTools_IndexedMapOfShape conformalRegions;
    TopExp::MapShapes(builder.Shape(), regionType, conformalRegions);
    if (conformalRegions.Extent() <= 0)
    {
        setError(errMsg, QStringLiteral("共形处理后没有得到有效的几何区域。"));
        return false;
    }

    TopoDS_Compound compound;
    BRep_Builder compoundBuilder;
    compoundBuilder.MakeCompound(compound);
    for (Standard_Integer i = 1; i <= conformalRegions.Extent(); ++i)
        compoundBuilder.Add(compound, conformalRegions(i));

    const TopAbs_ShapeEnum interfaceType =
        request.dimension == 3 ? TopAbs_FACE : TopAbs_EDGE;
    TopTools_IndexedDataMapOfShapeListOfShape interfaceOwners;
    TopExp::MapShapesAndAncestors(compound, interfaceType, regionType, interfaceOwners);
    for (Standard_Integer i = 1; i <= interfaceOwners.Extent(); ++i)
    {
        if (interfaceOwners.FindFromIndex(i).Extent() >= 2)
            ++info.sharedInterfaceCount;
    }

    BRepCheck_Analyzer analyzer(compound, Standard_True);
    if (!analyzer.IsValid())
    {
        setError(errMsg, QStringLiteral(
            "共形处理后的几何拓扑无效。请先修复几何中的微小缝隙或重叠区域。"));
        return false;
    }

    QString templateDir = inputInfo.absolutePath();
    if (!QFileInfo(templateDir).isWritable())
        templateDir = QDir::tempPath();
    temporaryBrep.setFileTemplate(
        QDir(templateDir).filePath(QStringLiteral("geocae_conformal_XXXXXX.brep")));
    temporaryBrep.setAutoRemove(true);
    if (!temporaryBrep.open())
    {
        setError(errMsg, QStringLiteral("无法创建共形几何临时文件。"));
        return false;
    }

    const QString brepPath = temporaryBrep.fileName();
    temporaryBrep.close();
    QFile::remove(brepPath);

    const QByteArray encodedBrepPath = QFile::encodeName(brepPath);
    const Standard_Boolean writeOk = BRepTools::Write(compound, encodedBrepPath.constData());
    const QFileInfo brepInfo(brepPath);
    if (!writeOk || !brepInfo.exists() || brepInfo.size() <= 0)
    {
        setError(errMsg, QStringLiteral("共形几何临时文件写入失败：%1").arg(brepPath));
        return false;
    }

    info.geometryPath = brepPath;
    info.outputRegionCount = conformalRegions.Extent();
    info.processed = true;
    return true;
}

Ng_OCC_Geometry* loadOccGeometry(const QString& path, QString* errMsg)
{
    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();
    const QByteArray fileName = QFile::encodeName(info.absoluteFilePath());

    if (ext == QStringLiteral("step") || ext == QStringLiteral("stp"))
        return Ng_OCC_Load_STEP(fileName.constData());
    if (ext == QStringLiteral("iges") || ext == QStringLiteral("igs"))
        return Ng_OCC_Load_IGES(fileName.constData());
    if (ext == QStringLiteral("brep"))
        return Ng_OCC_Load_BREP(fileName.constData());

    setError(errMsg, QStringLiteral("Netgen OCC only supports STEP, IGES and BREP input: %1").arg(path));
    return nullptr;
}

int mapSurfaceTypeToVtk(Ng_Surface_Element_Type type, int& nodeCount, int& ketype)
{
    switch (type)
    {
    case NG_TRIG:
        nodeCount = 3;
        ketype = 31;
        return VTK_TRIANGLE;
    case NG_QUAD:
        nodeCount = 4;
        ketype = 3;
        return VTK_QUAD;
    case NG_TRIG6:
        nodeCount = 6;
        ketype = 32;
        return VTK_QUADRATIC_TRIANGLE;
    case NG_QUAD6:
        nodeCount = 6;
        ketype = 4;
        return VTK_QUADRATIC_LINEAR_QUAD;
    case NG_QUAD8:
        nodeCount = 8;
        ketype = 5;
        return VTK_QUADRATIC_QUAD;
    default:
        break;
    }
    nodeCount = 0;
    ketype = 0;
    return -1;
}

int mapVolumeTypeToVtk(Ng_Volume_Element_Type type, int& nodeCount, int& ketype)
{
    switch (type)
    {
    case NG_TET:
        nodeCount = 4;
        ketype = 35;
        return VTK_TETRA;
    case NG_PYRAMID:
        nodeCount = 5;
        ketype = 39;
        return VTK_PYRAMID;
    case NG_PRISM:
        nodeCount = 6;
        ketype = 38;
        return VTK_WEDGE;
    case NG_TET10:
        nodeCount = 10;
        ketype = 35;
        return VTK_QUADRATIC_TETRA;
    default:
        break;
    }
    nodeCount = 0;
    ketype = 0;
    return -1;
}

QVector<int> readNetgenElementRegions(const QString& filePath, int dimension,
                                       int expectedElementCount)
{
    QVector<int> regions;
    if (filePath.trimmed().isEmpty() || expectedElementCount <= 0)
        return regions;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return regions;

    QTextStream stream(&file);
    bool foundSection = false;
    while (!stream.atEnd())
    {
        const QString section = stream.readLine().trimmed().toLower();
        const bool matches = dimension == 3
            ? section == QStringLiteral("volumeelements")
            : section.startsWith(QStringLiteral("surfaceelements"));
        if (matches)
        {
            foundSection = true;
            break;
        }
    }
    if (!foundSection)
        return regions;

    QString countLine;
    while (!stream.atEnd() && countLine.isEmpty())
        countLine = stream.readLine().trimmed();

    bool countOk = false;
    const int storedElementCount = countLine.toInt(&countOk);
    if (!countOk || storedElementCount != expectedElementCount)
        return {};

    regions.reserve(storedElementCount);
    while (!stream.atEnd() && regions.size() < storedElementCount)
    {
        const QString line = stream.readLine().simplified();
        if (line.isEmpty())
            continue;
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.isEmpty())
            return {};

        bool regionOk = false;
        const int region = fields.front().toInt(&regionOk);
        if (!regionOk)
            return {};
        regions.push_back(region);
    }

    return regions.size() == storedElementCount ? regions : QVector<int>{};
}

struct NetgenBoundaryFacet
{
    int dimension = -1;
    int boundaryId = 0;
    QVector<int> pointIds;
};

struct NetgenBoundaryMetadata
{
    QVector<NetgenBoundaryFacet> facets;
    QMap<QPair<int, int>, QString> names;
};

NetgenBoundaryMetadata readNetgenBoundaryMetadata(const QString& filePath, int dimension)
{
    NetgenBoundaryMetadata metadata;
    // Netgen stores codimension-one boundary elements in surfaceelements* (3D)
    // or edgesegmentsgi3 (2D), and lower-dimensional regions in
    // edgesegmentsgi3/pointelements with names in bcnames/cd2names/cd3names.
    // These are native Netgen regions, not Gmsh Physical groups.
    if ((dimension != 2 && dimension != 3) || filePath.trimmed().isEmpty())
        return metadata;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return metadata;

    QTextStream stream(&file);
    auto readNonEmptyLine = [&stream]() -> QString
    {
        while (!stream.atEnd())
        {
            const QString line = stream.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
                return line;
        }
        return {};
    };

    while (!stream.atEnd())
    {
        const QString section = stream.readLine().trimmed().toLower();
        if (dimension == 3 && section.startsWith(QStringLiteral("surfaceelements")))
        {
            bool countOk = false;
            const int count = readNonEmptyLine().toInt(&countOk);
            if (!countOk || count < 0)
                continue;

            metadata.facets.reserve(metadata.facets.size() + count);
            for (int i = 0; i < count && !stream.atEnd(); ++i)
            {
                const QStringList fields = readNonEmptyLine().simplified().split(
                    QLatin1Char(' '), Qt::SkipEmptyParts);
                // Netgen .vol: surfnr bcnr domin domout np p1 ... pN [geom info]
                if (fields.size() < 6)
                    continue;

                bool boundaryOk = false;
                bool pointCountOk = false;
                const int boundaryId = fields.at(1).toInt(&boundaryOk);
                const int pointCount = fields.at(4).toInt(&pointCountOk);
                if (!boundaryOk || boundaryId <= 0 || !pointCountOk || pointCount <= 0 ||
                    pointCount > NG_SURFACE_ELEMENT_MAXPOINTS ||
                    fields.size() < 5 + pointCount)
                {
                    continue;
                }

                NetgenBoundaryFacet facet;
                facet.dimension = 2;
                facet.boundaryId = boundaryId;
                facet.pointIds.reserve(pointCount);
                bool pointsOk = true;
                for (int p = 0; p < pointCount; ++p)
                {
                    bool pointOk = false;
                    const int netgenPointId = fields.at(5 + p).toInt(&pointOk);
                    if (!pointOk || netgenPointId <= 0)
                    {
                        pointsOk = false;
                        break;
                    }
                    facet.pointIds.push_back(netgenPointId - 1);
                }
                if (pointsOk)
                    metadata.facets.push_back(std::move(facet));
            }
        }
        else if (section == QStringLiteral("edgesegmentsgi3"))
        {
            bool countOk = false;
            const int count = readNonEmptyLine().toInt(&countOk);
            if (!countOk || count < 0)
                continue;

            metadata.facets.reserve(metadata.facets.size() + count);
            for (int i = 0; i < count && !stream.atEnd(); ++i)
            {
                const QStringList fields = readNonEmptyLine().simplified().split(
                    QLatin1Char(' '), Qt::SkipEmptyParts);
                // Netgen .vol: p1 p2 trignum1 trignum2 dist1 dist2 edsi.
                // edsi is stored zero-based; Segment::GetIndex() is edsi + 1.
                if (fields.size() < 7)
                    continue;

                bool firstPointOk = false;
                bool secondPointOk = false;
                bool edgeIndexOk = false;
                const int firstPoint = fields.at(0).toInt(&firstPointOk);
                const int secondPoint = fields.at(1).toInt(&secondPointOk);
                const int boundaryId = fields.at(6).toInt(&edgeIndexOk) + 1;
                if (!firstPointOk || !secondPointOk || !edgeIndexOk ||
                    firstPoint <= 0 || secondPoint <= 0 || boundaryId <= 0)
                {
                    continue;
                }

                NetgenBoundaryFacet facet;
                facet.dimension = 1;
                facet.boundaryId = boundaryId;
                facet.pointIds = {firstPoint - 1, secondPoint - 1};
                metadata.facets.push_back(std::move(facet));
            }
        }
        else if (section == QStringLiteral("pointelements"))
        {
            bool countOk = false;
            const int count = readNonEmptyLine().toInt(&countOk);
            if (!countOk || count < 0)
                continue;

            metadata.facets.reserve(metadata.facets.size() + count);
            for (int i = 0; i < count && !stream.atEnd(); ++i)
            {
                const QStringList fields = readNonEmptyLine().simplified().split(
                    QLatin1Char(' '), Qt::SkipEmptyParts);
                if (fields.size() < 2)
                    continue;

                bool pointOk = false;
                bool boundaryOk = false;
                const int pointId = fields.at(0).toInt(&pointOk);
                const int boundaryId = fields.at(1).toInt(&boundaryOk);
                if (!pointOk || pointId <= 0 || !boundaryOk || boundaryId <= 0)
                    continue;

                NetgenBoundaryFacet facet;
                facet.dimension = 0;
                facet.boundaryId = boundaryId;
                facet.pointIds = {pointId - 1};
                metadata.facets.push_back(std::move(facet));
            }
        }
        else if (section == QStringLiteral("bcnames") ||
                 section == QStringLiteral("cd2names") ||
                 section == QStringLiteral("cd3names"))
        {
            const int regionDimension = section == QStringLiteral("bcnames")
                ? dimension - 1
                : (section == QStringLiteral("cd2names") ? dimension - 2
                                                          : dimension - 3);
            bool countOk = false;
            const int count = readNonEmptyLine().toInt(&countOk);
            if (!countOk || count < 0)
                continue;

            for (int i = 0; i < count && !stream.atEnd(); ++i)
            {
                const QString line = readNonEmptyLine();
                const int separator = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
                const QString idText = separator < 0 ? line : line.left(separator);
                bool idOk = false;
                const int id = idText.toInt(&idOk);
                if (!idOk || id <= 0)
                    continue;
                const QString name = separator < 0 ? QString() : line.mid(separator).trimmed();
                if (regionDimension >= 0)
                    metadata.names.insert(qMakePair(regionDimension, id), name);
            }
        }
    }

    return metadata;
}

void addNetgenBoundaryMetadata(vtkUnstructuredGrid* grid,
                               const NetgenBoundaryMetadata& metadata)
{
    if (!grid || metadata.facets.isEmpty())
        return;

    // [dimension, boundary id, point count, p0 ... p7]. Point ids are zero-based VTK ids.
    auto facets = vtkSmartPointer<vtkIntArray>::New();
    facets->SetName("NetgenBoundaryFacets");
    facets->SetNumberOfComponents(3 + NG_SURFACE_ELEMENT_MAXPOINTS);
    facets->SetNumberOfTuples(metadata.facets.size());
    for (vtkIdType tuple = 0; tuple < metadata.facets.size(); ++tuple)
    {
        const NetgenBoundaryFacet& facet = metadata.facets.at(static_cast<int>(tuple));
        facets->SetComponent(tuple, 0, facet.dimension);
        facets->SetComponent(tuple, 1, facet.boundaryId);
        facets->SetComponent(tuple, 2, facet.pointIds.size());
        for (int component = 0; component < NG_SURFACE_ELEMENT_MAXPOINTS; ++component)
        {
            const int pointId = component < facet.pointIds.size()
                ? facet.pointIds.at(component)
                : -1;
            facets->SetComponent(tuple, 3 + component, pointId);
        }
    }
    grid->GetFieldData()->AddArray(facets);

    auto names = vtkSmartPointer<vtkStringArray>::New();
    names->SetName("NetgenBoundaryNames");
    names->SetNumberOfComponents(3);
    for (auto it = metadata.names.constBegin(); it != metadata.names.constEnd(); ++it)
    {
        names->InsertNextValue(QString::number(it.key().first).toStdString());
        names->InsertNextValue(QString::number(it.key().second).toStdString());
        names->InsertNextValue(it.value().toStdString());
    }
    grid->GetFieldData()->AddArray(names);
}

int regionCount(const QVector<int>& regions)
{
    QSet<int> unique;
    for (int region : regions)
    {
        if (region > 0)
            unique.insert(region);
    }
    return unique.size();
}

void addDefaultArrays(vtkUnstructuredGrid* grid, const QVector<int>& elementRegions)
{
    if (!grid)
        return;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();

    auto nodeId = vtkSmartPointer<vtkIntArray>::New();
    nodeId->SetName("node_id");
    nodeId->SetNumberOfTuples(nPts);

    auto originalPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalPointIds->SetName("vtkOriginalPointIds");
    originalPointIds->SetNumberOfTuples(nPts);

    for (vtkIdType i = 0; i < nPts; ++i)
    {
        const int id = static_cast<int>(i) + 1;
        nodeId->SetValue(i, id);
        originalPointIds->SetValue(i, i);
    }

    grid->GetPointData()->AddArray(nodeId);
    grid->GetPointData()->AddArray(originalPointIds);

    auto matype = vtkSmartPointer<vtkIntArray>::New();
    matype->SetName("MATYPE");
    auto layer = vtkSmartPointer<vtkIntArray>::New();
    layer->SetName("LAYER");
    auto ketype = vtkSmartPointer<vtkIntArray>::New();
    ketype->SetName("KETYPE");
    auto elemId = vtkSmartPointer<vtkIntArray>::New();
    elemId->SetName("elem_id");
    auto srcPart = vtkSmartPointer<vtkIntArray>::New();
    srcPart->SetName("src_part");
    auto tail1 = vtkSmartPointer<vtkIntArray>::New();
    tail1->SetName("TAIL1");
    auto tail2 = vtkSmartPointer<vtkIntArray>::New();
    tail2->SetName("TAIL2");
    auto originalCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalCellIds->SetName("vtkOriginalCellIds");
    auto groupName = vtkSmartPointer<vtkStringArray>::New();
    groupName->SetName("GroupName");

    matype->SetNumberOfTuples(nCells);
    layer->SetNumberOfTuples(nCells);
    ketype->SetNumberOfTuples(nCells);
    elemId->SetNumberOfTuples(nCells);
    srcPart->SetNumberOfTuples(nCells);
    tail1->SetNumberOfTuples(nCells);
    tail2->SetNumberOfTuples(nCells);
    originalCellIds->SetNumberOfTuples(nCells);
    groupName->SetNumberOfValues(nCells);

    QSet<int> uniqueRegions;
    if (elementRegions.size() == nCells)
    {
        for (int region : elementRegions)
        {
            if (region > 0)
                uniqueRegions.insert(region);
        }
    }
    QList<int> sortedRegions = uniqueRegions.values();
    std::sort(sortedRegions.begin(), sortedRegions.end());
    QMap<int, int> regionToMatype;
    int nextMatype = kDefaultCalculationMatype;
    for (int region : sortedRegions)
        regionToMatype.insert(region, nextMatype++);

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        const int cellType = cell ? cell->GetCellType() : -1;
        int k = 0;
        switch (cellType)
        {
        case VTK_LINE:
            k = 1;
            break;
        case VTK_QUADRATIC_EDGE:
            k = 2;
            break;
        case VTK_TRIANGLE:
            k = 31;
            break;
        case VTK_QUADRATIC_TRIANGLE:
            k = 32;
            break;
        case VTK_QUAD:
            k = 3;
            break;
        case VTK_QUADRATIC_LINEAR_QUAD:
            k = 4;
            break;
        case VTK_QUADRATIC_QUAD:
            k = 5;
            break;
        case VTK_TETRA:
        case VTK_QUADRATIC_TETRA:
            k = 35;
            break;
        case VTK_HEXAHEDRON:
            k = 6;
            break;
        case VTK_WEDGE:
            k = 38;
            break;
        case VTK_PYRAMID:
            k = 39;
            break;
        default:
            k = 0;
            break;
        }

        const int sourceRegion = elementRegions.size() == nCells
            ? elementRegions.at(static_cast<int>(cid))
            : 0;
        matype->SetValue(cid, regionToMatype.value(sourceRegion, kDefaultCalculationMatype));
        layer->SetValue(cid, kDefaultConstructionLayer);
        ketype->SetValue(cid, k);
        elemId->SetValue(cid, static_cast<int>(cid) + 1);
        srcPart->SetValue(cid, sourceRegion);
        tail1->SetValue(cid, 0);
        tail2->SetValue(cid, 0);
        originalCellIds->SetValue(cid, cid);
        groupName->SetValue(cid, sourceRegion > 0
                                     ? QStringLiteral("几何区域 %1").arg(sourceRegion).toStdString()
                                     : std::string{});
    }

    grid->GetCellData()->AddArray(matype);
    grid->GetCellData()->AddArray(layer);
    grid->GetCellData()->AddArray(ketype);
    grid->GetCellData()->AddArray(elemId);
    grid->GetCellData()->AddArray(srcPart);
    grid->GetCellData()->AddArray(tail1);
    grid->GetCellData()->AddArray(tail2);
    grid->GetCellData()->AddArray(originalCellIds);
    grid->GetCellData()->AddArray(groupName);
}

vtkSmartPointer<vtkUnstructuredGrid> convertNetgenMesh(Ng_Mesh* mesh, int dimension,
                                                       const QVector<int>& elementRegions,
                                                       QString* errMsg)
{
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();

    const int np = Ng_GetNP(mesh);
    if (np <= 0)
    {
        setError(errMsg, QStringLiteral("Netgen generated no points"));
        return nullptr;
    }

    for (int i = 1; i <= np; ++i)
    {
        double p[3] = {0.0, 0.0, 0.0};
        Ng_GetPoint(mesh, i, p);
        points->InsertNextPoint(p);
    }
    grid->SetPoints(points);
    QVector<int> importedRegions;

    if (dimension == 3)
    {
        const int ne = Ng_GetNE(mesh);
        for (int i = 1; i <= ne; ++i)
        {
            int pi[NG_VOLUME_ELEMENT_MAXPOINTS] = {};
            int nodeCount = 0;
            int ketype = 0;
            const int vtkType = mapVolumeTypeToVtk(Ng_GetVolumeElement(mesh, i, pi), nodeCount, ketype);
            Q_UNUSED(ketype);
            if (vtkType < 0 || nodeCount <= 0)
                continue;

            vtkIdType ids[NG_VOLUME_ELEMENT_MAXPOINTS] = {};
            bool ok = true;
            for (int k = 0; k < nodeCount; ++k)
            {
                if (pi[k] <= 0 || pi[k] > np)
                {
                    ok = false;
                    break;
                }
                ids[k] = static_cast<vtkIdType>(pi[k] - 1);
            }
            if (ok)
            {
                grid->InsertNextCell(vtkType, nodeCount, ids);
                importedRegions.push_back(elementRegions.value(i - 1, 0));
            }
        }
    }
    else
    {
        const int nse = Ng_GetNSE(mesh);
        for (int i = 1; i <= nse; ++i)
        {
            int pi[NG_SURFACE_ELEMENT_MAXPOINTS] = {};
            int nodeCount = 0;
            int ketype = 0;
            const int vtkType =
                mapSurfaceTypeToVtk(Ng_GetSurfaceElement(mesh, i, pi), nodeCount, ketype);
            Q_UNUSED(ketype);
            if (vtkType < 0 || nodeCount <= 0)
                continue;

            vtkIdType ids[NG_SURFACE_ELEMENT_MAXPOINTS] = {};
            bool ok = true;
            for (int k = 0; k < nodeCount; ++k)
            {
                if (pi[k] <= 0 || pi[k] > np)
                {
                    ok = false;
                    break;
                }
                ids[k] = static_cast<vtkIdType>(pi[k] - 1);
            }
            if (ok)
            {
                grid->InsertNextCell(vtkType, nodeCount, ids);
                importedRegions.push_back(elementRegions.value(i - 1, 0));
            }
        }
    }

    if (grid->GetNumberOfCells() <= 0)
    {
        setError(errMsg, QStringLiteral("Netgen generated no importable cells"));
        return nullptr;
    }

    addDefaultArrays(grid, importedRegions);
    return grid;
}

double orient3d(vtkPoints* points, vtkIdType a, vtkIdType b, vtkIdType c, vtkIdType d)
{
    double pa[3] = {};
    double pb[3] = {};
    double pc[3] = {};
    double pd[3] = {};
    points->GetPoint(a, pa);
    points->GetPoint(b, pb);
    points->GetPoint(c, pc);
    points->GetPoint(d, pd);

    const double ax = pb[0] - pa[0];
    const double ay = pb[1] - pa[1];
    const double az = pb[2] - pa[2];
    const double bx = pc[0] - pa[0];
    const double by = pc[1] - pa[1];
    const double bz = pc[2] - pa[2];
    const double cx = pd[0] - pa[0];
    const double cy = pd[1] - pa[1];
    const double cz = pd[2] - pa[2];

    return ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) +
           az * (bx * cy - by * cx);
}

template <std::size_t N>
struct IdArrayHash
{
    std::size_t operator()(const std::array<vtkIdType, N>& key) const noexcept
    {
        std::size_t seed = 0;
        for (vtkIdType value : key)
        {
            const std::size_t h = std::hash<vtkIdType>{}(value);
            seed ^= h + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6U) +
                    (seed >> 2U);
        }
        return seed;
    }
};

int cellIntValue(vtkUnstructuredGrid* grid, const char* name, vtkIdType cellId,
                 int defaultValue)
{
    if (!grid || !grid->GetCellData() || !name)
        return defaultValue;

    vtkDataArray* array = grid->GetCellData()->GetArray(name);
    if (!array || cellId < 0 || cellId >= array->GetNumberOfTuples() ||
        array->GetNumberOfComponents() <= 0)
        return defaultValue;

    return static_cast<int>(std::llround(array->GetComponent(cellId, 0)));
}

void addPointIdentityArrays(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return;

    const vtkIdType nPts = grid->GetNumberOfPoints();

    auto nodeId = vtkSmartPointer<vtkIntArray>::New();
    nodeId->SetName("node_id");
    nodeId->SetNumberOfTuples(nPts);

    auto originalPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalPointIds->SetName("vtkOriginalPointIds");
    originalPointIds->SetNumberOfTuples(nPts);

    for (vtkIdType i = 0; i < nPts; ++i)
    {
        nodeId->SetValue(i, static_cast<int>(i) + 1);
        originalPointIds->SetValue(i, i);
    }

    grid->GetPointData()->RemoveArray("node_id");
    grid->GetPointData()->RemoveArray("vtkOriginalPointIds");
    grid->GetPointData()->AddArray(nodeId);
    grid->GetPointData()->AddArray(originalPointIds);
}

double minScaledJacobian(vtkPoints* points, const vtkIdType hex[8])
{
    if (!points || !hex)
        return -1.0;

    // VTK_HEXAHEDRON corner neighbours ordered so a valid unit cube has a
    // positive determinant at every corner.
    static constexpr int kNeighbour[8][3] = {
        {1, 3, 4}, {2, 0, 5}, {3, 1, 6}, {0, 2, 7},
        {7, 5, 0}, {4, 6, 1}, {5, 7, 2}, {6, 4, 3},
    };

    double minimum = std::numeric_limits<double>::infinity();
    for (int corner = 0; corner < 8; ++corner)
    {
        double p0[3] = {};
        double p1[3] = {};
        double p2[3] = {};
        double p3[3] = {};
        points->GetPoint(hex[corner], p0);
        points->GetPoint(hex[kNeighbour[corner][0]], p1);
        points->GetPoint(hex[kNeighbour[corner][1]], p2);
        points->GetPoint(hex[kNeighbour[corner][2]], p3);

        const double e0[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const double e1[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        const double e2[3] = {p3[0] - p0[0], p3[1] - p0[1], p3[2] - p0[2]};

        const double cross01[3] = {
            e0[1] * e1[2] - e0[2] * e1[1],
            e0[2] * e1[0] - e0[0] * e1[2],
            e0[0] * e1[1] - e0[1] * e1[0],
        };
        const double det = cross01[0] * e2[0] + cross01[1] * e2[1] + cross01[2] * e2[2];
        const double n0 = std::sqrt(e0[0] * e0[0] + e0[1] * e0[1] + e0[2] * e0[2]);
        const double n1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
        const double n2 = std::sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
        const double denominator = n0 * n1 * n2;
        if (!(denominator > std::numeric_limits<double>::min()))
            return -1.0;

        minimum = std::min(minimum, det / denominator);
    }

    return std::isfinite(minimum) ? minimum : -1.0;
}


struct HexConversionStats
{
    vtkIdType hexCount = 0;
    vtkIdType invalidHexCount = 0;
    double minimumScaledJacobian = 1.0;
};

vtkSmartPointer<vtkUnstructuredGrid> convertTet4ToHex8(vtkUnstructuredGrid* tetGrid,
                                                       HexConversionStats* stats,
                                                       QString* errMsg)
{
    if (stats)
        *stats = HexConversionStats{};

    if (!tetGrid || !tetGrid->GetPoints() || tetGrid->GetNumberOfCells() <= 0)
    {
        setError(errMsg, QStringLiteral("No tetra mesh to convert to Hex8"));
        return nullptr;
    }

    auto hexGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();

    double p[3] = {};
    for (vtkIdType i = 0; i < tetGrid->GetNumberOfPoints(); ++i)
    {
        tetGrid->GetPoint(i, p);
        points->InsertNextPoint(p);
    }
    hexGrid->SetPoints(points);

    using EdgeKey = std::array<vtkIdType, 2>;
    using FaceKey = std::array<vtkIdType, 3>;

    std::unordered_map<EdgeKey, vtkIdType, IdArrayHash<2>> edgeMidpoints;
    std::unordered_map<FaceKey, vtkIdType, IdArrayHash<3>> faceCenters;
    edgeMidpoints.reserve(static_cast<std::size_t>(tetGrid->GetNumberOfCells()) * 3U);
    faceCenters.reserve(static_cast<std::size_t>(tetGrid->GetNumberOfCells()) * 2U);

    auto pointAverage = [&](const std::array<vtkIdType, 4>& ids, int count) -> vtkIdType
    {
        double q[3] = {};
        double sum[3] = {};
        for (int i = 0; i < count; ++i)
        {
            tetGrid->GetPoint(ids[i], q);
            sum[0] += q[0];
            sum[1] += q[1];
            sum[2] += q[2];
        }
        const double inv = 1.0 / static_cast<double>(count);
        return points->InsertNextPoint(sum[0] * inv, sum[1] * inv, sum[2] * inv);
    };

    auto edgeMidpoint = [&](vtkIdType a, vtkIdType b) -> vtkIdType
    {
        EdgeKey key = {std::min(a, b), std::max(a, b)};
        const auto it = edgeMidpoints.find(key);
        if (it != edgeMidpoints.end())
            return it->second;

        const vtkIdType id = pointAverage({key[0], key[1], 0, 0}, 2);
        edgeMidpoints.emplace(key, id);
        return id;
    };

    auto faceCenter = [&](vtkIdType a, vtkIdType b, vtkIdType c) -> vtkIdType
    {
        FaceKey key = {a, b, c};
        std::sort(key.begin(), key.end());
        const auto it = faceCenters.find(key);
        if (it != faceCenters.end())
            return it->second;

        const vtkIdType id = pointAverage({key[0], key[1], key[2], 0}, 3);
        faceCenters.emplace(key, id);
        return id;
    };

    auto matype = vtkSmartPointer<vtkIntArray>::New();
    matype->SetName("MATYPE");
    auto layer = vtkSmartPointer<vtkIntArray>::New();
    layer->SetName("LAYER");
    auto ketype = vtkSmartPointer<vtkIntArray>::New();
    ketype->SetName("KETYPE");
    auto elemId = vtkSmartPointer<vtkIntArray>::New();
    elemId->SetName("elem_id");
    auto srcPart = vtkSmartPointer<vtkIntArray>::New();
    srcPart->SetName("src_part");
    auto tail1 = vtkSmartPointer<vtkIntArray>::New();
    tail1->SetName("TAIL1");
    auto tail2 = vtkSmartPointer<vtkIntArray>::New();
    tail2->SetName("TAIL2");
    auto originalCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalCellIds->SetName("vtkOriginalCellIds");
    auto parentTetIds = vtkSmartPointer<vtkIdTypeArray>::New();
    parentTetIds->SetName("parent_tet_id");
    auto tetChildIndex = vtkSmartPointer<vtkIntArray>::New();
    tetChildIndex->SetName("tet_child_index");
    auto scaledJacobian = vtkSmartPointer<vtkDoubleArray>::New();
    scaledJacobian->SetName("MIN_SCALED_JACOBIAN");

    vtkIdType skipped = 0;
    vtkIdType invalid = 0;
    double globalMinScaledJacobian = 1.0;
    constexpr double kValidJacobianTolerance = 1e-12;

    for (vtkIdType cid = 0; cid < tetGrid->GetNumberOfCells(); ++cid)
    {
        vtkCell* cell = tetGrid->GetCell(cid);
        if (!cell || cell->GetCellType() != VTK_TETRA || cell->GetNumberOfPoints() != 4)
        {
            ++skipped;
            continue;
        }

        std::array<vtkIdType, 4> tet = {
            cell->GetPointId(0),
            cell->GetPointId(1),
            cell->GetPointId(2),
            cell->GetPointId(3),
        };

        if (std::abs(orient3d(points, tet[0], tet[1], tet[2], tet[3])) <=
            std::numeric_limits<double>::min())
        {
            ++invalid;
            continue;
        }

        const vtkIdType center = pointAverage(tet, 4);
        const int parentMatype =
            cellIntValue(tetGrid, "MATYPE", cid, kDefaultCalculationMatype);
        const int parentLayer =
            cellIntValue(tetGrid, "LAYER", cid, kDefaultConstructionLayer);
        const int parentSrcPart = cellIntValue(tetGrid, "src_part", cid, 0);
        const int parentTail1 = cellIntValue(tetGrid, "TAIL1", cid, 0);
        const int parentTail2 = cellIntValue(tetGrid, "TAIL2", cid, 0);

        for (int corner = 0; corner < 4; ++corner)
        {
            const vtkIdType v = tet[corner];
            std::array<vtkIdType, 3> other = {};
            int oi = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (i != corner)
                    other[oi++] = tet[i];
            }

            if (orient3d(points, v, other[0], other[1], other[2]) < 0.0)
                std::swap(other[0], other[1]);

            vtkIdType hex[8] = {
                v,
                edgeMidpoint(v, other[0]),
                faceCenter(v, other[0], other[1]),
                edgeMidpoint(v, other[1]),
                edgeMidpoint(v, other[2]),
                faceCenter(v, other[0], other[2]),
                center,
                faceCenter(v, other[1], other[2]),
            };

            const double quality = minScaledJacobian(points, hex);
            if (!(quality > kValidJacobianTolerance))
            {
                ++invalid;
                continue;
            }

            const vtkIdType outputCellId =
                hexGrid->InsertNextCell(VTK_HEXAHEDRON, 8, hex);
            matype->InsertNextValue(parentMatype);
            layer->InsertNextValue(parentLayer);
            ketype->InsertNextValue(6);
            elemId->InsertNextValue(static_cast<int>(outputCellId) + 1);
            srcPart->InsertNextValue(parentSrcPart);
            tail1->InsertNextValue(parentTail1);
            tail2->InsertNextValue(parentTail2);
            originalCellIds->InsertNextValue(outputCellId);
            parentTetIds->InsertNextValue(cid);
            tetChildIndex->InsertNextValue(corner);
            scaledJacobian->InsertNextValue(quality);
            globalMinScaledJacobian = std::min(globalMinScaledJacobian, quality);
        }
    }

    if (hexGrid->GetNumberOfCells() <= 0)
    {
        setError(errMsg, QStringLiteral("Tet to Hex8 conversion found no valid Tet4 cells"));
        return nullptr;
    }

    if (skipped > 0)
    {
        setError(errMsg,
                 QStringLiteral("Tet to Hex8 requires a pure first-order Tet4 mesh; unsupported cells: %1")
                     .arg(skipped));
        return nullptr;
    }

    if (invalid > 0)
    {
        setError(errMsg,
                 QStringLiteral("Tet to Hex8 produced invalid or degenerate elements: %1. "
                                "The generated mesh was rejected.")
                     .arg(invalid));
        return nullptr;
    }

    // Preserve Netgen's native region metadata. Tet4 -> Hex8 splits native
    // boundary edges and triangles, so add the generated midpoint/face-centre
    // ids to their point sets for the child-edge/child-face mapping below.
    if (tetGrid->GetFieldData())
        hexGrid->GetFieldData()->DeepCopy(tetGrid->GetFieldData());
    if (auto* facets = vtkIntArray::SafeDownCast(
            hexGrid->GetFieldData()->GetArray("NetgenBoundaryFacets")))
    {
        for (vtkIdType tuple = 0; tuple < facets->GetNumberOfTuples(); ++tuple)
        {
            const int boundaryDimension = static_cast<int>(facets->GetComponent(tuple, 0));
            const int pointCount = static_cast<int>(facets->GetComponent(tuple, 2));
            if (boundaryDimension == 1 && pointCount == 2 &&
                facets->GetNumberOfComponents() >= 6)
            {
                const vtkIdType a = static_cast<vtkIdType>(facets->GetComponent(tuple, 3));
                const vtkIdType b = static_cast<vtkIdType>(facets->GetComponent(tuple, 4));
                const EdgeKey edge = {std::min(a, b), std::max(a, b)};
                const auto midpointIt = edgeMidpoints.find(edge);
                if (midpointIt != edgeMidpoints.end())
                {
                    facets->SetComponent(tuple, 2, 3);
                    facets->SetComponent(tuple, 5, midpointIt->second);
                }
                continue;
            }

            if (boundaryDimension != 2 || pointCount != 3 ||
                facets->GetNumberOfComponents() < 10)
                continue;

            const vtkIdType a = static_cast<vtkIdType>(facets->GetComponent(tuple, 3));
            const vtkIdType b = static_cast<vtkIdType>(facets->GetComponent(tuple, 4));
            const vtkIdType c = static_cast<vtkIdType>(facets->GetComponent(tuple, 5));
            const EdgeKey ab = {std::min(a, b), std::max(a, b)};
            const EdgeKey bc = {std::min(b, c), std::max(b, c)};
            const EdgeKey ca = {std::min(c, a), std::max(c, a)};
            FaceKey abc = {a, b, c};
            std::sort(abc.begin(), abc.end());

            const auto abIt = edgeMidpoints.find(ab);
            const auto bcIt = edgeMidpoints.find(bc);
            const auto caIt = edgeMidpoints.find(ca);
            const auto faceIt = faceCenters.find(abc);
            if (abIt == edgeMidpoints.end() || bcIt == edgeMidpoints.end() ||
                caIt == edgeMidpoints.end() || faceIt == faceCenters.end())
            {
                continue;
            }

            facets->SetComponent(tuple, 2, 7);
            facets->SetComponent(tuple, 6, abIt->second);
            facets->SetComponent(tuple, 7, bcIt->second);
            facets->SetComponent(tuple, 8, caIt->second);
            facets->SetComponent(tuple, 9, faceIt->second);
        }
    }

    addPointIdentityArrays(hexGrid);
    hexGrid->GetCellData()->AddArray(matype);
    hexGrid->GetCellData()->AddArray(layer);
    hexGrid->GetCellData()->AddArray(ketype);
    hexGrid->GetCellData()->AddArray(elemId);
    hexGrid->GetCellData()->AddArray(srcPart);
    hexGrid->GetCellData()->AddArray(tail1);
    hexGrid->GetCellData()->AddArray(tail2);
    hexGrid->GetCellData()->AddArray(originalCellIds);
    hexGrid->GetCellData()->AddArray(parentTetIds);
    hexGrid->GetCellData()->AddArray(tetChildIndex);
    hexGrid->GetCellData()->AddArray(scaledJacobian);

    if (stats)
    {
        stats->hexCount = hexGrid->GetNumberOfCells();
        stats->invalidHexCount = invalid;
        stats->minimumScaledJacobian = globalMinScaledJacobian;
    }

    return hexGrid;
}

bool writeVtk(vtkUnstructuredGrid* grid, const QString& fileName, QString* errMsg)
{
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        setError(errMsg, QStringLiteral("No generated mesh to write"));
        return false;
    }

    auto writer = vtkSmartPointer<vtkUnstructuredGridWriter>::New();
    writer->SetFileName(QFile::encodeName(fileName).constData());
    writer->SetInputData(grid);
    writer->SetFileTypeToASCII();
    if (!writer->Write())
    {
        setError(errMsg, QStringLiteral("Failed to write VTK mesh: %1").arg(fileName));
        return false;
    }
    return true;
}

QString nasCardName(int vtkType)
{
    switch (vtkType)
    {
    case VTK_TRIANGLE:
    case VTK_QUADRATIC_TRIANGLE:
        return QStringLiteral("CTRIA3");
    case VTK_QUAD:
    case VTK_QUADRATIC_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
        return QStringLiteral("CQUAD4");
    case VTK_TETRA:
    case VTK_QUADRATIC_TETRA:
        return QStringLiteral("CTETRA");
    case VTK_HEXAHEDRON:
        return QStringLiteral("CHEXA");
    case VTK_WEDGE:
        return QStringLiteral("CPENTA");
    case VTK_PYRAMID:
        return QStringLiteral("CPYRAM");
    default:
        return QString();
    }
}

int nasNodeCount(int vtkType, int available)
{
    switch (vtkType)
    {
    case VTK_TRIANGLE:
        return 3;
    case VTK_QUADRATIC_TRIANGLE:
        return std::min(available, 6);
    case VTK_QUAD:
        return 4;
    case VTK_QUADRATIC_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
        return 4;
    case VTK_TETRA:
        return 4;
    case VTK_QUADRATIC_TETRA:
        return std::min(available, 10);
    case VTK_HEXAHEDRON:
        return 8;
    case VTK_WEDGE:
        return 6;
    case VTK_PYRAMID:
        return 5;
    default:
        return 0;
    }
}

struct LocalRestrictionStats
{
    int appliedControlCount = 0;
    int restrictedPointCount = 0;
};

struct ShapeCandidate
{
    TopoDS_Shape shape;
    QVector3D center;
    double characteristicLength = 0.0;
};

TopAbs_ShapeEnum shapeTypeForDimension(int dimension)
{
    if (dimension == 1)
        return TopAbs_EDGE;
    if (dimension == 2)
        return TopAbs_FACE;
    return TopAbs_SOLID;
}

bool shapeGeometryInfo(const TopoDS_Shape& shape, int dimension,
                       QVector3D& center, double& characteristicLength)
{
    if (shape.IsNull())
        return false;

    GProp_GProps props;
    if (dimension == 1)
        BRepGProp::LinearProperties(shape, props);
    else if (dimension == 2)
        BRepGProp::SurfaceProperties(shape, props);
    else
        BRepGProp::VolumeProperties(shape, props);

    const double measure = props.Mass();
    if (!std::isfinite(measure) || measure <= 0.0)
        return false;

    const gp_Pnt p = props.CentreOfMass();
    center = QVector3D(static_cast<float>(p.X()), static_cast<float>(p.Y()),
                       static_cast<float>(p.Z()));
    if (dimension == 1)
        characteristicLength = measure;
    else if (dimension == 2)
        characteristicLength = std::sqrt(measure);
    else
        characteristicLength = std::cbrt(measure);
    return std::isfinite(characteristicLength) && characteristicLength > 0.0;
}

QVector<ShapeCandidate> collectShapeCandidates(const TopoDS_Shape& topologyShape,
                                               int dimension)
{
    QVector<ShapeCandidate> candidates;
    if (topologyShape.IsNull())
        return candidates;

    TopTools_IndexedMapOfShape shapes;
    TopExp::MapShapes(topologyShape, shapeTypeForDimension(dimension), shapes);
    candidates.reserve(shapes.Extent());
    for (Standard_Integer i = 1; i <= shapes.Extent(); ++i)
    {
        ShapeCandidate candidate;
        candidate.shape = shapes(i);
        if (shapeGeometryInfo(candidate.shape, dimension, candidate.center,
                              candidate.characteristicLength))
        {
            candidates.push_back(candidate);
        }
    }
    return candidates;
}

int bestCandidateIndex(const NetgenLocalSizeControl& control,
                       const QVector<ShapeCandidate>& candidates,
                       const QSet<int>& used)
{
    int bestIndex = -1;
    double bestScore = std::numeric_limits<double>::infinity();
    for (int i = 0; i < candidates.size(); ++i)
    {
        if (used.contains(i))
            continue;
        const ShapeCandidate& candidate = candidates.at(i);
        const QVector3D delta = candidate.center - control.center;
        const double scale = std::max(
            1.0e-12, std::max(control.characteristicLength,
                              candidate.characteristicLength));
        const double distanceScore = static_cast<double>(delta.lengthSquared()) /
                                     (scale * scale);
        const double ratio = candidate.characteristicLength /
                             std::max(1.0e-12, control.characteristicLength);
        const double measureScore = std::abs(std::log(std::max(1.0e-12, ratio)));
        const double score = distanceScore + 0.20 * measureScore;
        if (score < bestScore)
        {
            bestScore = score;
            bestIndex = i;
        }
    }

    // Refuse clearly unrelated entities instead of silently refining the wrong
    // part after topology changes.
    return bestScore <= 6.0 ? bestIndex : -1;
}

void restrictPoint(Ng_Mesh* mesh, const gp_Pnt& point, double targetSize,
                   LocalRestrictionStats& stats)
{
    double p[3] = {point.X(), point.Y(), point.Z()};
    Ng_RestrictMeshSizePoint(mesh, p, targetSize);
    ++stats.restrictedPointCount;
}

void restrictPoint(Ng_Mesh* mesh, const QVector3D& point, double targetSize,
                   LocalRestrictionStats& stats)
{
    double p[3] = {static_cast<double>(point.x()), static_cast<double>(point.y()),
                   static_cast<double>(point.z())};
    Ng_RestrictMeshSizePoint(mesh, p, targetSize);
    ++stats.restrictedPointCount;
}

void restrictEdge(Ng_Mesh* mesh, const TopoDS_Edge& edge, double targetSize,
                  LocalRestrictionStats& stats)
{
    BRepAdaptor_Curve curve(edge);
    const Standard_Real first = curve.FirstParameter();
    const Standard_Real last = curve.LastParameter();

    QVector3D center;
    double length = 0.0;
    if (!shapeGeometryInfo(edge, 1, center, length))
        return;

    const int segments = std::clamp(
        static_cast<int>(std::ceil(length / std::max(targetSize * 0.75, 1.0e-12))),
        2, 512);
    for (int i = 0; i <= segments; ++i)
    {
        const double t = static_cast<double>(i) / segments;
        const Standard_Real parameter = first + (last - first) * t;
        restrictPoint(mesh, curve.Value(parameter), targetSize, stats);
    }
}

void restrictFace(Ng_Mesh* mesh, const TopoDS_Face& face, double targetSize,
                  double tolerance, LocalRestrictionStats& stats,
                  bool includeBoundaryEdges = true)
{
    // Refine all trimming edges first, which is essential for the surface mesh.
    if (includeBoundaryEdges)
    {
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(face, TopAbs_EDGE, edges);
        for (Standard_Integer i = 1; i <= edges.Extent(); ++i)
            restrictEdge(mesh, TopoDS::Edge(edges(i)), targetSize, stats);
    }

    QVector3D center;
    double characteristic = 0.0;
    if (!shapeGeometryInfo(face, 2, center, characteristic))
        return;
    restrictPoint(mesh, center, targetSize, stats);

    Standard_Real uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    if (!std::isfinite(uMin) || !std::isfinite(uMax) ||
        !std::isfinite(vMin) || !std::isfinite(vMax) ||
        uMax <= uMin || vMax <= vMin)
    {
        return;
    }

    BRepAdaptor_Surface surface(face, Standard_True);
    const int samples = std::clamp(
        static_cast<int>(std::ceil(characteristic / std::max(targetSize, 1.0e-12))),
        2, 32);
    for (int iu = 0; iu <= samples; ++iu)
    {
        const double fu = (static_cast<double>(iu) + 0.5) / (samples + 1.0);
        const Standard_Real u = uMin + (uMax - uMin) * fu;
        for (int iv = 0; iv <= samples; ++iv)
        {
            const double fv = (static_cast<double>(iv) + 0.5) / (samples + 1.0);
            const Standard_Real v = vMin + (vMax - vMin) * fv;
            BRepClass_FaceClassifier classifier(face, gp_Pnt2d(u, v), tolerance);
            const TopAbs_State state = classifier.State();
            if (state == TopAbs_IN || state == TopAbs_ON)
                restrictPoint(mesh, surface.Value(u, v), targetSize, stats);
        }
    }
}

void restrictSolid(Ng_Mesh* mesh, const TopoDS_Solid& solid, double targetSize,
                   double tolerance, LocalRestrictionStats& stats)
{
    // A volume restriction must also refine its boundary surface, otherwise the
    // later volume mesher cannot create small elements next to a coarse skin.
    // Restrict each shared edge only once to keep large multi-face solids fast.
    TopTools_IndexedMapOfShape boundaryEdges;
    TopExp::MapShapes(solid, TopAbs_EDGE, boundaryEdges);
    for (Standard_Integer i = 1; i <= boundaryEdges.Extent(); ++i)
        restrictEdge(mesh, TopoDS::Edge(boundaryEdges(i)), targetSize, stats);

    TopTools_IndexedMapOfShape boundaryFaces;
    TopExp::MapShapes(solid, TopAbs_FACE, boundaryFaces);
    for (Standard_Integer i = 1; i <= boundaryFaces.Extent(); ++i)
        restrictFace(mesh, TopoDS::Face(boundaryFaces(i)), targetSize, tolerance, stats, false);

    Bnd_Box box;
    BRepBndLib::Add(solid, box);
    if (box.IsVoid())
        return;

    Standard_Real xmin = 0.0, ymin = 0.0, zmin = 0.0;
    Standard_Real xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = std::max(0.0, xmax - xmin);
    const double dy = std::max(0.0, ymax - ymin);
    const double dz = std::max(0.0, zmax - zmin);
    const double spacing = std::max(targetSize * 1.5, 1.0e-12);
    const int nx = std::clamp(static_cast<int>(std::ceil(dx / spacing)), 1, 12);
    const int ny = std::clamp(static_cast<int>(std::ceil(dy / spacing)), 1, 12);
    const int nz = std::clamp(static_cast<int>(std::ceil(dz / spacing)), 1, 12);

    BRepClass3d_SolidClassifier classifier(solid);
    for (int ix = 0; ix < nx; ++ix)
    {
        const double x = xmin + dx * (ix + 0.5) / nx;
        for (int iy = 0; iy < ny; ++iy)
        {
            const double y = ymin + dy * (iy + 0.5) / ny;
            for (int iz = 0; iz < nz; ++iz)
            {
                const double z = zmin + dz * (iz + 0.5) / nz;
                const gp_Pnt point(x, y, z);
                classifier.Perform(point, tolerance);
                const TopAbs_State state = classifier.State();
                if (state == TopAbs_IN || state == TopAbs_ON)
                    restrictPoint(mesh, point, targetSize, stats);
            }
        }
    }
}

LocalRestrictionStats applyLocalSizeControls(Ng_Mesh* mesh,
                                             const TopoDS_Shape& topologyShape,
                                             const NetgenMeshingRequest& request)
{
    LocalRestrictionStats stats;
    if (!mesh || topologyShape.IsNull())
        return stats;

    const double tolerance = std::max(request.tolerance, Precision::Confusion());
    QVector<ShapeCandidate> candidates[4];
    QSet<int> usedCandidates[4];
    for (int dimension = 1; dimension <= 3; ++dimension)
        candidates[dimension] = collectShapeCandidates(topologyShape, dimension);

    for (const NetgenLocalSizeControl& control : request.localSizeControls)
    {
        if (control.dimension < 1 || control.dimension > 3 ||
            !std::isfinite(control.targetSize) || control.targetSize <= 0.0 ||
            control.targetSize >= request.lenMax * 0.999999)
        {
            continue;
        }

        const int candidateIndex = bestCandidateIndex(
            control, candidates[control.dimension], usedCandidates[control.dimension]);
        if (candidateIndex < 0)
            continue;

        const TopoDS_Shape& shape = candidates[control.dimension][candidateIndex].shape;
        usedCandidates[control.dimension].insert(candidateIndex);
        const int pointsBefore = stats.restrictedPointCount;
        if (control.dimension == 1)
            restrictEdge(mesh, TopoDS::Edge(shape), control.targetSize, stats);
        else if (control.dimension == 2)
            restrictFace(mesh, TopoDS::Face(shape), control.targetSize, tolerance, stats);
        else
            restrictSolid(mesh, TopoDS::Solid(shape), control.targetSize, tolerance, stats);

        if (stats.restrictedPointCount > pointsBefore)
            ++stats.appliedControlCount;
    }
    return stats;
}

} // namespace

bool NetgenMeshGenerator::generate(const NetgenMeshingRequest& request, NetgenMeshingResult* result,
                                   QString* errMsg)
{
    if (errMsg)
        errMsg->clear();
    if (result)
        *result = NetgenMeshingResult{};

    if (request.inputGeometryPath.trimmed().isEmpty() ||
        !QFileInfo::exists(request.inputGeometryPath))
    {
        setError(errMsg, QStringLiteral("Input geometry does not exist: %1").arg(request.inputGeometryPath));
        return false;
    }

    ensureNetgenInitialized();

    QTemporaryFile conformalBrep;
    ConformalGeometryInfo conformalInfo;
    if (!prepareConformalGeometry(request, conformalBrep, conformalInfo, errMsg))
        return false;

    Ng_OCC_Geometry* geom = loadOccGeometry(conformalInfo.geometryPath, errMsg);
    if (!geom)
    {
        if (errMsg && errMsg->isEmpty())
            *errMsg = QStringLiteral("Failed to load OCC geometry: %1").arg(conformalInfo.geometryPath);
        return false;
    }

    Ng_Mesh* mesh = Ng_NewMesh();
    if (!mesh)
    {
        setError(errMsg, QStringLiteral("Failed to create Netgen mesh"));
        return false;
    }

    bool ok = false;
    auto cleanup = [&]
    {
        Q_UNUSED(mesh);
        Q_UNUSED(geom);
        // Netgen 6.2.2606 nglib can corrupt the heap when Ng_DeleteMesh is
        // called after OCC meshing with this MSVC/OCC build. The upstream OCC
        // nglib example exits without deleting the mesh or geometry, so keep
        // these objects alive for now instead of crashing the host application.
    };

    Ng_Meshing_Parameters mp;
    mp.maxh = std::max(request.lenMin, request.lenMax);
    mp.minh = std::max(0.0, request.lenMin);
    mp.fineness = std::clamp(request.fineness, 0.0, 1.0);
    mp.second_order = request.elemType == kElemTypeHex8FromTet ? 0 : (request.elementOrder > 1 ? 1 : 0);
    mp.quad_dominated = request.elemType == 2 ? 1 : 0;
    mp.optsteps_2d = std::max(0, request.smoothing);
    mp.optsteps_3d = std::max(0, request.smoothing);
    mp.uselocalh = 1;
    mp.check_overlap = request.fixShape ? 1 : 0;
    mp.minedgelenenable = request.tolerance > 0.0 ? 1 : 0;
    mp.minedgelen = request.tolerance;

    Ng_Result ng = Ng_OCC_SetLocalMeshSize(geom, mesh, &mp);
    if (ng != NG_OK)
    {
        setError(errMsg, QStringLiteral("Netgen local mesh size failed: %1").arg(resultText(ng)));
        cleanup();
        return false;
    }

    const LocalRestrictionStats localRestrictionStats =
        applyLocalSizeControls(mesh, conformalInfo.controlShape, request);

    ng = Ng_OCC_GenerateEdgeMesh(geom, mesh, &mp);
    if (ng != NG_OK)
    {
        setError(errMsg, QStringLiteral("Netgen edge mesh failed: %1").arg(resultText(ng)));
        cleanup();
        return false;
    }

    ng = Ng_OCC_GenerateSurfaceMesh(geom, mesh, &mp);
    if (ng != NG_OK)
    {
        setError(errMsg, QStringLiteral("Netgen surface mesh failed: %1").arg(resultText(ng)));
        cleanup();
        return false;
    }

    if (request.dimension == 3)
    {
        ng = Ng_GenerateVolumeMesh(mesh, &mp);
        if (ng != NG_OK)
        {
            setError(errMsg, QStringLiteral("Netgen volume mesh failed: %1").arg(resultText(ng)));
            cleanup();
            return false;
        }
    }

    if (mp.second_order)
        Ng_OCC_Generate_SecondOrder(geom, mesh);

    QString metadataMeshPath = request.volPath.trimmed();
    QTemporaryFile temporaryMesh;
    if (metadataMeshPath.isEmpty())
    {
        temporaryMesh.setFileTemplate(
            QDir::tempPath() + QStringLiteral("/cae_netgen_regions_XXXXXX.vol"));
        if (temporaryMesh.open())
        {
            metadataMeshPath = temporaryMesh.fileName();
            temporaryMesh.close();
        }
    }
    if (!metadataMeshPath.isEmpty())
        Ng_SaveMesh(mesh, QFile::encodeName(metadataMeshPath).constData());

    const int sourceElementCount = request.dimension == 3 ? Ng_GetNE(mesh) : Ng_GetNSE(mesh);
    const QVector<int> elementRegions =
        readNetgenElementRegions(metadataMeshPath, request.dimension, sourceElementCount);
    const NetgenBoundaryMetadata boundaryMetadata =
        readNetgenBoundaryMetadata(metadataMeshPath, request.dimension);

    NetgenMeshingResult localResult;
    localResult.sourceRegionCount = conformalInfo.sourceRegionCount;
    localResult.conformalRegionCount = conformalInfo.processed
        ? conformalInfo.outputRegionCount
        : conformalInfo.sourceRegionCount;
    localResult.sharedInterfaceCount = conformalInfo.sharedInterfaceCount;
    localResult.conformalized = conformalInfo.processed;
    localResult.appliedLocalControlCount = localRestrictionStats.appliedControlCount;
    localResult.localRestrictionPointCount = localRestrictionStats.restrictedPointCount;
    localResult.pointCount = Ng_GetNP(mesh);
    localResult.surfaceElementCount = Ng_GetNSE(mesh);
    localResult.volumeElementCount = Ng_GetNE(mesh);
    localResult.regionCount = regionCount(elementRegions);
    if (localResult.regionCount > kMaxSolverMatype - kDefaultCalculationMatype + 1)
    {
        setError(errMsg, QStringLiteral("几何分区过多，自动生成的材料编号将超过 300"));
        cleanup();
        return false;
    }
    localResult.grid = convertNetgenMesh(mesh, request.dimension, elementRegions, errMsg);
    if (!localResult.grid)
    {
        cleanup();
        return false;
    }
    addNetgenBoundaryMetadata(localResult.grid, boundaryMetadata);

    if (!request.exportVol && !metadataMeshPath.isEmpty())
        QFile::remove(metadataMeshPath);

    if (request.dimension == 3 && request.elemType == kElemTypeHex8FromTet)
    {
        const int sourceBoundaryTriangleCount = localResult.surfaceElementCount;
        HexConversionStats conversionStats;
        localResult.grid = convertTet4ToHex8(localResult.grid, &conversionStats, errMsg);
        if (!localResult.grid)
        {
            cleanup();
            return false;
        }
        localResult.pointCount = static_cast<int>(localResult.grid->GetNumberOfPoints());
        // A first-order boundary triangle is split into three boundary quads by
        // the Tet4 -> Hex8 construction. No boundary cells are duplicated in
        // the volume grid; the display layer extracts them from Hex8 owners.
        const long long boundaryQuadCount =
            static_cast<long long>(sourceBoundaryTriangleCount) * 3LL;
        localResult.surfaceElementCount = static_cast<int>(std::min<long long>(
            boundaryQuadCount, std::numeric_limits<int>::max()));
        localResult.volumeElementCount = static_cast<int>(conversionStats.hexCount);
        localResult.invalidElementCount = static_cast<int>(conversionStats.invalidHexCount);
        localResult.minScaledJacobian = conversionStats.minimumScaledJacobian;
    }

    if (request.exportVtk && !request.vtkPath.trimmed().isEmpty())
    {
        if (!writeVtk(localResult.grid, request.vtkPath, errMsg))
        {
            cleanup();
            return false;
        }
    }

    if (request.exportNas && !request.nasPath.trimmed().isEmpty())
    {
        if (!writeNastran(localResult.grid, request.nasPath, errMsg))
        {
            cleanup();
            return false;
        }
    }

    if (result)
        *result = localResult;

    ok = true;
    cleanup();
    return ok;
}

bool NetgenMeshGenerator::writeNastran(vtkUnstructuredGrid* grid, const QString& fileName,
                                       QString* errMsg)
{
    if (!grid || !grid->GetPoints() || grid->GetNumberOfCells() <= 0)
    {
        setError(errMsg, QStringLiteral("No generated mesh to write"));
        return false;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        setError(errMsg, QStringLiteral("Failed to open Nastran output: %1").arg(fileName));
        return false;
    }

    QTextStream out(&file);
    out.setLocale(QLocale::c());
    out << "$ Generated by Netgen\n";
    out << "BEGIN BULK\n";

    double p[3] = {};
    for (vtkIdType i = 0; i < grid->GetNumberOfPoints(); ++i)
    {
        grid->GetPoint(i, p);
        out << "GRID," << (i + 1) << ",," << p[0] << "," << p[1] << "," << p[2] << "\n";
    }

    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;

        const QString card = nasCardName(cell->GetCellType());
        const int n = nasNodeCount(cell->GetCellType(), static_cast<int>(cell->GetNumberOfPoints()));
        if (card.isEmpty() || n <= 0)
            continue;

        out << card << "," << (cid + 1) << ",1";
        for (int i = 0; i < n; ++i)
            out << "," << (cell->GetPointId(i) + 1);
        out << "\n";
    }

    out << "ENDDATA\n";
    return true;
}
