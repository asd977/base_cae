#include "SmeshMeshGenerator.h"
#include <QDateTime>
#include <SmeshShapeIndexMap.h>
#include <PartitionFaceOrder.h>
#include "core/SmeshOccPartitioner.h"

#include <BOPAlgo_Builder.hxx>
#include <SurfaceDomainData.h>
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
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Pnt2d.hxx>
#include <TopoDS_Shape.hxx>

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
#include <vtkUnstructuredGridReader.h>
#include "tools/SMESHBridgeApi.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QTemporaryDir>
#include <QProcessEnvironment>

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

struct ConformalGeometryInfo
{
    QString geometryPath;
    // Keep the pre-fragment OCC shape for matching the UI entity IDs. Local
    // size restrictions are spatial, so sampling the original entity remains
    // valid after the material regions are made conformal.
    TopoDS_Shape controlShape;
    TopoDS_Shape meshShape;
    QHash<int, QVector<int>> controlImages[4];
    TopoDS_Shape partitionShape;
    QHash<int, QVector<int>> partitionImages[4];
    QHash<int,int> partitionOwners;
    QVector<int> solidDomains;
    int sourceRegionCount = 0;
    int outputRegionCount = 0;
    int sharedInterfaceCount = 0;
    bool processed = false;
    // For free CAD shells (terrain), several geometric faces belong to one
    // calculation surface domain. Indexed by OCC face index minus one.
    QVector<int> faceDomains;
    SurfaceDomainData::Definition domainData;
    double smallestFaceArea = std::numeric_limits<double>::infinity();
    double smallestEdgeLength = std::numeric_limits<double>::infinity();
    int verySmallFaces = 0;
    int veryShortEdges = 0;
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

bool prepareConformalGeometry(const SmeshMeshingRequest& request,
                              QTemporaryFile& temporaryBrep,
                              ConformalGeometryInfo& info,
                              QString* errMsg)
{
    info.geometryPath = request.inputGeometryPath;

    const QFileInfo inputInfo(request.inputGeometryPath);
    const QString suffix = inputInfo.suffix().toLower();
    const bool isStep = suffix == QStringLiteral("step") || suffix == QStringLiteral("stp");
    const bool isBrep = suffix == QStringLiteral("brep");
    if (!isStep && !isBrep)
        return true;

    TopoDS_Shape importedShape;
    if (isStep)
    {
        if (!loadStepShape(request.inputGeometryPath, importedShape, errMsg))
            return false;
    }
    else
    {
        BRep_Builder brepBuilder;
        if (!BRepTools::Read(importedShape,
                             QFile::encodeName(inputInfo.absoluteFilePath()).constData(),
                             brepBuilder) || importedShape.IsNull())
        {
            setError(errMsg, QStringLiteral("无法读取用于二维网格的 OCC BREP：%1")
                                 .arg(request.inputGeometryPath));
            return false;
        }
    }
    info.controlShape = importedShape;
    info.meshShape = importedShape;

    const TopAbs_ShapeEnum regionType = request.dimension == 3 ? TopAbs_SOLID : TopAbs_FACE;
    TopTools_IndexedMapOfShape sourceRegions;
    TopExp::MapShapes(importedShape, regionType, sourceRegions);
    info.sourceRegionCount = sourceRegions.Extent();
    info.outputRegionCount = sourceRegions.Extent();
    if (request.dimension == 2)
    {
        QVector<int> domains(sourceRegions.Extent(), 0);
        int nextDomain = 0;
        bool hasSurfaceShell = false;
        // Do not merge unrelated loose faces or the faces of solid material
        // regions. A free shell is an explicit surface object container.
        for (TopExp_Explorer shell(importedShape, TopAbs_SHELL, TopAbs_SOLID);
             shell.More(); shell.Next())
        {
            hasSurfaceShell = true;
            const int domain = ++nextDomain;
            TopTools_IndexedMapOfShape shellFaces;
            TopExp::MapShapes(shell.Current(), TopAbs_FACE, shellFaces);
            for (int i = 1; i <= shellFaces.Extent(); ++i)
            {
                const int face = sourceRegions.FindIndex(shellFaces(i));
                if (face > 0) domains[face - 1] = domain;
            }
        }
        if (hasSurfaceShell)
        {
            for (int& domain : domains) if (!domain) domain = ++nextDomain;
            info.faceDomains = domains;
            info.sourceRegionCount = info.outputRegionCount = nextDomain;
        }
        QString domainError;
        if(!SurfaceDomainData::read(request.inputGeometryPath,sourceRegions.Extent(),info.domainData,domainError))
        {setError(errMsg,domainError);return false;}
        if(!info.domainData.faceDomains.isEmpty())
        {
            info.faceDomains=info.domainData.faceDomains;
            info.sourceRegionCount=info.outputRegionCount=info.domainData.materialGroups.size();
        }
        // Diagnostics only: legitimate narrow soil layers must never be silently
        // deleted just because their size is below the global mesh minimum.
        for (Standard_Integer i = 1; i <= sourceRegions.Extent(); ++i)
        {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(sourceRegions(i), props);
            const double area = std::abs(props.Mass());
            if (area > 0.0 && std::isfinite(area))
            {
                info.smallestFaceArea = std::min(info.smallestFaceArea, area);
                if (area < request.lenMin * request.lenMin * 0.01)
                    ++info.verySmallFaces;
            }
        }
        TopTools_IndexedMapOfShape allEdges;
        TopExp::MapShapes(importedShape, TopAbs_EDGE, allEdges);
        for (Standard_Integer i = 1; i <= allEdges.Extent(); ++i)
        {
            GProp_GProps props;
            BRepGProp::LinearProperties(allEdges(i), props);
            const double length = std::abs(props.Mass());
            if (length > 0.0 && std::isfinite(length))
            {
                info.smallestEdgeLength = std::min(info.smallestEdgeLength, length);
                if (length < request.lenMin * 0.1)
                    ++info.veryShortEdges;
            }
        }
    }

    if (!request.conformalizeRegions)
    {
        if (request.progress) request.progress(QStringLiteral("未启用统一共享拓扑，使用源几何已有拓扑。"));
        return true;
    }

    // 单一区域没有材料界面，不需要做布尔碎片化。
    if (info.sourceRegionCount <= 1)
    {
        if (request.progress) request.progress(QStringLiteral("只有一个几何区域，无需执行跨区域 Partition。"));
        return true;
    }

    TopTools_ListOfShape arguments;
    for (Standard_Integer i = 1; i <= sourceRegions.Extent(); ++i)
        arguments.Append(sourceRegions(i));

    BOPAlgo_Builder builder;
    builder.SetArguments(arguments);
    builder.SetRunParallel(Standard_False);
    builder.SetNonDestructive(Standard_True);
    builder.SetFuzzyValue(std::max(request.tolerance, Precision::Confusion()));
    if (request.progress)
        request.progress(QStringLiteral("执行 OCC Partition：对 %1 个几何区域建立共享拓扑。").arg(sourceRegions.Extent()));
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

    if (!info.faceDomains.isEmpty())
    {
        QVector<int> resultDomains(conformalRegions.Extent(), 0);
        bool conflict = false;
        auto assignDomain = [&](const TopoDS_Shape& descendant, int domain)
        {
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(descendant, TopAbs_FACE, faces);
            for (int i = 1; i <= faces.Extent(); ++i)
            {
                const int index = conformalRegions.FindIndex(faces(i));
                if (!index) continue;
                int& assigned = resultDomains[index - 1];
                if (assigned && assigned != domain) conflict = true;
                assigned = domain;
            }
        };
        for (int i = 1; i <= sourceRegions.Extent(); ++i)
        {
            assignDomain(sourceRegions(i), info.faceDomains[i - 1]);
            const TopTools_ListOfShape& modified = builder.Modified(sourceRegions(i));
            for (TopTools_ListIteratorOfListOfShape item(modified); item.More(); item.Next())
                assignDomain(item.Value(), info.faceDomains[i - 1]);
        }
        if (conflict || std::any_of(resultDomains.cbegin(), resultDomains.cend(),
                                    [](int domain) {return domain <= 0;}))
        {
            setError(errMsg, QStringLiteral("地形面与其他面域存在重叠，或共形后面域归属无法确认。请检查面域范围。"));
            return false;
        }
        info.faceDomains = resultDomains;
    }

    const TopAbs_ShapeEnum interfaceType =
        request.dimension == 3 ? TopAbs_FACE : TopAbs_EDGE;
    TopTools_IndexedDataMapOfShapeListOfShape interfaceOwners;
    TopExp::MapShapesAndAncestors(compound, interfaceType, regionType, interfaceOwners);
    for (Standard_Integer i = 1; i <= interfaceOwners.Extent(); ++i)
    {
        const auto& owners=interfaceOwners.FindFromIndex(i);
        if(owners.Extent()<2)continue;
        if(info.faceDomains.isEmpty()){++info.sharedInterfaceCount;continue;}
        QSet<int> domains;
        for(TopTools_ListIteratorOfListOfShape owner(owners);owner.More();owner.Next())
        {const int face=conformalRegions.FindIndex(owner.Value());if(face>0)domains.insert(info.faceDomains[face-1]);}
        if(domains.size()>1)++info.sharedInterfaceCount;
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

    info.meshShape = compound;
    for (int dim = 1; dim <= request.dimension; ++dim) {
        const auto type = dim == 1 ? TopAbs_EDGE : dim == 2 ? TopAbs_FACE : TopAbs_SOLID;
        TopTools_IndexedMapOfShape original, output;
        TopExp::MapShapes(importedShape, type, original);
        TopExp::MapShapes(compound, type, output);
        for (int i = 1; i <= original.Extent(); ++i) {
            auto& images = info.controlImages[dim][i];
            auto add = [&](const TopoDS_Shape& shape) {
                TopTools_IndexedMapOfShape descendants; TopExp::MapShapes(shape, type, descendants);
                for (int j = 1; j <= descendants.Extent(); ++j) {
                    int id = output.FindIndex(descendants(j));
                    if (id > 0 && !images.contains(id)) images.push_back(id);
                }
            };
            add(original(i));
            for (TopTools_ListIteratorOfListOfShape it(builder.Modified(original(i))); it.More(); it.Next()) add(it.Value());
        }
    }
    info.geometryPath = brepPath;
    if (info.faceDomains.isEmpty()) info.outputRegionCount = conformalRegions.Extent();
    else
    {
        QSet<int> domains;
        for (int domain : info.faceDomains) domains.insert(domain);
        info.outputRegionCount = domains.size();
    }
    info.processed = true;
    if (request.progress)
        request.progress(QStringLiteral("OCC Partition 完成：%1 个区域，共享界面 %2 个。")
            .arg(info.outputRegionCount).arg(info.sharedInterfaceCount));
    return true;
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

void addDefaultArrays(vtkUnstructuredGrid* grid, const QVector<int>& elementRegions,
                      const SurfaceDomainData::Definition& domainData)
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
    QMap<int,int> materialToMatype;
    for (int region : sortedRegions)
    {
        const int material=domainData.materialGroups.value(region,region);
        if(!materialToMatype.contains(material))materialToMatype[material]=nextMatype++;
        regionToMatype.insert(region,materialToMatype.value(material));
    }

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
                                     ? domainData.names.value(sourceRegion,QStringLiteral("几何区域 %1").arg(sourceRegion)).toStdString()
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

    // Preserve SMESH's native region metadata. Tet4 -> Hex8 splits native
    // boundary edges and triangles, so add the generated midpoint/face-centre
    // ids to their point sets for the child-edge/child-face mapping below.
    if (tetGrid->GetFieldData())
        hexGrid->GetFieldData()->DeepCopy(tetGrid->GetFieldData());
    if (auto* facets = vtkIntArray::SafeDownCast(
            hexGrid->GetFieldData()->GetArray("SmeshBoundaryFacets")))
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
        return QStringLiteral("CTRIA3");
    case VTK_QUADRATIC_TRIANGLE:
        return QStringLiteral("CTRIA6");
    case VTK_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
        return QStringLiteral("CQUAD4");
    case VTK_QUADRATIC_QUAD:
        return QStringLiteral("CQUAD8");
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
        return std::min(available, 8);
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

TopAbs_ShapeEnum shapeTypeForDimension(int dimension)
{
    return dimension == 1 ? TopAbs_EDGE : dimension == 2 ? TopAbs_FACE : TopAbs_SOLID;
}

bool prepareGeometryScope(SmeshMeshingRequest& request, const QDir& work,
                          QVector<int>& originalRegionIds, QMap<int,QMap<int,int>>& scopedIndices, QString* error)
{
    if (request.allGeometry) return true;
    if (request.geometryScope.isEmpty()) {
        setError(error, QStringLiteral("请至少选择一个网格几何对象。")); return false;
    }
    TopoDS_Shape original;
    if (QFileInfo(request.inputGeometryPath).suffix().compare("brep", Qt::CaseInsensitive) == 0) {
        BRep_Builder builder;
        if (!BRepTools::Read(original, QFile::encodeName(request.inputGeometryPath).constData(), builder)) {
            setError(error, QStringLiteral("无法读取网格主几何。")); return false;
        }
    } else if (!loadStepShape(request.inputGeometryPath, original, error)) return false;
    TopTools_IndexedMapOfShape originalRegions;
    TopExp::MapShapes(original, shapeTypeForDimension(request.dimension), originalRegions);
    TopoDS_Compound scoped;
    BRep_Builder builder; builder.MakeCompound(scoped);
    QSet<int> used;
    for (const auto& entity : request.geometryScope) {
        const int id = entity.entityId;
        if (entity.dimension != request.dimension || id <= 0 || id > originalRegions.Extent()
            || used.contains(id)) {
            setError(error, QStringLiteral("所选网格几何编号无效或重复，请重新选择。")); return false;
        }
        used.insert(id);
        builder.Add(scoped, originalRegions(id));
        originalRegionIds.append(id);
    }
    for (int dim = 1; dim <= request.dimension; ++dim) {
        TopTools_IndexedMapOfShape full, selected;
        TopExp::MapShapes(original, shapeTypeForDimension(dim), full);
        TopExp::MapShapes(scoped, shapeTypeForDimension(dim), selected);
        for (int id = 1; id <= full.Extent(); ++id) {
            const int local = selected.FindIndex(full(id));
            if (local > 0) scopedIndices[dim][id] = local;
        }
    }
    const QString path = work.filePath("selected_geometry.brep");
    if (!BRepTools::Write(scoped, QFile::encodeName(path).constData())) {
        setError(error, QStringLiteral("无法准备所选网格几何。")); return false;
    }
    if (request.dimension == 2) {
        SurfaceDomainData::Definition sourceDomains, selected;
        QString message;
        if (!SurfaceDomainData::read(request.inputGeometryPath, originalRegions.Extent(), sourceDomains, message)) {
            setError(error, message); return false;
        }
        for (int index : originalRegionIds) {
            const int domain = sourceDomains.faceDomains.isEmpty() ? index : sourceDomains.faceDomains[index-1];
            selected.faceDomains.append(domain);
            selected.materialGroups[domain] = sourceDomains.materialGroups.value(domain, domain);
            selected.names[domain] = sourceDomains.names.value(domain);
        }
        if (!SurfaceDomainData::write(path, selected, message)) { setError(error, message); return false; }
    }
    request.inputGeometryPath = path;
    if (request.progress)
        request.progress(QStringLiteral("本次剖分几何：选中 %1 / %2 个区域。").arg(used.size()).arg(originalRegions.Extent()));
    return true;
}

} // namespace

bool SmeshMeshGenerator::writeNastran(vtkUnstructuredGrid* grid, const QString& fileName,
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
    out.setRealNumberPrecision(17);
    out << "$ Generated by SMESH\n";
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
        for (int i = 0; i < n; ++i) {
            if (i >= 6 && (i - 6) % 8 == 0) out << "\n+";
            out << "," << (cell->GetPointId(i) + 1);
        }
        out << "\n";
    }

    out << "ENDDATA\n";
    return true;
}

bool SmeshMeshGenerator::generate(const SmeshMeshingRequest& originalRequest, SmeshMeshingResult* result,
                                 QString* errMsg)
{
    SmeshMeshingRequest request = originalRequest;
    request.elemType = elementTypeForDimension(request.dimension, request.elemType);
    static std::mutex generationMutex;
    const std::lock_guard<std::mutex> guard(generationMutex);
    if (errMsg) errMsg->clear();
    if (result) *result = {};
    try {
        if (!QFileInfo::exists(request.inputGeometryPath)) {
            setError(errMsg, QStringLiteral("几何文件不存在：%1").arg(request.inputGeometryPath)); return false;
        }
        if ((request.dimension != 2 && request.dimension != 3) ||
            !std::isfinite(request.lenMin) || !std::isfinite(request.lenMax) ||
            request.lenMin < 0 || request.lenMax <= 0 || request.lenMin > request.lenMax ||
            !std::isfinite(request.fineness) || request.fineness < 0 || request.fineness > 1 ||
            request.nativeFineness < -1 || request.nativeFineness > 5 ||
            !std::isfinite(request.growthRate) || request.growthRate <= 0 || request.growthRate > 1 ||
            !std::isfinite(request.segmentsPerEdge) || request.segmentsPerEdge <= 0 ||
            !std::isfinite(request.segmentsPerRadius) || request.segmentsPerRadius <= 0 ||
            !std::isfinite(request.tolerance) || request.tolerance <= 0 ||
            (request.elementOrder != 1 && request.elementOrder != 2) ||
            (request.dimension == 2 && request.elemType != 1 && request.elemType != 2) ||
            (request.dimension == 3 && request.elemType != 3 && request.elemType != kElemTypeHex8FromTet) ||
            (request.elemType == kElemTypeHex8FromTet && request.elementOrder != 1)) {
            setError(errMsg, QStringLiteral("网格维度、单元类型或尺寸参数无效。")); return false;
        }
        if (QFileInfo::exists(request.inputGeometryPath + QStringLiteral(".shape-index.json"))) {
            const auto map = SmeshShapeIndexMap::load(request.inputGeometryPath);
            if (!map.valid()) { setError(errMsg,map.error());return false; }
            auto translate = [&](int dim,int& id) {
                id = map.requireIndex(dim,id);
                return id > 0;
            };
            for (auto& scope : request.geometryScope)
                if (!translate(scope.dimension,scope.entityId)) { setError(errMsg,QStringLiteral("所选几何不在导出的拓扑索引中。"));return false; }
            for (auto& control : request.localSizeControls)
                if (!translate(control.dimension,control.entityId)) { setError(errMsg,QStringLiteral("局部尺寸的几何编号已失效。"));return false; }
            for (auto& sub : request.workflow.submeshes)
                for (auto& entity : sub.entities)
                    if (!translate(entity.dimension,entity.entityId)) { setError(errMsg,QStringLiteral("子网格的几何编号已失效。"));return false; }
            for (auto& sweep : request.workflow.sweeps) {
                if (sweep.partitionRegion) continue;
                if (!translate(3,sweep.volume.entityId) || !translate(2,sweep.source.entityId)
                    || !translate(2,sweep.target.entityId)) { setError(errMsg,QStringLiteral("扫掠几何编号已失效。"));return false; }
            }
            for (auto& rule : request.workflow.partitions)
                if (rule.targetVolumeUiId > 0 && !translate(3,rule.target.entityId)) { setError(errMsg,QStringLiteral("分块目标几何编号已失效。"));return false; }
        }
        QTemporaryDir work;
        if (!work.isValid()) { setError(errMsg, QStringLiteral("无法创建网格临时目录。")); return false; }
        QVector<int> scopedOriginalRegions;
        QMap<int,QMap<int,int>> scopedIndices;
        if (!prepareGeometryScope(request, QDir(work.path()), scopedOriginalRegions, scopedIndices, errMsg)) return false;
        QTemporaryFile conformalBrep;
        ConformalGeometryInfo info;
        SmeshMeshingRequest prepared=request;
        SmeshOccPartitioner::Result partition;
        const bool partitioned=std::any_of(request.workflow.partitions.cbegin(),request.workflow.partitions.cend(),
            [](const SmeshPartitionSettings& rule){return rule.enabled;});
        if(partitioned) {
            if(request.dimension!=3){setError(errMsg,QStringLiteral("扫掠分块只支持三维体网格。"));return false;}
            SmeshOccPartitioner::Settings cutting;cutting.inputStepPath=request.inputGeometryPath;
            cutting.outputBrepPath=work.filePath("partition.brep");cutting.tolerance=request.tolerance;
            cutting.rules=request.workflow.partitions;
            // Resolve UI IDs against the exported model before binding cutters.
            TopoDS_Shape original;
            if(QFileInfo(request.inputGeometryPath).suffix().compare("brep",Qt::CaseInsensitive)==0) {
                BRep_Builder builder;if(!BRepTools::Read(original,QFile::encodeName(request.inputGeometryPath).constData(),builder)) {
                    setError(errMsg,QStringLiteral("无法读取扫掠分块的原始几何。"));return false;
                }
            } else if(!loadStepShape(request.inputGeometryPath,original,errMsg))return false;
            TopTools_IndexedMapOfShape originals;TopExp::MapShapes(original,TopAbs_SOLID,originals);
            for(auto& rule:cutting.rules)if(rule.enabled&&rule.targetVolumeUiId>0) {
                const int id = request.allGeometry ? rule.target.entityId
                    : scopedIndices.value(3).value(rule.target.entityId);
                if(id <= 0 || id > originals.Extent()) {
                    setError(errMsg,QStringLiteral("分块目标实体编号已失效，请重新选择。"));return false;
                }
                rule.targetVolumeUiId=id;
            }
            partition=SmeshOccPartitioner::partition(cutting,request.progress);
            prepared.inputGeometryPath=partition.brepPath;
            prepared.conformalizeRegions=request.conformalizeRegions;
        }
        if (!prepareConformalGeometry(prepared, conformalBrep, info, errMsg)) return false;
        if(partitioned) {
            info.partitionShape=info.controlShape;
            const bool wasProcessed=info.processed;
            for(int dim=1;dim<=3;++dim) {
                TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(info.partitionShape,shapeTypeForDimension(dim),shapes);
                for(int i=1;i<=shapes.Extent();++i)
                    info.partitionImages[dim][i]=wasProcessed?info.controlImages[dim].value(i):QVector<int>{i};
                info.controlImages[dim].clear();
                for(auto it=partition.controlImages[dim].cbegin();it!=partition.controlImages[dim].cend();++it) {
                    auto& images=info.controlImages[dim][it.key()];
                    for(int child:it.value())for(int finalId:info.partitionImages[dim].value(child))
                        if(!images.contains(finalId))images.push_back(finalId);
                }
            }
            TopTools_IndexedMapOfShape solids;TopExp::MapShapes(info.meshShape,TopAbs_SOLID,solids);
            info.solidDomains.fill(0,solids.Extent());
            for(const auto& region:partition.regions) {
                info.partitionOwners[region.index]=region.originalSolidId;
                for(int finalId:info.partitionImages[3].value(region.index)) {
                    int& owner=info.solidDomains[finalId-1];
                    if(owner&&owner!=region.originalSolidId){setError(errMsg,QStringLiteral("分块后原始实体发生重叠，无法保留材料分区。"));return false;}
                    owner=region.originalSolidId;
                }
            }
            info.controlShape=partition.originalShape;info.sourceRegionCount=partition.originalSolidCount;
            info.processed=true;
        }
        if (info.meshShape.IsNull()) { setError(errMsg, QStringLiteral("没有可用于网格划分的几何面或实体。")); return false; }
        if (!request.allGeometry && request.dimension == 3) {
            if (!info.solidDomains.isEmpty()) {
                for (int& domain : info.solidDomains) domain = scopedOriginalRegions.value(domain - 1, 0);
            } else {
                TopTools_IndexedMapOfShape solids; TopExp::MapShapes(info.meshShape, TopAbs_SOLID, solids);
                info.solidDomains.fill(0, solids.Extent());
                for (int i = 0; i < scopedOriginalRegions.size(); ++i) {
                    const QVector<int> images = info.processed ? info.controlImages[3].value(i + 1) : QVector<int>{i + 1};
                    for (int image : images) {
                        int& owner = info.solidDomains[image - 1];
                        if (owner && owner != scopedOriginalRegions[i]) {
                            setError(errMsg, QStringLiteral("所选实体存在体积重叠，无法确定网格区域归属。")); return false;
                        }
                        owner = scopedOriginalRegions[i];
                    }
                }
            }
        }
        const QString brepPath = work.filePath(QStringLiteral("input.brep"));
        if (!BRepTools::Write(info.meshShape, QFile::encodeName(brepPath).constData())) {
            setError(errMsg, QStringLiteral("网格几何准备失败。")); return false;
        }
        const QString rawPath = work.filePath(QStringLiteral("native.vtk"));
        QString dll = QProcessEnvironment::systemEnvironment().value(QStringLiteral("CAE_SMESH_BRIDGE_DLL"));
        if (dll.isEmpty()) dll = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins/CAE_SMESHBridge.dll"));
        QLibrary library(dll); library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load()) { setError(errMsg, QStringLiteral("网格计算组件加载失败：%1").arg(library.errorString())); return false; }
        using Abi = std::uint32_t (*)();
        using Generate = decltype(&cae_smesh_generate_workflow);
        const auto abi = reinterpret_cast<Abi>(library.resolve("cae_smesh_abi_version"));
        const auto generate = reinterpret_cast<Generate>(library.resolve("cae_smesh_generate_workflow"));
        if (!abi || abi() != 16 || !generate) { setError(errMsg, QStringLiteral("网格计算组件版本不匹配，请重新编译。")); return false; }
        CaeSMESHWorkflowRequest workflowNative{};
        workflowNative.preferHex=request.workflow.preferHex?1:0;
        auto& native=workflowNative.mesh;
        QByteArray brep = brepPath.toUtf8(), vtk = rawPath.toUtf8();
        auto& params = native.parameters;
        const QFileInfo sourceFile(originalRequest.inputGeometryPath);
        QStringList generationParts{sourceFile.absoluteFilePath(), QString::number(sourceFile.size()),
            QString::number(sourceFile.lastModified().toMSecsSinceEpoch()),
            QString::number(request.dimension), QString::number(request.conformalizeRegions)};
        for (const auto& scope : originalRequest.geometryScope)
            generationParts << QStringLiteral("scope:%1:%2").arg(scope.dimension).arg(scope.entityId);
        for (const auto& rule : request.workflow.partitions) {
            generationParts << QStringLiteral("cut:%1:%2:%3:%4").arg(rule.enabled).arg(rule.axis)
                .arg(rule.coordinate,0,'g',17).arg(rule.targetVolumeUiId);
            generationParts << QString::number(rule.origin.x(),'g',17) << QString::number(rule.origin.y(),'g',17)
                << QString::number(rule.origin.z(),'g',17) << QString::number(rule.normal.x(),'g',17)
                << QString::number(rule.normal.y(),'g',17) << QString::number(rule.normal.z(),'g',17);
        }
        const QByteArray geometryGeneration = generationParts.join(QLatin1Char('|')).toUtf8();
        params.geometryGenerationUtf8 = geometryGeneration.constData();
        params.brepPathUtf8 = brep.constData(); params.vtkPathUtf8 = vtk.constData();
        params.maxSize = request.lenMax; params.minSize = request.lenMin;
        params.optimize = request.optimize; params.surfaceCurvature = request.surfaceCurvature;
        params.tolerance = request.tolerance;
        params.fineness = request.nativeFineness>=0?request.nativeFineness:std::clamp(int(std::lround(request.fineness * 4)),0,4);
        params.growthRate = request.growthRate; params.nbSegPerEdge = request.segmentsPerEdge; params.nbSegPerRadius = request.segmentsPerRadius;
        native.dimension = request.dimension; native.quadrangle = request.elemType == 2;
        native.elementOrder = request.elementOrder;
        std::vector<CaeSMESHLocalSizeControl> sizes;
        std::vector<CaeSMESHEdgeSegmentsControl> segments;
        QMap<QPair<int,int>, double> targets;
        for (const auto& control : request.localSizeControls) {
            if (control.dimension < 1 || control.dimension > request.dimension ||
                !std::isfinite(control.targetSize) || control.targetSize <= 0) {
                setError(errMsg, QStringLiteral("局部尺寸参数无效。")); return false;
            }
            TopTools_IndexedMapOfShape originals;
            TopExp::MapShapes(info.controlShape, shapeTypeForDimension(control.dimension), originals);
            const int originalId = request.allGeometry ? control.entityId
                : scopedIndices.value(control.dimension).value(control.entityId);
            if (originalId <= 0 || originalId > originals.Extent()) {
                setError(errMsg, QStringLiteral("局部控制实体 %1 不在当前几何范围内，请重新选择。").arg(control.entityId)); return false;
            }
            QVector<int> images = info.controlImages[control.dimension].value(originalId);
            if (!info.processed) images = {originalId};
            if (images.isEmpty()) { setError(errMsg, QStringLiteral("局部控制实体在共形分割后丢失。")); return false; }
            for (int id : images) {
                const auto key = qMakePair(control.dimension, id);
                const double size = std::max(request.lenMin, control.targetSize);
                targets[key] = targets.contains(key) ? std::min(targets[key], size) : size;
            }
        }
        std::vector<CaeSMESHScopedNetgenControl> scopes;
        std::vector<CaeSMESHFaceMinimumControl> faceMinimums;
        std::vector<CaeSMESHFaceCurvatureControl> faceCurvatures;
        std::vector<CaeSMESHApplicationFaceSettings> faceSettings;
        std::vector<CaeSMESHSweepRequest> sweeps;
        std::vector<std::int32_t> selectedSolids,surfaceSolids;
        QMap<int,QPair<int,double>> fixedEdges;
        QMap<int,bool> surfaceModes;
        for(auto it=targets.cbegin();it!=targets.cend();++it)if(it.key().first==3)surfaceModes[it.key().second]=true;
        auto resolveIn=[&](const SmeshEntityReference& entity,int& originalId,QVector<int>& images,bool onPartition)->bool {
            const auto& controlShape=onPartition?info.partitionShape:info.controlShape;
            if(entity.dimension<1||entity.dimension>request.dimension||entity.entityId<=0) {
                setError(errMsg,QStringLiteral("局部网格设置的几何编号无效。"));return false;
            }
            TopTools_IndexedMapOfShape originals;
            TopExp::MapShapes(controlShape,shapeTypeForDimension(entity.dimension),originals);
            originalId = onPartition || request.allGeometry ? entity.entityId
                : scopedIndices.value(entity.dimension).value(entity.entityId);
            if (onPartition && entity.dimension == 2 && entity.parentRegion > 0) {
                TopTools_IndexedMapOfShape solids;
                TopExp::MapShapes(controlShape,TopAbs_SOLID,solids);
                if (entity.parentRegion > solids.Extent()) {
                    setError(errMsg,QStringLiteral("分块区域编号已失效。"));return false;
                }
                const auto faces = GeometryPartitionTopology::orderedFaces(solids(entity.parentRegion));
                if (entity.entityId > faces.size()) {
                    setError(errMsg,QStringLiteral("分块端面编号已失效。"));return false;
                }
                originalId = originals.FindIndex(faces[entity.entityId - 1].shape);
            }
            if(originalId<=0||originalId>originals.Extent()) {
                setError(errMsg,QStringLiteral("实体 %1 不在当前几何范围内，请重新选择。").arg(entity.entityId));return false;
            }
            images=onPartition?info.partitionImages[entity.dimension].value(originalId):
                info.processed?info.controlImages[entity.dimension].value(originalId):QVector<int>{originalId};
            if(images.isEmpty()){setError(errMsg,QStringLiteral("实体 %1 在共形分割后丢失。").arg(entity.entityId));return false;}return true;
        };
        auto resolve=[&](const SmeshEntityReference& entity,int& id,QVector<int>& images){return resolveIn(entity,id,images,false);};
        struct Effective { double maximum,minimum;int fineness,optimize,curvature; };
        QMap<QString,Effective> effective;
        QMap<QString,Effective> surfaceEffective;
        QMap<QString,QVector<int>> originalScopes;
        QMap<QString,int> scopeDimensions;
        const Effective base{request.lenMax,request.lenMin,params.fineness,params.optimize,params.surfaceCurvature};
        effective[request.workflow.meshUuid]=base;
        surfaceEffective[request.workflow.meshUuid]=base;
        QMap<QPair<int,int>,QString> owners;
        QMap<QString,QVector<int>> nativeScopes;
        for(const auto& sub:request.workflow.submeshes) {
            if(sub.uuid.isEmpty()||effective.contains(sub.uuid)||sub.entities.isEmpty()) {
                setError(errMsg,QStringLiteral("局部网格设置重复或未选择几何实体。"));return false;
            }
            const QString parent=sub.parentUuid.isEmpty()?request.workflow.meshUuid:sub.parentUuid;
            if(!effective.contains(parent)){setError(errMsg,QStringLiteral("局部网格设置的父级无效或顺序错误。"));return false;}
            const auto inherited=sub.dimension<3?surfaceEffective[parent]:effective[parent];
            Effective value{sub.maxSize>0?sub.maxSize:inherited.maximum,sub.minSize>=0?sub.minSize:inherited.minimum,
                sub.fineness>=0?sub.fineness:inherited.fineness,sub.optimize>=0?sub.optimize:inherited.optimize,
                sub.surfaceCurvature>=0?sub.surfaceCurvature:inherited.curvature};
            if(!std::isfinite(value.maximum)||!std::isfinite(value.minimum)||value.maximum<=0||value.minimum<0||(sub.dimension>=2&&value.minimum>value.maximum)||
                value.fineness<0||value.fineness>5||sub.curvatureSegments<0||(sub.curvatureSegments>0&&sub.curvatureSegments<3)||sub.curvatureSegments>1000||sub.edgeSegments<0||sub.edgeBias<=0||!std::isfinite(sub.edgeBias)) {
                setError(errMsg,QStringLiteral("“%1”的尺寸或继承参数无效；最小尺寸不能超过最大尺寸。").arg(sub.name));return false;
            }
            effective[sub.uuid]=value;scopeDimensions[sub.uuid]=sub.dimension;
            surfaceEffective[sub.uuid]=sub.dimension==3&&!sub.surfaceSizing?surfaceEffective[parent]:value;
            if(scopeDimensions.contains(parent)&&scopeDimensions[parent]<=sub.dimension){setError(errMsg,QStringLiteral("子级网格设置的几何维度必须低于父级。"));return false;}
            for(const auto& entity:sub.entities) {
                if(entity.dimension!=sub.dimension){setError(errMsg,QStringLiteral("局部网格设置与几何实体维度不一致。"));return false;}
                int originalId=0;QVector<int> images;if(!resolve(entity,originalId,images))return false;
                const auto key=qMakePair(sub.dimension,originalId);
                if(owners.contains(key)){setError(errMsg,QStringLiteral("几何实体 %1 被多个局部网格设置重复选中。").arg(entity.entityId));return false;}
                owners[key]=sub.uuid;originalScopes[sub.uuid].push_back(originalId);
                if(scopeDimensions.contains(parent)) {
                    TopTools_IndexedMapOfShape parents,children;TopExp::MapShapes(info.controlShape,shapeTypeForDimension(scopeDimensions[parent]),parents);TopExp::MapShapes(info.controlShape,shapeTypeForDimension(sub.dimension),children);
                    bool belongs=false;for(int id:originalScopes[parent]){TopTools_IndexedMapOfShape contained;TopExp::MapShapes(parents(id),shapeTypeForDimension(sub.dimension),contained);belongs|=contained.Contains(children(originalId));}
                    if(!belongs){setError(errMsg,QStringLiteral("“%1”中的实体 %2 不属于父级几何范围。").arg(sub.name).arg(entity.entityId));return false;}
                }
                for(int id:images) {
                    nativeScopes[sub.uuid].push_back(id);targets[qMakePair(sub.dimension,id)]=value.maximum;
                    if(sub.dimension==3) {
                        CaeSMESHScopedNetgenControl nativeScope{};nativeScope.solidShapeIndex=id;
                        nativeScope.fineness=value.fineness;nativeScope.optimize=value.optimize;nativeScope.surfaceCurvature=value.curvature;
                        nativeScope.automaticMinSize=value.minimum==0?1:0;nativeScope.manualMinSize=value.minimum;
                        nativeScope.growthRate=-1.;nativeScope.nbSegPerEdge=-1.;nativeScope.nbSegPerRadius=-1.;scopes.push_back(nativeScope);surfaceModes[id]=sub.surfaceSizing;
                    } else if(sub.dimension==2) {
                        faceMinimums.push_back({id,value.minimum==0?0:1,value.minimum});
                        if(sub.curvatureSegments>0)faceCurvatures.push_back({id,sub.curvatureSegments});
                        faceSettings.push_back({id,value.minimum,value.fineness,value.optimize,value.curvature,sub.curvatureSegments});
                    } else if(sub.edgeSegments>0)fixedEdges[id]=qMakePair(sub.edgeSegments,sub.edgeBias);
                }
            }
        }
        QSet<int> sweepVolumes;
        for(const auto& task:request.workflow.sweeps) {
            if(request.dimension!=3||request.elementOrder!=1||request.elemType!=3) {
                setError(errMsg,QStringLiteral("扫掠网格需要整体剖分采用三维、一阶四面体网格。"));return false;
            }
            int originalVolume=0,source=0,target=0;QVector<int> volumeImages,sourceImages,targetImages;
            if(task.partitionRegion&&!partitioned){setError(errMsg,QStringLiteral("分块扫掠缺少切面，请重新预览分块。"));return false;}
            if(!resolveIn(task.volume,originalVolume,volumeImages,task.partitionRegion)||
               !resolveIn(task.source,source,sourceImages,task.partitionRegion)||
               !resolveIn(task.target,target,targetImages,task.partitionRegion))return false;
            if(volumeImages.size()!=1||sourceImages.size()!=1||targetImages.size()!=1||source==target||task.layers<1||task.sourceSize<=0) {
                setError(errMsg,QStringLiteral("“%1”的扫掠体或端面已分裂、重合，或参数无效；请先分块并重新选择。").arg(task.name));return false;
            }
            const int solidId=volumeImages[0];
            if(sweepVolumes.contains(solidId)) {
                setError(errMsg,QStringLiteral("体 %1 被多个扫掠网格任务重复选中，请保留一份扫掠设置。").arg(task.volumeId));return false;
            }
            sweepVolumes.insert(solidId);
            TopTools_IndexedMapOfShape solids,faces;TopExp::MapShapes(info.meshShape,TopAbs_SOLID,solids);TopExp::MapShapes(info.meshShape,TopAbs_FACE,faces);
            TopTools_IndexedMapOfShape contained;TopExp::MapShapes(solids(solidId),TopAbs_FACE,contained);
            if(!contained.Contains(faces(sourceImages[0]))||!contained.Contains(faces(targetImages[0]))) {
                setError(errMsg,QStringLiteral("扫掠端面必须属于同一个几何体。"));return false;
            }
            for(const auto& setting:faceSettings)if(contained.Contains(faces(setting.shapeIndex))) {
                setError(errMsg,QStringLiteral("扫掠体已有面网格设置，请移除重叠设置或取消扫掠。"));return false;
            }
            CaeSMESHSweepRequest sweep{};sweep.brepPathUtf8=brep.constData();sweep.vtkPathUtf8=vtk.constData();
            sweep.volumeShapeIndex=solidId;sweep.sourceShapeIndex=sourceImages[0];sweep.targetShapeIndex=targetImages[0];
            sweep.layers=task.layers;sweep.sourceSize=task.sourceSize;sweep.quadrangleSource=task.quadrangleSource;sweep.tolerance=request.tolerance;sweeps.push_back(sweep);
        }
        // A Solid sizing node can cover the entire model. Sweep owns its chosen
        // solids; the sizing hypotheses belong to the remaining Tet regions.
        // Resolve this on final OCC images so split CAD solids work as well.
        int overriddenSolidSettings=0;
        for(int solid:sweepVolumes) {
            for(const auto& scope:scopes)if(scope.solidShapeIndex==solid)++overriddenSolidSettings;
            targets.remove(qMakePair(3,solid));surfaceModes.remove(solid);
        }
        scopes.erase(std::remove_if(scopes.begin(),scopes.end(),[&](const CaeSMESHScopedNetgenControl& scope){
            return sweepVolumes.contains(scope.solidShapeIndex);
        }),scopes.end());
        if(overriddenSolidSettings>0&&request.progress)
            request.progress(QStringLiteral("体网格设置中有 %1 个区域使用扫掠；这些区域保留扫掠层数和起始面尺寸，其余区域应用体网格参数。")
                .arg(overriddenSolidSettings));
        if(!request.workflow.computeSubmeshUuid.isEmpty()) {
            if(!sweeps.empty()||request.dimension!=3||request.elementOrder!=1||request.elemType!=3||scopeDimensions.value(request.workflow.computeSubmeshUuid)!=3) {
                setError(errMsg,QStringLiteral("单体划分仅支持一阶四面体；包含扫掠网格时请生成全部网格。"));return false;
            }
            for(int id:nativeScopes.value(request.workflow.computeSubmeshUuid))selectedSolids.push_back(id);
            if(selectedSolids.empty()){setError(errMsg,QStringLiteral("未找到待划分的体网格设置。"));return false;}
        }
        if(!sweeps.empty())for(auto it=surfaceModes.cbegin();it!=surfaceModes.cend();++it)if(!it.value()) {
            setError(errMsg,QStringLiteral("扫掠混合网格的体网格设置必须同时控制表面及内部尺寸。"));return false;
        }
        for(auto it=surfaceModes.cbegin();it!=surfaceModes.cend();++it)if(it.value())surfaceSolids.push_back(it.key());
        QByteArray session=request.workflow.meshUuid.toUtf8();
        workflowNative.scopes=scopes.data();workflowNative.scopeCount=int(scopes.size());
        workflowNative.faceMinimums=faceMinimums.data();workflowNative.faceMinimumCount=int(faceMinimums.size());
        workflowNative.faceCurvatures=faceCurvatures.data();workflowNative.faceCurvatureCount=int(faceCurvatures.size());
        workflowNative.faces=faceSettings.data();workflowNative.faceCount=int(faceSettings.size());
        workflowNative.surfaceSolids=surfaceSolids.data();workflowNative.surfaceSolidCount=int(surfaceSolids.size());workflowNative.explicitSurfaceScope=1;
        workflowNative.sweeps=sweeps.data();workflowNative.sweepCount=int(sweeps.size());
        workflowNative.selectedSolids=selectedSolids.data();workflowNative.selectedSolidCount=int(selectedSolids.size());
        workflowNative.sessionUtf8=session.constData();workflowNative.resetSession=selectedSolids.empty()?1:0;
        for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
            const int dimension = it.key().first, id = it.key().second;
            if (dimension == 1) {
                TopTools_IndexedMapOfShape edges; TopExp::MapShapes(info.meshShape, TopAbs_EDGE, edges);
                GProp_GProps props; BRepGProp::LinearProperties(edges(id), props);
                CaeSMESHEdgeSegmentsControl control{}; control.shapeIndex = id;
                const double divisions = std::ceil(std::abs(props.Mass()) / it.value());
                if (!std::isfinite(divisions) || divisions > std::numeric_limits<int>::max()) {
                    setError(errMsg, QStringLiteral("边局部尺寸过小，分段数超出支持范围。")); return false;
                }
                control.divisions = fixedEdges.contains(id)?fixedEdges[id].first:std::max(1, int(divisions));
                control.bias = fixedEdges.contains(id)?fixedEdges[id].second:1.; segments.push_back(control);
            } else {
                CaeSMESHLocalSizeControl control{}; control.dimension = dimension;
                TopTools_IndexedMapOfShape shapes;
                TopExp::MapShapes(info.meshShape, shapeTypeForDimension(dimension), shapes);
                control.shapeIndex = id; control.targetSize = it.value(); sizes.push_back(control);
            }
        }
        params.localSizes = sizes.data(); params.localSizeCount = int(sizes.size());
        params.edgeSegments = segments.data(); params.edgeSegmentCount = int(segments.size());
        char error[8192]{}; CaeSMESHNetgenResult nativeResult{};
        const auto progress = [](void* context, const char* message) {
            const auto* req = static_cast<const SmeshMeshingRequest*>(context);
            if (req->progress) req->progress(QString::fromUtf8(message));
        };
        if (generate(&workflowNative, &nativeResult, error, sizeof(error), progress,
                     const_cast<SmeshMeshingRequest*>(&request)) != 0) {
            const QString detail = QString::fromUtf8(error);
            if (detail.contains(QStringLiteral("incompatible directions across their shared face"),
                                Qt::CaseInsensitive))
            {
                const QRegularExpression ids(QStringLiteral("V(\\d+) and V(\\d+)"));
                const QRegularExpressionMatch match = ids.match(detail);
                setError(errMsg, match.hasMatch()
                    ? QStringLiteral("相邻扫掠区域 %1 与 %2 的方向不兼容，请统一相邻区域的扫掠方向。")
                        .arg(match.captured(1), match.captured(2))
                    : QStringLiteral("相邻扫掠区域的方向不兼容，请统一相邻区域的扫掠方向。"));
            }
            else if (detail.contains(QStringLiteral("More than 2 faces with not quadrangle elements"),
                                     Qt::CaseInsensitive))
            {
                setError(errMsg, QStringLiteral("扫掠区域的侧面网格不满足要求。请检查起始面与相邻区域方向，或改用普通体网格。"));
            }
            else
            {
                setError(errMsg, QStringLiteral("网格划分失败：%1").arg(detail));
            }
            return false;
        }
        auto reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
        reader->SetFileName(QFile::encodeName(rawPath).constData()); reader->ReadAllScalarsOn(); reader->Update();
        auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New(); grid->DeepCopy(reader->GetOutput());
        if (!grid->GetNumberOfPoints() || !grid->GetNumberOfCells()) {
            setError(errMsg, QStringLiteral("没有生成有效网格单元。")); return false;
        }
        auto* source = grid->GetCellData()->GetArray("matype");
        if (!source || source->GetNumberOfTuples() != grid->GetNumberOfCells()) {
            setError(errMsg, QStringLiteral("生成的网格缺少几何区域编号。")); return false;
        }
        QVector<int> regions; QSet<int> materials;
        for (vtkIdType cell = 0; cell < grid->GetNumberOfCells(); ++cell) {
            int region = int(source->GetTuple1(cell));
            if(request.dimension==3&&!info.solidDomains.isEmpty())region=info.solidDomains.value(region-1,0);
            if (request.dimension == 2 && !info.faceDomains.isEmpty()) region = info.faceDomains.value(region-1, 0);
            if (region <= 0) { setError(errMsg, QStringLiteral("生成的单元缺少有效的几何区域。")); return false; }
            regions.push_back(region); materials.insert(info.domainData.materialGroups.value(region, region));
        }
        if (materials.size() > kMaxSolverMatype-kDefaultCalculationMatype+1) {
            setError(errMsg, QStringLiteral("独立材料分组过多，材料编号将超过 300。")); return false;
        }
        grid->GetCellData()->RemoveArray("matype"); addDefaultArrays(grid, regions, info.domainData);
        SmeshMeshingResult output;
        output.grid = grid; output.pointCount = int(grid->GetNumberOfPoints());
        output.surfaceElementCount = request.dimension == 2 ? int(grid->GetNumberOfCells()) : 0;
        if (request.dimension == 3) {
            output.volumeElementCount = int(grid->GetNumberOfCells());
            auto* facets = grid->GetFieldData()->GetArray("SmeshBoundaryFacets");
            if (facets) for (vtkIdType i=0; i<facets->GetNumberOfTuples(); ++i)
                if (int(facets->GetComponent(i,0)) == 2) ++output.surfaceElementCount;
        }
        output.sourceRegionCount = info.sourceRegionCount; output.conformalRegionCount = info.outputRegionCount;
        output.sharedInterfaceCount = info.sharedInterfaceCount; output.conformalized = info.processed;
        output.regionCount = regionCount(regions); output.appliedLocalControlCount = request.localSizeControls.size()+request.workflow.submeshes.size();
        output.nativeSubmeshCount = targets.size();
        if (request.elemType == kElemTypeHex8FromTet) {
            HexConversionStats stats; output.grid = convertTet4ToHex8(grid, &stats, errMsg);
            if (!output.grid) return false;
            output.pointCount = int(output.grid->GetNumberOfPoints()); output.volumeElementCount = int(stats.hexCount);
            output.surfaceElementCount *= 3; output.invalidElementCount = int(stats.invalidHexCount);
            output.minScaledJacobian = stats.minimumScaledJacobian;
        }
        if (request.exportVtk && !request.vtkPath.isEmpty() && !writeVtk(output.grid, request.vtkPath, errMsg)) return false;
        if (request.exportNas && !request.nasPath.isEmpty() && !writeNastran(output.grid, request.nasPath, errMsg)) return false;
        if (result) *result = output;
        return true;
    } catch (const Standard_Failure& ex) {
        setError(errMsg, QStringLiteral("网格几何处理失败：%1").arg(QString::fromUtf8(ex.GetMessageString())));
    } catch (const std::exception& ex) { setError(errMsg, QString::fromUtf8(ex.what())); }
    return false;
}

