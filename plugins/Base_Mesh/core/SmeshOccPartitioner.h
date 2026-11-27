#pragma once

#include <QString>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QByteArray>
#include <QVector3D>
#include <QVector>
#include <QtGlobal>

#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BOPAlgo_Splitter.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

#include <SmeshWorkflowData.h>
#include <QMap>
#include <TopTools_ListIteratorOfListOfShape.hxx>

// SMESH manual partition backend.
//
// Partitioning is performed only on a temporary OpenCASCADE working copy with
// BOPAlgo_Splitter. The project CAD is never modified. The resulting BREP is
// passed directly to the standalone SMESH bridge, so partitioning and sweeping
// share one OCC/SMESH workflow with no second mesher involved.
class SmeshOccPartitioner
{
public:
    struct Settings
    {
        QString inputStepPath;
        QString outputBrepPath;
        double tolerance = 1.0e-6;
        QVector<SmeshPartitionSettings> rules;
    };

    struct RegionInfo
    {
        int index = 0;
        int originalSolidId = 0;
        double volume = 0.0;
        double bbox[6] = {0, 0, 0, 0, 0, 0};
    };

    struct Result
    {
        QString brepPath;
        QString stepPath;
        int originalSolidCount = 0;
        int regionCount = 0;
        double originalVolume = 0.0;
        double partitionedVolume = 0.0;
        QStringList appliedPlanes;
        std::vector<RegionInfo> regions;
        TopoDS_Shape originalShape;
        QMap<int,QMap<int,QVector<int>>> controlImages;
    };

    using Logger = std::function<void(const QString&)>;

    static Result partition(const Settings& settings, const Logger& log = Logger())
    {
        if (settings.inputStepPath.isEmpty())
            throw std::runtime_error("手动分块：输入 CAD 路径为空");
        if (settings.outputBrepPath.isEmpty())
            throw std::runtime_error("手动分块：临时 BREP 路径为空");
        if (settings.rules.isEmpty())
            throw std::runtime_error("手动分块：没有启用的切割规则");

        const TopoDS_Shape source = readInputCad(settings.inputStepPath, log);
        if (source.IsNull())
            throw std::runtime_error("手动分块：OpenCASCADE CAD 读取结果为空");

        // Use the same unique-solid identity space as Base_Geometry preview.
        // TopExp_Explorer counts repeated occurrences when a result compound and
        // its child solids are both exported; that was the V8.11 "preview says
        // 2 regions, execution says original Solid=4" mismatch.
        TopTools_IndexedMapOfShape originalSolids;
        TopExp::MapShapes(source, TopAbs_SOLID, originalSolids);
        if (originalSolids.Extent() <= 0)
            throw std::runtime_error("手动分块：CAD 中没有可分块的 Solid");

        Result result;
        result.originalShape=source;
        QMap<int,QMap<int,QVector<TopoDS_Shape>>> history;
        TopTools_IndexedMapOfShape originals[4];
        for(int dim=1;dim<=3;++dim)
            TopExp::MapShapes(source,dim==1?TopAbs_EDGE:dim==2?TopAbs_FACE:TopAbs_SOLID,originals[dim]);
        result.originalSolidCount = originalSolids.Extent();
        for (int i = 1; i <= originalSolids.Extent(); ++i)
            result.originalVolume += shapeVolume(originalSolids(i));

        Bnd_Box bounds;
        BRepBndLib::Add(source, bounds);
        if (bounds.IsVoid())
            throw std::runtime_error("手动分块：无法取得模型包围盒");

        double xmin = 0.0, ymin = 0.0, zmin = 0.0;
        double xmax = 0.0, ymax = 0.0, zmax = 0.0;
        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double dx = std::max(0.0, xmax - xmin);
        const double dy = std::max(0.0, ymax - ymin);
        const double dz = std::max(0.0, zmax - zmin);
        const double diag = std::max(1.0e-9, std::sqrt(dx * dx + dy * dy + dz * dz));
        const double half = std::max(1.0, diag * 2.5);
        const double eps = std::max(std::max(settings.tolerance, 1.0e-12) * 20.0, diag * 1.0e-10);

        for (const auto& rule : settings.rules)
        {
            if (!rule.enabled) continue;
            if (rule.targetVolumeUiId > result.originalSolidCount)
                throw std::runtime_error(QString(
                    "手动分块：切面绑定的目标实体 %1 已不存在，请重新选择实体")
                    .arg(rule.targetVolumeUiId).toStdString());
        }

        BRep_Builder builder;
        TopoDS_Compound solidCompound;
        builder.MakeCompound(solidCompound);

        bool anySplit = false;
        int splitBodyCount = 0;
        int regionIndex = 0;
        for (int solidId = 1; solidId <= originalSolids.Extent(); ++solidId)
        {
            const TopoDS_Shape solid = originalSolids(solidId);
            double localBox[6] = {0, 0, 0, 0, 0, 0};
            shapeBounds(solid, localBox);

            TopTools_ListOfShape tools;
            QStringList localPlaneDescriptions;
            for (const SmeshPartitionSettings& rule : settings.rules)
            {
                if (!rule.enabled) continue;
                if (rule.targetVolumeUiId > 0 && rule.targetVolumeUiId != solidId)
                    continue;

                QVector3D normal = rule.planeNormal();
                if (normal.lengthSquared() <= 1.0e-16f)
                    continue;
                normal.normalize();
                const QVector3D origin = rule.planeOrigin();

                TopoDS_Shape toolFace;
                if (rule.axis == 4)
                {
                    if (!(rule.coordinate > 1.0e-6))
                        continue;
                    const gp_Ax3 ax3(gp_Pnt(origin.x(), origin.y(), origin.z()),
                                     gp_Dir(normal.x(), normal.y(), normal.z()));
                    const gp_Cylinder cyl(ax3, rule.coordinate);
                    BRepBuilderAPI_MakeFace cylMaker(cyl, 0.0, 2.0 * 3.14159265358979323846, -half, half);
                    if (!cylMaker.IsDone())
                        continue;
                    toolFace = cylMaker.Face();
                }
                else
                {
                    // A cutter touching only the body's bounding box is not a useful
                    // split.  Skip it here; the OCC result below remains authoritative.
                    if (!planeIntersectsBounds(origin, normal,
                                               localBox[0], localBox[1], localBox[2],
                                               localBox[3], localBox[4], localBox[5], eps))
                        continue;

                    const gp_Pln pln(
                        gp_Pnt(origin.x(), origin.y(), origin.z()),
                        gp_Dir(normal.x(), normal.y(), normal.z()));
                    BRepBuilderAPI_MakeFace faceMaker(pln, -half, half, -half, half);
                    if (!faceMaker.IsDone())
                        continue;
                    toolFace = faceMaker.Face();
                }

                tools.Append(toolFace);
                const QString desc = QStringLiteral("V%1: %2")
                    .arg(solidId).arg(planeDescription(rule, normal));
                localPlaneDescriptions << desc;
            }

            if (tools.IsEmpty())
            {
                RegionInfo info;
                info.index = ++regionIndex;
                info.originalSolidId = solidId;
                info.volume = shapeVolume(solid);
                shapeBounds(solid, info.bbox);
                result.partitionedVolume += info.volume;
                result.regions.push_back(info);
                builder.Add(solidCompound, solid);
                for(int dim=1;dim<=3;++dim) {
                    TopTools_IndexedMapOfShape local;
                    TopExp::MapShapes(solid,dim==1?TopAbs_EDGE:dim==2?TopAbs_FACE:TopAbs_SOLID,local);
                    for(int i=1;i<=local.Extent();++i)history[dim][originals[dim].FindIndex(local(i))].push_back(local(i));
                }
                continue;
            }

            TopTools_ListOfShape oneArgument;
            oneArgument.Append(solid);
            BOPAlgo_Splitter splitter;
            splitter.SetArguments(oneArgument);
            splitter.SetTools(tools);
            splitter.SetRunParallel(false);
            splitter.SetNonDestructive(true);
            splitter.SetFuzzyValue(std::max(settings.tolerance, 1.0e-9));
            splitter.Perform();
            if (splitter.HasErrors())
                throw std::runtime_error(QString(
                    "手动分块：实体 %1 的 OpenCASCADE Splitter 执行失败")
                    .arg(solidId).toStdString());

            for(int dim=1;dim<=3;++dim) {
                const auto kind=dim==1?TopAbs_EDGE:dim==2?TopAbs_FACE:TopAbs_SOLID;
                TopTools_IndexedMapOfShape local;TopExp::MapShapes(solid,kind,local);
                for(int i=1;i<=local.Extent();++i) {
                    auto& images=history[dim][originals[dim].FindIndex(local(i))];
                    images.push_back(local(i));
                    for(TopTools_ListIteratorOfListOfShape it(splitter.Modified(local(i)));it.More();it.Next())images.push_back(it.Value());
                }
            }

            TopTools_IndexedMapOfShape children;
            TopExp::MapShapes(splitter.Shape(), TopAbs_SOLID, children);
            int validChildren = 0;
            for (int i = 1; i <= children.Extent(); ++i)
            {
                const TopoDS_Shape child = children(i);
                const double volume = shapeVolume(child);
                if (!(volume > std::max(result.originalVolume * 1.0e-13, 1.0e-30)))
                    continue;

                RegionInfo info;
                info.index = ++regionIndex;
                info.originalSolidId = solidId;
                info.volume = volume;
                shapeBounds(child, info.bbox);
                result.partitionedVolume += volume;
                result.regions.push_back(info);
                builder.Add(solidCompound, child);
                ++validChildren;
            }

            if (validChildren <= 0)
                throw std::runtime_error(QString(
                    "手动分块：实体 %1 分块后没有得到有效 Solid")
                    .arg(solidId).toStdString());

            if (validChildren > 1)
            {
                anySplit = true;
                ++splitBodyCount;
                result.appliedPlanes.append(localPlaneDescriptions);
            }
        }

        result.regionCount = static_cast<int>(result.regions.size());
        if (!anySplit)
            throw std::runtime_error(
                "手动分块：切面没有穿过所选实体内部，请调整坐标后重新预览");

        validateMass(result, settings.tolerance, diag);
        for(int dim=1;dim<=3;++dim) {
            const auto kind=dim==1?TopAbs_EDGE:dim==2?TopAbs_FACE:TopAbs_SOLID;
            TopTools_IndexedMapOfShape output;TopExp::MapShapes(solidCompound,kind,output);
            for(auto it=history[dim].cbegin();it!=history[dim].cend();++it) {
                auto& ids=result.controlImages[dim][it.key()];
                for(const auto& image:it.value()) {
                    TopTools_IndexedMapOfShape descendants;TopExp::MapShapes(image,kind,descendants);
                    for(int j=1;j<=descendants.Extent();++j) {
                        const int id=output.FindIndex(descendants(j));if(id>0&&!ids.contains(id))ids.push_back(id);
                    }
                }
            }
        }

        if (!BRepTools::Write(solidCompound, settings.outputBrepPath.toStdString().c_str()))
            throw std::runtime_error("手动分块：无法写出临时 BREP");
        result.brepPath = settings.outputBrepPath;

        QFileInfo brepInfo(settings.outputBrepPath);
        const QString stepPath = brepInfo.dir().filePath(
            brepInfo.completeBaseName() + QStringLiteral(".step"));
        STEPControl_Writer stepWriter;
        if (stepWriter.Transfer(solidCompound, STEPControl_AsIs) == IFSelect_RetDone
            && stepWriter.Write(QFile::encodeName(stepPath).constData()) == IFSelect_RetDone)
        {
            result.stepPath = stepPath;
        }
        else
        {
            emitLog(log, QStringLiteral(
                "[WARN][OccPartition] OCC 分块 BREP 已写出，但 STEP 备用副本写出失败。"));
        }

        emitLog(log, QString(
            "[OccPartition] 手动分块完成：原始 Solid=%1；实际切开 %2 个实体；结果 Region=%3；体积 %4 -> %5；BREP=%6%7")
            .arg(result.originalSolidCount).arg(splitBodyCount).arg(result.regionCount)
            .arg(result.originalVolume, 0, 'g', 14)
            .arg(result.partitionedVolume, 0, 'g', 14)
            .arg(result.brepPath)
            .arg(result.stepPath.isEmpty() ? QString()
                : QStringLiteral("；STEP备用=") + result.stepPath));
        for (const QString& p : result.appliedPlanes)
            emitLog(log, QString("[OccPartition]   - %1").arg(p));
        for (const RegionInfo& r : result.regions)
        {
            emitLog(log, QString(
                "[OccPartition] Region %1: volume=%2, bbox=[%3,%4,%5]-[%6,%7,%8]")
                .arg(r.index).arg(r.volume, 0, 'g', 12)
                .arg(r.bbox[0], 0, 'g', 7).arg(r.bbox[1], 0, 'g', 7).arg(r.bbox[2], 0, 'g', 7)
                .arg(r.bbox[3], 0, 'g', 7).arg(r.bbox[4], 0, 'g', 7).arg(r.bbox[5], 0, 'g', 7));
        }
        return result;
    }

private:
    // Geometry -> Mesh prefers the exact OCC BREP when available, while older
    // Geometry builds may still publish STEP/STP.  Do not force every CAD input
    // through STEPControl_Reader: a valid BREP is not a STEP file.
    static TopoDS_Shape readInputCad(const QString& path, const Logger& log)
    {
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile())
            throw std::runtime_error(QString("手动分块：输入 CAD 文件不存在：%1")
                .arg(path).toStdString());

        const QString suffix = fi.suffix().toLower();
        const QByteArray nativePath = QFile::encodeName(fi.absoluteFilePath());

        auto tryBrep = [&]() -> TopoDS_Shape
        {
            TopoDS_Shape shape;
            BRep_Builder builder;
            if (BRepTools::Read(shape, nativePath.constData(), builder) && !shape.IsNull())
            {
                emitLog(log, QString("[OccPartition] OpenCASCADE 输入：BREP (%1)")
                    .arg(fi.fileName()));
                return shape;
            }
            return TopoDS_Shape();
        };

        auto tryStep = [&]() -> TopoDS_Shape
        {
            STEPControl_Reader reader;
            const IFSelect_ReturnStatus status = reader.ReadFile(nativePath.constData());
            if (status != IFSelect_RetDone)
                return TopoDS_Shape();
            if (reader.TransferRoots() <= 0)
                return TopoDS_Shape();
            TopoDS_Shape shape = reader.OneShape();
            if (!shape.IsNull())
                emitLog(log, QString("[OccPartition] OpenCASCADE 输入：STEP (%1)")
                    .arg(fi.fileName()));
            return shape;
        };

        TopoDS_Shape shape;
        if (suffix == QStringLiteral("brep") || suffix == QStringLiteral("brp"))
        {
            shape = tryBrep();
            if (shape.IsNull())
                throw std::runtime_error(QString(
                    "手动分块：OpenCASCADE 无法读取 BREP：%1")
                    .arg(fi.absoluteFilePath()).toStdString());
            return shape;
        }

        if (suffix == QStringLiteral("step") || suffix == QStringLiteral("stp"))
        {
            shape = tryStep();
            if (shape.IsNull())
                throw std::runtime_error(QString(
                    "手动分块：OpenCASCADE 无法读取 STEP：%1")
                    .arg(fi.absoluteFilePath()).toStdString());
            return shape;
        }

        // Compatibility fallback for CAD exchange files without a conventional
        // suffix.  BREP is attempted first because Geometry publishes it as the
        // preferred exact OCC representation.
        shape = tryBrep();
        if (!shape.IsNull())
            return shape;
        shape = tryStep();
        if (!shape.IsNull())
            return shape;

        throw std::runtime_error(QString(
            "手动分块：无法识别或读取 CAD 文件（支持 BREP/BRP/STEP/STP）：%1")
            .arg(fi.absoluteFilePath()).toStdString());
    }

    static void emitLog(const Logger& log, const QString& text)
    {
        if (log) log(text);
    }

    static double shapeVolume(const TopoDS_Shape& shape)
    {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props);
        const double v = props.Mass();
        return (std::isfinite(v) && v > 0.0) ? v : 0.0;
    }

    static void shapeBounds(const TopoDS_Shape& shape, double bbox[6])
    {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (box.IsVoid())
        {
            std::fill(bbox, bbox + 6, 0.0);
            return;
        }
        box.Get(bbox[0], bbox[1], bbox[2], bbox[3], bbox[4], bbox[5]);
    }

    static bool planeIntersectsBounds(const QVector3D& origin, const QVector3D& normal,
        double xmin, double ymin, double zmin,
        double xmax, double ymax, double zmax, double eps)
    {
        double minD = std::numeric_limits<double>::max();
        double maxD = -std::numeric_limits<double>::max();
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
                for (int iz = 0; iz < 2; ++iz)
                {
                    const QVector3D p{
                        static_cast<float>(ix ? xmax : xmin),
                        static_cast<float>(iy ? ymax : ymin),
                        static_cast<float>(iz ? zmax : zmin)};
                    const double d = QVector3D::dotProduct(p - origin, normal);
                    minD = std::min(minD, d);
                    maxD = std::max(maxD, d);
                }
        return !(minD > eps || maxD < -eps);
    }

    static QString planeDescription(const SmeshPartitionSettings& rule, const QVector3D& normal)
    {
        if (rule.axis == 4)
        {
            return QString("柱面穿透切 R=%1 N=(%2,%3,%4)")
                .arg(rule.coordinate, 0, 'g', 6)
                .arg(normal.x(), 0, 'g', 4)
                .arg(normal.y(), 0, 'g', 4)
                .arg(normal.z(), 0, 'g', 4);
        }
        if (rule.axis == 3)
        {
            const QString desc = rule.sourceFaceUiId > 0
                ? QString("CAD面#%1").arg(rule.sourceFaceUiId)
                : QStringLiteral("任意平面");
            return desc + QString(" N=(%1,%2,%3)")
                .arg(normal.x(), 0, 'g', 5)
                .arg(normal.y(), 0, 'g', 5)
                .arg(normal.z(), 0, 'g', 5);
        }
        static const char* axisNames[] = {"X", "Y", "Z"};
        const int axis = std::max(0, std::min(2, rule.axis));
        return QString("%1=%2")
            .arg(QString::fromLatin1(axisNames[axis]))
            .arg(rule.coordinate, 0, 'g', 12);
    }

    static void validateMass(const Result& result, double tolerance, double diag)
    {
        if (!(result.originalVolume > 0.0) || !(result.partitionedVolume > 0.0))
            throw std::runtime_error("手动分块：OCC 体积校验失败");
        const double relErr = std::abs(result.partitionedVolume - result.originalVolume) /
            std::max(result.originalVolume, 1.0e-30);
        const double relTol = std::max(1.0e-8,
            std::min(1.0e-5, std::max(tolerance, 1.0e-12) / std::max(diag, 1.0e-12) * 50.0));
        if (relErr > relTol)
        {
            throw std::runtime_error(QString(
                "手动分块 OCC 体积不守恒：原体积=%1，子块总和=%2，相对误差=%3")
                .arg(result.originalVolume, 0, 'g', 14)
                .arg(result.partitionedVolume, 0, 'g', 14)
                .arg(relErr, 0, 'g', 8).toStdString());
        }
    }
};
